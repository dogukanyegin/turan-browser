"""Build/package Turan with a pinned minimal CEF binary; never fetch Chromium."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from release_gate import validate
from safe_paths import validate_inventory

ROOT=Path(__file__).resolve().parents[1]
DEPS=ROOT/'.deps'
LOCK=json.loads((ROOT/'cef.lock.json').read_text())

def source_files():
    directories=('src','resources','tests','tools','docs','packaging','.gitea','.github')
    names=('CMakeLists.txt','cef.lock.json','filter.lock.json','tor-browser.lock.json','release.json','release-acceptance.json','release-scope.json',
           '.gitignore','.gitattributes','LICENSE','README.md','THIRD_PARTY_NOTICES.md',
           'TEST_REPORT.md','SECURITY.md','PRIVACY.md','INSTALLATION.md','RELEASE_NOTES.md','BETA_BUILD_CHECKLIST.md')
    files=[ROOT/name for name in names if (ROOT/name).is_file()]
    for directory in directories:
        files.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.name not in ('owned-files.nsh','install-files.nsh') and p.suffix.lower() not in ('.pem','.pfx','.key','.pyc'))
    return sorted(files,key=lambda p:p.relative_to(ROOT).as_posix())

def source_digest():
    digest=hashlib.sha256()
    for path in source_files():
        digest.update(path.relative_to(ROOT).as_posix().encode()+b'\0')
        digest.update(path.read_bytes()+b'\0')
    return digest.hexdigest()

def runtime_files(app,inventory,required=True):
    try:
        names=validate_inventory(inventory.read_text(encoding='utf-8').splitlines())
    except ValueError as exc:
        raise RuntimeError('Invalid runtime file inventory: '+str(exc)) from exc
    files=[]
    real_app=app.resolve()
    for name in names:
        path=app/name
        # Never package symlink/junction/reparse-point ancestors. Windows
        # junctions appear as reparse points, but not always as symlink leaves.
        parents=[path, *path.parents]
        for component in parents:
            if component == app.parent:
                break
            if component.is_symlink():
                raise RuntimeError('Redirected runtime path: '+name)
            if component.exists() and getattr(component.lstat(),'st_file_attributes',0)&0x400:
                raise RuntimeError('Windows reparse point in runtime path: '+name)
        if not path.resolve().is_relative_to(real_app):
            raise RuntimeError('Redirected runtime file: '+name)
        if path.is_file():files.append(path)
        elif required:raise RuntimeError('Missing runtime file: '+name)
    return files

def run(argv):
    print('RUN',subprocess.list2cmdline([str(x) for x in argv]),flush=True)
    subprocess.run([str(x) for x in argv],cwd=ROOT,check=True)

def download(url,path,algorithm,expected):
    if path.exists() and hashlib.new(algorithm,path.read_bytes()).hexdigest()==expected:return
    temporary=path.with_suffix(path.suffix+'.partial')
    with urllib.request.urlopen(url,timeout=60) as source,temporary.open('wb') as target:
        total=0
        while True:
            block=source.read(1024*1024)
            if not block:break
            target.write(block);total+=len(block)
            if total%(20*1024*1024)<1024*1024:print('downloaded',total//(1024*1024),'MiB',flush=True)
    if hashlib.new(algorithm,temporary.read_bytes()).hexdigest()!=expected:
        raise RuntimeError('Dependency checksum mismatch')
    temporary.replace(path)

def cef():
    folder=DEPS/LOCK['archive'].removesuffix('.tar.bz2')
    if not (folder/'include/cef_app.h').exists() or not (folder/'Release/libcef.dll').exists():
        DEPS.mkdir(exist_ok=True)
        archive=DEPS/LOCK['archive'];download(LOCK['url'],archive,'sha256',LOCK['sha256'])
        with tarfile.open(archive) as tar:
            for member in tar.getmembers():
                if not (DEPS/member.name).resolve().is_relative_to(DEPS.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError('Unsafe CEF archive member')
            tar.extractall(DEPS)
    return folder

def cmake():
    exe=shutil.which('cmake')
    if exe:return Path(exe)
    vswhere=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
    candidate=Path(install)/'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if not candidate.exists():raise RuntimeError('Install CMake 3.21+ and Visual Studio C++ x64 tools')
    return candidate

def nsis():
    exe=shutil.which('makensis')
    if exe:return Path(exe)
    folder=DEPS/('nsis-'+LOCK['nsis']['version']);exe=folder/'makensis.exe'
    if not exe.exists():
        archive=DEPS/'nsis.zip';download(LOCK['nsis']['url'],archive,'sha256',LOCK['nsis']['sha256'])
        with zipfile.ZipFile(archive) as z:
            for name in z.namelist():
                if not (DEPS/name).resolve().is_relative_to(DEPS.resolve()):raise RuntimeError('Unsafe NSIS archive')
            z.extractall(DEPS)
    return exe

def performance_report(data):
    if not data.get('baseline') or not data.get('current'):
        return ('# Engineering performance — comparison not verified\n\n'
                'The immutable 0.1.0 portable baseline was not supplied to this build.\n'
                'No 0.1.0 versus current comparison was run, and this is not a\n'
                'performance acceptance result. Windows 10, long sessions and 8 GB\n'
                'hardware measurements still require separate verification.\n')
    rows=[('Page-title ready',None,'startupSeconds','s'),
          ('1 tab, private committed bytes','oneTab','privateMiB','MiB'),
          ('10 tabs, private committed bytes','tenTabs','privateMiB','MiB'),
          ('1 tab, summed working sets','oneTab','summedWorkingSetMiB','MiB'),
          ('10 tabs, summed working sets','tenTabs','summedWorkingSetMiB','MiB'),
          ('1 tab, idle CPU / one core','oneTab','idleCpuPercentOneCore','%'),
          ('10 tabs, idle CPU / one core','tenTabs','idleCpuPercentOneCore','%')]
    text=['# Measured engineering binary performance','',
          'Captured UTC: '+data['capturedAtUtc'],
          'Current DLL SHA-256: `'+data['currentDllSha256']+'`','',
          '| Measurement | Immutable 0.1.0 / CEF 144 | Current / CEF 154 |',
          '| --- | ---: | ---: |']
    for label,part,key,unit in rows:
        values=[statistics.median([row[part][key] if part else row[key] for row in data[group]]) for group in ('baseline','current')]
        text.append('| '+label+' | '+' | '.join(f'{v:.2f}'+('%' if unit=='%' else ' '+unit) for v in values)+' |')
    text.extend(['','Two alternating runs per binary on Windows 11, fresh profiles, local static pages,',
                 'two seconds of settling and five seconds of sampling. Logical processors: '+str(data['logicalProcessors'])+'.',
                 'Working-set sums count shared pages repeatedly. Engine/shell/OS cache and scanning',
                 'effects are confounded; this small sample does not establish a general improvement.',
                 'The earlier 7.172-second first launch remains unexplained. Windows 10, realistic',
                 'long sessions and low-memory acceptance remain pending. Raw paired samples are',
                 'embedded in build-manifest.json under verification.performance_comparison.',''])
    return '\n'.join(text)

def build():
    release=validate();digest=source_digest()
    run([sys.executable,ROOT/'tests/release_gate_test.py'])
    run([sys.executable,'-m','unittest','discover','-s',ROOT/'tests','-p','*test.py'])
    engine=cef();tool=cmake()
    from filter_deps import prepare as filter_dependencies
    filter_dependencies()
    # CMake selects the installed VS version automatically.
    run([tool,'-S',ROOT,'-B',ROOT/'out-cef','-A','x64','-DCEF_ROOT='+str(engine)])
    run([tool,'--build',ROOT/'out-cef','--config','Release','--parallel','2'])
    run([sys.executable,ROOT/'tools/brand_bootstrap.py',ROOT/'out-cef/app/Release/TuranBrowser.exe',ROOT/'resources/turan.ico',ROOT/'out-cef/app/Release/TuranBrowser.dll'])
    run([tool.parent/'ctest.exe','--test-dir',ROOT/'out-cef','-C','Release','--output-on-failure'])
    run([sys.executable,ROOT/'tests/startup_preflight.py','--runtime',ROOT/'out-cef/app/Release'])
    run([sys.executable,ROOT/'tests/diagnostic_guard_cef.py'])
    run([sys.executable,ROOT/'tests/smoke_cef.py'])
    run([sys.executable,ROOT/'tests/shield_cef.py'])
    run([sys.executable,ROOT/'tests/tor_browser_cef.py'])
    tor_qa=ROOT/'.deps/tor-qa/extracted'
    if (tor_qa/'Browser/firefox.exe').is_file():
        run([sys.executable,ROOT/'tests/tor_browser_cef.py','--tor-root',tor_qa])
    run([sys.executable,ROOT/'tests/filter_corpus.py'])
    run([sys.executable,ROOT/'tests/session_cef.py'])
    baseline=ROOT/'dist/TuranBrowser-0.1.0-windows-x64-portable.zip'
    has_baseline=baseline.is_file()
    if has_baseline:
        run([sys.executable,ROOT/'tests/profile_migration_cef.py'])
    else:
        print('NOT TESTED: immutable 0.1.0 portable baseline not supplied; historical engine migration and 0.1.0 performance comparisons remain unverified',flush=True)
    run([sys.executable,ROOT/'tests/ui_smoke.py'])
    run([sys.executable,ROOT/'tests/navigation_cef.py'])
    run([sys.executable,ROOT/'tests/permissions_cef.py'])
    run([sys.executable,ROOT/'tests/dns_cef.py'])
    run([sys.executable,ROOT/'tests/tls_cef.py'])
    run([sys.executable,ROOT/'tests/extensions_cef.py'])
    run([sys.executable,ROOT/'tests/extensions_cef.py','--runtime','chrome-native-tabs','--topology'])
    run([sys.executable,ROOT/'tests/native_chrome_host_cef.py'])
    run([sys.executable,ROOT/'tests/extensions_cef.py','--runtime','chrome-reparent-tabs','--topology'])
    run([sys.executable,ROOT/'tests/thirdparty_extensions_cef.py'])
    run([sys.executable,ROOT/'tests/thirdparty_extensions_cef.py','--runtime','chrome-reparent-tabs'])
    run([sys.executable,ROOT/'tests/integrated_chrome_cef.py'])
    run([sys.executable,ROOT/'tests/thirdparty_extensions_cef.py','--runtime','chrome-integrated'])
    run([sys.executable,ROOT/'tests/extensions_lifecycle_cef.py','--native-picker'])
    run([sys.executable,ROOT/'tests/password_vault_cef.py'])
    shutil.copyfile(ROOT/'artifacts/password-vault-cef.json',ROOT/'artifacts/password-vault-cef-default.json')
    run([sys.executable,ROOT/'tests/password_vault_cef.py','--runtime','chrome-integrated'])
    shutil.copyfile(ROOT/'artifacts/password-vault-cef.json',ROOT/'artifacts/password-vault-cef-integrated.json')
    run([sys.executable,ROOT/'tests/subscriptions_cef.py'])
    run([sys.executable,ROOT/'tests/subscriptions_cef.py','--runtime','chrome-integrated'])
    run([sys.executable,ROOT/'tests/update_panel_cef.py'])
    performance_path=ROOT/'artifacts/performance-comparison.json'
    current_dll=hashlib.sha256((ROOT/'out-cef/app/Release/TuranBrowser.dll').read_bytes()).hexdigest()
    performance=json.loads(performance_path.read_text()) if performance_path.exists() else {}
    if not has_baseline:
        # Never reuse old measurement files: their DLL hashes and workloads do
        # not prove anything about the just-built executable.
        performance={'status':'not_tested', 'reason':'Immutable 0.1.0 baseline archive unavailable',
                     'currentDllSha256':current_dll}
    else:
        if performance.get('currentDllSha256')!=current_dll:
            run([sys.executable,ROOT/'tests/performance_cef.py'])
            performance=json.loads(performance_path.read_text())
        if performance.get('currentDllSha256')!=current_dll:
            raise RuntimeError('Performance evidence does not match the current DLL')
    if source_digest()!=digest:raise RuntimeError('Source changed during build; rebuild before packaging')
    verification={
        'scope':'Windows 11 x64 engineering tests; incomplete Stable acceptance',
        'core_ctest':['turan_core','turan_session','turan_store','turan_profile','turan_filter','turan_tor','turan_vault','turan_subscription','turan_update'],
        'cef_fixture':(ROOT/'artifacts/cef-integration.txt').read_text().splitlines(),
        'session_fixture':(ROOT/'artifacts/session-cef.txt').read_text().strip(),
        'profile_migration_fixture':(json.loads((ROOT/'artifacts/profile-migration-cef.json').read_text()) if has_baseline else
            {'status':'not_tested', 'reason':'Immutable 0.1.0 baseline archive unavailable'}),
        'shield_fixture':json.loads((ROOT/'artifacts/shield-cef.json').read_text()),
        'filter_corpus':json.loads((ROOT/'artifacts/filter-corpus.json').read_text()),
        'native_filter_dependencies':json.loads((ROOT/'filter.lock.json').read_text()),
        'tor_browser_catalog':json.loads((ROOT/'tor-browser.lock.json').read_text()),
        'tor_browser_refusal_fixture':json.loads((ROOT/'artifacts/tor-browser-refusal-cef.json').read_text()),
        'permission_fixture':json.loads((ROOT/'artifacts/permissions-cef.json').read_text()),
        'tls_fixture':json.loads((ROOT/'artifacts/tls-cef.json').read_text()),
    }
    tor_full=ROOT/'artifacts/tor-browser-full-cef.json'
    if tor_full.exists():
        tor_evidence=json.loads(tor_full.read_text())
        if tor_evidence.get('passed') and tor_evidence.get('dllSha256')==current_dll:
            verification['tor_browser_actual_external_launch']=tor_evidence
    dns=json.loads((ROOT/'artifacts/dns-cef.json').read_text())
    verification['dns_fixture']={name:{key:case[key] for key in ('httpsAttempts','classicAttempts','httpsResponses','passed')} for name,case in dns.items() if isinstance(case,dict)}
    extensions=json.loads((ROOT/'artifacts/thirdparty-extensions.json').read_text())
    verification['third_party_extension_probe']={name:{'version':case['extension']['version'],**{key:value for key,value in case.items() if isinstance(value,bool)}} for name,case in extensions.items()}
    hosted=json.loads((ROOT/'artifacts/thirdparty-extensions-chrome-reparent-tabs.json').read_text())
    verification['native_host_publisher_probe']={name:{'version':case['extension']['version'],'scope':'native root activation and specific publisher operations; no vault/autofill acceptance',**{key:value for key,value in case.items() if isinstance(value,bool)}} for name,case in hosted.items()}
    native=json.loads((ROOT/'artifacts/native-chrome-host-cef.json').read_text())
    verification['native_chrome_host_probe']={key:value for key,value in native.items() if isinstance(value,(bool,str,int))}
    verification['diagnostic_guard_fixture']=json.loads((ROOT/'artifacts/diagnostic-guard-cef.json').read_text())
    probe=json.loads((ROOT/'artifacts/mv3-probe.json').read_text())
    verification['mv3_probe_operations']=probe['report']['results']
    verification['performance_comparison']=performance
    for key,filename in (
        ('integrated_chrome_candidate','integrated-chrome-cef.json'),
        ('integrated_publisher_operations','thirdparty-extensions-chrome-integrated.json'),
        ('native_unpacked_extension_lifecycle','extensions-lifecycle-cef.json'),
        ('explicit_password_forms_alloy','password-vault-cef-default.json'),
        ('explicit_password_forms_chrome','password-vault-cef-integrated.json'),
        ('subscriptions_alloy','subscriptions-cef-alloy.json'),
        ('subscriptions_chrome','subscriptions-cef-chrome-integrated.json'),
        ('native_update_panel','update-panel-cef.json')):
        proof=json.loads((ROOT/'artifacts'/filename).read_text())
        cases=list(proof.values()) if key=='integrated_publisher_operations' else [proof]
        for case in cases:
            bound=case.get('dllSha256',case.get('binarySha256'))
            if bound!=current_dll or case.get('failure') or case.get('passed') is False:
                raise RuntimeError('New feature evidence does not match verified binary: '+filename)
        verification[key]=proof
    (ROOT/'out-cef/app/Release/PERFORMANCE_REPORT.md').write_text(performance_report(performance),encoding='utf-8')
    topology_path=ROOT/'artifacts/extension-api-tab-model.json'
    if topology_path.exists():
        topology=json.loads(topology_path.read_text())
        verification['additional_architecture_experiment']={key:topology.get(key) for key in ('scope','sameEngineWindow','nativeTabs','exitCode','failure')}
    manifest={'release':release,'engine':LOCK['version'],'source_sha256':digest,'verification':verification,'binary_sha256':{}}
    app=ROOT/'out-cef/app/Release'
    documents={'README.md','TEST_REPORT.md','RELEASE_NOTES.md','INSTALLATION.md','SECURITY.md','PRIVACY.md','THIRD_PARTY_NOTICES.md','PERFORMANCE_REPORT.md'}
    for path in runtime_files(app,ROOT/'out-cef/runtime-files.txt',required=False):
        relative=path.relative_to(app)
        if path.is_file() and 'Profile' not in relative.parts and path.name not in documents|{'build-manifest.json','portable.flag'}:
            manifest['binary_sha256'][relative.as_posix()]=hashlib.sha256(path.read_bytes()).hexdigest()
    (app/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

def package():
    release=validate();version=release['version']
    app=ROOT/'out-cef/app/Release'
    if not (app/'TuranBrowser.dll').exists():raise RuntimeError('Build first')
    manifest=json.loads((app/'build-manifest.json').read_text())
    if manifest['source_sha256']!=source_digest() or manifest['release']!=release or manifest['engine']!=LOCK['version']:
        raise RuntimeError('Source/release/engine changed since verified build; rebuild first')
    for name,digest in manifest['binary_sha256'].items():
        if hashlib.sha256((app/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Built binary changed: '+name)
    dist=ROOT/'dist';dist.mkdir(exist_ok=True)
    (app/'portable.flag').write_text('Portable profile: Profile/\n')
    for name in ('README.md','TEST_REPORT.md','RELEASE_NOTES.md','INSTALLATION.md','SECURITY.md','PRIVACY.md','THIRD_PARTY_NOTICES.md'):
        shutil.copyfile(ROOT/name,app/name)
        shutil.copyfile(ROOT/name,dist/name)
    shutil.copyfile(app/'PERFORMANCE_REPORT.md',dist/'PERFORMANCE_REPORT.md')
    owned=runtime_files(app,ROOT/'out-cef/runtime-files.txt')
    def nsis_string(value):return value.replace('$','$$').replace('"','$\\"')
    (ROOT/'packaging/windows/owned-files.nsh').write_text(''.join('Delete "$INSTDIR\\'+p.relative_to(app).as_posix().replace('/','\\')+'"\n' for p in owned),encoding='utf-8')
    install=[];directory=None
    for path in owned:
        relative=path.relative_to(app);parent=relative.parent.as_posix()
        destination='$INSTDIR'+('\\'+nsis_string(parent.replace('/','\\')) if parent!='.' else '')
        if destination!=directory:install.append('SetOutPath "'+destination+'"\n');directory=destination
        install.append('File "'+nsis_string(str(path))+'"\n')
    install.append('SetOutPath "$INSTDIR"\n')
    (ROOT/'packaging/windows/install-files.nsh').write_text(''.join(install),encoding='utf-8')
    with zipfile.ZipFile(dist/('TuranBrowser-'+version+'-windows-x64-portable.zip'),'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for p in owned+[app/'portable.flag']:z.write(p,'TuranBrowser/'+p.relative_to(app).as_posix())
    product={'stable':'Turan Browser','beta':'Turan Browser Beta','development':'Turan Browser Development'}[release['channel']]
    registration={'stable':'TuranBrowser','beta':'TuranBrowserBeta','development':'TuranBrowserDevelopment'}[release['channel']]
    run([nsis(),'/INPUTCHARSET','UTF8','/DVERSION='+version,'/DPRODUCT_NAME='+product,'/DREGISTRATION_ID='+registration,'/DAPP_DIR='+str(app),'/DOUTPUT='+str(dist/('TuranBrowser-'+version+'-windows-x64-setup.exe')),ROOT/'packaging/windows/installer.nsi'])
    if manifest['source_sha256']!=source_digest():raise RuntimeError('Source changed during packaging; rebuild and package again')
    with zipfile.ZipFile(dist/('TuranBrowser-'+version+'-source.zip'),'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for path in source_files():archive.write(path,path.relative_to(ROOT).as_posix())
        archive.write(app/'build-manifest.json','verification/build-manifest.json')
        archive.write(app/'PERFORMANCE_REPORT.md','verification/PERFORMANCE_REPORT.md')
    if manifest['source_sha256']!=source_digest():raise RuntimeError('Source changed while archiving; rebuild and package again')
    hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in dist.iterdir() if p.suffix in ('.exe','.zip')}
    (dist/'SHA256SUMS.txt').write_text(''.join(v+'  '+k+'\n' for k,v in hashes.items()))
    # Reopen actual archives and verify the source digest, CRC, runtime SHA-256
    # inventory and rejection of unsafe member names before reporting success.
    from verify_distribution import verify
    package_proof=verify(dist/('TuranBrowser-'+version+'-source.zip'),
                         dist/('TuranBrowser-'+version+'-windows-x64-portable.zip'),
                         dist/('TuranBrowser-'+version+'-windows-x64-setup.exe'))
    (dist/'PACKAGE_INTEGRITY.json').write_text(json.dumps(package_proof,indent=2)+'\n',encoding='utf-8')
    # Verify the actual extracted portable, per-user installer and uninstall
    # behavior on a disposable Windows machine. This is not a Win10/11 matrix.
    run([sys.executable,ROOT/'tests/package_smoke.py'])
    # Archive caches are reproducible, so discard them after successful packaging.
    for p in (DEPS/LOCK['archive'],DEPS/'nsis.zip'):
        if p.is_file():p.unlink()
    print('Packages:',dist)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command',choices=['deps','build','package','all'])
    args=parser.parse_args()
    try:
        if args.command=='deps':
            from filter_deps import prepare as filter_dependencies
            print(cef());print(filter_dependencies())
        if args.command in ('build','all'):build()
        if args.command in ('package','all'):package()
    except (OSError,RuntimeError,subprocess.CalledProcessError) as e:
        print('ERROR:',e,file=sys.stderr);sys.exit(1)
