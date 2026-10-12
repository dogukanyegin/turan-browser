"""Assemble and verify a static HTTPS Turan update publication offline.

No network access, private key access, server credentials, or untrusted archive
extraction. The private key is used only by update_release.py publish. Never
serve a partial pair: deploy payload first, then manifest and .sig last.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

from safe_paths import validate_inventory
from update_release import https_url, safe_name, public_blob


def _digest(path):
    sha=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(65536),b''):
            sha.update(chunk)
    return sha.hexdigest()


def _plain_file(path,root):
    if not path.is_file() or path.is_symlink() or not path.resolve(strict=True).is_relative_to(root):
        raise ValueError('Missing/redirected release file: '+str(path))
    for part in [path,*path.parents]:
        if part==root.parent:break
        if getattr(part.lstat(),'st_file_attributes',0)&0x400:
            raise ValueError('Reparse point in publication')


def _read_signed(manifest,public_key,openssl):
    signature=Path(str(manifest)+'.sig')
    if not manifest.is_file() or not signature.is_file():
        raise ValueError('Both manifest and signature must exist')
    if manifest.stat().st_size>1024*1024 or signature.stat().st_size>1024:
        raise ValueError('Signature/manifest size exceeded')
    if len(public_blob(openssl,public_key))==0:
        raise ValueError('Invalid publisher key')
    result=subprocess.run([str(openssl),'dgst','-sha256','-verify',str(public_key),
         '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',
         '-signature',str(signature),str(manifest)],capture_output=True)
    if result.returncode:
        raise ValueError('Publisher RSA-PSS/SHA-256 signature verification failed')
    data=manifest.read_bytes()
    if not data.endswith(b'\n') or b'\r' in data:
        raise ValueError('Manifest must use canonical ASCII/LF')
    try:lines=data.decode('ascii').splitlines()
    except UnicodeDecodeError:raise ValueError('Manifest is not ASCII') from None
    if len(lines)<10 or lines[0]!='TURAN-UPDATE-1':
        raise ValueError('Invalid Turan manifest format')
    keys=['version','sequence','channel','kind','issued','expires','base','files']
    metadata={}
    for number,key in enumerate(keys,1):
        parts=lines[number].split('\t')
        if len(parts)!=2 or parts[0]!=key:
            raise ValueError('Signed metadata has an unexpected schema')
        metadata[key]=parts[1]
    base=metadata['base']
    if not https_url(base) or not base.endswith('/'):
        raise ValueError('Signed publication base must be canonical HTTPS')
    if metadata['kind'] not in ('portable','installer') or metadata['channel'] not in ('development','beta','stable'):
        raise ValueError('Invalid release kind/channel')
    if not re.fullmatch(r'[0-9]+(\.[0-9]+){3}',metadata['version']):
        raise ValueError('Invalid version')
    try:
        count=int(metadata['files']);seq=int(metadata['sequence']);issued=int(metadata['issued']);expires=int(metadata['expires'])
    except ValueError:raise ValueError('Non-numeric signed metadata') from None
    now=int(time.time())
    if not (0<count<=4096 and 0<seq and issued<expires<=issued+31*86400 and len(lines)==9+count):
        raise ValueError('Invalid signed metadata constraints')
    if issued>now+300 or expires<now:
        raise ValueError('Signed release metadata is expired or not yet valid')
    files=[]
    total=0
    for line in lines[9:]:
        fields=line.split('\t')
        if len(fields)!=4 or fields[0]!='file' or not fields[1].isdigit() or not re.fullmatch(r'[a-f0-9]{64}',fields[2]) or not safe_name(fields[3]):
            raise ValueError('Invalid signed inventory entry')
        size=int(fields[1]);total+=size
        if size>2*1024**3 or total>4*1024**3:
            raise ValueError('Signed runtime size budget exceeded')
        files.append((fields[3],size,fields[2]))
    validate_inventory([entry[0] for entry in files])
    if [row[0] for row in files]!=sorted([row[0] for row in files],key=str.casefold):
        raise ValueError('Signed inventory is not sorted')
    return metadata,files


def assemble(runtime,manifest,public_key,openssl,destination):
    root=runtime.resolve(strict=True)
    metadata,files=_read_signed(manifest,public_key,openssl)
    if destination.exists():
        raise ValueError('Output directory exists; cannot overwrite previous release')
    dest_parent=destination.parent.resolve(strict=True)
    if dest_parent==root or dest_parent.is_relative_to(root):
        raise ValueError('Output cannot be nested inside the runtime')
    with tempfile.TemporaryDirectory(prefix='.turan-publish-',dir=dest_parent) as temp:
        staging=Path(temp)
        for name,size,digest in files:
            source=root/name
            _plain_file(source,root)
            if source.stat().st_size!=size or _digest(source)!=digest:
                raise ValueError('Payload does not match signed inventory: '+name)
            target=staging/'payload'/name
            target.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source,target)
            if target.stat().st_size!=size or _digest(target)!=digest:
                raise ValueError('Copied payload verification failed: '+name)
        # Manifest/signature are published after payloads. A static server must
        # retain the same HTTP paths provided below and disallow redirects.
        shutil.copyfile(manifest,staging/'manifest.txt')
        shutil.copyfile(Path(str(manifest)+'.sig'),staging/'manifest.txt.sig')
        output={
            'serverConfigurationRequired':True,
            'payloadUrlPrefix':metadata['base'],
            'uploadPayloadFrom':'payload/',
            'uploadManifestLastFrom':'manifest.txt',
            'uploadSignatureLastFrom':'manifest.txt.sig',
            'manifestSha256':_digest(staging/'manifest.txt'),
            'fileCount':len(files),
            'totalBytes':sum(size for _,size,_ in files),
            'mode':'unsigned HTTPS transport; content authenticated by publisher RSA-PSS and SHA256',
            'status':'NOT_DEPLOYED',
        }
        (staging/'deployment.json').write_text(json.dumps(output,indent=2)+'\n',encoding='utf-8')
        os.rename(staging,destination)
    return output


def verify(destination,public_key,openssl):
    base=destination.resolve(strict=True)
    metadata,files=_read_signed(base/'manifest.txt',public_key,openssl)
    total=0
    for name,size,digest in files:
        path=base/'payload'/name
        _plain_file(path,base)
        if path.stat().st_size!=size or _digest(path)!=digest:
            raise ValueError('Published payload mismatch: '+name)
        total+=size
    observed=set()
    for path in (base/'payload').rglob('*'):
        if path.is_symlink():
            raise ValueError('Payload contains a symbolic link')
        if path.is_file():
            observed.add(path.relative_to(base/'payload').as_posix())
    if observed!={name for name,_,_ in files}:
        raise ValueError('Extra/missing payload files')
    return {'verified':True,'files':len(files),'bytes':total,'baseUrl':metadata['base'],
            'publisherManifestSha256':_digest(base/'manifest.txt')}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sub=parser.add_subparsers(dest='cmd',required=True)
    build=sub.add_parser('assemble')
    build.add_argument('--runtime',type=Path,required=True)
    build.add_argument('--manifest',type=Path,required=True)
    build.add_argument('--destination',type=Path,required=True)
    check=sub.add_parser('verify')
    check.add_argument('--deployment',type=Path,required=True)
    for cmd in (build,check):
        cmd.add_argument('--public-key',type=Path,required=True)
        cmd.add_argument('--openssl',type=Path,required=True)
    args=parser.parse_args()
    result=assemble(args.runtime,args.manifest,args.public_key,args.openssl,args.destination) if args.cmd=='assemble' else verify(args.deployment,args.public_key,args.openssl)
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
