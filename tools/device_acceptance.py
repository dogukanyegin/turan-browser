"""Record REAL human-observed Windows peripheral and accessibility results.

No simulated hardware, hidden bypass flags or automatic PASS defaults. Run on
Windows workstation in a normal interactive console with actual user hardware.
Do not include camera/microphone recordings, images or sensitive window content.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import sys

ROOT=Path(__file__).resolve().parents[1]
CHECKS={
 'pdf_native_save_dialog':('Open Turan, save a PDF using its normal native Save As dialog; confirm resulting PDF exists and opens in a separate viewer.', 'native Save As + independent PDF viewer'),
 'physical_print':('Print a Turan web page to an actual physical printer and inspect the PAPER for correct output. Microsoft Print to PDF does NOT count.', 'real paper printer'),
 'physical_camera':('Allow a test site physical camera access; confirm live video, deny/revoke and confirm capture stops.', 'real physical camera'),
 'physical_microphone':('Allow a test site physical microphone access; confirm live voice signal, then revoke and confirm capture ends.', 'real physical microphone'),
 'screen_sharing':('Grant screen sharing for a test site, select a window/display, confirm real frames, then stop sharing and confirm track ends.', 'real screen share'),
 'native_notification':('Grant then revoke notification permission and confirm an actual system notification appears and then is refused.', 'Windows notification center'),
 'keyboard_accessibility':('Using ONLY keyboard, navigate address bar, tabs, downloads, extensions, settings, dialogs and confirm visible focus indicators.', 'actual keyboard navigation'),
 'screen_reader':('Verify spoken control names, reading order and actionable controls using Narrator or NVDA on Turan windows and dialogs.', 'real screen reader'),
 'high_dpi_contrast':('At 200% Windows scaling and with high contrast, verify labels, menus, fields, dialogs and resizing are usable.', '200% DPI + high contrast'),
}


def hash_file(file):
    h=hashlib.sha256()
    with file.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1048576),b''):
            h.update(chunk)
    return h.hexdigest()


def save(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    tmp=path.with_name(path.name+'.tmp')
    tmp.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    os.replace(tmp,path)


def load_resumable_report(path, expected):
    """Load a partial manual review only for the same workstation and binaries.

    Never import a claimed PASS from a different binary or from malformed data.
    """
    if not path.is_file():
        raise ValueError('No previous manual report found: ' + str(path))
    try:
        previous=json.loads(path.read_text(encoding='utf-8'))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise ValueError('Previous manual report is unreadable or invalid JSON') from exc
    if not isinstance(previous,dict) or previous.get('status')!='incomplete':
        raise ValueError('Resume requires a report with status=incomplete')
    for key in ('workstation','browserSha256','dllSha256'):
        if previous.get(key)!=expected[key]:
            raise ValueError('Report mismatch: ' + key + ' differs from current runtime')
    old_checks=previous.get('checks')
    if not isinstance(old_checks,dict) or len(old_checks)>=len(CHECKS):
        raise ValueError('No valid incomplete check set to resume')
    for key, record in old_checks.items():
        if key not in CHECKS or not isinstance(record,dict):
            raise ValueError('Unexpected manual check in previous report: '+str(key))
        if record.get('result') not in ('passed','failed','not_tested') or record.get('scope')!=CHECKS[key][1]:
            raise ValueError('Malformed manual check result: '+key)
    return previous


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime',type=Path,default=ROOT/'out-cef/app/Release')
    p.add_argument('--report',type=Path,default=ROOT/'artifacts/manual-device-acceptance.json')
    p.add_argument('--resume',action='store_true',help='Continue an incomplete report without erasing existing observations')
    args=p.parse_args()
    if sys.platform!='win32' or sys.getwindowsversion().product_type!=1:
        raise RuntimeError('Use a real Windows 10 or Windows 11 workstation')
    if not sys.stdin.isatty():
        raise RuntimeError('Interactive console required: cannot approve hardware tests non-interactively')
    runtime=args.runtime.resolve(strict=True)
    exe=runtime/'TuranBrowser.exe';dll=runtime/'TuranBrowser.dll'
    if not exe.is_file() or not dll.is_file():
        raise RuntimeError('Missing compiled executable/DLL')
    report={'timestampUtc':datetime.now(timezone.utc).isoformat(),'status':'incomplete',
        'workstation':platform.platform(),'browserSha256':hash_file(exe),
        'dllSha256':hash_file(dll),'method':'human-observed physical tests; not automated verification',
        'checks':{}}
    if args.resume:
        report=load_resumable_report(args.report,report)
        report['lastResumedUtc']=datetime.now(timezone.utc).isoformat()
        print('Resuming existing results:', ', '.join(report['checks']) or '(none)', flush=True)
    save(args.report,report)
    print('All tests require DIRECT observation. Never answer PASS for simulation.',flush=True)
    print('Tests run on the installed runtime, not this script. Use a disposable browser profile.',flush=True)
    for key,(instructions,scope) in CHECKS.items():
        if key in report['checks']:
            print('KEEP existing result:',key,report['checks'][key]['result'],flush=True)
            continue
        print('\n'+key.upper()+'\n'+instructions,flush=True)
        while True:
            answer=input('Observed result? [p]ass / [f]ail / [n]ot tested: ').strip().lower()
            if answer in ('p','f','n'):break
        outcome={'p':'passed','f':'failed','n':'not_tested'}[answer]
        comment=input('Optional short observation (NO personal information): ').strip()[:280]
        report['checks'][key]={'result':outcome,'scope':scope,'observation':comment}
        report['status']='completed_manual_review' if len(report['checks'])==len(CHECKS) else 'incomplete'
        save(args.report,report)
    report['allObservedPass']=all(c['result']=='passed' for c in report['checks'].values())
    save(args.report,report)
    print('\nManual evidence saved:',args.report,'\nAll observed PASS:',report['allObservedPass'],flush=True)
    print('Manual confirmation is NOT an independent audit and cannot by itself approve Stable.',flush=True)
    return 0 if report['allObservedPass'] else 1

if __name__=='__main__':sys.exit(main())
