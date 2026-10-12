"""Fetch publisher test artifacts only; these extensions are not redistributed."""
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def prepare():
    lock = json.loads((ROOT/'tests/extensions.thirdparty.lock.json').read_text())
    folder = (ROOT/'.deps/test-extensions').resolve()
    folder.mkdir(parents=True, exist_ok=True)
    result = {}
    for name, entry in lock.items():
        target = folder/(name+'-'+entry['version'])
        receipt = target/'turan-test-receipt.json'
        if not receipt.is_file() or json.loads(receipt.read_text()) != entry:
            archive = folder/(name+'.zip')
            with urllib.request.urlopen(entry['url'], timeout=60) as response, archive.open('wb') as output:
                size = 0
                while block := response.read(1024*1024):
                    size += len(block)
                    if size > 50*1024*1024: raise RuntimeError('Extension archive too large')
                    output.write(block)
            if hashlib.sha256(archive.read_bytes()).hexdigest() != entry['sha256']:
                raise RuntimeError('Publisher artifact SHA-256 mismatch: '+name)
            if target.exists():
                assert target.resolve().is_relative_to(folder)
                shutil.rmtree(target)
            target.mkdir()
            with zipfile.ZipFile(archive) as bundle:
                total = 0
                for item in bundle.infolist():
                    total += item.file_size
                    if not (target/item.filename).resolve().is_relative_to(target) or item.external_attr>>16 & 0o170000 == 0o120000:
                        raise RuntimeError('Unsafe extension archive path')
                    if total > 200*1024*1024: raise RuntimeError('Uncompressed test extension too large')
                bundle.extractall(target)
            receipt.write_text(json.dumps(entry, indent=2)+'\n')
            archive.unlink()
        manifests = [p for p in target.rglob('manifest.json') if json.loads(p.read_text()).get('manifest_version') == 3]
        if len(manifests) != 1: raise RuntimeError('Expected one MV3 manifest: '+name)
        result[name] = manifests[0].parent
        manifest = json.loads(manifests[0].read_text())
        print(name, manifest['version'], manifest.get('action'), manifest.get('options_ui'), flush=True)
    return result

if __name__ == '__main__': prepare()
