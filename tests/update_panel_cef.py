"""Actual CEF/native update-panel safety test on a fresh owned profile.

No global input, Windows trust-store change, user profile, production publisher
key or update service is used. The unconfigured policy rejects the forced native
check before a WinHTTP request can be constructed; packet capture is not claimed.
"""
import argparse
import ctypes
from ctypes import wintypes as w
import hashlib
import http.server
import json
from pathlib import Path
import re
import subprocess
import threading
import time
import uuid
from ui_smoke import windows,u
from process_tree import hold_children,wait_children

ROOT=Path(__file__).resolve().parents[1]
u.GetWindow.argtypes=[w.HWND,w.UINT];u.GetWindow.restype=w.HWND
u.IsWindowVisible.argtypes=[w.HWND];u.IsWindowVisible.restype=w.BOOL
u.IsWindow.argtypes=[w.HWND];u.IsWindow.restype=w.BOOL


def until(test,seconds=25):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        value=test()
        if value:return value
        time.sleep(.05)
    raise AssertionError('The native updater fixture did not reach the expected state')


def text(hwnd):
    length=u.SendMessageW(hwnd,0xE,0,0)
    value=ctypes.create_unicode_buffer(length+1)
    u.SendMessageW(hwnd,0xD,length+1,ctypes.cast(value,ctypes.c_void_p).value)
    return value.value


def command(hwnd,id):
    u.SendMessageW(hwnd,0x111,id,0)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def snapshot(path):
    return {p.relative_to(path).as_posix():sha(p) for p in path.rglob('*') if p.is_file()}


