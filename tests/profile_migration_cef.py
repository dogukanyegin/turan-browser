"""Upgrade immutable 0.1.0 profiles, verify snapshots, restore and reopen the old engine."""
import argparse
import contextlib
import ctypes
import hashlib
import http.server
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import time
import zipfile
from ui_smoke import windows,u
from process_tree import hold_children,wait_children,ProcessEntry,k,creation_time

ROOT=Path(__file__).resolve().parents[1]
BASELINE_SHA='4afa4098507abb2abde6a04456ab8ea90d886017c1f77c2484775f880fad635c'
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inventory(root):
    return {p.relative_to(root).as_posix():digest(p) for p in root.rglob('*')
            if p.is_file() and p.relative_to(root).as_posix().lower() not in ('lockfile','.turan-profile.lock')}
def wait(test,timeout=40):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.05)
    raise AssertionError('Profile migration fixture timed out')
def main(exe):
    archive=ROOT/'dist/TuranBrowser-0.1.0-windows-x64-portable.zip'
    assert digest(archive)==BASELINE_SHA
    engine=json.loads((ROOT/'cef.lock.json').read_text())['version']
    proof={'baselineArchiveSha256':BASELINE_SHA,'engine':engine,'checks':{}}
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            seed=self.path=='/seed'
            data=('''<title>WAIT</title><script>
                %s
                document.title=document.cookie.includes('migration_cookie=fixture')&&
                localStorage.getItem('migration_storage')==='fixture'?'MIGRATION_OK':'MIGRATION_LOST';
                </script>''' % ("localStorage.setItem('migration_storage','fixture');" if seed else '')).encode()
            self.send_response(200);self.send_header('Content-Type','text/html')
            if seed:self.send_header('Set-Cookie','migration_cookie=fixture; Max-Age=86400; Path=/; SameSite=Lax')
            self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
        def log_message(self,*args):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    base='http://127.0.0.1:'+str(server.server_port)
    process=None;owned_workers=[]
    @contextlib.contextmanager
    def process_cleanup():
        try:yield
        finally:
            if process and process.poll() is None:
                children=hold_children(process.pid)
                subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
                process.wait(timeout=15);wait_children(children)
            for pid,handle in owned_workers:
                try:
                    if k.WaitForSingleObject(handle,0)!=0:
                        subprocess.run(['taskkill','/PID',str(pid),'/T','/F'],capture_output=True)
                        assert k.WaitForSingleObject(handle,15000)==0
                finally:k.CloseHandle(handle)
    def launch(binary,profile,url):
        nonlocal process
        process=subprocess.Popen([str(binary),'--profile='+str(profile),'--url='+url],cwd=binary.parent)
        window=wait(lambda:windows(process.pid,'TuranWindow'))[0]
        def ready():
            text=ctypes.create_unicode_buffer(512);u.GetWindowTextW(window,text,512)
            if text.value.startswith('MIGRATION_LOST'):raise AssertionError('Persistent cookie or localStorage was lost')
            return text.value.startswith('MIGRATION_OK')
        wait(ready);return window
    def close(window):
        children=hold_children(process.pid)
        u.PostMessageW(window,0x10,0,0);assert process.wait(timeout=30)==0
        wait_children(children)
    def acknowledge(proc,expected):
        dialog=wait(lambda:windows(proc.pid,'#32770'))[0]
        assert not windows(proc.pid,'TuranWindow')
        button=wait(lambda:u.GetDlgItem(dialog,1) or u.GetDlgItem(dialog,2))
        messages=[]
        callback_type=ctypes.WINFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_ssize_t)
        @callback_type
        def collect(hwnd,unused):
            text=ctypes.create_unicode_buffer(4096);u.GetWindowTextW(hwnd,text,4096)
            if text.value:messages.append(text.value)
            return True
        u.EnumChildWindows.argtypes=[ctypes.c_void_p,callback_type,ctypes.c_ssize_t]
        u.EnumChildWindows(dialog,collect,0)
        u.PostMessageW(button,0xf5,0,0)
        code=proc.wait(timeout=30)
        assert code==expected,{'exit':code,'expected':expected,'ownDialog':messages}
    def utility(profile,option,expected):
        proc=subprocess.Popen([str(exe),'--profile='+str(profile),option],cwd=exe.parent)
        try:acknowledge(proc,expected)
        finally:
            if proc.poll() is None:
                subprocess.run(['taskkill','/PID',str(proc.pid),'/T','/F'],capture_output=True);proc.wait(timeout=15)
    def backup_worker(parent,created):
        snapshot=k.CreateToolhelp32Snapshot(2,0);entry=ProcessEntry();entry.dwSize=ctypes.sizeof(entry)
        found=[]
        try:
            more=k.Process32FirstW(snapshot,ctypes.byref(entry))
            while more:
                if entry.parent==parent and entry.exe=='TuranProfileTool.exe' and creation_time(entry.pid)>=created:found.append(entry.pid)
                more=k.Process32NextW(snapshot,ctypes.byref(entry))
        finally:k.CloseHandle(snapshot)
        return found
    try:
        with tempfile.TemporaryDirectory(prefix='turan-profile-migration-') as temp,process_cleanup():
            outer=Path(temp);old_dir=outer/'baseline';old_dir.mkdir()
            with zipfile.ZipFile(archive) as source:
                assert all((old_dir/name).resolve().is_relative_to(old_dir.resolve()) for name in source.namelist())
                source.extractall(old_dir)
            old=next(old_dir.rglob('TuranBrowser.exe'));profile=outer/'Profile';profile.mkdir()
            metadata='S 1 1 0 0 1 0\nB "Legacy bookmark" "https://legacy.example"\nH "Legacy history" "https://history.example"\n'
            (profile/'browser-state.txt').write_text(metadata)
            window=launch(old,profile,base+'/seed');u.SendMessageW(window,0x111,106,0);close(window)
            before=inventory(profile)
            # New engine must snapshot every old file before its first profile write.
            window=launch(exe,profile,base+'/check')
            snapshots=list((outer/'Profile.turan-backups').glob('snapshot-*'));assert len(snapshots)==1
            snapshot=snapshots[0];assert inventory(snapshot/'profile')==before
            rows=[shlex.split(line) for line in (snapshot/'backup-manifest.txt').read_text(encoding='utf-8').splitlines()]
            assert rows[0][0:2]==['TURAN_PROFILE_BACKUP','1'] and rows[0][3]=='unknown'
            files={row[3]:row[2] for row in rows[1:] if row[0]=='F'};assert files==before
            assert (profile/'turan-engine.txt').read_text().strip()=='TURAN_ENGINE 1 "'+engine+'"'
            assert 'Legacy bookmark' in (profile/'browser-state.txt').read_text() and 'Legacy history' in (profile/'browser-state.txt').read_text()
            proof['checks']['fullPreUpgradeSnapshot']=True
            proof['checks']['persistentCookiesAndLocalStorageAfterUpgrade']=True
            # A second Turan writer, including backup utilities, must be rejected.
            utility(profile,'--backup-profile='+str(outer/'busy-snapshot'),6)
            assert not (outer/'busy-snapshot').exists()
            proof['checks']['liveProfileBackupRefused']=True
            close(window)
            assert 'Legacy bookmark' in (profile/'browser-state.txt').read_text()
            after=inventory(profile)
            utility(profile,'--restore-profile='+str(snapshot),0)
            previous=list(outer.glob('Profile.before-restore-*'));assert len(previous)==1
            assert inventory(previous[0])==after and inventory(profile)==before
            proof['checks']['restorePreservedCurrentProfile']=True
            window=launch(old,profile,base+'/check');close(window)
            proof['checks']['persistentCookiesAndLocalStorageAfterRollback']=True
            # An explicit manual backup runs without any CEF writes.
            before_manual=inventory(profile);manual=outer/'manual'
            utility(profile,'--backup-profile='+str(manual),0)
            assert inventory(profile)==before_manual and inventory(manual/'profile')==before_manual
            proof['checks']['closedProfileUtilityDidNotInitializeEngine']=True
            # Tampered snapshots must leave the current complete profile unchanged.
            (manual/'profile/browser-state.txt').write_text('tampered')
            utility(profile,'--restore-profile='+str(manual),6)
            assert inventory(profile)==before_manual
            proof['checks']['tamperedRestoreRefused']=True
            marker=profile/'turan-engine.txt';marker.write_text('TURAN_ENGINE 1 "155.0.1+gdeadbeef+chromium-155.0.8100.1"\n')
            future=inventory(profile)
            process=subprocess.Popen([str(exe),'--profile='+str(profile),'--url='+base+'/check'],cwd=exe.parent)
            acknowledge(process,6);assert inventory(profile)==future
            proof['checks']['newerEngineDowngradeRefusedBeforeWrites']=True
            marker.unlink()
            attempted=profile/'turan-engine-attempt.txt'
            attempted.write_text('TURAN_ENGINE 1 "155.0.1+gdeadbeef+chromium-155.0.8100.1"\n')
            future=inventory(profile)
            process=subprocess.Popen([str(exe),'--profile='+str(profile),'--url='+base+'/check'],cwd=exe.parent)
            acknowledge(process,6);assert inventory(profile)==future
            proof['checks']['newerAttemptedEngineDowngradeRefused']=True
            attempted.unlink()
            # A successful CEF initialization can precede a failed marker write.
            # The pre-start attempt must remain authoritative in that case.
            readonly=outer/'Readonly';(readonly/'Default').mkdir(parents=True)
            (readonly/'browser-state.txt').write_text(metadata)
            readonly_marker=readonly/'turan-engine.txt'
            readonly_marker.write_text('TURAN_ENGINE 1 "144.0.36+gdeadbeef+chromium-144.0.7559.264"\n')
            k.SetFileAttributesW.argtypes=[ctypes.c_wchar_p,ctypes.c_ulong]
            assert k.SetFileAttributesW(str(readonly_marker),1)
            try:
                process=subprocess.Popen([str(exe),'--profile='+str(readonly),'--url=about:blank'],cwd=exe.parent)
                acknowledge(process,6)
                assert '144.0.36' in readonly_marker.read_text()
                assert engine in (readonly/'turan-engine-attempt.txt').read_text()
                assert (readonly/'Default/Preferences').is_file()
                retained=list((outer/'Readonly.turan-backups').glob('snapshot-*'));assert len(retained)==1
                assert inventory(retained[0]/'profile')=={'browser-state.txt':digest(readonly/'browser-state.txt'),'turan-engine.txt':digest(readonly_marker)}
                proof['checks']['failedSuccessMarkerKeepsAttemptedEngineBound']=True
            finally:assert k.SetFileAttributesW(str(readonly_marker),128)
            # A crash between the two directory moves must roll back before CEF opens.
            previous_crash=outer/'Profile.before-restore-123-1';stage=outer/'Profile.restore-staging-123-1'
            profile.rename(previous_crash)
            journal=outer/'Profile.restore-transaction.txt'
            journal.write_text('TURAN_PROFILE_RESTORE 1 "'+stage.name+'" "'+previous_crash.name+'"\n')
            window=launch(exe,profile,base+'/check');close(window)
            assert not journal.exists() and not previous_crash.exists()
            proof['checks']['crashHandoffRecoveredBeforeEngineStartup']=True
            # The native menu action shuts CEF down before taking its complete snapshot.
            prior=set((outer/'Profile.turan-backups').glob('snapshot-*'))
            window=launch(exe,profile,base+'/check')
            children=hold_children(process.pid);created=creation_time(process.pid)
            u.SendMessageW(window,0x111,207,0)
            worker=wait(lambda:backup_worker(process.pid,created))[0]
            handle=k.OpenProcess(0x100000|0x1000,False,worker);assert handle
            owned_workers.append((worker,handle))
            class Worker:
                pid=worker
                def wait(self,timeout):
                    assert k.WaitForSingleObject(handle,int(timeout*1000))==0
                    code=ctypes.c_ulong();assert k.GetExitCodeProcess(handle,ctypes.byref(code));return code.value
            k.GetExitCodeProcess.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong)]
            assert process.wait(timeout=30)==0
            acknowledge(Worker(),0);wait_children(children)
            new=set((outer/'Profile.turan-backups').glob('snapshot-*'))-prior;assert len(new)==1
            assert inventory(next(iter(new))/'profile')==inventory(profile)
            proof['checks']['nativeCloseAndBackupAction']=True
            before_rejected=inventory(profile)
            assert subprocess.run([str(exe.parent/'TuranProfileTool.exe'),'--profile='+str(profile),
                '--backup-profile='+str(outer/'invalid-worker'),'--parent-handle=0','--ready-handle=0'],cwd=exe.parent).returncode==2
            assert inventory(profile)==before_rejected and not (outer/'invalid-worker').exists()
            proof['checks']['invalidWorkerHandleRefused']=True
            assert digest(archive)==BASELINE_SHA
    finally:
        if process and process.poll() is None:
            children=hold_children(process.pid)
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15);wait_children(children)
        server.shutdown();server.server_close()
        (ROOT/'artifacts').mkdir(exist_ok=True)
        (ROOT/'artifacts/profile-migration-cef.json').write_text(json.dumps(proof,indent=2),encoding='utf-8')
    assert len(proof['checks'])==13 and all(proof['checks'].values())
    print('PASS actual CEF 144 -> 154 full-profile snapshot, persistent cookie/storage migration, verified rollback to 144, live/tampered/downgrade refusal, crash recovery and native backup action',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,default=ROOT/'out-cef/app/Release/TuranBrowser.exe')
    main(parser.parse_args().exe.resolve())
