"""Compare immutable 0.1.0 and current CEF builds using local pages and Win32 counters."""
import argparse
import hashlib
from datetime import datetime,timezone
import ctypes
from ctypes import wintypes as w
import http.server
import json
import os
from pathlib import Path
import statistics
import socket
import urllib.request
import subprocess
import tempfile
import threading
import time
import zipfile
from ui_smoke import windows,u,navigate_bar
from process_tree import k,descendants,hold_children,wait_children

ROOT=Path(__file__).resolve().parents[1]
class Memory(ctypes.Structure):
    _fields_=[('cb',w.DWORD),('faults',w.DWORD)]+[(name,ctypes.c_size_t) for name in
        ('peakWorking','working','peakPaged','paged','peakNonPaged','nonPaged','pagefile','peakPagefile','private')]
p=ctypes.WinDLL('psapi',use_last_error=True)
p.GetProcessMemoryInfo.argtypes=[w.HANDLE,ctypes.POINTER(Memory),w.DWORD];p.GetProcessMemoryInfo.restype=w.BOOL
k.GetProcessTimes.argtypes=[w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4;k.GetProcessTimes.restype=w.BOOL
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,ctypes.c_int]

def counters(root):
    private=working=0;cpu={};denied=0
    for pid in descendants(root):
        handle=k.OpenProcess(0x1000,False,pid)
        if not handle:denied+=1;continue
        try:
            memory=Memory();memory.cb=ctypes.sizeof(memory)
            if p.GetProcessMemoryInfo(handle,ctypes.byref(memory),memory.cb):
                private+=memory.private;working+=memory.working
            else:denied+=1
            times=[w.FILETIME() for _ in range(4)]
            if k.GetProcessTimes(handle,*[ctypes.byref(value) for value in times]):
                cpu[pid]=sum((value.dwHighDateTime<<32)+value.dwLowDateTime for value in times[2:])/10_000_000
        finally:k.CloseHandle(handle)
    return private,working,cpu,denied

def wait(test):
    deadline=time.monotonic()+20
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        time.sleep(.02)
    raise RuntimeError('Browser performance fixture timed out')

def idle(root):
    start=time.monotonic();previous=counters(root)[2];cpu=0;private=[];working=[];denied=0
    while time.monotonic()-start<5:
        time.sleep(.25)
        a,b,current,d=counters(root);private.append(a);working.append(b);denied=max(denied,d)
        cpu+=sum(max(0,value-previous.get(pid,0)) for pid,value in current.items());previous=current
    elapsed=time.monotonic()-start
    return {'privateMiB':round(statistics.median(private)/1048576,2),
            'summedWorkingSetMiB':round(statistics.median(working)/1048576,2),
            'idleCpuPercentOneCore':round(100*cpu/elapsed,3),
            'idleCpuPercentMachine':round(100*cpu/elapsed/(os.cpu_count() or 1),3),
            'unreadableProcesses':denied,'sampleSeconds':round(elapsed,2)}

def measure(exe,url):
    with tempfile.TemporaryDirectory(prefix='turan-performance-profile-') as temp:
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        args=[str(exe),'--profile='+temp,'--url='+url,'--remote-debugging-port='+str(port)]
        if exe.with_suffix('.dll').exists():args.append('--turan-test-startup')
        start=time.monotonic();process=subprocess.Popen(args,cwd=exe.parent)
        children=[]
        try:
            window=wait(lambda:windows(process.pid,'TuranWindow'))[0]
            def ready():
                text=ctypes.create_unicode_buffer(512);u.GetWindowTextW(window,text,512)
                return text.value.startswith('BENCH_READY')
            wait(ready);startup=time.monotonic()-start
            time.sleep(2) # exclude initial page/engine work from the idle sample
            one=idle(process.pid)
            for index in range(1,10):
                u.SendMessageW(window,0x111,103,0)
                def created():
                    try:
                        rows=json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=1))
                        return len([row for row in rows if row['type']=='page'])>=index+1 and any(row['url'].startswith('file:///') for row in rows)
                    except OSError:return False
                wait(created)
                navigate_bar(window,url+'?tab='+str(index))
                wait(ready)
            assert u.SendMessageW(u.GetDlgItem(window,10),0x1304,0,0)==10
            time.sleep(2)
            ten=idle(process.pid)
            children=hold_children(process.pid)
            u.PostMessageW(window,0x10,0,0);assert process.wait(timeout=15)==0
            pending=children;children=[];wait_children(pending)
            timings=Path(temp)/'startup-timing.txt'
            stages=dict(line.split() for line in timings.read_text().splitlines()) if timings.exists() else {}
            return {'startupSeconds':round(startup,3),'startupStagesMs':{key:int(value) for key,value in stages.items()},'oneTab':one,'tenTabs':ten}
        finally:
            if process.poll() is None:
                children=hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=10)
            if children:wait_children(children)

def main(exe,runs):
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            data=b'<title>BENCH_READY</title><h1>Local Turan performance fixture</h1>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    artifact=(ROOT/'artifacts').resolve();artifact.mkdir(exist_ok=True)
    result={'logicalProcessors':os.cpu_count(),'notes':'Fresh profile each run; local static HTTP; alternating baseline/current; Local DevTools inventory waits for asynchronous tab creation before navigation; 2 second settle, 5 second idle sample. Working-set sums double-count shared pages. Engine upgrade and shell changes are confounded; small sample, no long-run or independent Windows 10 validation.','baseline':[],'current':[]}
    result['capturedAtUtc']=datetime.now(timezone.utc).isoformat()
    result['currentDllSha256']=hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()
    result['currentExeSha256']=hashlib.sha256(exe.read_bytes()).hexdigest()
    result['baselinePortableSha256']=hashlib.sha256((ROOT/'dist/TuranBrowser-0.1.0-windows-x64-portable.zip').read_bytes()).hexdigest()
    try:
        with tempfile.TemporaryDirectory(prefix='baseline-performance-',dir=artifact) as temp:
            target=Path(temp).resolve();assert target.is_relative_to(artifact)
            with zipfile.ZipFile(ROOT/'dist/TuranBrowser-0.1.0-windows-x64-portable.zip') as archive:
                for name in archive.namelist():assert (target/name).resolve().is_relative_to(target)
                archive.extractall(target)
            baseline=target/'TuranBrowser/TuranBrowser.exe'
            url='http://127.0.0.1:'+str(server.server_port)+'/page'
            for index in range(runs):
                for name,binary in (('baseline',baseline),('current',exe.resolve())):
                    row=measure(binary,url);result[name].append(row);print(name,index+1,json.dumps(row),flush=True)
    except Exception as error:
        result['failure']=str(error)
        (artifact/'performance-failure.json').write_text(json.dumps(result,indent=2)+'\n')
        raise
    finally:server.shutdown();server.server_close()
    (artifact/'performance-comparison.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    parser.add_argument('--runs',type=int,default=2)
    args=parser.parse_args();main(args.exe,args.runs)
