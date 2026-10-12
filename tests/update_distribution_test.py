"""Offline static HTTPS staging checks, using synthetic disposable RSA keys."""
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from update_release import publish
from update_distribution import assemble,verify

@unittest.skipUnless(shutil.which('openssl'),'OpenSSL unavailable; publication checks NOT TESTED')
class StaticPublisherTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='turan-publication-')
        self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name);self.openssl=Path(shutil.which('openssl'))
        self.secret=self.base/'secret.pem';self.public=self.base/'public.pem'
        subprocess.run([str(self.openssl),'genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072','-out',str(self.secret)],capture_output=True,check=True)
        subprocess.run([str(self.openssl),'pkey','-in',str(self.secret),'-pubout','-out',str(self.public)],capture_output=True,check=True)
        self.runtime=self.base/'runtime';self.runtime.mkdir()
        for name in ['TuranBrowser.exe','TuranBrowser.dll','TuranStartup.exe','TuranUpdateTool.exe','libcef.dll']:
            (self.runtime/name).write_bytes(('fixture:'+name).encode())
        self.inventory=self.base/'inventory.txt'
        self.inventory.write_text('\n'.join(p.name for p in self.runtime.iterdir())+'\n')
        self.manifest=self.base/'release.manifest';self.destination=self.base/'deployment'
        publish(SimpleNamespace(runtime=self.runtime,inventory=self.inventory,private_key=self.secret,
              expected_public_key=self.public,base_url='https://updates.example.invalid/files/',
              version='1.0.1.0',kind='portable',days=7,output=self.manifest,
              openssl=self.openssl,channel='stable',sequence=10))

    def test_stage_and_verify_offline_only(self):
        result=assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)
        self.assertEqual(result['status'],'NOT_DEPLOYED')
        self.assertEqual(verify(self.destination,self.public,self.openssl)['files'],5)
        self.assertFalse((self.destination/'secret.pem').exists())
        with self.assertRaisesRegex(ValueError,'exists'):assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)

    def test_changed_runtime_refused_without_partial_output(self):
        (self.runtime/'TuranBrowser.dll').write_bytes(b'tampered')
        with self.assertRaisesRegex(ValueError,'does not match'):assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)
        self.assertFalse(self.destination.exists())

    def test_changed_signed_manifest_refused(self):
        self.manifest.write_bytes(self.manifest.read_bytes().replace(b'1.0.1.0',b'1.0.2.0'))
        with self.assertRaisesRegex(ValueError,'signature verification failed'):assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)
        self.assertFalse(self.destination.exists())

    def test_copied_package_modification_detected(self):
        assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)
        (self.destination/'payload'/'TuranBrowser.exe').write_bytes(b'evil')
        with self.assertRaisesRegex(ValueError,'mismatch'):verify(self.destination,self.public,self.openssl)

    def test_extra_payload_refused(self):
        assemble(self.runtime,self.manifest,self.public,self.openssl,self.destination)
        (self.destination/'payload'/'extra.dll').write_bytes(b'extra')
        with self.assertRaisesRegex(ValueError,'Extra/missing'):verify(self.destination,self.public,self.openssl)

if __name__=='__main__':unittest.main()
