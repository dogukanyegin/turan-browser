"""Create publisher update inventories/signatures; never download Chromium.

The private RSA key remains outside the source/distribution. Signing uses an
explicit OpenSSL executable; 3072/4096-bit RSA, exponent 65537, PSS/SHA-256,
32-byte salt. Public-key provisioning creates an external C++ build header.
This is publisher metadata signing, not Windows Authenticode signing.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time
import os
import tempfile
from urllib.parse import urlsplit
from safe_paths import safe_windows_relative, validate_inventory


def der_item(data, offset=0):
    if offset+2>len(data):
        raise ValueError('Truncated DER')
    tag, length=data[offset:offset+2]
    offset+=2
    if length&128:
        count=length&127
        if not 1<=count<=4 or offset+count>len(data):
            raise ValueError('Invalid DER length')
        length=int.from_bytes(data[offset:offset+count], 'big')
        offset+=count
    end=offset+length
    if end>len(data):
        raise ValueError('Truncated DER value')
    return tag,data[offset:end],end


def public_blob(openssl, public_key):
    der=subprocess.check_output([str(openssl),'rsa','-pubin','-in',str(public_key),'-RSAPublicKey_out','-outform','DER'],stderr=subprocess.PIPE)
    tag,seq,end=der_item(der)
    if tag!=48 or end!=len(der):
        raise ValueError('Expected RSA public key')
    tag,n,offset=der_item(seq)
    tag2,e,end=der_item(seq,offset)
    if tag!=2 or tag2!=2 or end!=len(seq):
        raise ValueError('Invalid RSA key')
    n=n.lstrip(b'\0')
    e=e.lstrip(b'\0')
    bits=int.from_bytes(n,'big').bit_length()
    if bits not in (3072,4096) or int.from_bytes(e,'big')!=65537:
        raise ValueError('Publisher key requires RSA 3072/4096, exponent 65537')
    return struct.pack('<6I',0x31415352,bits,len(e),len(n),0,0)+e+n


def https_url(url):
    parsed=urlsplit(url)
    return (parsed.scheme=='https' and parsed.netloc==parsed.hostname and '.' in (parsed.hostname or '') and
            re.fullmatch(r'[a-z0-9.-]+',parsed.hostname or '') and parsed.path.startswith('/') and
            not parsed.query and not parsed.fragment and '%' not in url and
            not any(part in ('.','..') for part in parsed.path.split('/')) and len(url)<=2048)


def provision(args):
    if not https_url(args.manifest_url):
        raise ValueError('Canonical HTTPS manifest URL required, without credentials, ports, query or redirects')
    if args.sequence<1:
        raise ValueError('A nonzero monotonic installed release sequence is required')
    blob=public_blob(args.openssl,args.public_key)
    text=('// Publisher public key only. Generated outside the source tree.\n'
          'namespace turan::update::configured {\n'
          f'constexpr char public_key_hex[]={json.dumps(blob.hex())};\n'
          f'constexpr char manifest_url[]={json.dumps(args.manifest_url)};\n'
          f'constexpr char channel[]={json.dumps(args.channel)};\n'
          f'constexpr unsigned long long sequence={args.sequence}ULL;\n'
          '}\n')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(text,encoding='ascii')
    print(json.dumps({'publisher_public_key_sha256':hashlib.sha256(blob).hexdigest(),
                      'channel':args.channel,'manifest_url':args.manifest_url,'authenticode':False}))


ROOT_NAMES={
    'turanbrowser.exe','turanbrowser.dll','turanstartup.exe','turanprofiletool.exe','turanupdatetool.exe','libcef.dll','chrome_elf.dll',
    'd3dcompiler_47.dll','dxil.dll','dxcompiler.dll','libegl.dll','libglesv2.dll','vulkan-1.dll','vk_swiftshader.dll','vk_swiftshader_icd.json',
    'chrome_100_percent.pak','chrome_200_percent.pak','resources.pak','icudtl.dat','v8_context_snapshot.bin',
    'cef-license.txt','cef-credits.html','license.txt','third_party_notices.md','readme.md','test_report.md',
    'release_notes.md','installation.md','security.md','privacy.md','performance_report.md','build-manifest.json'
}


def safe_name(name):
    if not safe_windows_relative(name):
        return False
    parts=name.split('/')
    lower=name.lower()
    if lower.startswith(('resources/','locales/','docs/')):
        return len(parts)<=8 and '/profile/' not in lower and '/profiles/' not in lower
    return lower in ROOT_NAMES


def publish(args):
    if not https_url(args.base_url) or not args.base_url.endswith('/'):
        raise ValueError('A canonical HTTPS payload base ending in / is required')
    if not re.fullmatch(r'(0|[1-9][0-9]{0,4})(\.(0|[1-9][0-9]{0,4})){3}',args.version) or any(int(x)>65535 for x in args.version.split('.')):
        raise ValueError('Version must be major.minor.patch.revision')
    if args.sequence<1 or not 1<=args.days<=31:
        raise ValueError('Invalid sequence or validity duration')
    names=args.inventory.read_text(encoding='utf-8').splitlines()
    if len(names)>4096:
        raise ValueError('Runtime inventory size exceeded')
    names=sorted(validate_inventory(names), key=str.casefold)
    if not {'turanbrowser.exe','turanbrowser.dll','turanstartup.exe','turanupdatetool.exe','libcef.dll'}<={n.lower() for n in names}:
        raise ValueError('The engine/bootstrap/DLL are mandatory')
    now=int(time.time())
    lines=['TURAN-UPDATE-1',f'version\t{args.version}',f'sequence\t{args.sequence}',f'channel\t{args.channel}',
           f'kind\t{args.kind}',f'issued\t{now}',f'expires\t{now+args.days*86400}',f'base\t{args.base_url}',f'files\t{len(names)}']
    total=0
    root=args.runtime.resolve(strict=True)
    for name in names:
        if not safe_name(name):
            raise ValueError('Unsafe runtime path: '+name)
        path=root/name
        if not path.is_file() or path.is_symlink() or not path.resolve(strict=True).is_relative_to(root):
            raise ValueError('Missing/redirected runtime payload: '+name)
        # Windows directory junctions are also excluded, not just symlink leaves.
        for component in [path,*path.parents]:
            if component==root.parent:
                break
            if getattr(component.lstat(),'st_file_attributes',0)&0x400:
                raise ValueError('Reparse point in runtime payload')
        size=path.stat().st_size
        total+=size
        if size>2*1024**3 or total>4*1024**3:
            raise ValueError('Runtime size budget exceeded')
        digest=hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda:stream.read(65536),b''):
                digest.update(block)
        lines.append(f'file\t{size}\t{digest.hexdigest()}\t{name}')
    # Complete every key and payload validation BEFORE publishing even one byte.
    # A failed or interrupted signing operation must not leave a signed-looking
    # manifest alongside an old/unrelated .sig from a previous release.
    manifest_bytes=('\n'.join(lines)+'\n').encode('ascii')
    signature=Path(str(args.output)+'.sig')
    if args.output.exists() or signature.exists():
        raise ValueError('Release output already exists; choose a fresh release filename')
    if args.channel=='stable' and not getattr(args,'expected_public_key',None):
        raise ValueError('Stable signing requires the explicitly provisioned publisher public key')
    private=args.private_key.resolve(strict=True)
    if private.is_relative_to(root):
        raise ValueError('Private signing key must not reside in the runtime payload tree')
    # Derive the actual signer public key; do not assume that a header and a key match.
    public_der=subprocess.check_output([str(args.openssl),'pkey','-in',str(private),'-pubout'],stderr=subprocess.PIPE)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.turan-signing-',dir=args.output.parent) as temp:
        staging=Path(temp)
        public_pem=staging/'signer-public.pem'
        public_pem.write_bytes(public_der)
        derived_blob=public_blob(args.openssl,public_pem)
        expected=getattr(args,'expected_public_key',None)
        if expected is not None and derived_blob!=public_blob(args.openssl,expected):
            raise ValueError('Private signer and provisioned public key are different: publication refused')
        temporary_manifest=staging/'manifest'
        temporary_signature=staging/'manifest.sig'
        temporary_manifest.write_bytes(manifest_bytes)
        subprocess.run([str(args.openssl),'dgst','-sha256','-sign',str(private),
                        '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',
                        '-out',str(temporary_signature),str(temporary_manifest)],check=True)
        # Verify the exact completed bytes before making either file visible.
        subprocess.run([str(args.openssl),'dgst','-sha256','-verify',str(public_pem),
                        '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',
                        '-signature',str(temporary_signature),str(temporary_manifest)],
                       stdout=subprocess.DEVNULL,check=True)
        # Two filenames cannot be moved as a single atomic transaction. If the
        # second promotion fails, remove the first. Only serve completed pairs.
        try:
            os.replace(temporary_manifest,args.output)
            os.replace(temporary_signature,signature)
        except BaseException:
            args.output.unlink(missing_ok=True)
            signature.unlink(missing_ok=True)
            raise
    print(json.dumps({'manifest_sha256':hashlib.sha256(manifest_bytes).hexdigest(),'files':len(names),
                      'payload_bytes':total,'signature_bytes':signature.stat().st_size,
                      'publisher_public_key_sha256':hashlib.sha256(derived_blob).hexdigest(),
                      'authenticode':False}))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='command',required=True)
    provision_cmd=commands.add_parser('provision')
    provision_cmd.add_argument('--public-key',type=Path,required=True)
    provision_cmd.add_argument('--manifest-url',required=True)
    provision_cmd.add_argument('--output',type=Path,required=True)
    publish_cmd=commands.add_parser('publish')
    publish_cmd.add_argument('--runtime',type=Path,required=True)
    publish_cmd.add_argument('--inventory',type=Path,required=True)
    publish_cmd.add_argument('--private-key',type=Path,required=True)
    publish_cmd.add_argument('--expected-public-key',type=Path,help='Public PEM key used for production provisioning; mandatory for stable')
    publish_cmd.add_argument('--base-url',required=True)
    publish_cmd.add_argument('--version',required=True)
    publish_cmd.add_argument('--kind',choices=('portable','installer'),required=True)
    publish_cmd.add_argument('--days',type=int,default=7)
    publish_cmd.add_argument('--output',type=Path,required=True)
    for cmd in (provision_cmd,publish_cmd):
        cmd.add_argument('--openssl',type=Path,required=True)
        cmd.add_argument('--channel',choices=('development','beta','stable'),required=True)
        cmd.add_argument('--sequence',type=int,required=True)
    args=parser.parse_args()
    (provision if args.command=='provision' else publish)(args)


if __name__=='__main__':
    main()
