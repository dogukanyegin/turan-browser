"""Offline, read-only integrity audit of a Turan source/portable distribution.

This checks package structure and SHA-256 provenance, NOT that the Windows
application works, nor that an NSIS installer contains the same files.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile
from safe_paths import safe_windows_relative, validate_inventory

REQUIRED_RUNTIME = frozenset({
    'TuranBrowser.exe', 'TuranBrowser.dll', 'TuranProfileTool.exe',
    'TuranUpdateTool.exe', 'libcef.dll', 'portable.flag',
})
EXEMPT_SOURCE = frozenset({'verification/build-manifest.json', 'verification/PERFORMANCE_REPORT.md'})
HEX_256 = re.compile(r'[0-9a-f]{64}\Z')


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def _safe_name(name):
    return safe_windows_relative(name)


def _archive_index(archive, prefix=''):
    """Reject duplicate/path-escaping entries and symlinks, including unused ones."""
    names = set()
    files = set()
    folded = set()
    for entry in archive.infolist():
        name = entry.filename
        is_dir = name.endswith('/')
        if is_dir:
            name = name[:-1]
        if not _safe_name(name):
            raise ValueError('Unsafe archive path: ' + repr(entry.filename))
        if prefix and not name.startswith(prefix):
            raise ValueError('Unexpected archive root: ' + name)
        lower = name.casefold()
        if lower in folded:
            raise ValueError(('Duplicate archive entry: ' if name in names else 'Case-alias archive entry: ') + name)
        folded.add(lower)
        names.add(name)
        if not is_dir:
            files.add(name)
        if entry.create_system == 3 and ((entry.external_attr >> 16) & 0o170000) == 0o120000:
            raise ValueError('Symlink in archive: ' + name)
    # ZIP often omits directory entries. Reject a file also used as a parent.
    folded_files = {p.casefold() for p in files}
    for name in names:
        parents = name.casefold().split('/')[:-1]
        for i in range(1, len(parents)+1):
            if '/'.join(parents[:i]) in folded_files:
                raise ValueError('Archive file shadows directory: ' + name)
    return names


def _json_member(archive, name):
    return json.loads(archive.read(name))


def _source_digest(archive, names):
    digest = hashlib.sha256()
    for name in sorted(names - EXEMPT_SOURCE):
        if name.endswith('/') or name in EXEMPT_SOURCE:
            continue
        digest.update(name.encode('utf-8') + b'\0')
        digest.update(archive.read(name) + b'\0')
    return digest.hexdigest()


def verify(source_path, portable_path, installer_path=None):
    source_path, portable_path = Path(source_path), Path(portable_path)
    with zipfile.ZipFile(source_path) as source, zipfile.ZipFile(portable_path) as portable:
        source_names = _archive_index(source)
        portable_names = _archive_index(portable, 'TuranBrowser/')
        if source.testzip() is not None or portable.testzip() is not None:
            raise ValueError('Invalid ZIP CRC')
        manifest = _json_member(portable, 'TuranBrowser/build-manifest.json')
        if _json_member(source, 'verification/build-manifest.json') != manifest:
            raise ValueError('Source/archive build manifests differ')
        release = _json_member(source, 'release.json')
        engine = _json_member(source, 'cef.lock.json')
        if release != manifest.get('release') or engine['version'] != manifest.get('engine'):
            raise ValueError('Release/engine metadata mismatch')
        actual_source = _source_digest(source, source_names)
        if actual_source != manifest.get('source_sha256'):
            raise ValueError('Source code does not match the built release')
        digest_map = manifest.get('binary_sha256')
        if not isinstance(digest_map, dict) or not digest_map:
            raise ValueError('Missing binary hash inventory')
        present = set()
        try:
            validate_inventory(digest_map.keys())
        except ValueError as exc:
            raise ValueError('Invalid binary manifest inventory: ' + str(exc)) from exc
        for name, claimed in digest_map.items():
            if not isinstance(name, str) or not _safe_name(name) or not isinstance(claimed, str) or not HEX_256.fullmatch(claimed):
                raise ValueError('Invalid binary manifest entry')
            member = 'TuranBrowser/' + name
            if member not in portable_names:
                raise ValueError('Missing packaged binary: ' + name)
            if sha256(portable.read(member)) != claimed:
                raise ValueError('Binary SHA-256 mismatch: ' + name)
            present.add(name)
        # New source versions must ship the actual DLL-independent recovery launcher.
        required = set(REQUIRED_RUNTIME)
        if 'src/update/startup.cpp' in source_names:
            required.add('TuranStartup.exe')
        for name in required:
            if 'TuranBrowser/' + name not in portable_names:
                raise ValueError('Missing required runtime: ' + name)
            if name != 'portable.flag' and name not in present:
                raise ValueError('Required runtime omitted from hash inventory: ' + name)
        result = {
            'result': 'PACKAGE_INTEGRITY_PASSED_NOT_STABLE_ACCEPTANCE',
            'version': release['version'],
            'engine': engine['version'],
            'source_sha256': actual_source,
            'portable_sha256': sha256(portable_path.read_bytes()),
            'hashed_runtime_files': len(digest_map),
            'source_matches_built_package': True,
            'windows_execution_tested': False,
            'installer_payload_verified': False,
        }
    if installer_path:
        installer_path = Path(installer_path)
        with installer_path.open('rb') as inp:
            if inp.read(2) != b'MZ':
                raise ValueError('Setup is not a Windows PE executable')
        result['installer_sha256'] = sha256(installer_path.read_bytes())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('portable', type=Path)
    parser.add_argument('--installer', type=Path)
    parser.add_argument('--json-output', type=Path)
    args = parser.parse_args()
    result = verify(args.source, args.portable, args.installer)
    output = json.dumps(result, ensure_ascii=False, indent=2) + '\n'
    if args.json_output:
        args.json_output.write_text(output, encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
