"""Actual opt-in native Chrome-host lifecycle, input and extension topology QA.

Only fresh profiles and this process's windows are touched. Stock engine controls
remain present; this is not a custom-toolbar or accessibility acceptance test.
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
from ui_smoke import windows,u,CALLBACK
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate,cdp,until

ROOT=Path(__file__).resolve().parents[1]
u.EnumChildWindows.argtypes=[w.HWND,CALLBACK,w.LPARAM]
u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.GetClientRect.argtypes=[w.HWND,ctypes.POINTER(w.RECT)]
u.SetWindowPos.argtypes=[w.HWND,w.HWND,ctypes.c_int,ctypes.c_int,ctypes.c_int,ctypes.c_int,w.UINT]
u.ShowWindow.argtypes=[w.HWND,ctypes.c_int]

def children(owner,cls):
    found=[]
    @CALLBACK
    def visit(hwnd,unused):
        name=ctypes.create_unicode_buffer(64);u.GetClassNameW(hwnd,name,64)
        if name.value==cls:found.append(hwnd)
        return True
    u.EnumChildWindows(owner,visit,0);return found

def main():
    proof={'runtime':'chrome-reparent-tabs','scope':'opt-in same-thread HWND adapter; stock engine controls retained'}
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
      with tempfile.TemporaryDirectory(prefix='turan-native-chrome-host-') as temporary:
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';base='http://127.0.0.1:'+str(server.server_port)
        process=subprocess.Popen([str(exe),'--profile='+temporary,'--turan-runtime=chrome-reparent-tabs',
            '--remote-debugging-port='+str(port),'--load-extension='+str(ROOT/'tests/extensions/mv3_probe'),
            '--url='+base+'/fixture'],cwd=exe.parent);handles=[]
        try:
            def targets():
                try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
                except OSError:return []
            worker=until(lambda:next((r for r in targets() if r['type']=='service_worker'),None))
            page=until(lambda:next((r for r in targets() if r['type']=='page' and r['url']==base+'/fixture'),None))
            native=until(lambda:windows(process.pid,'TuranWindow'))[0];tabs=u.GetDlgItem(native,10)
            frame=until(lambda:[h for h in children(native,'Chrome_WidgetWin_1') if u.GetParent(h)==native])[0]
            proof['nativeOwnership']=u.GetParent(frame)==native
            rows=evaluate(worker,'chrome.windows.getAll({populate:true})');proof['initialTopology']=rows
            assert len(rows)==1 and rows[0]['type']=='normal'
            first=rows[0]['tabs'][0]['id'];window_id=rows[0]['id']
            initial=cdp(page,'Page.getLayoutMetrics')['cssLayoutViewport']
            assert u.SetWindowPos(native,None,0,0,1000,680,0x16)
            resized=until(lambda:(r if (r:=cdp(page,'Page.getLayoutMetrics')['cssLayoutViewport'])['clientWidth']<initial['clientWidth'] else None))
            proof['resizeReachesRenderer']={'before':initial,'after':resized}
            rect=w.RECT();u.GetWindowRect(frame,ctypes.byref(rect));proof['frameSizeAfterResize']=[rect.right-rect.left,rect.bottom-rect.top]
            u.ShowWindow(native,6);u.ShowWindow(native,9)
            proof['minimizeRestore']=until(lambda:bool(u.IsWindowVisible(frame)) and evaluate(page,'document.readyState')=='complete')
            evaluate(page,"document.querySelector('#typing').focus()")
            cdp(page,'Input.insertText',{'text':'Turan input ğ ü'})
            proof['rendererTextInput']=evaluate(page,"document.querySelector('#typing').value")=='Turan input ğ ü'
            assert proof['rendererTextInput']
            u.SendMessageW(native,0x111,245,0)
            proof['nativeFullscreen']=until(lambda:not u.IsWindowVisible(u.GetDlgItem(native,20)))
            u.SendMessageW(native,0x111,245,0)
            until(lambda:bool(u.IsWindowVisible(u.GetDlgItem(native,20))))
            u.SendMessageW(native,0x111,103,0);until(lambda:u.SendMessageW(tabs,0x1304,0,0)==2)
            def two():
                r=evaluate(worker,'chrome.windows.getAll({populate:true})')
                return r if len(r)==1 and r[0]['id']==window_id and len(r[0]['tabs'])==2 else None
            proof['nativeTwoTabsTopology']=until(two)
            # Native selection must select the actual engine tab, not just its UI row.
            point=85|(20<<16);u.SendMessageW(tabs,0x201,1,point);u.SendMessageW(tabs,0x202,0,point)
            proof['nativeSelectionReachesEngine']=until(lambda:evaluate(worker,'chrome.tabs.query({active:true})')[0]['id']==first)
            created=evaluate(worker,'chrome.tabs.create({windowId:'+str(window_id)+',url:'+json.dumps(base+'/fixture?extension')+'})')
            until(lambda:u.SendMessageW(tabs,0x1304,0,0)==3)
            proof['extensionCreatedTabRegistered']=created['windowId']==window_id
            evaluate(worker,'chrome.tabs.update('+str(first)+',{active:true})')
            proof['engineActivationMatchesNative']=until(lambda:u.SendMessageW(tabs,0x130B,0,0)==0)
            evaluate(worker,'chrome.tabs.update('+str(created['id'])+',{active:true})')
            until(lambda:u.SendMessageW(tabs,0x130B,0,0)==2)
            u.SendMessageW(native,0x111,104,0);until(lambda:u.SendMessageW(tabs,0x1304,0,0)==2)
            proof['closeOneRetainsSharedFrame']=u.IsWindowVisible(frame) and evaluate(page,"document.querySelector('#fixture').textContent")=='Turan fixture'
            assert proof['closeOneRetainsSharedFrame']
            # Record focus truth without compensating/fabricating extension APIs.
            evaluate(worker,'chrome.windows.update('+str(window_id)+',{focused:true})')
            proof['engineReportsFocused']=evaluate(worker,'chrome.windows.get('+str(window_id)+')')['focused']
            proof['automaticWindowFocusAcceptance']='passed in this observation' if proof['engineReportsFocused'] else 'FAILED: windows.update did not establish active engine-frame state'
            evaluate(page,"document.cookie='normalOnly=yes; path=/';localStorage.normalOnly='yes'")
            u.SendMessageW(native,0x111,105,0);until(lambda:u.SendMessageW(tabs,0x1304,0,0)==3)
            value=ctypes.create_unicode_buffer(base+'/fixture?private');edit=u.GetDlgItem(native,20)
            proof['privateEditEnabledBeforeWait']=bool(u.IsWindowEnabled(edit))
            # The tab row is created synchronously; CefBrowser creation is async.
            # Enter navigation only when the native control is actually ready.
            until(lambda:bool(u.IsWindowEnabled(edit)))
            # A real omnibox click gives its native edit focus. Sending WM_SETTEXT
            # to an unfocused edit can be overwritten by initial address events.
            point=10|(12<<16);u.SendMessageW(edit,0x201,1,point);u.SendMessageW(edit,0x202,0,point)
            u.SendMessageW(edit,0xC,0,ctypes.cast(value,ctypes.c_void_p).value);u.SendMessageW(edit,0x100,0xD,0)
            private=until(lambda:next((r for r in targets() if r['type']=='page' and r['url']==base+'/fixture?private'),None))
            until(lambda:evaluate(private,'document.readyState')=='complete')
            proof['privateCookieAndStorageIsolated']=evaluate(private,"!document.cookie.includes('normalOnly')&&localStorage.normalOnly===undefined")
            evaluate(private,"document.cookie='privateOnly=yes; path=/';localStorage.privateOnly='yes'")
            assert proof['privateCookieAndStorageIsolated']
            u.SendMessageW(native,0x111,104,0);until(lambda:u.SendMessageW(tabs,0x1304,0,0)==2)
            proof['privateValuesAbsentFromNormal']=evaluate(page,"!document.cookie.includes('privateOnly')&&localStorage.privateOnly===undefined")
            assert proof['privateValuesAbsentFromNormal']
            proof['noSeparateVisibleEngineRoot']=not [h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)]
            handles=hold_children(process.pid);u.PostMessageW(native,0x10,0,0)
            proof['exitCode']=process.wait(timeout=20);wait_children(handles);handles=[]
            proof['privateHistoryAbsent']=base+'/fixture?private' not in (Path(temporary)/'browser-state.txt').read_text()
            assert proof['privateHistoryAbsent']
            assert proof['exitCode']==0 and proof['noSeparateVisibleEngineRoot']
        except Exception as error:
            proof['failure']=str(error);proof['targetsAtFailure']=targets()
            proof['nativeActiveTabAtFailure']=u.SendMessageW(tabs,0x130B,0,0) if 'tabs' in locals() else None
            raise
        finally:
            if process.poll() is None:
                handles=handles or hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15)
            if handles:wait_children(handles)
    finally:
        server.shutdown();server.server_close()
        (ROOT/'artifacts/native-chrome-host-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2),flush=True)

if __name__=='__main__':main()
