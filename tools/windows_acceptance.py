"""Windows workstation acceptance with incremental, failure-preserving reporting.

The report is written *before* lengthy subprocesses begin and after each result.
No report or passing test is evidence of Stable release approval.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]


def sha256(file):
    h = hashlib.sha256()
    with file.open('rb') as stream:
        for data in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(data)
    return h.hexdigest()


def actual_windows():
    if sys.platform != 'win32':
        raise RuntimeError('A real Windows 10/11 workstation is required')
    version = sys.getwindowsversion()
    os_kind = ('Windows 11' if version.build >= 22000 else 'Windows 10'
               if version.major == 10 and version.build >= 10240 else 'unsupported')
    return {'platform': platform.platform(), 'release': os_kind,
            'major': version.major, 'minor': version.minor,
            'build': version.build, 'product_type': version.product_type,
            'workstation': version.product_type == 1,
            'architecture': platform.machine()}


def _tail(data, max_len=2500):
    if isinstance(data, bytes):
        data = data.decode('utf-8', errors='replace')
    return (data or '')[-max_len:]


def run(argv, timeout):
    try:
        p = subprocess.run([str(x) for x in argv], cwd=ROOT,
                           capture_output=True, text=True, encoding='utf-8',
                           errors='replace', timeout=timeout)
        return {'passed': p.returncode == 0, 'returncode': p.returncode,
                'stdout_tail': _tail(p.stdout), 'stderr_tail': _tail(p.stderr)}
    except subprocess.TimeoutExpired as error:
        return {'passed': False, 'returncode': None, 'timedOut': True,
                'timeoutSeconds': timeout, 'stdout_tail': _tail(error.stdout),
                'stderr_tail': _tail(error.stderr),
                'error': 'Test exceeded its timeout; the result is NOT a pass'}
    except Exception as error:
        return {'passed': False, 'returncode': None, 'error': f'{type(error).__name__}: {error}'}


def write_report(path, report):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + '.tmp')
    temp.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    os.replace(temp, path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', type=Path, default=ROOT / 'out-cef/app/Release')
    parser.add_argument('--report', type=Path, default=ROOT / 'artifacts/windows-workstation-acceptance.json')
    parser.add_argument('--skip-ui', action='store_true', help='Check only CEF-free startup and fingerprints')
    parser.add_argument('--extended', action='store_true', help='Run real Chromium, extension, network and session integration probes')
    parser.add_argument('--soak-minutes', type=int, default=0, help='Real local-page soak (60 recommended); zero disables')
    args = parser.parse_args()
    report_path = args.report.resolve()
    report = {'timestampUtc': datetime.now(timezone.utc).isoformat(),
              'status': 'running', 'scope': 'Actual Windows isolated-profile checks; not Stable approval',
              'tests': {}, 'notTested': [
                  'Physical printer and print preview', 'Real hardware mic/camera',
                  'Screen sharing and actual notification delivery',
                  'Accessibility audit with real assistive technology',
                  'Long soak and 8 GiB low-memory performance',
                  'Independent vulnerability review and network leak packet capture',
                  'Production release-key custody and real endpoint']}
    # Persist before entering the lengthy test suite. Also persist preflight failures.
    write_report(report_path, report)
    print('Acceptance report:', report_path, flush=True)
    try:
        machine = actual_windows()
        report['machine'] = machine
        if (not machine['workstation'] or machine['release'] == 'unsupported'
                or platform.machine().lower() not in ('amd64', 'x86_64')):
            raise RuntimeError('Actual Windows 10/11 x64 workstation required; server or simulation refused')
        runtime = args.runtime.resolve(strict=True)
        report['runtime'] = str(runtime)
        binaries = tuple(runtime / name for name in
                         ('TuranBrowser.exe', 'TuranStartup.exe', 'TuranBrowser.dll', 'libcef.dll'))
        missing = [str(file) for file in binaries if not file.is_file()]
        if missing:
            raise RuntimeError('Missing required runtime files: ' + ', '.join(missing))
        report['sha256'] = {file.name: sha256(file) for file in binaries}
        write_report(report_path, report)

        cases = [('cef_free_startup', [sys.executable, ROOT / 'tests/startup_preflight.py',
                                      '--runtime', runtime], 90)]
        if not args.skip_ui:
            cases += [
                ('native_ui', [sys.executable, ROOT / 'tests/ui_smoke.py',
                               '--exe', runtime / 'TuranBrowser.exe'], 180),
                ('cef_isolated_profiles', [sys.executable, ROOT / 'tests/smoke_cef.py',
                                           '--exe', runtime / 'TuranBrowser.exe'], 220)]
        if args.extended and not args.skip_ui:
            cases += [
                ('integrated_chrome_tabs_windows', [sys.executable,ROOT/'tests/integrated_chrome_cef.py'],240),
                ('extension_lifecycle', [sys.executable,ROOT/'tests/extensions_lifecycle_cef.py','--native-picker'],240),
                ('publisher_extensions', [sys.executable,ROOT/'tests/thirdparty_extensions_cef.py',
                                          '--runtime','chrome-integrated'],360),
                ('profile_session_recovery', [sys.executable,ROOT/'tests/session_cef.py'],280),
                ('site_permissions_fake_media', [sys.executable,ROOT/'tests/permissions_cef.py'],260),
                ('navigation_pdf_engine', [sys.executable,ROOT/'tests/navigation_cef.py'],240),
                ('tls_certificate_refusal', [sys.executable,ROOT/'tests/tls_cef.py'],180),
                ('dns_encrypted_provider', [sys.executable,ROOT/'tests/dns_cef.py'],240),
            ]
        if args.soak_minutes and not args.skip_ui:
            cases.append(('soak_local_cef',[sys.executable,ROOT/'tests/soak_cef.py',
                 '--exe',runtime/'TuranBrowser.exe','--minutes',str(args.soak_minutes)],
                 args.soak_minutes*60+180))
        for name, cmd, timeout in cases:
            if name != 'cef_free_startup' and not report['tests']['cef_free_startup']['passed']:
                report['tests'][name] = {'passed': False, 'notRun': True,
                                         'reason': 'CEF-free startup preflight failed'}
                write_report(report_path, report)
                print(name, 'NOT RUN - startup preflight failed', flush=True)
                continue
            print('RUN', name, '(timeout', str(timeout) + 's)', flush=True)
            report['tests'][name] = {'status': 'running', 'passed': False}
            write_report(report_path, report)
            result = run(cmd, timeout)
            report['tests'][name] = result
            write_report(report_path, report)
            print(name, 'PASS' if result['passed'] else 'FAIL', flush=True)
            if not result['passed']:
                print('  details:', result.get('error', '') or _tail(result.get('stderr_tail')), flush=True)

        success = all(v.get('passed', False) for v in report['tests'].values())
        report['status'] = 'passed_limited_scope' if success else 'failed'
        report['extendedRequested']=bool(args.extended)
        report['soakMinutesRequested']=args.soak_minutes
        if args.extended and success:
            report['notes']='Integration fixtures passed, but no full Chrome Web Store, authenticated Bitwarden, or custom UI certification is implied.'
    except Exception as error:
        report['status'] = 'failed'
        report['error'] = f'{type(error).__name__}: {error}'
        print('Acceptance error:', report['error'], file=sys.stderr, flush=True)
    finally:
        write_report(report_path, report)
        print('Final report:', report_path, flush=True)
    return 0 if report['status'] == 'passed_limited_scope' else 1


if __name__ == '__main__':
    sys.exit(main())
