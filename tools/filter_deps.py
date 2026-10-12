"""Small pinned native RE2/Abseil dependencies. Never downloads Chromium."""
import hashlib
import json
from pathlib import Path
import tarfile
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
def prepare():
    lock=json.loads((ROOT/'filter.lock.json').read_text());deps=ROOT/'.deps';deps.mkdir(exist_ok=True)
    for name in ('re2','abseil'):
        item=lock[name];archive=deps/item['archive'];folder=deps/item['folder']
        if not archive.exists():
            with urllib.request.urlopen(item['url'],timeout=45) as response:data=response.read(item['size']+1)
            if len(data)!=item['size'] or hashlib.sha256(data).hexdigest()!=item['sha256']:raise RuntimeError('Filter dependency checksum/size mismatch: '+name)
            archive.write_bytes(data)
        if hashlib.sha256(archive.read_bytes()).hexdigest()!=item['sha256']:raise RuntimeError('Filter dependency checksum mismatch: '+name)
        if not (folder/'CMakeLists.txt').is_file():
            with tarfile.open(archive) as tar:
                members=[]
                for member in tar:
                    if member.name.split('/')[0]!=item['folder'] or not (deps/member.name).resolve().is_relative_to(deps.resolve()):raise RuntimeError('Unsafe filter dependency path')
                    if member.issym() and member.name in item['skippedLinks'] and member.linkname=='../LICENSE':continue
                    if not (member.isdir() or member.isreg()):raise RuntimeError('Unsafe filter dependency member')
                    members.append(member)
                tar.extractall(deps,members=members)
    psl=ROOT/'resources/public_suffix_list.dat'
    if not psl.is_file() or hashlib.sha256(psl.read_bytes()).hexdigest()!=lock['publicSuffix']['sha256']:raise RuntimeError('Pinned Public Suffix List changed')
    return {name:deps/lock[name]['folder'] for name in ('re2','abseil')}

if __name__=='__main__':print(prepare())
