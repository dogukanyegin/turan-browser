"""Real CEF permission UI, persistence and private-context tests with a fake media device."""
import ctypes
import http.server
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request
from ui_smoke import u,windows,navigate_bar
from process_tree import hold_children,wait_children
from thirdparty_extensions_cef import evaluate

ROOT=Path(__file__).resolve().parents[1]

def until(test,seconds=15):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        result=test()
        if result:return result
        time.sleep(.1)
    raise AssertionError('Timed out waiting for permission test state')

def main():
    report={'hardwareScope':'Chromium fake media device; no physical microphone/camera capture','os':'Windows 11 x64'}
    class Server(http.server.BaseHTTPRequestHandler):
        def handle(self):
            try:super().handle()
            except ConnectionResetError:pass
        def do_GET(self):
            body=b'''<title>Permission fixture</title><p>Local permission fixture</p><script>
            addEventListener('message',e=>{if(e.source===parent&&e.data==='request-microphone'){
              navigator.mediaDevices.getUserMedia({audio:true}).then(s=>{s.getTracks().forEach(t=>t.stop());parent.postMessage('granted','*')}).catch(e=>parent.postMessage(e.name,'*'));
            }});</script>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(body)))
            self.end_headers();self.wfile.write(body)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    embedded=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=embedded.serve_forever,daemon=True).start()
    base='http://127.0.0.1:'+str(server.server_port)
    proc=None;handles=[]
    try:
      with tempfile.TemporaryDirectory(prefix='turan-permissions-') as temporary:
        profile=Path(temporary);state=profile/'browser-state.txt';exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
        def launch(suffix):
            nonlocal proc,port,window
            with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
            proc=subprocess.Popen([str(exe),'--profile='+temporary,'--url='+base+suffix,
                '--remote-debugging-port='+str(port),'--use-fake-device-for-media-stream',
                '--enable-logging','--vmodule=media_stream*=3,user_media*=3',
                '--log-file='+str(ROOT/'artifacts/permissions-engine.log')],cwd=exe.parent)
            window=until(lambda:windows(proc.pid,'TuranWindow'))[0]
            return page(base+suffix)
        def targets():
            try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
            except OSError:return []
        def page(url):
            target=until(lambda:next((row for row in targets() if row['url']==url and row['type']=='page'),None))
            until(lambda:evaluate(target,'location.href==='+json.dumps(url)+'&&document.readyState==="complete"'))
            return target
        def request_media(target,camera=False):
            constraints='{video:true}' if camera else '{audio:true}'
            evaluate(target,"window.permissionError='';window.permissionResult='pending';navigator.mediaDevices.getUserMedia("+constraints+").then(s=>{setTimeout(()=>{window.permissionResult=s.getTracks().every(t=>t.readyState==='live')?'granted':'ended-before-observation';s.getTracks().forEach(t=>t.stop())},250)}).catch(e=>{window.permissionError=e.message;window.permissionResult=e.name});'started'")
        def result(target):
            value=evaluate(target,'window.permissionResult');report['lastRendererResult']=value
            if value not in ('pending','granted','NotAllowedError','1','denied',None):report['lastRendererError']=evaluate(target,'window.permissionError')
            return value
        def prompt(choice):
            dialog=until(lambda:windows(proc.pid,'TuranPermission'))[0];report['lastPromptChoice']=choice
            button=until(lambda:u.GetDlgItem(dialog,choice))
            u.PostMessageW(button,0xf5,0,0)
            until(lambda:not windows(proc.pid,'TuranPermission'))
        def navigate(url):
            navigate_bar(window,url)
            return page(url)
        def close():
            nonlocal handles
            handles=hold_children(proc.pid);u.PostMessageW(window,0x10,0,0)
            assert proc.wait(timeout=15)==0;wait_children(handles);handles=[]
        def policy(kind):
            return [line for line in state.read_text(encoding='utf-8').splitlines() if line.startswith('Q ') and line.split()[-2]==str(kind)]
        def manage(kind,command):
            saved=[int(line.split()[-2]) for line in state.read_text(encoding='utf-8').splitlines() if line.startswith('Q ')]
            rows=saved+[bit for bit in (1,2,4,8,16) if bit not in saved];index=rows.index(kind)
            u.SendMessageW(window,0x111,203,0);panel=windows(proc.pid,'TuranPanel')[-1]
            listing=u.GetDlgItem(panel,30);u.SendMessageW(listing,0x186,index,0)
            u.SendMessageW(panel,0x111,(1<<16)|30,listing);u.SendMessageW(panel,0x111,command,0)
            u.SendMessageW(panel,0x10,0,0)
        # No user profile or clipboard/screen content is accessed by this fixture.
        port=0;window=None
        try:
            normal=launch('/normal')
            request_media(normal);prompt(70);until(lambda:result(normal)=='granted')
            assert len(policy(1))==1 and policy(1)[0].endswith(' 1 1')
            report['nativeConsentAndPersistentMicrophoneRule']=True
            request_media(normal);until(lambda:result(normal)=='granted');assert not windows(proc.pid,'TuranPermission')
            evaluate(normal,"window.permissionResult='pending';navigator.mediaDevices.getUserMedia({audio:true}).then(s=>{window.heldStream=s;window.permissionResult='granted'}).catch(e=>window.permissionResult=e.name);'started'")
            until(lambda:result(normal)=='granted')
            time.sleep(.5)
            assert evaluate(normal,'window.heldStream.getAudioTracks()[0].readyState')=='live'
            manage(1,34)
            until(lambda:evaluate(normal,'window.heldStream.getAudioTracks()[0].readyState')=='ended')
            report['revokingMicrophoneStopsExistingCapture']=True
            request_media(normal);until(lambda:result(normal)=='NotAllowedError');assert not windows(proc.pid,'TuranPermission')
            assert policy(1)[0].endswith(' 1 0');report['siteManagerBlockReachesEngine']=True
            manage(1,32);assert not policy(1)
            request_media(normal);prompt(70);until(lambda:result(normal)=='granted')
            report['siteManagerAskResetsEngine']=True
            normal_policy=policy(1)
            u.SendMessageW(window,0x111,105,0);time.sleep(.5);private=navigate(base+'/private')
            request_media(private);prompt(70);until(lambda:result(private)=='granted')
            assert policy(1)==normal_policy and '/private' not in state.read_text(encoding='utf-8')
            request_media(private);until(lambda:result(private)=='granted');assert not windows(proc.pid,'TuranPermission')
            u.SendMessageW(window,0x111,104,0)
            until(lambda:u.SendMessageW(u.GetDlgItem(window,10),0x1304,0,0)==1)
            u.SendMessageW(window,0x111,105,0);time.sleep(.5);private=navigate(base+'/private-new-session')
            request_media(private);prompt(71);until(lambda:result(private)=='NotAllowedError')
            assert policy(1)==normal_policy
            report['privateConsentIsSeparateAndForgottenAfterLastPrivateTab']=True
            u.SendMessageW(window,0x111,104,0);close()
            normal=launch('/restart');request_media(normal)
            until(lambda:result(normal)=='granted');assert not windows(proc.pid,'TuranPermission')
            report['normalConsentSurvivesRestart']=True
            request_media(normal,True);prompt(70);until(lambda:result(normal)=='granted')
            assert len(policy(2))==1 and policy(2)[0].endswith(' 2 1')
            manage(2,34);request_media(normal,True);until(lambda:result(normal)=='NotAllowedError')
            report['cameraConsentAndRevocation']=True
            evaluate(normal,"window.permissionResult='pending';Notification.requestPermission().then(value=>window.permissionResult=value);'started'")
            prompt(70);until(lambda:result(normal)=='granted')
            assert len(policy(8))==1 and policy(8)[0].endswith(' 8 1')
            manage(8,34)
            until(lambda:evaluate(normal,'Notification.permission')=='denied')
            report['notificationConsentAndEngineRevocation']=True
            evaluate(normal,"window.permissionResult='pending';navigator.geolocation.getCurrentPosition(()=>window.permissionResult='unexpected-position',e=>window.permissionResult=String(e.code),{timeout:5000});'started'")
            prompt(71);until(lambda:result(normal)=='1')
            assert len(policy(4))==1 and policy(4)[0].endswith(' 4 0')
            report['geolocationDenial']=True
            manage(4,33)
            until(lambda:evaluate(normal,"(async()=>{return (await navigator.permissions.query({name:'geolocation'})).state})()")=='granted')
            manage(4,32)
            until(lambda:evaluate(normal,"(async()=>{return (await navigator.permissions.query({name:'geolocation'})).state})()")=='prompt')
            report['geolocationManagerAllowAndResetWithoutReadingPosition']=True
            manage(16,34)
            assert evaluate(normal,"(async()=>{return (await navigator.permissions.query({name:'clipboard-read'})).state})()")=='denied'
            report['clipboardPermissionDenialWithoutReadingClipboard']=True
            manage(16,33)
            until(lambda:evaluate(normal,"(async()=>{return (await navigator.permissions.query({name:'clipboard-read'})).state})()")=='granted')
            manage(16,32)
            until(lambda:evaluate(normal,"(async()=>{return (await navigator.permissions.query({name:'clipboard-read'})).state})()")=='prompt')
            report['clipboardManagerAllowAndResetWithoutReadingClipboard']=True
            # Navigation destroys pending media UI; an old document cannot receive new consent.
            manage(1,32);request_media(normal)
            until(lambda:windows(proc.pid,'TuranPermission'))
            normal=navigate(base+'/cancelled-request')
            until(lambda:not windows(proc.pid,'TuranPermission'))
            assert not policy(1);report['navigationCancelsPendingPrompt']=True
            iframe_origin='http://127.0.0.1:'+str(embedded.server_port)
            assert evaluate(normal,'(async()=>{const frame=document.createElement("iframe");frame.id="fixture-frame";frame.allow="microphone";const loaded=new Promise(resolve=>frame.onload=()=>resolve(true));frame.src='+json.dumps(iframe_origin+'/embedded')+';document.body.append(frame);return await loaded})()')
            evaluate(normal,'document.getElementById("fixture-frame").contentWindow.postMessage("request-microphone","*");"requested"')
            until(lambda:windows(proc.pid,'TuranPermission'))
            evaluate(normal,'document.getElementById("fixture-frame").src='+json.dumps(iframe_origin+'/replacement')+';"navigated"')
            until(lambda:not windows(proc.pid,'TuranPermission'))
            assert iframe_origin not in state.read_text(encoding='utf-8')
            report['subframeNavigationCancelsMediaConsentWithoutPersistingIt']=True
            close();report['cleanExit']=True
        except Exception as error:
            report['failure']=str(error);raise
        finally:
            if proc and proc.poll() is None:
                handles=handles or hold_children(proc.pid)
                subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True)
                proc.wait(timeout=15)
            if handles:wait_children(handles)
    finally:
        server.shutdown();server.server_close()
        embedded.shutdown();embedded.server_close()
        (ROOT/'artifacts/permissions-cef.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report,indent=2))

if __name__=='__main__':main()
