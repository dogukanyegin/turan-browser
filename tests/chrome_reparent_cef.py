"""Isolated Win32 reparenting experiment for the real normal Chromium tab model.

This probes native ownership and engine APIs; it is not a production UI or an
accessibility/DPI certification. No third-party or user's profile is opened.
"""
import ctypes
from ctypes import wintypes as w
import http.server
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import urllib.request
from ui_smoke import windows,u,navigate_bar
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate,cdp,until

ROOT=Path(__file__).resolve().parents[1]
u.GetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int];u.GetWindowLongPtrW.restype=ctypes.c_ssize_t
u.SetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int,ctypes.c_ssize_t];u.SetWindowLongPtrW.restype=ctypes.c_ssize_t
u.SetParent.argtypes=[w.HWND,w.HWND];u.SetParent.restype=w.HWND
u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.GetAncestor.argtypes=[w.HWND,w.UINT];u.GetAncestor.restype=w.HWND
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
u.GetWindowDpiAwarenessContext.argtypes=[w.HWND];u.GetWindowDpiAwarenessContext.restype=ctypes.c_void_p
u.AreDpiAwarenessContextsEqual.argtypes=[ctypes.c_void_p,ctypes.c_void_p];u.AreDpiAwarenessContextsEqual.restype=w.BOOL

def main():
    proof={'scope':'same-process native ownership experiment; production Alloy path unchanged'}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            data=b'<title>MV3 fixture</title><p id="fixture">Turan fixture</p><input id="typing">'
            self.send_response(200);self.end_headers();self.wfile.write(data)
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length',0)));self.send_response(204);self.end_headers()
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
      with tempfile.TemporaryDirectory(prefix='turan-chrome-reparent-') as temporary:
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';base='http://127.0.0.1:'+str(server.server_port)
        process=subprocess.Popen([str(exe),'--profile='+temporary,'--turan-runtime=chrome-native-tabs',
            '--remote-debugging-port='+str(port),'--load-extension='+str(ROOT/'tests/extensions/mv3_probe'),
            '--url='+base+'/fixture'],cwd=exe.parent);children=[]
        try:
            def targets():
                try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
                except OSError:return []
            worker=until(lambda:next((r for r in targets() if r['type']=='service_worker'),None))
            page=until(lambda:next((r for r in targets() if r['type']=='page' and r['url']==base+'/fixture'),None))
            native=until(lambda:windows(process.pid,'TuranWindow'))[0]
            chrome=until(lambda:[h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)])[0]
            proof['initialTopology']=evaluate(worker,'chrome.windows.getAll({populate:true})')
            proof['matchingDpiAwareness']=bool(u.AreDpiAwarenessContextsEqual(u.GetWindowDpiAwarenessContext(native),u.GetWindowDpiAwarenessContext(chrome)))
            assert proof['matchingDpiAwareness'],'Native/Chrome DPI contexts differ; no forced reset attempted'
            style=u.GetWindowLongPtrW(chrome,-16)
            u.SetWindowLongPtrW(chrome,-16,(style&~0x80CF0000)|0x40000000) # child, no popup/caption/resizing frame
            ctypes.set_last_error(0);u.SetParent(chrome,native)
            proof['setParentError']=ctypes.get_last_error()
            proof['nativeParentAssigned']=u.GetParent(chrome)==native and u.GetAncestor(chrome,2)==native
            assert proof['nativeParentAssigned'],'Chrome frame could not be parented into Turan'
            rect=w.RECT();u.GetClientRect(native,ctypes.byref(rect))
            assert u.SetWindowPos(chrome,None,0,112,rect.right,max(1,rect.bottom-140),0x24)
            until(lambda:evaluate(page,'document.readyState')=='complete')
            proof['rendererMetricsAfterReparent']=cdp(page,'Page.getLayoutMetrics')['cssLayoutViewport']
            u.SendMessageW(native,0x111,103,0)
            until(lambda:u.SendMessageW(u.GetDlgItem(native,10),0x1304,0,0)==2)
            def normal_two():
                rows=evaluate(worker,'chrome.windows.getAll({populate:true})')
                return rows if len(rows)==1 and rows[0]['type']=='normal' and len(rows[0].get('tabs',[]))==2 else None
            proof['twoTabsTopology']=until(normal_two)
            navigate_bar(native,base+'/fixture?second')
            second=until(lambda:next((r for r in targets() if r['type']=='page' and r['url']==base+'/fixture?second'),None))
            until(lambda:evaluate(second,'document.readyState')=='complete')
            # Select the first native tab using only this process's control.
            tabs=u.GetDlgItem(native,10);point=85|(20<<16)
            u.SendMessageW(tabs,0x201,1,point);u.SendMessageW(tabs,0x202,0,point)
            first_id=next(tab['id'] for tab in proof['twoTabsTopology'][0]['tabs'] if tab['url']==base+'/fixture')
            proof['nativeSelectionReachesEngine']=until(lambda:evaluate(worker,'chrome.tabs.query({active:true})')[0]['id']==first_id)
            proof['noSeparateVisibleChromeRoot']=not [h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)]
            proof['rendererStillExecutes']=evaluate(page,"document.querySelector('#fixture').textContent")=='Turan fixture'
            children=hold_children(process.pid);u.PostMessageW(native,0x10,0,0)
            proof['exitCode']=process.wait(timeout=20);wait_children(children);children=[]
        except Exception as error:proof['failure']=str(error);raise
        finally:
            if process.poll() is None:
                children=children or hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15)
            if children:wait_children(children)
    finally:
        server.shutdown();server.server_close()
        (ROOT/'artifacts/chrome-reparent-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2),flush=True)
    assert proof['exitCode']==0 and proof['noSeparateVisibleChromeRoot'] and proof['rendererStillExecutes']

if __name__=='__main__':main()
