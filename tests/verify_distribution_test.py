"""Exercise provenance checks against small, deliberately tampered fixtures."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_distribution import verify, REQUIRED_RUNTIME


def zwrite(path, items):
    with zipfile.ZipFile(path, 'w') as archive:
        for name, data in items.items():
            archive.writestr(name, data)


def synthetic(root):
    source = root / 'src.zip'
    portable = root / 'run.zip'
    release = dict(version='0.2.0-dev', major=0, minor=2, patch=0, revision=0, channel='development', stable=False)
    lock = dict(version='cef-fixture')
    contents = {
        'release.json': json.dumps(release).encode(),
        'cef.lock.json': json.dumps(lock).encode(),
        'src/main.cpp': b'fixture source',
    }
    source_hash = hashlib.sha256()
    for key, data in sorted(contents.items()):
        source_hash.update(key.encode() + b'\0' + data + b'\0')
    binaries = {name: (b'MZ' if name.endswith('.exe') else b'dummy') for name in REQUIRED_RUNTIME - {'portable.flag'}}
    manifest = dict(release=release, engine='cef-fixture', source_sha256=source_hash.hexdigest(),
                    binary_sha256={n:hashlib.sha256(d).hexdigest() for n, d in binaries.items()})
    contents['verification/build-manifest.json'] = json.dumps(manifest).encode()
    zwrite(source, contents)
    zwrite(portable, {'TuranBrowser/'+name: data for name, data in
                      {**binaries, 'portable.flag':b'1', 'build-manifest.json':json.dumps(manifest).encode()}.items()})
    return source, portable, contents


class DistributionAuditTests(unittest.TestCase):
    def test_valid_package(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            result = verify(source, portable)
            self.assertTrue(result['source_matches_built_package'])
            self.assertFalse(result['windows_execution_tested'])

    def test_source_tamper_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, contents = synthetic(Path(t))
            contents['src/main.cpp'] = b'REPLACED CODE'
            zwrite(source, contents)
            with self.assertRaisesRegex(ValueError, 'Source code'):
                verify(source, portable)

    def test_runtime_tamper_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable) as archive:
                members = {name:archive.read(name) for name in archive.namelist()}
            members['TuranBrowser/TuranBrowser.dll'] = b'new-content'
            zwrite(portable, members)
            with self.assertRaisesRegex(ValueError, 'Binary SHA'):
                verify(source, portable)

    def test_unlisted_runtime_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable) as archive:
                members = {name:archive.read(name) for name in archive.namelist()}
            del members['TuranBrowser/libcef.dll']
            zwrite(portable, members)
            with self.assertRaisesRegex(ValueError, 'Missing packaged binary'):
                verify(source, portable)

    def test_zip_traversal_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/../profile-secret.txt', 'bad')
            with self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                verify(source, portable)

    def test_duplicate_zip_member_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/portable.flag', 'duplicate')
            with self.assertRaisesRegex(ValueError, 'Duplicate archive entry'):
                verify(source, portable)

    def test_case_insensitive_archive_alias_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/turanbrowser.dll', 'case alias')
            with self.assertRaisesRegex(ValueError, 'Case-alias archive entry'):
                verify(source, portable)

    def test_windows_device_name_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/resources/CON.txt', 'device alias')
            with self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                verify(source, portable)

    def test_archive_file_cannot_shadow_directory(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/something', 'file')
                archive.writestr('TuranBrowser/something/child.txt', 'ambiguous')
            with self.assertRaisesRegex(ValueError, 'shadows directory'):
                verify(source, portable)

    def test_windows_trailing_dot_and_ads_refused(self):
        with tempfile.TemporaryDirectory() as t:
            source, portable, _ = synthetic(Path(t))
            with zipfile.ZipFile(portable, 'a') as archive:
                archive.writestr('TuranBrowser/filename.', b'data')
            with self.assertRaisesRegex(ValueError, 'Unsafe archive path'):
                verify(source, portable)



if __name__ == '__main__':
    unittest.main()