def main(expected_dll=None):
    exe=ROOT/'out-cef/app/Release/TuranBrowser.exe'
    dll=exe.with_suffix('.dll')
    helper=exe.parent/'TuranUpdateTool.exe'
    report={'passed':False,'dllSha256':sha(dll),'helperSha256':sha(helper),
            'scope':'Windows 11 actual CEF native updater panels, unconfigured policy, worker shutdown join and helper refusal; no production update or packet-capture certification',
            'windowsTrustStoreModified':False,'authenticodeClaim':False,'runtimes':{}}
    requests=[]
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append(self.path)
            data=b'<title>Turan updater native fixture</title><p>Local CEF fixture</p>'
            self.send_response(200);self.send_header('Content-Type','text/html');self.end_headers();self.wfile.write(data)
        def log_message(self,*_):pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    process=None;children=[]
    work=ROOT/'artifacts'/('update-panel-cef-'+uuid.uuid4().hex)
    work.mkdir(parents=True)
    report['ownedFixture']=str(work)
    try:
        if expected_dll is not None:
            assert re.fullmatch(r'[0-9a-f]{64}',expected_dll),'Expected DLL must be a SHA-256'
            assert report['dllSha256']==expected_dll,'The test must bind to the caller-specified DLL'
        generated=ROOT/'out-cef/generated/turan_update_inventory.h'
        entries=re.findall(r'^"([^"\r\n]+)",$',generated.read_text(encoding='utf-8'),re.M)
        expected=(ROOT/'out-cef/runtime-files.txt').read_text(encoding='utf-8').splitlines()
        assert entries==expected and len(entries)==len(set(entries))
        assert 'TuranUpdateTool.exe' in entries and 'docs/UPDATE_ARCHITECTURE.md' in entries
        assert not any(any(p.lower() in ('profile','profiles') for p in name.split('/')) for name in entries)
        assert all(not name.startswith(('/', '\\')) and ':' not in name and '..' not in name.split('/') for name in entries)
        cache=(ROOT/'out-cef/CMakeCache.txt').read_text(encoding='utf-8')
        assert re.search(r'^TURAN_UPDATE_TRUST_HEADER:FILEPATH=$',cache,re.M)
        report['compiledInventoryExact']=True
        report['compiledInventoryEntries']=len(entries)
        report['compiledInventorySha256']=sha(generated)
        report['productionTrustHeaderUnconfigured']=True
        # The actual shipped helper must fail without creating/changing any app
        # file, even when the synthetic application directory contains profiles.
        synthetic=work/'synthetic-helper-app'
        (synthetic/'Profile').mkdir(parents=True)
        (synthetic/'Profiles/default').mkdir(parents=True)
        for name,data in {'TuranBrowser.exe':b'fictional old executable','TuranBrowser.dll':b'fictional old DLL',
                          'portable.flag':b'Portable profile: Profile/\n','Uninstall.exe':b'fictional uninstaller',
                          'Profile/password-vault.dat':b'fictional encrypted vault bytes',
                          'Profiles/default/user-sentinel.bin':b'fictional unrelated profile bytes'}.items():
            (synthetic/name).write_bytes(data)
        original=snapshot(synthetic)
        result=subprocess.run([str(helper),'--apply','--app',str(synthetic),'--stage',str(work)],cwd=helper.parent,
                              capture_output=True,timeout=10)
        assert result.returncode==3 and snapshot(synthetic)==original
        report['actualShippedHelperUnconfiguredExit']=3
        report['actualHelperLeavesSyntheticAppAndProfilesByteIdentical']=True
        for runtime in ('default','chrome-integrated'):
            profile=work/runtime/'Profile'
            profile.mkdir(parents=True)
            (profile/'browser-state.txt').write_text('S 1 1 0 0 1 0\nB "Unrelated updater regression bookmark" "https://legacy.example/"\n',encoding='utf-8')
            sentinel=profile/'unrelated-user-data.bin'
            sentinel.write_bytes(b'Turan updater fixture: preserve this user file byte-for-byte')
            sentinel_hash=sha(sentinel)
            path='/'+runtime
            args=[str(exe),'--profile='+str(profile),'--url=http://127.0.0.1:'+str(server.server_port)+path]
            if runtime!='default':args.append('--turan-runtime='+runtime)
            process=subprocess.Popen(args,cwd=exe.parent)
            controller=until(lambda:windows(process.pid,'TuranWindow'))[0]
            until(lambda:path in requests)
            owner=controller
            if runtime=='chrome-integrated':
                owner=until(lambda:[h for h in windows(process.pid,'Chrome_WidgetWin_1') if u.IsWindowVisible(h)])[0]
            command(controller,216)
            panel=until(lambda:windows(process.pid,'TuranSecureUpdateWindow'))[0]
            assert u.GetWindow(panel,4)==owner,'Native panel must belong to the actual browser root'
            caption=text(panel)
            assert 'secure update' in caption,'The real native window caption must be initialized'
            assert 'secure update' in text(u.GetDlgItem(panel,300))
            status=text(u.GetDlgItem(panel,305))
            assert 'NorthByte' in status and 'not been provisioned' in status and 'Unauthenticated packages cannot be installed' in status
            controls={name:bool(u.IsWindowEnabled(u.GetDlgItem(panel,id))) for name,id in [('check',301),('download',302),('install',303)]}
            assert controls=={'check':False,'download':False,'install':False}
            # Explicitly craft a native command despite the disabled check
            # control. The library independently rejects the missing trust root.
            command(panel,301)
            until(lambda:'No authenticated NorthByte update channel has been provisioned.' in text(u.GetDlgItem(panel,305)))
            assert not any(u.IsWindowEnabled(u.GetDlgItem(panel,id)) for id in (301,302,303))
            assert not windows(process.pid,'#32770'),'No installer confirmation or fake success dialog is expected'
            u.SendMessageW(panel,0x10,0,0)
            until(lambda:not u.IsWindow(panel))
            # Queue a second forced check and immediately close the root. The
            # registered worker must be cancelled/joined before DLL unload.
            command(controller,216)
            panel=until(lambda:windows(process.pid,'TuranSecureUpdateWindow'))[0]
            assert u.GetWindow(panel,4)==owner
            command(panel,301)
            children=hold_children(process.pid)
            u.PostMessageW(controller,0x10,0,0)
            exit_code=process.wait(timeout=25)
            assert exit_code==0
            wait_children(children);children=[];process=None
            assert sha(sentinel)==sentinel_hash
            assert 'Unrelated updater regression bookmark' in (profile/'browser-state.txt').read_text(encoding='utf-8')
            assert not list(profile.glob('.turan-update*'))
            report['runtimes'][runtime]={
                'actualCefDocumentLoaded':True,'mainCommand216OpensNativePanel':True,
                'nativePanelCaption':caption,
                'ownedByActualBrowserRoot':True,'unconfiguredStatusAccurate':True,
                'checkDownloadInstallDisabled':controls,'forcedNativeCheckFailsBeforeRequestByPolicy':True,
                'panelCloseAndRootCloseWithRegisteredWorker':True,'exitCode':exit_code,
                'legacyBookmarkAndUserSentinelPreserved':True,
                'productionPublisherKey':False,'productionUpdateService':False}
        report['passed']=True
    except BaseException as error:
        report['failure']=str(error)
        raise
    finally:
        if process and process.poll() is None:
            children=children or hold_children(process.pid)
            subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True)
            process.wait(timeout=20)
        if children:wait_children(children)
        server.shutdown();server.server_close()
        (ROOT/'artifacts/update-panel-cef.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,indent=2))


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-dll',help='Optional immutable SHA-256 binding supplied by CI')
    main(parser.parse_args().expected_dll)
