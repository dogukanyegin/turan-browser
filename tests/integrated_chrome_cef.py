"""Real normal-window/tab API acceptance candidate on the pinned CEF engine.

Fresh profiles, synthetic loopback documents and a local MV3 fixture only.
No SetParent or native focus compensation is used by this test.
"""
import ctypes
from ctypes import wintypes as w
import hashlib
import http.server
import json
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import urllib.request
from ui_smoke import windows, u
from process_tree import hold_children, wait_children
from thirdparty_extensions_cef import evaluate, until

ROOT=Path(__file__).resolve().parents[1]
u.GetParent.argtypes=[w.HWND];u.GetParent.restype=w.HWND
u.GetWindow.argtypes=[w.HWND,w.UINT];u.GetWindow.restype=w.HWND
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.GetWindowLongPtrW.argtypes=[w.HWND,ctypes.c_int];u.GetWindowLongPtrW.restype=ctypes.c_ssize_t

def main():
    exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
    proof={'runtime':'chrome-integrated','scope':'normal native Chromium window with Turan native panels; custom toolbar acceptance remains separate',
           'binarySha256':hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            data=b'<title>Turan integrated fixture</title><p id="fixture">Turan</p>'
            self.send_response(200);self.end_headers();self.wfile.write(data)
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length',0)));self.send_response(204);self.end_headers()
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
      with tempfile.TemporaryDirectory(prefix='turan-integrated-') as temporary:
        profile=Path(temporary)/'profile';extension=Path(temporary)/'probe'
        shutil.copytree(ROOT/'tests/extensions/mv3_probe',extension)
        manifest=json.loads((extension/'manifest.json').read_text())
        manifest['permissions'].append('tabGroups')
        (extension/'manifest.json').write_text(json.dumps(manifest))
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        base='http://127.0.0.1:'+str(server.server_port)
        process=subprocess.Popen([str(exe),'--profile='+str(profile),'--turan-runtime=chrome-integrated',
            '--remote-debugging-port='+str(port),'--load-extension='+str(extension),'--url='+base+'/first'],cwd=exe.parent)
        handles=[]
        def targets():
            try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
            except OSError:return []
        try:
            worker=until(lambda:next((r for r in targets() if r['type']=='service_worker'),None))
            until(lambda:evaluate(worker,'typeof chrome !== "undefined" && !!chrome.windows'))
            page=until(lambda:next((r for r in targets() if r['type']=='page' and r['url']==base+'/first'),None))
            controller=until(lambda:windows(process.pid,'TuranWindow'))[0]
            frame=until(lambda:[h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)])[0]
            proof['oneVisibleRoot']=len([h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)])==1 and not u.IsWindowVisible(controller)
            proof['unmodifiedRootStyle']=not u.GetParent(frame) and not (u.GetWindowLongPtrW(frame,-16)&0x40000000)
            assert proof['oneVisibleRoot'] and proof['unmodifiedRootStyle']
            rows=evaluate(worker,'chrome.windows.getAll({populate:true})');proof['initialTopology']=rows
            assert len(rows)==1 and rows[0]['type']=='normal'
            window_id=rows[0]['id'];first=rows[0]['tabs'][0]['id']
            second=evaluate(worker,'chrome.tabs.create({windowId:'+str(window_id)+',url:'+json.dumps(base+'/second')+'})')
            third=evaluate(worker,'chrome.tabs.create({windowId:'+str(window_id)+',url:'+json.dumps(base+'/third')+'})')
            rows=evaluate(worker,'chrome.windows.getAll({populate:true})')
            proof['threeTabsOneWindow']=len(rows)==1 and len(rows[0]['tabs'])==3 and all(t['windowId']==window_id for t in rows[0]['tabs'])
            assert proof['threeTabsOneWindow']
            evaluate(worker,'chrome.tabs.move('+str(third['id'])+',{index:0})')
            proof['reorder']=evaluate(worker,'chrome.tabs.query({windowId:'+str(window_id)+'})')[0]['id']==third['id']
            evaluate(worker,'chrome.tabs.update('+str(third['id'])+',{pinned:true,muted:true})')
            t=evaluate(worker,'chrome.tabs.get('+str(third['id'])+')')
            proof['pinAndMute']=t['pinned'] and t['mutedInfo']['muted']
            group=evaluate(worker,'chrome.tabs.group({tabIds:['+str(first)+','+str(second['id'])+']})')
            evaluate(worker,'chrome.tabGroups.update('+str(group)+',{title:"Turan QA",color:"blue",collapsed:false})')
            g=evaluate(worker,'chrome.tabGroups.get('+str(group)+')')
            proof['tabGroup']=g['title']=='Turan QA' and g['color']=='blue'
            assert proof['reorder'] and proof['pinAndMute'] and proof['tabGroup']
            other=evaluate(worker,'chrome.windows.create({url:'+json.dumps(base+'/other')+',focused:true,type:"normal"})')
            until(lambda:len([h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)])==2)
            evaluate(worker,'chrome.windows.update('+str(window_id)+',{focused:true})')
            proof['automaticFocus']=until(lambda:evaluate(worker,'chrome.windows.get('+str(window_id)+')')['focused'])
            assert proof['automaticFocus']
            evaluate(worker,'chrome.tabs.move('+str(third['id'])+',{windowId:'+str(other['id'])+',index:0})')
            proof['moveBetweenWindows']=evaluate(worker,'chrome.tabs.get('+str(third['id'])+')')['windowId']==other['id']
            assert proof['moveBetweenWindows']
            evaluate(worker,'chrome.tabs.update('+str(first)+',{active:true})')
            # Existing native permission manager is owned by the real frame.
            u.SendMessageW(controller,0x111,203,0)
            panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
            proof['nativePanelOwnedByEngine']=u.GetWindow(panel,4)==frame
            assert proof['nativePanelOwnedByEngine']
            u.PostMessageW(panel,0x10,0,0)
            until(lambda:not windows(process.pid,'TuranPanel'))
            # The same Chromium top-level frame exposes a real Turan Shield
            # native command without cropping/reparenting Chromium controls.
            u.SendMessageW(frame,0x112,0x1F10,0) # WM_SYSCOMMAND / Turan Shield
            shield_panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
            proof['systemMenuTuranShield']=u.GetWindow(shield_panel,4)==frame
            assert proof['systemMenuTuranShield']
            u.PostMessageW(shield_panel,0x10,0,0)
            # The branded native command dock is an *owned tool widget*, not a
            # reparented engine or second Chromium tab/window. It starts
            # collapsed and cannot steal focus from the real Chrome frame.
            dock=until(lambda:windows(process.pid,'TuranEngineTools'))[0]
            proof['turanDockOwnedByChrome']=u.GetWindow(dock,4)==frame
            proof['turanDockAvailable']=bool(u.GetDlgItem(dock,500)) and bool(u.GetDlgItem(dock,107))
            assert proof['turanDockOwnedByChrome'] and proof['turanDockAvailable']
            u.SendMessageW(frame,0x112,0x1F00,0) # Toggle Turan controls
            u.SendMessageW(dock,0x111,202,0)     # Native Downloads command
            dock_panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
            proof['turanDockRoutesToOwnedPanel']=u.GetWindow(dock_panel,4)==frame
            assert proof['turanDockRoutesToOwnedPanel']
            u.PostMessageW(dock_panel,0x10,0,0)
            # The engine, not a synthetic native tab array, owns close semantics.
            evaluate(worker,'chrome.windows.remove('+str(other['id'])+')')
            until(lambda:len(evaluate(worker,'chrome.windows.getAll()'))==1)
            proof['oneWindowCloseRetainsOther']=process.poll() is None and evaluate(page,'document.querySelector("#fixture").textContent')=='Turan'
            assert proof['oneWindowCloseRetainsOther']
            handles=hold_children(process.pid)
            u.PostMessageW(frame,0x10,0,0)
            proof['lastWindowCloseExit']=process.wait(timeout=20)
            wait_children(handles);handles=[]
            assert proof['lastWindowCloseExit']==0
        except Exception as error:
            proof['failure']=repr(error);proof['targetsAtFailure']=targets();raise
        finally:
            if process.poll() is None:
                handles=handles or hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15)
            if handles:wait_children(handles)
    finally:
        server.shutdown();server.server_close()
        (ROOT/'artifacts/integrated-chrome-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
        print(json.dumps(proof,indent=2),flush=True)

if __name__=='__main__':main()
