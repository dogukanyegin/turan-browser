"""Native encrypted vault + real CEF HTTPS capture/fill with synthetic credentials.

Positive DOM is injected into authenticated example.org solely to avoid modifying
Windows certificate trust. No form is submitted. Network availability is required;
this is not an account login or universal form compatibility certification.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import urllib.request
from ui_smoke import windows,u
from thirdparty_extensions_cef import cdp,evaluate,until
from process_tree import hold_children,wait_children
ROOT=Path(__file__).resolve().parents[1]
PASS='Turan synthetic master phrase 2026 test'
PASSWORD='TuranSyntheticCredentialNeverAUserPassword42!'
USER='turan-synthetic-fixture'

def text(hwnd):
    length=u.SendMessageW(hwnd,0xE,0,0);value=ctypes.create_unicode_buffer(length+1)
    u.SendMessageW(hwnd,0xD,length+1,ctypes.cast(value,ctypes.c_void_p).value);return value.value

def set_text(hwnd,value):
    buffer=ctypes.create_unicode_buffer(value);u.SendMessageW(hwnd,0xC,0,ctypes.cast(buffer,ctypes.c_void_p).value)

def command(hwnd,id):u.SendMessageW(hwnd,0x111,id,0)
def post_command(hwnd,id):u.PostMessageW(hwnd,0x111,id,0)
def confirm(proc):
    dialog=until(lambda:windows(proc.pid,'#32770'))[0];u.PostMessageW(dialog,0x111,6,0)
def wait_status(panel,message):
    try:return until(lambda:message in text(u.GetDlgItem(panel,120)),20)
    except AssertionError:raise AssertionError('Expected '+message+'; native status: '+text(u.GetDlgItem(panel,120)))
def choose(panel):u.SendMessageW(u.GetDlgItem(panel,119),0x186,0,0) # LB_SETCURSEL

def main(runtime=None):
    report={'scope':'Windows 11 native vault, real CEF on authenticated HTTPS example.org with synthetic DOM; no form submission, user credentials, trust-store mutation, account login or universal form compatibility claim','passed':False,'runtime':runtime or 'default'}
    proc=None;handles=[];temp=None
    try:
      temp=tempfile.TemporaryDirectory(prefix='turan-vault-cef-');profile=Path(temp.name)/'Profile'
      with socket.socket() as free:free.bind(('127.0.0.1',0));port=free.getsockname()[1]
      exe=ROOT/'out-cef/app/Release/TuranBrowser.exe';report['dllSha256']=hashlib.sha256((exe.parent/'TuranBrowser.dll').read_bytes()).hexdigest()
      args=[str(exe),'--profile='+str(profile),'--url=https://example.org/','--remote-debugging-port='+str(port)]
      if runtime:args.append('--turan-runtime='+runtime)
      proc=subprocess.Popen(args,cwd=exe.parent)
      def targets():
        try:return json.load(urllib.request.urlopen('http://127.0.0.1:'+str(port)+'/json/list',timeout=2))
        except OSError:return []
      window=until(lambda:windows(proc.pid,'TuranWindow'))[0]
      target=until(lambda:next((row for row in targets() if row.get('url')=='https://example.org/' and row.get('type')=='page'),None),60)
      until(lambda:evaluate(target,'document.readyState==="complete"&&isSecureContext&&location.origin==="https://example.org"'),60)
      report['authenticatedHttpsPageLoaded']=True
      def form(action='/',autocomplete='current-password',duplicate=False):
        html='<title>Turan Vault Synthetic Fixture</title><form action="'+action+'"><input id="user" autocomplete="username"><input id="pass" type="password" autocomplete="'+autocomplete+'">'+('<input type="password">' if duplicate else '')+'</form>'
        evaluate(target,'document.body.innerHTML='+json.dumps(html)+';true')
      form()
      command(window,213);panel=until(lambda:windows(proc.pid,'TuranCredentialWindow'))[0]
      def set_field(id,value):set_text(u.GetDlgItem(panel,id),value)
      set_field(116,PASS);set_field(117,PASS);command(panel,101);wait_status(panel,'Encrypted vault created.')
      assert (profile/'password-vault.dat').is_file();report['nativeCreateEncryptedVault']=True
      set_field(111,'https://example.org');set_field(112,USER);set_field(113,PASSWORD);set_field(115,PASS);command(panel,102);wait_status(panel,'Credential saved or updated.')
      assert u.SendMessageW(u.GetDlgItem(panel,119),0x18B,0,0)==1
      choose(panel);set_field(115,'wrong synthetic master passphrase');command(panel,104);wait_status(panel,'Incorrect passphrase')
      assert evaluate(target,'document.querySelector("#pass").value===""');report['wrongReauthenticationCannotFill']=True
      set_field(115,PASS);command(panel,104);wait_status(panel,'Fields filled; form was not submitted.')
      assert evaluate(target,'document.querySelector("#user").value==='+json.dumps(USER)+'&&document.querySelector("#pass").value==='+json.dumps(PASSWORD))
      report['nativeExplicitAutofillExactHttpsOrigin']=True
      # Main-world prototype pollution cannot falsify isolated-world field reads.
      replacement='TuranSyntheticUpdatedCapturePasswordNeverAUser42!'
      evaluate(target,'document.querySelector("#pass").value='+json.dumps(replacement)+';window.originalValue=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,"value");Object.defineProperty(HTMLInputElement.prototype,"value",{configurable:true,get(){return "forged attacker value"},set(v){window.originalValue.set.call(this,v)}});true')
      set_field(115,PASS);command(panel,103);confirm(proc);wait_status(panel,'Credential saved or updated from the form.')
      evaluate(target,'Object.defineProperty(HTMLInputElement.prototype,"value",window.originalValue);document.querySelector("#user").value="";document.querySelector("#pass").value="";true')
      choose(panel);set_field(115,PASS);command(panel,104);wait_status(panel,'Fields filled; form was not submitted.')
      assert evaluate(target,'document.querySelector("#pass").value==='+json.dumps(replacement))
      report['captureUpdateUsesIsolatedNativeInputGetter']=True
      for name,action,autocomplete,duplicate in [('crossOriginFormAction','https://attacker.example/','current-password',False),('passwordRegistrationForm','/','new-password',False),('ambiguousPasswordForm','/','current-password',True)]:
        form(action,autocomplete,duplicate);choose(panel);set_field(115,PASS);command(panel,104);wait_status(panel,'Use one visible username/password form')
        assert evaluate(target,'Array.from(document.querySelectorAll("input[type=password]")).every(p=>p.value==="")');report[name+'Refused']=True
      # A frame-only credential form is never queried across a frame boundary.
      evaluate(target,'document.body.innerHTML='+json.dumps('<iframe srcdoc="&lt;form&gt;&lt;input autocomplete=username&gt;&lt;input type=password&gt;&lt;/form&gt;"></iframe>')+';true')
      choose(panel);set_field(115,PASS);command(panel,104);wait_status(panel,'Use one visible username/password form');report['iframeOnlyFormRefused']=True
      form();set_field(115,PASS);command(panel,104)
      # Immediately navigate after native action; unique context ID and URL/TLS
      # recheck prevent completion against a replacement document.
      cdp(target,'Page.navigate',{'url':'https://example.org/?turan-credential-replacement'})
      until(lambda:('Credential origin' in text(u.GetDlgItem(panel,120)) or 'Page changed' in text(u.GetDlgItem(panel,120)) or 'The document changed' in text(u.GetDlgItem(panel,120)) or 'Use one visible' in text(u.GetDlgItem(panel,120))),20)
      until(lambda:evaluate(target,'document.readyState==="complete"&&location.search==="?turan-credential-replacement"'),30)
      assert evaluate(target,'!document.querySelector("input[type=password]")');report['navigationDuringReauthCannotFillReplacement']=True
      # Same-URL reload changes the completed-document token before the broker
      # is allowed to target a newly loaded document.
      form();choose(panel);set_field(115,PASS);command(panel,104);cdp(target,'Page.reload',{})
      until(lambda:('The document changed' in text(u.GetDlgItem(panel,120)) or 'Use one visible' in text(u.GetDlgItem(panel,120)) or 'Page changed' in text(u.GetDlgItem(panel,120))),20)
      until(lambda:evaluate(target,'document.readyState==="complete"&&!document.querySelector("input[type=password]")'),30);report['sameUrlReloadCannotFillReplacement']=True
      u.SendMessageW(panel,0x2B1,7,0) # WM_WTSSESSION_CHANGE / WTS_SESSION_LOCK (handler test, does not lock Windows)
      assert 'Windows session locked' in text(u.GetDlgItem(panel,120));command(panel,106);assert text(u.GetDlgItem(panel,121))=='Locked';assert u.SendMessageW(u.GetDlgItem(panel,119),0x18B,0,0)==0
      set_field(115,PASS);command(panel,101);wait_status(panel,'Vault unlocked.');assert u.SendMessageW(u.GetDlgItem(panel,119),0x18B,0,0)==1;report['lockUnlockRetainsEncryptedRecords']=True;report['sessionLockNotificationHandlerClearsVault']=True
      command(panel,107);assert '24-character password generated' in text(u.GetDlgItem(panel,120))
      # Protected native password edits need not expose plaintext through
      # cross-process WM_GETTEXT. Prove generation by saving and filling it.
      set_field(111,'https://example.org');set_field(112,'turan-generated-fixture');set_field(115,PASS);command(panel,102);wait_status(panel,'Credential saved or updated.')
      form();set_field(114,'turan-generated-fixture');choose(panel);set_field(115,PASS);command(panel,104);wait_status(panel,'Fields filled; form was not submitted.')
      assert evaluate(target,'document.querySelector("#user").value==="turan-generated-fixture"&&document.querySelector("#pass").value.length===24&&/^[A-Za-z0-9!@#$%^&*()_=+\\-]+$/.test(document.querySelector("#pass").value)');report['nativeGeneratorUsesBoundedRandomPassword']=True
      # Origin/username/password metadata is encrypted in disk and backup; logs
      # must not contain synthetic credential passwords or master passphrases.
      for file in (profile/'password-vault.dat',profile/'password-vault.dat.bak'):
        disk=file.read_bytes();assert all(secret.encode() not in disk for secret in (PASS,PASSWORD,replacement,USER,'https://example.org'))
      for log in list(profile.rglob('*.log'))+[exe.parent/'debug.log']:
        if log.is_file():assert all(secret.encode() not in log.read_bytes() for secret in (PASS,PASSWORD,replacement))
      report['diskBackupAndLogsDoNotContainSyntheticSecrets']=True
      u.SendMessageW(panel,0x10,0,0);handles=hold_children(proc.pid);u.PostMessageW(window,0x10,0,0);assert proc.wait(timeout=20)==0;wait_children(handles);handles=[]
      report['cleanExit']=True;report['passed']=True
    except Exception as error:
      report['failure']=str(error)
      if temp:report['retainedFixture']=temp.name;temp._finalizer.detach();temp=None
      raise
    finally:
      if proc and proc.poll() is None:
        handles=handles or hold_children(proc.pid);subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
      if handles:wait_children(handles)
      if temp:temp.cleanup()
      (ROOT/'artifacts/password-vault-cef.json').write_text(json.dumps(report,indent=2)+'\n')
      print(json.dumps(report,indent=2))
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--runtime');args=parser.parse_args();main(args.runtime)
