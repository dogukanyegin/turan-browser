"""Windows Chromium/CEF long-session evidence, no production profiles or external URLs.

The test establishes a real local network workload and samples the complete
process tree, but does not by itself certify a browser's long-term reliability,
accessibility or performance on other hardware.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import http.server
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

ROOT=Path(__file__).resolve().parents[1]


def write_atomic(path, payload):
    path.parent.mkdir(parents=True,exist_ok=True)
    temp=path.with_name(path.name+'.tmp')
    temp.write_text(json.dumps(payload,indent=2)+'\n',encoding='utf-8')
    os.replace(temp,path)


def run(exe, minutes, interval, report_path, runtime):
    if sys.platform!='win32':
        raise RuntimeError('Physical Windows required for Chromium/CEF soak')
    if not 5<=minutes<=1440 or not 5<=interval<=300:
        raise ValueError('minutes must be 5..1440; sample interval 5..300 seconds')
    # Imports require Windows DLLs. Nothing in the workload uses desktop capture.
    from performance_cef import counters
    from ui_smoke import windows,u
    from process_tree import hold_children,wait_children
    import ctypes
    from ctypes import wintypes
    class MEMORYSTATUSEX(ctypes.Structure):
        _fields_=[('dwLength',wintypes.DWORD),('dwMemoryLoad',wintypes.DWORD)]+[
            (field,ctypes.c_ulonglong) for field in ('ullTotalPhys','ullAvailPhys','ullTotalPageFile',
             'ullAvailPageFile','ullTotalVirtual','ullAvailVirtual','ullAvailExtendedVirtual')]
    memory=MEMORYSTATUSEX();memory.dwLength=ctypes.sizeof(memory)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
        raise OSError('Unable to read installed physical RAM')
    exe=exe.resolve(strict=True)
    if not exe.is_file() or not exe.with_suffix('.dll').is_file():
        raise RuntimeError('Compiled TuranBrowser.exe and TuranBrowser.dll required')
    digest=hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()
    evidence={'status':'running','startedUtc':datetime.now(timezone.utc).isoformat(),
        'runtime':runtime,'dllSha256':digest,'platform':platform.platform(),
        'physicalRamGiB':round(memory.ullTotalPhys/1024**3,2),
        'minutesRequested':minutes,'sampleIntervalSeconds':interval,'samples':[],
        'notes':'Isolated fresh profile and local loopback page; process tree sampled. Working-set sum may count shared pages more than once. Does not certify physical hardware peripherals, 8 GiB workload, other OS versions or internet performance.'}
    write_atomic(report_path,evidence)
    counter={'requests':0}
    class Fixture(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            counter['requests']+=1
            # Repeated rendering/script/navigation pressure without contacting
            # public hosts or leaking user URL/history information.
            data=(b'<!doctype html><meta charset="utf-8"><title>Turan soak fixture</title>'
                  b'<p id="content">Local workload</p><script>'
                  b'let n=0;setInterval(()=>{document.querySelector("#content").textContent='
                  b'"Synthetic work "+(++n);},1000);'
                  b'setTimeout(()=>location.replace("/page?tick="+Date.now()),15000);'
                  b'</script>')
            self.send_response(200)
            self.send_header('Content-Type','text/html; charset=utf-8')
            self.send_header('Cache-Control','no-store')
            self.send_header('Content-Length',str(len(data)))
            self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Fixture)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    process=None;children=[]
    try:
        with tempfile.TemporaryDirectory(prefix='turan-soak-isolated-') as temporary:
            with socket.socket() as free:
                free.bind(('127.0.0.1',0));port=free.getsockname()[1]
            url=f'http://127.0.0.1:{server.server_port}/page'
            process=subprocess.Popen([str(exe),'--profile='+str(Path(temporary)/'profile'),
                        '--url='+url,'--turan-runtime='+runtime,
                        '--remote-debugging-port='+str(port)],cwd=exe.parent)
            def has_browser_target():
                try:
                    with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list',timeout=1) as response:
                        return any(t.get('type')=='page' and t.get('url','').startswith(url)
                                   for t in json.load(response))
                except (OSError,ValueError):return False
            deadline=time.monotonic()+45
            while time.monotonic()<deadline and process.poll() is None and not has_browser_target():
                time.sleep(.5)
            if process.poll() is not None or not has_browser_target():
                raise RuntimeError('CEF engine/renderer failed to open local soak workload')
            start=time.monotonic();end=start+60*minutes
            previous={};last=time.monotonic()
            while time.monotonic()<end:
                if process.poll() is not None:
                    raise RuntimeError(f'Browser exited during soak: {process.returncode}')
                if not has_browser_target():
                    raise RuntimeError('CEF page target unavailable during soak')
                private,working,cpu,denied=counters(process.pid)
                elapsed=time.monotonic()-last
                core_cpu=100*sum(max(0,value-previous.get(pid,0)) for pid,value in cpu.items())/max(elapsed,.01) if previous else None
                evidence['samples'].append({'elapsedSeconds':round(time.monotonic()-start,1),
                   'privateMiB':round(private/1048576,2),'summedWorkingSetMiB':round(working/1048576,2),
                   'cpuPercentOneCore':round(core_cpu,2) if core_cpu is not None else None,
                   'unreadableProcesses':denied,'loopbackRequests':counter['requests']})
                if len(evidence['samples'])>20000:
                    raise RuntimeError('Soak sample count exceeded safe limit')
                write_atomic(report_path,evidence)
                previous=cpu;last=time.monotonic()
                print(f'SOAK {round((last-start)/60,1)} / {minutes} min; privateMiB={evidence["samples"][-1]["privateMiB"]}',flush=True)
                time.sleep(min(interval,max(0,end-time.monotonic())))
            frames=[h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)]
            frames+=windows(process.pid,'TuranWindow') if not frames else []
            if not frames:
                raise RuntimeError('No owned browser window available for clean shutdown')
            children=hold_children(process.pid)
            for frame in frames:u.PostMessageW(frame,0x10,0,0)
            try:code=process.wait(timeout=25)
            except subprocess.TimeoutExpired:raise RuntimeError('Browser failed to cleanly exit after soak')
            wait_children(children);children=[]
            if code!=0:raise RuntimeError(f'Nonzero exit after soak: {code}')
            evidence['status']='passed_limited_scope'
            evidence['actualMinutes']=round((time.monotonic()-start)/60,2)
            evidence['requestCount']=counter['requests']
            evidence['finishedUtc']=datetime.now(timezone.utc).isoformat()
            print('SOAK PASS — limited scope, not Stable certification',flush=True)
    except BaseException as error:
        evidence['status']='failed';evidence['error']=str(error)
        raise
    finally:
        if process is not None and process.poll() is None:
            children=children or hold_children(process.pid)
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
            try:process.wait(timeout=15)
            except subprocess.TimeoutExpired:pass
        if children:
            try:wait_children(children)
            except Exception:pass
        server.shutdown();server.server_close()
        write_atomic(report_path,evidence)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    parser.add_argument('--minutes',type=int,default=60)
    parser.add_argument('--sample-seconds',type=int,default=30)
    parser.add_argument('--runtime',choices=('chrome-integrated','alloy'),default='chrome-integrated')
    parser.add_argument('--report',type=Path,default=ROOT/'artifacts/soak-workstation.json')
    args=parser.parse_args()
    run(args.exe,args.minutes,args.sample_seconds,args.report,args.runtime)

if __name__=='__main__':main()
