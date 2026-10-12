"""Verify ZIP runtime, per-user installer and conservative uninstall in a temp tree."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import winreg
import zipfile
import json
import hashlib

ROOT=Path(__file__).resolve().parents[1]
KEY=r'Software\Microsoft\Windows\CurrentVersion\Uninstall'
def installed(view):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,KEY+'\\'+registration,0,winreg.KEY_READ|view):return True
    except FileNotFoundError:return False

def main():
    global registration
    release=json.loads((ROOT/'release.json').read_text());version=release['version']
    registration={'stable':'TuranBrowser','beta':'TuranBrowserBeta','development':'TuranBrowserDevelopment'}[release['channel']]
    product={'stable':'Turan Browser','beta':'Turan Browser Beta','development':'Turan Browser Development'}[release['channel']]
    # Do not overwrite an existing user's installation/shortcut registration.
    shortcut=Path(os.environ['APPDATA'])/'Microsoft/Windows/Start Menu/Programs'/product
    if installed(winreg.KEY_WOW64_32KEY) or installed(winreg.KEY_WOW64_64KEY) or shortcut.exists():
        raise RuntimeError('An existing Turan registration/shortcut prevents isolated installer testing')
    artifact_root=(ROOT/'artifacts').resolve();artifact_root.mkdir(exist_ok=True)
    # Revalidate canonical Windows member paths (case aliases, reserved names,
    # symlinks and traversals) before any extraction to a real NTFS directory.
    sys.path.insert(0,str(ROOT/'tools'))
    from verify_distribution import verify
    source_archive=ROOT/('dist/TuranBrowser-'+version+'-source.zip')
    portable_archive=ROOT/('dist/TuranBrowser-'+version+'-windows-x64-portable.zip')
    setup_archive=ROOT/('dist/TuranBrowser-'+version+'-windows-x64-setup.exe')
    verify(source_archive,portable_archive,setup_archive)
    historical=ROOT/'dist/TuranBrowser-0.1.0-windows-x64-portable.zip'
    migration_tested=historical.is_file()
    with tempfile.TemporaryDirectory(prefix='package-test-',dir=artifact_root) as temporary:
        folder=Path(temporary).resolve()
        assert folder.is_relative_to(artifact_root) # checked before recursive temp cleanup
        archive=ROOT/('dist/TuranBrowser-'+version+'-windows-x64-portable.zip')
        with zipfile.ZipFile(archive) as z:
            assert z.testzip() is None
            assert 'TuranBrowser/portable.flag' in z.namelist()
            for n in z.namelist():assert (folder/n).resolve().is_relative_to(folder)
            z.extractall(folder)
        app=folder/'TuranBrowser'
        for required in ('TuranBrowser.exe','TuranBrowser.dll','TuranStartup.exe','TuranProfileTool.exe','TuranUpdateTool.exe','libcef.dll','CEF-LICENSE.txt','CEF-CREDITS.html','locales/tr.pak','docs/PASSWORD_VAULT.md','docs/FILTER_SUBSCRIPTIONS.md','docs/UPDATE_ARCHITECTURE.md'):
            assert (app/required).is_file()
        manifest=json.loads((app/'build-manifest.json').read_text())
        assert manifest['release']['version']==version
        def verify_payload(root):
            for name,digest in manifest['binary_sha256'].items():
                file=root/name
                assert file.is_file() and hashlib.sha256(file.read_bytes()).hexdigest()==digest,name
        verify_payload(app)
        subprocess.run([str(app/'TuranStartup.exe'),'--check-only'],check=True,timeout=30)
        with zipfile.ZipFile(ROOT/('dist/TuranBrowser-'+version+'-source.zip')) as source:
            assert 'src/main.cpp' in source.namelist() and 'src/core/session.cpp' in source.namelist()
            assert json.loads(source.read('release.json'))['version']==version
            assert json.loads(source.read('verification/build-manifest.json'))==manifest
            sys.path.insert(0,str(ROOT/'tools'))
            from build_cef import source_files,source_digest
            assert source_digest()==manifest['source_sha256']
            for file in source_files():
                assert source.read(file.relative_to(ROOT).as_posix())==file.read_bytes()
        subprocess.run([sys.executable,str(ROOT/'tests/ui_smoke.py'),'--exe',str(app/'TuranBrowser.exe'),'--portable'],check=True)
        if migration_tested:
            subprocess.run([sys.executable,str(ROOT/'tests/profile_migration_cef.py'),'--exe',str(app/'TuranBrowser.exe')],check=True)
        else:
            print('NOT TESTED: portable historical 0.1.0 profile migration baseline not supplied')
        target=folder/'Installed'
        setup=ROOT/('dist/TuranBrowser-'+version+'-windows-x64-setup.exe')
        command='"'+str(setup)+'" /S /D='+str(target)
        subprocess.run(command,check=True,timeout=120,shell=False)
        try:
            assert (target/'TuranBrowser.dll').is_file() and (target/'Uninstall.exe').is_file()
            verify_payload(target)
            subprocess.run([str(target/'TuranStartup.exe'),'--check-only'],check=True,timeout=30)
            assert not (target/'portable.flag').exists()
            assert installed(winreg.KEY_WOW64_32KEY)
            subprocess.run([sys.executable,str(ROOT/'tests/smoke_cef.py'),'--exe',str(target/'TuranBrowser.exe')],check=True)
            if migration_tested:
                subprocess.run([sys.executable,str(ROOT/'tests/profile_migration_cef.py'),'--exe',str(target/'TuranBrowser.exe')],check=True)
            (target/'keep-user-file.txt').write_text('User-created file must survive uninstall')
        finally:
            if (target/'Uninstall.exe').exists():
                subprocess.run('"'+str(target/'Uninstall.exe')+'" /S _?='+str(target),check=True,timeout=60,shell=False)
        assert not (target/'TuranBrowser.exe').exists()
        assert (target/'keep-user-file.txt').exists()
        assert not installed(winreg.KEY_WOW64_32KEY)
        assert not shortcut.exists()
    proof=('PASS ZIP CRC/runtime/default portable profile, installer runtime, '
           'uninstall registration cleanup and user-file preservation\n'
           + ('PASS 0.1.0 migration with baseline\n' if migration_tested else
              'NOT TESTED 0.1.0 migration (baseline unavailable)\n'))
    (artifact_root/'package-test.txt').write_text(proof);print(proof)

if __name__=='__main__':main()
