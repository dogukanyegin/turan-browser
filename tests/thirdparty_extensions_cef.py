"""Real publisher MV3 packages on the isolated normal-window architecture probe.

No account credentials, Web Store bypass, or production extension support claim.
"""
import ctypes
import argparse
import http.server
import json
import hashlib
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from test_extensions import prepare
from ui_smoke import windows, u
from process_tree import hold_children as descendant_handles, wait_children as wait_handles
from cdp_transport import cdp

def evaluate(target, expression):
    result = cdp(target, 'Runtime.evaluate', {'expression': expression, 'awaitPromise': True, 'returnByValue': True, 'userGesture':True})
    if 'exceptionDetails' in result: raise RuntimeError(json.dumps(result['exceptionDetails']))
    return result.get('result', {}).get('value')

def until(test,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        time.sleep(.2)
    raise AssertionError('Extension UI did not reach the required ready state')

def main(runtime='chrome-native-tabs'):
    packages = prepare(); reports = {}
    requests = []
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append((self.headers.get('Host'), self.path))
            data = b'<title>MV3 fixture</title><style>body{background:white;color:black}</style><p id="fixture">Turan fixture</p><form><input autocomplete="username"><input type="password" autocomplete="current-password"></form>'
            self.send_response(200); self.send_header('Content-Type','text/html'); self.end_headers(); self.wfile.write(data)
        def do_POST(self):
            self.rfile.read(int(self.headers.get('Content-Length',0)))
            self.send_response(204); self.end_headers()
        def log_message(self,*args): pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0), Server)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        for name, directory in packages.items():
            with tempfile.TemporaryDirectory(prefix='turan-extension-'+name+'-') as temp:
                profile = Path(temp)
                # Disable the shell's starter Shield: blocking must come from uBOL.
                (profile/'browser-state.txt').write_text('S 0 1 0 0 1 0\n')
                with socket.socket() as available:
                    available.bind(('127.0.0.1',0)); debug_port = available.getsockname()[1]
                exe = ROOT/'out-cef/app/Release/TuranBrowser.exe'
                url = 'http://127.0.0.1:'+str(server.server_port)+'/fixture'
                proc = subprocess.Popen([str(exe),'--profile='+temp,'--turan-runtime='+runtime,
                    '--remote-debugging-port='+str(debug_port), '--load-extension='+str(ROOT/'tests/extensions/mv3_probe')+','+str(directory),
                    '--host-resolver-rules=MAP *.doubleclick.net 127.0.0.1', '--url='+url], cwd=exe.parent)
                report = {'scope':'normal-window architecture candidate; production shell unchanged','runtime':runtime,
                          'binarySha256':hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()}
                handles = []
                try:
                    def targets(): return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(debug_port)+'/json/list', timeout=3))
                    deadline = time.monotonic()+30
                    while time.monotonic()<deadline:
                        try:
                            rows = targets()
                            worker = next((r for r in rows if r['type']=='service_worker' and r['url'].endswith('/worker.js')), None)
                            page = next((r for r in rows if r['type']=='page' and r['url']==url),None)
                            if worker and page: break
                        except OSError: pass
                        if proc.poll() is not None: raise RuntimeError('Probe exited early')
                        time.sleep(.2)
                    assert worker and page, 'Fixture worker/page missing'
                    extensions = evaluate(worker, '(async()=>await chrome.management.getAll())()')
                    extension = next(r for r in extensions if r['id'] != worker['url'].split('/')[2] and r.get('installType')=='development')
                    report['extension'] = extension
                    manifest = json.loads((directory/'manifest.json').read_text())
                    report['manifestVersion'] = manifest['manifest_version']
                    assert extension['enabled'] and report['manifestVersion']==3
                    if name=='uBOL':
                        before = len(requests)
                        expression = "(async()=>{try{await fetch('http://googleads.g.doubleclick.net:"+str(server.server_port)+"/pagead/ads?client=turan-test',{mode:'no-cors'});return false}catch(e){return true}})()"
                        report['actualAdRequestBlocked'] = evaluate(page, expression)
                        report['requestReachedServer'] = any('doubleclick.net' in host for host,path in requests[before:])
                        assert report['actualAdRequestBlocked'] and not report['requestReachedServer']
                    elif name=='DarkReader':
                        time.sleep(3)
                        report['dynamicStyleInjected'] = evaluate(page, "!!document.querySelector('style.darkreader')")
                        report['pageColors'] = evaluate(page, "({background:getComputedStyle(document.body).backgroundColor,color:getComputedStyle(document.body).color})")
                        assert report['dynamicStyleInjected'], 'Dark Reader did not apply its actual dynamic theme'
                    popup = 'chrome-extension://'+extension['id']+'/'+manifest['action']['default_popup']
                    focus='(async()=>{const rows=await chrome.tabs.query({url:'+json.dumps(url)+'});await chrome.windows.update(rows[0].windowId,{focused:true});await chrome.tabs.update(rows[0].id,{active:true});return rows[0].windowId})()'
                    def focus_fixture():
                        if runtime=='chrome-reparent-tabs':
                            # Model the user activating Turan's native root. Do
                            # not pretend windows.update can raise a child HWND.
                            root=windows(proc.pid,'TuranWindow')[0]
                            u.SetForegroundWindow.argtypes=[ctypes.c_void_p];u.SetForegroundWindow.restype=ctypes.c_bool
                            report['nativeForegroundRequestAccepted']=bool(u.SetForegroundWindow(root))
                            point=85|(20<<16);strip=u.GetDlgItem(root,10)
                            u.SendMessageW(strip,0x201,1,point);u.SendMessageW(strip,0x202,0,point)
                        identifier=evaluate(worker,focus)
                        until(lambda:evaluate(worker,'(async()=>{return (await chrome.windows.get('+str(identifier)+')).focused})()'))
                        return identifier
                    report['fixtureWindowId']=focus_fixture()
                    background = next(r for r in targets() if r['type']=='service_worker' and r['url'].startswith('chrome-extension://'+extension['id']+'/'))
                    report['configuredActionPopup']=evaluate(background,'chrome.action.getPopup({})')
                    open_popup='(async()=>{await chrome.action.openPopup({windowId:'+str(report['fixtureWindowId'])+'});return true})()'
                    try:
                        report['actionPopupOpened'] = evaluate(background, open_popup)
                    except RuntimeError as error:
                        if name!='Bitwarden':raise
                        # Bitwarden can disable its action until the first-run
                        # browser-password-manager consent screen is dismissed.
                        report['initialActionError'] = str(error)
                        created = cdp(page,'Target.createTarget',{'url':popup})
                        onboarding = until(lambda:next((r for r in targets() if r['id']==created['targetId']),None))
                        report['onboardingText'] = until(lambda:evaluate(onboarding,"(()=>{const text=document.body?.innerText||'';return [...document.querySelectorAll('button')].some(b=>b.innerText.trim()==='Skip')||/email|get started|create account|log in/i.test(text)?text:false})()"))
                        skipped = evaluate(onboarding,"(()=>{const skip=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()==='Skip');if(skip)skip.click();return !!skip})()")
                        report['onboardingSkipped']=skipped
                        report['onboardingReady']=until(lambda:any(word in (evaluate(onboarding,'document.body?.innerText || ""') or '').lower() for word in ('email','get started','create account','log in')))
                        cdp(page,'Target.closeTarget',{'targetId':created['targetId']})
                        focus_fixture()
                        background=until(lambda:next((r for r in targets() if r['type']=='service_worker' and r['url'].startswith('chrome-extension://'+extension['id']+'/')),None))
                        report['actionPopupOpened'] = evaluate(background, open_popup)
                    popup_target = until(lambda:next((r for r in targets() if r['url'].split('#')[0].split('?')[0]==popup),None))
                    report['popupText'] = until(lambda:evaluate(popup_target,'document.body?.innerText || ""'))
                    assert report['popupText'] and len(report['popupText'])>20, 'Extension popup failed to render'
                    if name=='Bitwarden':
                        evaluate(popup_target,"(()=>{const skip=[...document.querySelectorAll('button')].find(b=>b.innerText.trim()==='Skip');if(skip)skip.click();return !!skip})()")
                        time.sleep(1)
                        report['afterSkipText'] = evaluate(popup_target,'document.body?.innerText || ""')
                        report['loggedOutUI'] = any(word in report['afterSkipText'].lower() for word in ('email','get started','create account','log in'))
                        assert report['loggedOutUI'], 'Expected real Bitwarden logged-out UI after onboarding'
                        report['vaultLoginAndAutofill'] = 'NOT TESTED: no test vault/account credentials'
                    await_disable = '(async()=>{await chrome.management.setEnabled('+json.dumps(extension['id'])+',false);return (await chrome.management.get('+json.dumps(extension['id'])+')).enabled===false})()'
                    report['engineDisable'] = evaluate(worker, await_disable)
                    if name=='uBOL':
                        before=len(requests)
                        report['adRequestSucceedsWhenDisabled']=not evaluate(page,expression)
                        report['disabledRequestReachedServer']=any('doubleclick.net' in host for host,path in requests[before:])
                        assert report['adRequestSucceedsWhenDisabled'] and report['disabledRequestReachedServer'],'Network failure was not caused by uBOL'
                    elif name=='DarkReader':
                        cdp(page,'Page.reload');time.sleep(1)
                        report['disabledPageColors']=evaluate(page,"({background:getComputedStyle(document.body).backgroundColor,color:getComputedStyle(document.body).color})")
                        assert report['disabledPageColors']=={'background':'rgb(255, 255, 255)','color':'rgb(0, 0, 0)'},'Disabling Dark Reader did not restore fixture colors'
                    await_enable = await_disable.replace(',false)',',true)').replace('===false','===true')
                    report['engineEnable'] = evaluate(worker, await_enable)
                    assert report['engineDisable'] and report['engineEnable']
                    handles = descendant_handles(proc.pid)
                    for hwnd in windows(proc.pid,'TuranWindow'): u.PostMessageW(hwnd,0x10,0,0)
                    report['exitCode'] = proc.wait(timeout=15)
                    wait_handles(handles); handles=[]
                    assert report['exitCode']==0
                    assert report['actionPopupOpened'],'Actual extension action popup failed; loading/page fallback is insufficient'
                except Exception as error:
                    report['failure'] = str(error)
                finally:
                    if proc.poll() is None:
                        handles=handles or descendant_handles(proc.pid)
                        subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                        proc.wait(timeout=15)
                    if handles: wait_handles(handles)
                reports[name] = report
                print(name,json.dumps({'version':manifest.get('version'),**{key:value for key,value in report.items() if isinstance(value,bool) or key=='failure'}}),flush=True)
    finally:
        server.shutdown();server.server_close()
    filename='thirdparty-extensions.json' if runtime=='chrome-native-tabs' else 'thirdparty-extensions-'+runtime+'.json'
    (ROOT/'artifacts'/filename).write_text(json.dumps(reports,indent=2)+'\n')
    assert all('failure' not in report for report in reports.values()), 'Third-party test failure; evidence retained'

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',choices=['chrome-native-tabs','chrome-reparent-tabs','chrome-integrated'],default='chrome-native-tabs')
    main(parser.parse_args().runtime)
