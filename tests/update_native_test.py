"""Native updater publisher-tool/TLS/helper negative integration tests.

Disposable keys/certificates are generated in an owned artifacts directory;
no certificate is installed into the Windows trust store, no user profile is
read and no test trust root is copied into runtime/source packages.
"""
import argparse
import hashlib
import http.server
import importlib.util
import json
from pathlib import Path
import socket
import ssl
import subprocess
import threading
import time
import uuid

ROOT=Path(__file__).resolve().parents[1]


def run(argv,expected=0):
    result=subprocess.run([str(x) for x in argv],cwd=ROOT,text=True,encoding='utf-8',errors='replace',capture_output=True,timeout=60)
    if result.returncode!=expected:
        raise AssertionError(f'{argv[0]} exit {result.returncode}, expected {expected}: {result.stdout}\n{result.stderr}')
    return result.stdout


def snapshot(root):
    return {p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,default=ROOT/'artifacts/update-native-build/Release/turan_update_tests.exe')
    parser.add_argument('--helper',type=Path,default=ROOT/'artifacts/update-native-build/Release/TuranUpdateTool.exe')
    parser.add_argument('--openssl',type=Path,default=Path(r'C:\Program Files\Git\usr\bin\openssl.exe'))
    args=parser.parse_args()
    work=ROOT/'artifacts'/('update-native-integration-'+uuid.uuid4().hex)
    work.mkdir(parents=True)
    private_key,public_key=work/'fixture.key',work/'fixture-public.pem'
    run([args.openssl,'genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',private_key])
    run([args.openssl,'pkey','-in',private_key,'-pubout','-out',public_key])
    spec=importlib.util.spec_from_file_location('update_release',ROOT/'tools/update_release.py')
    publisher=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publisher)
    blob=publisher.public_blob(args.openssl,public_key)
    (work/'public.blob').write_bytes(blob)
    runtime=work/'runtime'
    (runtime/'resources').mkdir(parents=True)
    names=['TuranBrowser.dll','TuranBrowser.exe','libcef.dll','resources/test.txt']
    for name in names:
        (runtime/name).write_bytes(('Synthetic signed native update payload: '+name).encode())
    inventory=work/'inventory.txt'
    inventory.write_text('\n'.join(names)+'\n',encoding='ascii')
    manifest=work/'portable.manifest'
    run(['python',ROOT/'tools/update_release.py','publish','--runtime',runtime,'--inventory',inventory,'--private-key',private_key,
         '--base-url','https://updates.example.invalid/releases/0.3.0/','--version','0.3.0.0','--kind','portable','--days','1',
         '--output',manifest,'--openssl',args.openssl,'--channel','development','--sequence','2'])
    signer=run([args.native,'--authenticate',work/'public.blob',manifest,Path(str(manifest)+'.sig')])
    header=work/'publisher-config.h'
    provision=run(['python',ROOT/'tools/update_release.py','provision','--public-key',public_key,
                   '--manifest-url','https://updates.example.invalid/development/portable.manifest','--output',header,
                   '--openssl',args.openssl,'--channel','development','--sequence','1'])
    assert blob.hex() in header.read_text()
    # Malicious relative paths are rejected by the actual release producer.
    inventory.write_text('../Profile/Cookies\n',encoding='ascii')
    run(['python',ROOT/'tools/update_release.py','publish','--runtime',runtime,'--inventory',inventory,'--private-key',private_key,
         '--base-url','https://updates.example.invalid/releases/0.3.0/','--version','0.3.0.0','--kind','portable',
         '--output',work/'rejected.manifest','--openssl',args.openssl,'--channel','development','--sequence','2'],expected=1)
    assert not (work/'rejected.manifest').exists()
    app=work/'unconfigured-app'
    app.mkdir()
    (app/'Profile').mkdir()
    (app/'Profile/vault.bin').write_bytes(b'fictional encrypted vault data')
    before=snapshot(app)
    run([args.helper,'--apply','--app',app,'--stage',work],expected=3)
    assert snapshot(app)==before
    run([args.helper,'--unsigned-update','--app',app],expected=2)
    assert snapshot(app)==before
    plain_http=run([args.native,'--download','http://127.0.0.1/manifest','ignored'])
    # TLS service on the standard allowed HTTPS port. No trust-store changes.
    cert,key=work/'tls-cert.pem',work/'tls-fixture.key'
    run([args.openssl,'req','-x509','-newkey','rsa:3072','-nodes','-keyout',key,'-out',cert,'-days','1',
         '-subj','/CN=127.0.0.1','-addext','subjectAltName=IP:127.0.0.1'])
    hits=[]
    tcp=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            hits.append(self.path)
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'UNTRUSTED TLS PAYLOAD')
        def log_message(self,*_):
            pass
    class Server(http.server.HTTPServer):
        def get_request(self):
            connection,address=self.socket.accept()
            tcp.append(address)
            try:
                return context.wrap_socket(connection,server_side=True),address
            except Exception:
                connection.close()
                raise
    context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert,key)
    server=Server(('127.0.0.1',443),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        rejected_tls=run([args.native,'--download','https://127.0.0.1/manifest','ignored'])
        assert tcp and not hits, 'TLS handshake attempted and HTTP request blocked before document GET'
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
    report={
        'passed':True,'native_sha256':hashlib.sha256(args.native.read_bytes()).hexdigest(),
        'helper_sha256':hashlib.sha256(args.helper.read_bytes()).hexdigest(),
        'openssl_to_cng_real_pss':signer.strip(),'public_header_matches_key':True,
        'malicious_producer_inventory_rejected':True,'unconfigured_helper_exit':3,
        'no_unsigned_cli_override':True,'profile_fixture_unchanged':True,
        'http_rejected':plain_http.strip(),'untrusted_tls_rejected':rejected_tls.strip(),
        'tls_tcp_connections':len(tcp),'tls_document_gets':len(hits),
        'windows_trust_store_modified':False,'authenticode':False,'fixtures':str(work)
    }
    (ROOT/'artifacts/update-native-integration.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
