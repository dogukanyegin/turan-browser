"""0.9.0 Beta release guard: do not regress to Development/Stable identity."""
import json
import pathlib
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from release_gate import validate

class BetaReleaseTests(unittest.TestCase):
    def test_beta_metadata(self):
        info=validate(ROOT)
        self.assertEqual(info['version'],'0.9.0-beta.1')
        self.assertEqual(info['channel'],'beta')
        self.assertFalse(info['stable'])
        self.assertEqual(info['revision'],1)

    def test_beta_invariant_rejects_stable_or_revision_change(self):
        with tempfile.TemporaryDirectory(prefix='turan-beta-release-') as tmp:
            root=pathlib.Path(tmp)
            for name in ('release.json','release-scope.json','release-acceptance.json'):
                (root/name).write_bytes((ROOT/name).read_bytes())
            data=json.loads((root/'release.json').read_text(encoding='utf-8'))
            for edit in ({'stable':True},{'revision':0},{'channel':'development'}):
                bad={**data,**edit}
                (root/'release.json').write_text(json.dumps(bad),encoding='utf-8')
                with self.assertRaisesRegex(RuntimeError,'Beta'):
                    validate(root)

    def test_beta_installation_identity_is_separate(self):
        self.assertIn('TuranBrowserBeta',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))
        for name in ('tools/build_cef.py','tests/package_smoke.py'):
            contents=(ROOT/name).read_text(encoding='utf-8')
            self.assertIn("'beta':'TuranBrowserBeta'",contents)
            self.assertIn("'beta':'Turan Browser Beta'",contents)

if __name__=='__main__': unittest.main()
