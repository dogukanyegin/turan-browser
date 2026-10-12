"""Reject unsafe diagnostic invocations before opening any user profile."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from build_cef import runtime_files

def main():
    reports=[];app=ROOT/'out-cef/app/Release'
    # Hard-linked read-only engine inputs avoid another persistent runtime copy.
    # portable.flag confines even a regressed launch to this disposable folder.
    with tempfile.TemporaryDirectory(prefix='turan-diagnostic-guard-') as temporary:
        target=Path(temporary)/'app';target.mkdir()
        for source in runtime_files(app,ROOT/'out-cef/runtime-files.txt',required=False):
            destination=target/source.relative_to(app);destination.parent.mkdir(parents=True,exist_ok=True)
            try:os.link(source,destination)
            except OSError:shutil.copyfile(source,destination)
        (target/'portable.flag').write_text('Isolated test profile\n')
        profile=Path(temporary)/'never-open-this-profile'
        cases=[['--turan-runtime=chrome-reparent-tabs'],['--turan-runtime=chrome-native-tabs'],
               ['--turan-runtime=unknown','--profile='+str(profile)],
               ['--turan-test-dns','--profile='+str(profile),'--url=https://example.org'],
               ['--turan-test-file-dialog','--profile='+str(profile),'--url=https://example.org']]
        for arguments in cases:
            process=subprocess.Popen([str(target/'TuranBrowser.exe')]+arguments,cwd=target)
            try:code=process.wait(timeout=15)
            finally:
                if process.poll() is None:
                    subprocess.run(['taskkill','/PID',str(process.pid),'/T','/F'],capture_output=True);process.wait(timeout=15)
            report={'arguments':[r.split('=')[0] for r in arguments],'exitCode':code,
                    'noProfileCreated':not profile.exists() and not (target/'Profile').exists(),
                    'noProfileLeaseOrRecoveryFiles':not list(Path(temporary).glob('*.turan-*')) and not list(target.glob('Profile.*'))}
            reports.append(report)
            assert code==2 and report['noProfileCreated'] and report['noProfileLeaseOrRecoveryFiles'],report
    (ROOT/'artifacts/diagnostic-guard-cef.json').write_text(json.dumps(reports,indent=2)+'\n')
    print(json.dumps(reports,indent=2))

if __name__=='__main__':main()
