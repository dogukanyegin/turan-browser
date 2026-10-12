from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from release_evidence import evaluate, REQUIRED_AUTOMATED, REQUIRED_DEVICE

class EvidenceTests(unittest.TestCase):
    def fixture(self,name):
        return {'status':'passed_limited_scope','machine':{'release':name,'workstation':True,'architecture':'AMD64'},
                'sha256':{'TuranBrowser.dll':'abcd'},
                'tests':{key:{'passed':True} for key in REQUIRED_AUTOMATED}}
    def device(self):
        return {'dllSha256':'abcd','checks':{key:{'result':'passed'} for key in REQUIRED_DEVICE}}
    def test_missing_os_ambiguous_binary_blocks(self):
        result=evaluate(None,self.fixture('Windows 11'))
        self.assertEqual(result['status'],'BLOCKED')
        self.assertTrue(any('Windows 10' in x for x in result['findings']))
    def test_other_binary_blocked(self):
        w10=self.fixture('Windows 10');w11=self.fixture('Windows 11')
        w10['sha256']['TuranBrowser.dll']='other'
        result=evaluate(w10,w11,self.device(),self.device())
        self.assertTrue(any('DIFFERENT' in x for x in result['findings']))
    def test_even_full_test_fixtures_do_not_self_certify_product(self):
        result=evaluate(self.fixture('Windows 10'),self.fixture('Windows 11'),self.device(),self.device())
        self.assertEqual(result['status'],'BLOCKED')
        self.assertIn('Production update server', ' '.join(result['findings']))
    def test_not_tested_is_never_passed(self):
        d=self.device();d['checks']['physical_print']['result']='not_tested'
        result=evaluate(self.fixture('Windows 10'),self.fixture('Windows 11'),d,self.device())
        self.assertIn('physical_print',' '.join(result['findings']))

if __name__=='__main__':unittest.main()
