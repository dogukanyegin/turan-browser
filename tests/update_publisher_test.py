"""Offline RSA-PSS manifest publication QA with disposable fixture keys.

This does not test WinHTTP, the Windows updater helper, or production signing.
"""
from pathlib import Path
import hashlib
import subprocess
import sys
import tempfile
import unittest
from types import SimpleNamespace
import shutil

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from update_release import publish, provision, public_blob


@unittest.skipUnless(shutil.which('openssl'), 'OpenSSL unavailable; signing fixture NOT TESTED')
class PublisherTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='turan-signing-unit-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.key=self.root/'private.pem'
        self.pub=self.root/'public.pem'
        self.openssl=Path(shutil.which('openssl'))
        subprocess.run([str(self.openssl),'genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072',
                        '-out',str(self.key)],check=True,capture_output=True)
        subprocess.run([str(self.openssl),'pkey','-in',str(self.key),'-pubout','-out',str(self.pub)],
                       check=True,capture_output=True)
        self.runtime=self.root/'runtime';self.runtime.mkdir()
        for name in ('TuranBrowser.dll','TuranBrowser.exe','TuranStartup.exe','TuranUpdateTool.exe','libcef.dll'):
            (self.runtime/name).write_bytes(name.encode())
        self.inventory=self.root/'inventory.txt'
        self.inventory.write_text('TuranBrowser.dll\nTuranBrowser.exe\nTuranStartup.exe\nTuranUpdateTool.exe\nlibcef.dll\n',encoding='utf-8')
        self.out=self.root/'update.manifest'

    def args(self):
        return SimpleNamespace(base_url='https://updates.example.invalid/files/',version='0.3.0.0',
                               sequence=7,days=1,kind='portable',runtime=self.runtime,
                               inventory=self.inventory,output=self.out,private_key=self.key,
                               openssl=self.openssl,channel='development')

    def test_publisher_signature_is_authentic_pss_sha256(self):
        publish(self.args())
        signature=Path(str(self.out)+'.sig')
        self.assertEqual(signature.stat().st_size,384)
        result=subprocess.run([str(self.openssl),'dgst','-sha256','-verify',str(self.pub),
                               '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',
                               '-signature',str(signature),str(self.out)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,result.stderr)
        # Signed bytes must not authenticate after even one character changes.
        self.out.write_bytes(self.out.read_bytes().replace(b'0.3.0.0',b'0.3.0.1'))
        result=subprocess.run([str(self.openssl),'dgst','-sha256','-verify',str(self.pub),
                               '-sigopt','rsa_padding_mode:pss','-sigopt','rsa_pss_saltlen:32',
                               '-signature',str(signature),str(self.out)],capture_output=True)
        self.assertNotEqual(result.returncode,0)

    def test_private_profile_and_case_aliased_paths_rejected(self):
        for files in ('Profile/private.txt\n',
                      'TuranBrowser.exe\nturanbrowser.EXE\nlibcef.dll\n',
                      'TuranBrowser.exe\nresources/CON.txt\nlibcef.dll\n'):
            with self.subTest(files=files):
                self.inventory.write_text(files,encoding='utf-8')
                with self.assertRaises(ValueError):publish(self.args())
                self.assertFalse(self.out.exists())

    def test_mismatched_publisher_key_does_not_leave_unsigned_output(self):
        other=self.root/'other-private.pem'
        other_public=self.root/'other-public.pem'
        subprocess.run([str(self.openssl),'genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:3072',
                        '-out',str(other)],check=True,capture_output=True)
        subprocess.run([str(self.openssl),'pkey','-in',str(other),'-pubout','-out',str(other_public)],
                       check=True,capture_output=True)
        args=self.args()
        args.expected_public_key=other_public
        with self.assertRaisesRegex(ValueError,'different'):
            publish(args)
        self.assertFalse(self.out.exists())
        self.assertFalse(Path(str(self.out)+'.sig').exists())

    def test_stable_needs_provisioned_key_and_refuses_reused_output(self):
        args=self.args();args.channel='stable'
        with self.assertRaisesRegex(ValueError,'provisioned'):
            publish(args)
        self.assertFalse(self.out.exists())
        args.expected_public_key=self.pub
        publish(args)
        with self.assertRaisesRegex(ValueError,'already exists'):
            publish(args)

    def test_public_key_export_contains_no_private_key(self):
        header=self.root/'generated_public.h'
        args=SimpleNamespace(manifest_url='https://updates.example.invalid/channel/update.manifest',
                             sequence=6,openssl=self.openssl,public_key=self.pub,output=header,
                             channel='development')
        provision(args)
        content=header.read_text(encoding='ascii')
        self.assertIn(public_blob(self.openssl,self.pub).hex(),content)
        self.assertNotIn('BEGIN PRIVATE KEY',content)
        self.assertEqual(hashlib.sha256(public_blob(self.openssl,self.pub)).hexdigest(),
                         hashlib.sha256(bytes.fromhex(content.split('public_key_hex[]=')[1].split('"')[1])).hexdigest())


if __name__=='__main__':
    unittest.main()
