"""Native CEF panel + authenticated external Tor Browser launch on an owned QA copy.

This verifies process launch, not bootstrap, Tor routing, leak resistance or anonymity.
An optional --tor-root must be an extracted QA package under .deps/tor-qa, never a
user installation. Normal builds run refusal tests without needing another engine.
"""
import argparse
import contextlib
import ctypes
import hashlib
import http.server
import json
import re
from pathlib import Path
import subprocess
import tempfile
import threading
import time
from ui_smoke import windows,u,navigate_bar
from process_tree import hold_children,wait_children,creation_time

ROOT=Path(__file__).resolve().parents[1]

def until(test,seconds=25):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.1)
    raise AssertionError('Tor Browser fixture did not reach the expected state')

def text(hwnd):
    length=u.SendMessageW(hwnd,0xE,0,0);value=ctypes.create_unicode_buffer(length+1)
    u.SendMessageW(hwnd,0xD,length+1,ctypes.cast(value,ctypes.c_void_p).value);return value.value

def set_text(hwnd,value):
    buffer=ctypes.create_unicode_buffer(value);u.SendMessageW(hwnd,0xC,0,ctypes.cast(buffer,ctypes.c_void_p).value)

def command(hwnd,id):u.SendMessageW(hwnd,0x111,id,0)

@contextlib.contextmanager
def preserve_fixture_on_failure():
    # Retain owned temporary evidence on failure; stop engine processes first.
    temporary=tempfile.TemporaryDirectory(prefix='turan-tor-cef-')
    try:
        yield temporary.name
    except BaseException:
        temporary._finalizer.detach()
        raise
    else:temporary.cleanup()

def process_information(pid,parent=False):
    # Read only the exact launched child's process metadata, no command interpolation.
    field='ParentProcessId' if parent else 'ProcessId'
    script=f'[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new($false); Get-CimInstance Win32_Process -Filter "{field} = {int(pid)}" | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json -Compress'
    value=subprocess.check_output(['powershell.exe','-NoProfile','-Command',script],text=True,encoding='utf-8',errors='replace').strip()
    return [] if not value else ([json.loads(value)] if value.startswith('{') else json.loads(value))

