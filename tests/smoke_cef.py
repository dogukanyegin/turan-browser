"""Actual CEF integration test against a local HTTP fixture, isolated profile."""
import argparse
import ctypes
import http.server
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from ui_smoke import windows,u
from process_tree import hold_children,wait_children,k,creation_time

ROOT=Path(__file__).resolve().parents[1]
requests=[]
class Server(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        requests.append(self.path)
        headers={'Content-Type':'text/html; charset=utf-8','Access-Control-Allow-Origin':'*'}
        if self.path in ('/download','/private-download'):
            data=b'Turan download integration fixture\n'
            filename='private-smoke-download.txt' if self.path=='/private-download' else 'smoke-download.txt'
            headers.update({'Content-Type':'application/octet-stream','Content-Disposition':'attachment; filename="'+filename+'"'})
        elif self.path=='/private':
            data=b'''<title>wait</title><script>
            const isolated=!document.cookie.includes('normal_cookie=');
            document.cookie='private_cookie=yes;path=/';
            document.title=isolated?'PRIVATE_OK':'PRIVATE_LEAK';</script>'''
        elif self.path=='/normalcheck':
            data=b'''<title>wait</title><script>document.title=
            document.cookie.includes('normal_cookie=yes')&&!document.cookie.includes('private_cookie=')?'NORMAL_OK':'NORMAL_LEAK';</script>'''
        else:
            data=('''<title>wait</title><script>
            document.cookie='normal_cookie=yes;path=/';
            fetch('http://ads.example:%d/blocked').catch(()=>{document.title='TURAN_SMOKE_OK'});
            </script>'''%self.server.server_port).encode()
        self.send_response(200)
        for key,value in headers.items():self.send_header(key,value)
        self.send_header('Content-Length',str(len(data)))
        self.end_headers();self.wfile.write(data)
    def log_message(self,*args):pass

def main(exe):
    exe=exe.resolve()
    srv=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=srv.serve_forever,daemon=True).start()
    try:
        with tempfile.TemporaryDirectory(prefix='turan-integration-') as temp:
            folder=Path(temp);result=folder/'result.txt';profile=folder/'Profile'
            proc=subprocess.Popen([str(exe),'--profile='+str(profile),
                '--url=http://127.0.0.1:'+str(srv.server_port),
                '--host-resolver-rules=MAP ads.example 127.0.0.1',
                '--smoke-result='+str(result)],cwd=exe.parent)
            held={};deadline=time.monotonic()+50
            try:
                while proc.poll() is None:
                    for pid,handle in hold_children(proc.pid):
                        key=(pid,creation_time(pid))
                        if key in held:
                            if handle:k.CloseHandle(handle)
                        else:held[key]=(pid,handle)
                    if time.monotonic()>deadline:raise RuntimeError('CEF smoke timed out')
                    time.sleep(.1)
                code=proc.returncode
            finally:
                if proc.poll() is None:
                    subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                    proc.wait(timeout=15)
                wait_children(list(held.values()))
            if code or not result.exists():raise RuntimeError('CEF smoke failed; exit '+str(code))
            proof=result.read_text()
            assert 'PRIVATE_COOKIE_ISOLATION_OK' in proof and 'TABS=3' in proof
            assert (profile/'smoke-download.txt').read_bytes()==b'Turan download integration fixture\n'
            assert (profile/'private-smoke-download.txt').read_bytes()==b'Turan download integration fixture\n'
            state=(profile/'browser-state.txt').read_text()
            assert '/private' not in state and 'B ' in state and 'H ' in state
            assert sum(line.startswith('D ') for line in state.splitlines())==1
            assert 'private-smoke-download' not in state
            assert '/blocked' not in requests
            # Reopen the actual native download manager against the same profile.
            reopened=subprocess.Popen([str(exe),'--profile='+str(profile),'--url=about:blank'],cwd=exe.parent)
            children=[]
            try:
                deadline=time.monotonic()+15;window=None
                while time.monotonic()<deadline:
                    found=windows(reopened.pid,'TuranWindow')
                    if found:window=found[0];break
                    time.sleep(.1)
                assert window
                u.SendMessageW(window,0x111,202,0)
                panel=windows(reopened.pid,'TuranPanel')[0];listing=u.GetDlgItem(panel,30)
                assert u.SendMessageW(listing,0x18b,0,0)==1 # LB_GETCOUNT
                u.SendMessageW(listing,0x186,0,0) # select restored row
                u.SendMessageW(panel,0x111,(1<<16)|30,listing) # LBN_SELCHANGE
                u.IsWindowEnabled.argtypes=[ctypes.c_void_p]
                assert u.IsWindowEnabled(u.GetDlgItem(panel,31))
                assert not u.IsWindowEnabled(u.GetDlgItem(panel,32)) and not u.IsWindowEnabled(u.GetDlgItem(panel,33))
                children=hold_children(reopened.pid)
                u.PostMessageW(window,0x10,0,0);assert reopened.wait(timeout=15)==0
            finally:
                if reopened.poll() is None:
                    children=children or hold_children(reopened.pid)
                    subprocess.run(['taskkill','/PID',str(reopened.pid),'/T','/F'],capture_output=True)
                    reopened.wait(timeout=15)
                wait_children(children)
            proof+='PROFILE_PERSISTENCE_OK\nPRIVATE_HISTORY_EXCLUSION_OK\nPRIVATE_DOWNLOAD_METADATA_EXCLUSION_OK\nDOWNLOAD_HISTORY_RESTART_OK\n'
            print(proof,flush=True)
            (ROOT/'artifacts').mkdir(exist_ok=True)
            (ROOT/'artifacts/cef-integration.txt').write_text(proof)
    finally:srv.shutdown();srv.server_close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    main(parser.parse_args().exe)
