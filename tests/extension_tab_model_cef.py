"""Try a real extension-created tab in one embedded Chrome popup context."""
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
def main():
    proof={'scope':'isolated architecture experiment; not production extension support'}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            data=b'<title>MV3 fixture</title><p id="fixture">Turan fixture</p>'
            self.send_response(200);self.end_headers();self.wfile.write(data)
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length',0)));self.send_response(204);self.end_headers()
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
      with tempfile.TemporaryDirectory(prefix='turan-tab-model-') as temporary:
        with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
        exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';base='http://127.0.0.1:'+str(server.server_port)
        proc=subprocess.Popen([str(exe),'--profile='+temporary,'--turan-runtime=chrome',
          '--remote-debugging-port='+str(port),'--load-extension='+str(ROOT/'tests/extensions/mv3_probe'),
          '--url='+base+'/fixture'],cwd=exe.parent);handles=[]
        try:
          worker=None;deadline=time.monotonic()+30
          while time.monotonic()<deadline:
            try:
              rows=json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
              worker=next((row for row in rows if row['type']=='service_worker'),None)
              if worker and any(row['url']==base+'/fixture' for row in rows):break
            except OSError:pass
            time.sleep(.2)
          assert worker,'MV3 worker missing'
          proof['before']=evaluate(worker,'chrome.windows.getAll({populate:true})')
          window_id=next(row['id'] for row in proof['before'] if any(tab.get('url')==base+'/fixture' for tab in row.get('tabs',[])))
          proof['created']=evaluate(worker,'chrome.tabs.create({windowId:'+str(window_id)+',url:'+json.dumps(base+'/fixture?api-created')+'})')
          time.sleep(1)
          proof['after']=evaluate(worker,'chrome.windows.getAll({populate:true})')
          native=windows(proc.pid,'TuranWindow')[0]
          proof['nativeTabs']=u.SendMessageW(u.GetDlgItem(native,10),0x1304,0,0)
          fixtures=[row for row in proof['after'] if any('/fixture' in tab.get('url','') for tab in row.get('tabs',[]))]
          proof['sameEngineWindow']=len(fixtures)==1 and len(fixtures[0]['tabs'])==2
          handles=hold_children(proc.pid);u.PostMessageW(native,0x10,0,0)
          try:proof['exitCode']=proc.wait(timeout=15)
          except subprocess.TimeoutExpired:proof['shutdownTimedOut']=True
        except Exception as error:proof['failure']=str(error);raise
        finally:
          if proc.poll() is None:
            handles=handles or hold_children(proc.pid)
            subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
          if handles:wait_children(handles)
    finally:
      server.shutdown();server.server_close()
      (ROOT/'artifacts/extension-api-tab-model.json').write_text(json.dumps(proof,indent=2)+'\n')
      print(json.dumps(proof,indent=2))
    assert proof.get('sameEngineWindow') and proof.get('nativeTabs')==2 and proof.get('exitCode')==0,'Extension-created tab model not integrated with Turan native UI'
if __name__=='__main__':main()