def main(tor_root=None):
    exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
    report={'scope':'Windows 11 native CEF controls and external-process launch; Tor connection/leak/anonymity acceptance is not covered','dllSha256':hashlib.sha256((exe.parent/'TuranBrowser.dll').read_bytes()).hexdigest(),'passed':False}
    requests=[]
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path);data=('<title>'+self.path+'</title>Tor launcher fixture').encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);threading.Thread(target=server.serve_forever,daemon=True).start()
    base='http://127.0.0.1:'+str(server.server_port)
    process=None;tor_pid=None;tor_created=0;tor_core=None;original_core=None
    try:
      if tor_root:
        tor_root=tor_root.resolve();qa=(ROOT/'.deps/tor-qa').resolve()
        assert tor_root.is_relative_to(qa) and tor_root!=qa and not tor_root.is_symlink(),'An owned extracted QA package is required'
        assert (tor_root/'Browser/TorBrowser/Data/Browser/profiles.ini').is_file()
        # Refuse to touch a live process using this fixture before deliberately mutating it.
        encoded=str(tor_root/'Browser/firefox.exe').replace("'","''")
        script=f"@(Get-CimInstance Win32_Process | Where-Object {{$_.ExecutablePath -eq '{encoded}'}}).Count"
        assert subprocess.check_output(['powershell.exe','-NoProfile','-Command',script],text=True).strip()=='0'
        result=subprocess.run([str(ROOT/'out-cef/Release/turan_tor_tests.exe'),'--verify',str(tor_root)],capture_output=True,text=True)
        assert result.returncode==0 and 'VERIFIED 172' in result.stdout
        report['authenticatedCatalogFiles']=172
        # Change one same-size byte in a QA-only immutable file, then restore exactly.
        tor_core=tor_root/'Browser/application.ini';original_core=tor_core.read_bytes()
        altered=bytearray(original_core);altered[-2]^=1;tor_core.write_bytes(altered)
        rejection=subprocess.run([str(ROOT/'out-cef/Release/turan_tor_tests.exe'),'--verify',str(tor_root)],capture_output=True,text=True)
        assert rejection.returncode==2 and 'REJECTED' in rejection.stdout
        tor_core.write_bytes(original_core);original_core=None;report['tamperedCoreRefused']=True
        extra=tor_root/'Browser/turan-qa-unexpected.dll';assert not extra.exists();extra.write_bytes(b'fixture')
        try:
            rejection=subprocess.run([str(ROOT/'out-cef/Release/turan_tor_tests.exe'),'--verify',str(tor_root)],capture_output=True,text=True)
            assert rejection.returncode==2;report['unexpectedLoadableCodeRefused']=True
        finally:extra.unlink()
      with preserve_fixture_on_failure() as temporary:
        profile=Path(temporary)
        (profile/'browser-state.txt').write_text('S 1 1 0 0 1 0\nB "Unrelated legacy data" "https://legacy.example/"\n')
        process=subprocess.Popen([str(exe),'--profile='+str(profile),'--url='+base+'/normal-launch'],cwd=exe.parent)
        window=until(lambda:windows(process.pid,'TuranWindow'))[0];until(lambda:'/normal-launch' in requests)
        command(window,210);panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
        assert 'Tor Browser' in text(panel),repr(text(panel))
        set_text(u.GetDlgItem(panel,90),str(profile/'missing-tor'))
        command(panel,93);until(lambda:u.IsWindowEnabled(u.GetDlgItem(panel,93)))
        assert 'refused' in text(u.GetDlgItem(panel,95))
        report['nativeMissingInstallationRefused']=True
        assert not (profile/'tor-browser-path.txt').exists()
        u.SendMessageW(panel,0x10,0,0)
        # Local/internal pages cannot cause an external file/scheme handler launch.
        command(window,240);until(lambda:text(u.GetDlgItem(window,20))=='')
        command(window,210);panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
        assert not u.IsWindowEnabled(u.GetDlgItem(panel,94));report['internalUrlLaunchDisabled']=True
        u.SendMessageW(panel,0x10,0,0)
        if tor_root:
          for private in (True,False):
            marker='/private-external-secret' if private else '/normal-launch'
            if private:command(window,105)
            navigate_bar(window,base+marker);until(lambda:marker in requests and marker in text(window))
            command(window,210);panel=until(lambda:windows(process.pid,'TuranPanel'))[0]
            assert base+marker==text(u.GetDlgItem(panel,92))
            set_text(u.GetDlgItem(panel,90),str(tor_root)+'\\') # trailing separator must not hang validation
            # FILETIME baseline protects discovery from Windows historical parent-ID reuse.
            started_filetime=time.time_ns()//100+116444736000000000
            command(panel,94);until(lambda:u.IsWindowEnabled(u.GetDlgItem(panel,94)),60)
            assert 'Started;' in text(u.GetDlgItem(panel,95)),text(u.GetDlgItem(panel,95))
            match=re.search(r'process ID: ([0-9]+)\.',text(u.GetDlgItem(panel,95)));assert match
            launcher_pid=int(match.group(1))
            def find_browser():
                # Firefox's Windows launcher can exit after spawning a browser process.
                rows=process_information(launcher_pid)+process_information(launcher_pid,parent=True)
                for child in rows:
                    if Path(child.get('ExecutablePath') or '')==tor_root/'Browser/firefox.exe' and base+marker in child.get('CommandLine','') and creation_time(child['ProcessId'])>=started_filetime:
                        return child
            child=until(find_browser);tor_pid=child['ProcessId'];tor_created=creation_time(tor_pid);assert tor_created
            assert Path(child['ExecutablePath'])==tor_root/'Browser/firefox.exe'
            assert base+marker in child['CommandLine'] and '--profile' not in child['CommandLine']
            assert ' -profile ' not in child['CommandLine'] and '--no-remote' not in child['CommandLine']
            until(lambda:windows(tor_pid,'MozillaWindowClass'),30)
            # Only the exact process that this test launched may be terminated.
            assert creation_time(tor_pid)==tor_created
            subprocess.run(['taskkill','/PID',str(tor_pid),'/T','/F'],capture_output=True,check=True)
            tor_pid=None
            report['privateActualExternalProcess' if private else 'normalActualExternalProcess']=True
            if private:
                assert not (profile/'tor-browser-path.txt').exists()
                for name in ('browser-state.txt','session-v1.txt'):
                    assert marker not in (profile/name).read_text(encoding='utf-8')
                report['privateUrlAndFolderNotPersisted']=True
                u.SendMessageW(panel,0x10,0,0);command(window,104)
            else:
                assert (profile/'tor-browser-path.txt').is_file();data=(profile/'tor-browser-path.txt').read_text(encoding='utf-8')
                assert marker not in data and 'private-external-secret' not in data
                report['normalFolderOnlyPreferenceSaved']=True
                u.SendMessageW(panel,0x10,0,0)
          # External application owns its profile. No Turan data is imported into it.
          assert not (tor_root/'Browser/TorBrowser/Data/Browser/browser-state.txt').exists()
          assert not (tor_root/'Browser/TorBrowser/Data/Browser/profile.default/browser-state.txt').exists()
          report['noTuranMetadataImport']=True
        children=hold_children(process.pid);u.PostMessageW(window,0x10,0,0)
        assert process.wait(timeout=20)==0;wait_children(children);process=None
        assert 'Unrelated legacy data' in (profile/'browser-state.txt').read_text(encoding='utf-8')
        report['legacyDataPreserved']=True
      report['passed']=True
    except Exception as exc:
        report['failure']=str(exc);raise
    finally:
      if original_core is not None:tor_core.write_bytes(original_core)
      if tor_pid and creation_time(tor_pid)==tor_created:
        subprocess.run(['taskkill','/PID',str(tor_pid),'/T','/F'],capture_output=True)
      if process and process.poll() is None:
        children=hold_children(process.pid);subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15);wait_children(children)
      server.shutdown();server.server_close()
      name='tor-browser-full-cef.json' if tor_root else 'tor-browser-refusal-cef.json'
      (ROOT/'artifacts'/name).write_text(json.dumps(report,indent=2)+'\n')
      print(json.dumps(report,indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tor-root',type=Path)
    main(parser.parse_args().tor_root)
