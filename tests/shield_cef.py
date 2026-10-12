"""Drive actual CEF requests, CSS and native Shield controls on local fixtures."""
import base64
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
from navigation_cef import text,set_text,until
from thirdparty_extensions_cef import evaluate,cdp
from process_tree import hold_children,wait_children

ROOT=Path(__file__).resolve().parents[1]
RULES='''||script.fixture.example^$script,third-party,domain=127.0.0.1
||image.fixture.example^$image
||xhr.fixture.example^$xhr
@@||xhr.fixture.example^*/allowed$xhr
||specific.fixture.example^$xhr,domain=frame.fixture.example
127.0.0.1##.advert
##.generic
127.0.0.1#@#.keep
##.keep
127.0.0.1##[data-promoted]:has(.offer)
127.0.0.1##body{background:url(http://exfil.fixture.example/leak)}
'''

def main():
    requests=[];proof={'scope':'Win11 actual Alloy CEF; bounded ABP subset, no subscription acceptance'}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append((self.headers.get('Host'),self.path,self.headers.get('Cookie','')))
            headers={'Content-Type':'text/html; charset=utf-8','Access-Control-Allow-Origin':self.headers.get('Origin','null'),'Access-Control-Allow-Credentials':'true','Cache-Control':'no-store'}
            if self.path.startswith('/image'):
                headers['Content-Type']='image/png';data=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+a6T8AAAAASUVORK5CYII=')
            elif self.path.startswith('/script'):
                headers['Content-Type']='text/javascript';data=b'window.fixtureScriptExecuted=true;'
            elif self.path.startswith('/data') or self.path.startswith('/allowed'):
                data=b'fixture-data'
            else:
                data=b'''<title>Shield fixture</title><div class="advert">ad</div><div class="generic">generic</div><div class="keep">exception</div><div data-promoted><span class="offer">offer</span></div><div id="dynamic"></div>'''
                if self.path=='/csp':headers['Content-Security-Policy']="default-src 'self'; style-src 'none'; script-src 'none'"
            if self.path.startswith('/set-cookie'):headers['Set-Cookie']='fixture_cookie=yes; Path=/'
            self.send_response(200)
            for key,value in headers.items():self.send_header(key,value)
            self.send_header('Content-Length',str(len(data)));self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):pass
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    with tempfile.TemporaryDirectory(prefix='turan-shield-') as temporary:
        profile=Path(temporary);(profile/'shield-custom.txt').write_text(RULES,encoding='utf-8')
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';url=f'http://127.0.0.1:{server.server_port}/fixture'
        proc=None;handles=[]
        def start():
            nonlocal proc
            proc=subprocess.Popen([str(exe),'--profile='+temporary,'--url='+url,'--remote-debugging-port='+str(port),
                '--host-resolver-rules=MAP *.fixture.example 127.0.0.1, MAP *.example.co.uk 127.0.0.1, MAP *.blogspot.com 127.0.0.1','--no-proxy-server'],cwd=exe.parent)
            window=until(lambda:windows(proc.pid,'TuranWindow'))[0]
            def targets():
                try:return json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list',timeout=3))
                except OSError:return []
            page=until(lambda:next((r for r in targets() if r['url']==url),None))
            until(lambda:evaluate(page,'document.readyState')=='complete')
            return window,page,targets
        def stop(window):
            nonlocal handles
            handles=hold_children(proc.pid);u.PostMessageW(window,0x10,0,0)
            assert proc.wait(timeout=15)==0;wait_children(handles);handles=[]
        def fetch(page,host,path):
            address=f'http://{host}:{server.server_port}{path}'
            return evaluate(page,f"fetch({json.dumps(address)},{{cache:'no-store',credentials:'include'}}).then(r=>r.text()).then(()=>true).catch(()=>false)")
        def resource(page,host,path,kind):
            address=f'http://{host}:{server.server_port}{path}'
            return evaluate(page,f"new Promise(resolve=>{{const el=document.createElement({json.dumps(kind)});el.onload=()=>resolve(true);el.onerror=()=>resolve(false);el.src={json.dumps(address)};document.body.append(el);}})")
        def navigate(page,address):
            cdp(page,'Page.navigate',{'url':address});until(lambda:evaluate(page,'location.href')==address and evaluate(page,'document.readyState')=='complete');time.sleep(.4)
        def panel(window):
            u.SendMessageW(window,0x111,107,0)
            return until(lambda:next((h for h in windows(proc.pid,'TuranPanel') if u.GetDlgItem(h,80)),None))
        def reload_page(page):
            cdp(page,'Page.reload');time.sleep(.5);until(lambda:evaluate(page,'document.readyState')=='complete')
        try:
            window,page,targets=start()
            until(lambda:evaluate(page,"getComputedStyle(document.querySelector('.advert')).display")=='none')
            assert evaluate(page,"getComputedStyle(document.querySelector('.generic')).display")=='none'
            assert evaluate(page,"getComputedStyle(document.querySelector('.keep')).display")!='none'
            assert evaluate(page,"getComputedStyle(document.querySelector('[data-promoted]')).display")=='none'
            evaluate(page,"document.querySelector('#dynamic').innerHTML='<div class=advert>dynamic</div>'")
            assert evaluate(page,"getComputedStyle(document.querySelector('#dynamic .advert')).display")=='none'
            assert not any('exfil.fixture.example' in host for host,path,cookie in requests)
            proof['scopedGenericExceptionHasAndDynamicCosmetics']=True
            proof['maliciousCssCannotInitiateRequest']=True
            before=len(requests)
            assert not resource(page,'script.fixture.example','/script-blocked','script')
            assert not resource(page,'image.fixture.example','/image-blocked','img')
            assert not fetch(page,'xhr.fixture.example','/data-blocked')
            allowed=fetch(page,'xhr.fixture.example','/allowed');proof['allowedResult']=allowed
            assert allowed
            assert fetch(page,'script.fixture.example','/data-type-negative')
            observed=[path for host,path,cookie in requests[before:]]
            assert '/script-blocked' not in observed and '/image-blocked' not in observed and '/data-blocked' not in observed
            assert '/allowed' in observed and '/data-type-negative' in observed
            proof['realNetworkTypesExceptionsAndNegativeControls']=observed
            shield=panel(window);until(lambda:'Custom: 3' in text(u.GetDlgItem(shield,80)))
            count=u.SendMessageW(u.GetDlgItem(shield,30),0x18B,0,0);assert count==3
            proof['actualBlockedEventCount']=count
            assert not fetch(page,'xhr.fixture.example','/allowed?oversize='+('a'*65536))
            until(lambda:u.SendMessageW(u.GetDlgItem(shield,30),0x18B,0,0)==4)
            import ctypes
            line=ctypes.create_unicode_buffer(4096)
            u.SendMessageW(u.GetDlgItem(shield,30),0x189,3,ctypes.cast(line,ctypes.c_void_p).value)
            assert '[URL budget]' in line.value
            assert not any(path.startswith('/allowed?oversize=') for host,path,cookie in requests)
            proof['actualOversizedExceptionUrlBlockedByBudget']=True
            u.SendMessageW(shield,0x111,81,0);time.sleep(.6)
            assert resource(page,'script.fixture.example','/script-site-allowed','script')
            assert evaluate(page,"getComputedStyle(document.querySelector('.advert')).display")!='none'
            assert (profile/'shield-sites.txt').read_text().strip()=='127.0.0.1'
            u.SendMessageW(shield,0x10,0,0);stop(window)
            window,page,targets=start();assert fetch(page,'xhr.fixture.example','/data-restart-allowed')
            shield=panel(window);u.SendMessageW(shield,0x111,81,0);time.sleep(.6)
            assert not fetch(page,'xhr.fixture.example','/data-restart-blocked')
            proof['siteExceptionNativeToggleRestartAndReenable']=True
            u.SendMessageW(shield,0x111,82,0)
            editor=until(lambda:next((h for h in windows(proc.pid,'TuranPanel') if u.GetDlgItem(h,84)),None))
            old=(profile/'shield-custom.txt').read_bytes()
            set_text(u.GetDlgItem(editor,84),'||xhr.fixture.example^$unsupported-option\n')
            u.SendMessageW(editor,0x111,85,0);until(lambda:'Rejected rule' in text(u.GetDlgItem(editor,86)))
            assert (profile/'shield-custom.txt').read_bytes()==old
            assert not fetch(page,'xhr.fixture.example','/data-after-invalid')
            set_text(u.GetDlgItem(editor,84),RULES+'||new.fixture.example^$xhr\n')
            u.SendMessageW(editor,0x111,85,0);until(lambda:text(u.GetDlgItem(editor,86)).startswith('Saved'))
            assert not fetch(page,'new.fixture.example','/data-new-blocked')
            assert (profile/'shield-custom.txt.bak').read_bytes()==old
            proof['asyncNativeEditorRejectRetainAndAtomicBackupApply']=True
            u.SendMessageW(editor,0x10,0,0);u.SendMessageW(shield,0x10,0,0)
            # Document scope for requests made by a subframe differs from the
            # cookie top-level site. Cross-origin frame uses its own $domain.
            frame_url=f'http://frame.fixture.example:{server.server_port}/frame'
            evaluate(page,f"(()=>{{const f=document.createElement('iframe');f.src={json.dumps(frame_url)};document.body.append(f);}})()")
            time.sleep(.7)
            frame_target=until(lambda:next((r for r in targets() if r['url']==frame_url),None))
            assert not fetch(frame_target,'specific.fixture.example','/data-frame')
            assert fetch(page,'specific.fixture.example','/data-top-negative')
            proof['subframeDocumentScopeAndTopNegativeControl']=True
            # Cookie siblings are same-site by PSL; two blogspot tenants are not.
            sibling=f'http://a.example.co.uk:{server.server_port}/cookie-fixture'
            navigate(page,sibling)
            assert fetch(page,'b.example.co.uk','/set-cookie-sibling')
            assert fetch(page,'b.example.co.uk','/data-cookie-sibling')
            assert next(cookie for host,path,cookie in reversed(requests) if path=='/data-cookie-sibling')=='fixture_cookie=yes'
            navigate(page,f'http://a.blogspot.com:{server.server_port}/cookie-fixture')
            assert fetch(page,'b.blogspot.com','/set-cookie-private-suffix')
            assert fetch(page,'b.blogspot.com','/data-cookie-private-suffix')
            assert next(cookie for host,path,cookie in reversed(requests) if path=='/data-cookie-private-suffix')==''
            proof['actualPslSiblingCookiesAndPrivateSuffixBoundary']=True
            navigate(page,url)
            u.SendMessageW(window,0x111,105,0);time.sleep(.6)
            private=until(lambda:next((r for r in targets() if r['type']=='page' and r['id']!=page['id']),None))
            navigate(private,url);assert not fetch(private,'xhr.fixture.example','/data-private-blocked')
            shield=panel(window);assert not u.IsWindowEnabled(u.GetDlgItem(shield,82))
            private_count=u.SendMessageW(u.GetDlgItem(shield,30),0x18B,0,0);assert private_count==1
            sites_before=(profile/'shield-sites.txt').read_bytes()
            u.SendMessageW(shield,0x111,81,0);time.sleep(.6)
            assert fetch(private,'xhr.fixture.example','/data-private-site-allowed')
            assert (profile/'shield-sites.txt').read_bytes()==sites_before
            u.SendMessageW(shield,0x10,0,0);u.SendMessageW(window,0x111,104,0);time.sleep(.5)
            shield=panel(window);assert u.SendMessageW(u.GetDlgItem(shield,30),0x18B,0,0)==0
            assert not fetch(page,'xhr.fixture.example','/data-normal-still-blocked')
            u.SendMessageW(shield,0x10,0,0)
            proof['privateEventAndSiteExceptionIsolation']=True
            navigate(page,f'http://127.0.0.1:{server.server_port}/csp')
            assert evaluate(page,"getComputedStyle(document.querySelector('.advert')).display")!='none'
            proof['strictCspCosmeticLimitation']='style-src none blocks injected styles; no CSP bypass claimed'
            stop(window)
            assert 'data-private-blocked' not in (profile/'browser-state.txt').read_text()
            proof['passed']=True
        except Exception as error:
            proof['failure']=repr(error);proof['requestsAtFailure']=requests
            shield=panel(window);proof['panelAtFailure']=text(u.GetDlgItem(shield,80));raise
        finally:
            if proc and proc.poll() is None:
                handles=handles or hold_children(proc.pid)
                subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
            if handles:wait_children(handles)
            server.shutdown();server.server_close()
            (ROOT/'artifacts/shield-cef.json').write_text(json.dumps(proof,indent=2)+'\n')
            print(json.dumps(proof,indent=2))

if __name__=='__main__':main()
