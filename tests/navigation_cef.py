"""Drive native navigation controls; verify real renderer, find and PDF output."""
import ctypes
from ctypes import wintypes as w
import http.server
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

from ui_smoke import windows,u
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate

ROOT=Path(__file__).resolve().parents[1]
u.GetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int];u.GetWindowLongPtrW.restype=ctypes.c_ssize_t
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL

def text(hwnd):
    size=u.SendMessageW(hwnd,0xE,0,0)
    value=ctypes.create_unicode_buffer(size+1)
    u.SendMessageW(hwnd,0xD,size+1,ctypes.cast(value,ctypes.c_void_p).value)
    return value.value

def set_text(hwnd,value):
    buffer=ctypes.create_unicode_buffer(value)
    u.SendMessageW(hwnd,0xC,0,ctypes.cast(buffer,ctypes.c_void_p).value)

def until(predicate,timeout=15):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=predicate()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Native/engine condition timed out')

def main():
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            data=b'<title>Navigation fixture</title><h1>Turan PDF fixture</h1><p>needle one</p><p>needle two</p>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    proof={}
    try:
        with tempfile.TemporaryDirectory(prefix='turan-navigation-') as temporary:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
            exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
            url='http://127.0.0.1:'+str(server.server_port)+'/fixture'
            proc=subprocess.Popen([str(exe),'--profile='+temporary,'--url='+url,'--remote-debugging-port='+str(port),'--turan-test-file-dialog'],cwd=exe.parent)
            handles=[]
            try:
                window=until(lambda:windows(proc.pid,'TuranWindow'))[0]
                def targets():
                    try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=3))
                    except OSError:return []
                page=until(lambda:next((r for r in targets() if r['url']==url),None))
                until(lambda:evaluate(page,'document.readyState')=='complete')
                time.sleep(.5)
                u.SendMessageW(window,0x111,244,0)
                time.sleep(.3)
                initial_scale=evaluate(page,'window.devicePixelRatio')
                u.SendMessageW(window,0x111,242,0)
                time.sleep(.3)
                proof['zoomInitialAndIncreased']=[initial_scale,evaluate(page,'window.devicePixelRatio')]
                until(lambda:evaluate(page,'window.devicePixelRatio')>initial_scale*1.15)
                u.SendMessageW(window,0x111,244,0)
                until(lambda:abs(evaluate(page,'window.devicePixelRatio')-initial_scale)<.02)
                proof['zoomChangesRendererAndResets']=True
                u.SendMessageW(window,0x111,241,0)
                panel=until(lambda:windows(proc.pid,'TuranPanel'))[-1]
                set_text(u.GetDlgItem(panel,60),'needle')
                until(lambda:text(u.GetDlgItem(panel,65)).endswith('/ 2'))
                first=text(u.GetDlgItem(panel,65));u.SendMessageW(panel,0x111,61,0)
                until(lambda:text(u.GetDlgItem(panel,65))!=first)
                proof['findMatchesAndNext']=True
                u.SendMessageW(panel,0x10,0,0)
                style=u.GetWindowLongPtrW(window,-16)
                u.SendMessageW(window,0x111,245,0)
                assert u.GetWindowLongPtrW(window,-16)&0x00CF0000==0
                assert not u.IsWindowVisible(u.GetDlgItem(window,20))
                u.SendMessageW(window,0x111,245,0)
                assert u.GetWindowLongPtrW(window,-16)==style
                assert u.IsWindowVisible(u.GetDlgItem(window,20))
                proof['fullscreenRestoresNativeControls']=True
                pdf=Path(temporary)/'navigation-test.pdf'
                proof['pdfChooser']='Isolated local fixture selects a path through CefDialogHandler; native chooser automation remains unverified'
                u.PostMessageW(window,0x111,247,0)
                until(lambda:pdf.is_file() and pdf.stat().st_size>1000)
                data=pdf.read_bytes()
                assert data.startswith(b'%PDF-') and b'%%EOF' in data and b'/Type /Page' in data
                until(lambda:text(u.GetDlgItem(window,21))=='PDF saved')
                proof['actualCefPdfBytes']=len(data)
                u.SendMessageW(window,0x111,248,0)
                until(lambda:any(r['url'].startswith('devtools://') for r in targets()))
                u.SendMessageW(window,0x111,248,0)
                until(lambda:not any(r['url'].startswith('devtools://') for r in targets()))
                proof['devToolsOpenAndClose']=True
                u.SendMessageW(window,0x111,248,0) # root close must also close tools
                handles=hold_children(proc.pid)
                u.PostMessageW(window,0x10,0,0)
                assert proc.wait(timeout=15)==0
                wait_children(handles);handles=[]
                proof['shutdownWithDevTools']=True
            except Exception as error:
                if 'pdf' in locals():proof['pdfExistsAfterFailure']=pdf.exists()
                proof['failure']=str(error);raise
            finally:
                if proc.poll() is None:
                    handles=handles or hold_children(proc.pid)
                    subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                    proc.wait(timeout=15)
                if handles:wait_children(handles)
    finally:
        server.shutdown();server.server_close()
        (ROOT/'artifacts/navigation-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2))

if __name__=='__main__':main()
