"""CEF-free startup recovery smoke checks, Windows only.

No user profile is touched, and the fixture deliberately does not start CEF.
An invalid journal must prevent browser startup and preserve its original bytes.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile


def run(startup, expected):
    result = subprocess.run([str(startup), '--check-only'], timeout=20, capture_output=True)
    if result.returncode != expected:
        raise AssertionError(f'{startup} exit {result.returncode}, expected {expected}')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runtime', type=Path, required=True)
    args = parser.parse_args()
    runtime = args.runtime.resolve(strict=True)
    for name in ('TuranStartup.exe', 'TuranBrowser.exe', 'TuranBrowser.dll', 'libcef.dll'):
        if not (runtime / name).is_file():
            raise AssertionError('Missing newly built startup runtime: ' + name)
    run(runtime / 'TuranStartup.exe', 0)
    with tempfile.TemporaryDirectory(prefix='turan-startup-fixture-') as directory:
        app = Path(directory)
        for name in ('TuranStartup.exe', 'TuranBrowser.exe', 'TuranBrowser.dll', 'libcef.dll'):
            shutil.copyfile(runtime / name, app / name)
        user = app / 'sample-user-file.txt'
        user.write_bytes(b'User file must not be modified by startup checks')
        before = hashlib.sha256(user.read_bytes()).digest()
        run(app / 'TuranStartup.exe', 0)
        journal = app / '.turan-update-transaction'
        journal.write_bytes(b'DELIBERATELY CORRUPT TRANSACTION - MUST FAIL CLOSED\n')
        run(app / 'TuranStartup.exe', 13)
        assert journal.read_bytes() == b'DELIBERATELY CORRUPT TRANSACTION - MUST FAIL CLOSED\n'
        assert hashlib.sha256(user.read_bytes()).digest() == before
        journal.unlink()
        (app / 'TuranBrowser.dll').unlink()
        run(app / 'TuranStartup.exe', 14)
        assert hashlib.sha256(user.read_bytes()).digest() == before
    print('PASS CEF-free startup preflight: clean boot, invalid journal refusal, missing DLL refusal, user-file preservation')

if __name__ == '__main__':
    main()
