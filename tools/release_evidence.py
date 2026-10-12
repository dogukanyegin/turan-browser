"""Evidence cross-check for distinct Windows 10/11 workstations and device QA.

This script MUST NOT edit release-acceptance.json or declare a Stable release.
An absent evidence item remains blocked, never converted to 'passed'.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

REQUIRED_AUTOMATED=('cef_free_startup','native_ui','cef_isolated_profiles',
 'integrated_chrome_tabs_windows','extension_lifecycle','publisher_extensions',
 'profile_session_recovery','site_permissions_fake_media','navigation_pdf_engine',
 'tls_certificate_refusal','dns_encrypted_provider','soak_local_cef')
REQUIRED_DEVICE=('pdf_native_save_dialog','physical_print','physical_camera','physical_microphone',
 'screen_sharing','native_notification','keyboard_accessibility','screen_reader','high_dpi_contrast')


def evaluate(win10,win11,devices10=None,devices11=None):
    rows={'Windows 10':win10,'Windows 11':win11}
    failures=[]
    dll=set()
    for expected,evidence in rows.items():
        if not evidence:
            failures.append(expected+': missing automated workstation report');continue
        machine=evidence.get('machine',{})
        if machine.get('release')!=expected or machine.get('workstation') is not True or machine.get('architecture','').lower() not in ('amd64','x86_64'):
            failures.append(expected+': not a matching real x64 workstation')
        if evidence.get('status')!='passed_limited_scope':failures.append(expected+': automated suite not fully passed')
        actual=evidence.get('tests',{})
        for name in REQUIRED_AUTOMATED:
            if actual.get(name,{}).get('passed') is not True:
                failures.append(expected+': missing/failed '+name)
        key=evidence.get('sha256',{}).get('TuranBrowser.dll')
        if not key:failures.append(expected+': missing runtime DLL SHA-256')
        else:dll.add(key.lower())
    if len(dll)>1:failures.append('Two workstations tested DIFFERENT browser DLL hashes')
    for expected,evidence in (('Windows 10',devices10),('Windows 11',devices11)):
        if not evidence:
            failures.append(expected+': missing physical/accessibility observations');continue
        checks=evidence.get('checks',{})
        for name in REQUIRED_DEVICE:
            if checks.get(name,{}).get('result')!='passed':
                failures.append(expected+': physical/manual check missing or not passed: '+name)
        if evidence.get('dllSha256','').lower() not in dll:
            failures.append(expected+': manual device report DLL does not match workstation binary')
    # Further product-level blockers require engineering and operator approval:
    failures.extend(['Full original Turan toolbar + Chromium production architecture not independently accepted',
                     'Production update server/key deployment and live upgrade/rollback not independently accepted',
                     'No independent security review evidence supplied'])
    return {'timestampUtc':datetime.now(timezone.utc).isoformat(),'status':'BLOCKED',
            'message':'Cross-workstation QA is only one part of release acceptance; no automatic Stable promotion',
            'findings':failures,'matchedDllSha256':next(iter(dll)) if len(dll)==1 else None}


def load(file):
    return json.loads(file.read_text(encoding='utf-8')) if file else None


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('win10','win11','devices10','devices11'):
        p.add_argument('--'+key,type=Path)
    p.add_argument('--report',type=Path,default=Path('artifacts/combined-release-evidence.json'))
    a=p.parse_args()
    result=evaluate(load(a.win10),load(a.win11),load(a.devices10),load(a.devices11))
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'status':result['status'],'findings':len(result['findings']),
                      'report':str(a.report)},indent=2))
    return 1

if __name__=='__main__':sys.exit(main())
