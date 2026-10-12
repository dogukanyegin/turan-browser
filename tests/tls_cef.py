"""Default TLS certificate validation against a fresh untrusted local certificate."""
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import urllib.request
from ui_smoke import windows,u
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate,until
ROOT=Path(__file__).resolve().parents[1]
def main():
    proof={'scope':'local untrusted self-signed certificate only; no trust-store changes'}
    proc=None;handles=[];servers=[];temporary=None
    try:
      temporary=tempfile.TemporaryDirectory(prefix='turan-tls-')
      folder=Path(temporary.name);profile=folder/'Profile'
      openssl=shutil.which('openssl') or str(Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Git/usr/bin/openssl.exe')
      if not Path(openssl).is_file():raise RuntimeError('OpenSSL is required for this disposable TLS certificate fixture')
      key=folder/'fixture.key';certificate=folder/'fixture.crt'
      subprocess.run([openssl,'req','-x509','-newkey','rsa:2048','-sha256','-nodes','-days','1',
        '-subj','/CN=127.0.0.1','-addext','subjectAltName=IP:127.0.0.1',
        '-keyout',str(key),'-out',str(certificate)],check=True,capture_output=True)
      received=[]
      class Server(http.server.BaseHTTPRequestHandler):
        def handle(self):
          try:super().handle()
          except (ConnectionResetError,BrokenPipeError):pass
        def do_GET(self):
          received.append((self.server.server_port,self.path))
          data=b'<title>TLS fixture HTTP recovery</title><p id="fixture">Local recovery</p>'
          self.send_response(200);self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
      plain=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
      secure=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
      context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(str(certificate),str(key))
      secure.socket=context.wrap_socket(secure.socket,server_side=True)
      servers=[plain,secure]
      for server in servers:threading.Thread(target=server.serve_forever,daemon=True).start()
      with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
      exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';url='http://127.0.0.1:'+str(plain.server_port)+'/start'
      proc=subprocess.Popen([str(exe),'--profile='+str(profile),'--url='+url,'--remote-debugging-port='+str(port)],cwd=exe.parent)
      def targets():
        try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
        except OSError:return []
      target=until(lambda:next((row for row in targets() if row['url']==url and row['type']=='page'),None))
      until(lambda:evaluate(target,'document.readyState==="complete"&&location.href==='+json.dumps(url)))
      destination='https://127.0.0.1:'+str(secure.server_port)+'/tls-fixture'
      data=subprocess.check_output(['node',str(ROOT/'tests/cdp_tls.mjs')],input=json.dumps({'url':target['webSocketDebuggerUrl'],'destination':destination}),text=True,encoding='utf-8')
      rejection=json.loads(data);proof['certificateError']=rejection['failure']['errorText']
      assert proof['certificateError']=='net::ERR_CERT_AUTHORITY_INVALID',rejection
      proof['httpsDocumentNeverRequested']=not any(port==secure.server_port for port,path in received)
      assert proof['httpsDocumentNeverRequested'],'Untrusted HTTPS content reached the application server'
      evaluate(target,'location.href='+json.dumps(url+'?recovered'))
      until(lambda:evaluate(target,'location.href==='+json.dumps(url+'?recovered')+'&&document.title==="TLS fixture HTTP recovery"'))
      proof['httpNavigationRecoversAfterCertificateRejection']=True
      handles=hold_children(proc.pid)
      for window in windows(proc.pid,'TuranWindow'):u.PostMessageW(window,0x10,0,0)
      assert proc.wait(timeout=15)==0;wait_children(handles);handles=[];proof['cleanExit']=True
    except Exception as error:proof['failure']=str(error);raise
    finally:
      if proc and proc.poll() is None:
        handles=handles or hold_children(proc.pid);subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
      if handles:wait_children(handles)
      for server in servers:server.shutdown();server.server_close()
      if temporary:temporary.cleanup()
      (ROOT/'artifacts/tls-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
      print(json.dumps(proof,indent=2))
if __name__=='__main__':main()
