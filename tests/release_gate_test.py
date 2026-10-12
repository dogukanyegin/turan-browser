"""A version edit cannot bypass unresolved production acceptance blockers."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from release_gate import validate, blockers, REQUIRED_IDS, OPTIONAL_IDS

class ReleaseGateTests(unittest.TestCase):
    def test_incomplete_stable_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix='turan-release-gate-') as temp:
            root=Path(temp)
            data={'version':'1.0.0','major':1,'minor':0,'patch':0,'revision':0,'channel':'stable','stable':True}
            (root/'release.json').write_text(json.dumps(data))
            (root/'release-acceptance.json').write_text(json.dumps({'criteria':[{'id':'extension-window-topology','status':'failed','evidence':'failure.json'}]}))
            with self.assertRaisesRegex(RuntimeError,'extension-window-topology'):validate(root)
            data.update(version='0.2.0-dev',major=0,minor=2,channel='development',stable=False)
            (root/'release.json').write_text(json.dumps(data))
            self.assertEqual(validate(root)['channel'],'development')
            data.update(version='1.0.0',major=1,minor=0)
            (root/'release.json').write_text(json.dumps(data))
            with self.assertRaises(RuntimeError):validate(root)

    def test_stable_prerelease_label_cannot_bypass(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            release={'version':'1.0.0-dev','major':1,'minor':0,'patch':0,'channel':'development','stable':False}
            (root/'release.json').write_text(json.dumps(release))
            (root/'release-acceptance.json').write_text(json.dumps({'criteria':[
                {'id':'extension-window-topology','status':'failed','evidence':'failed run'}]}))
            with self.assertRaisesRegex(RuntimeError, 'extension-window-topology'):
                validate(root)

    def test_optional_scope_does_not_block_stable(self):
        rows=[{'id':identity, 'status':'passed', 'evidence':'validated fixture'} for identity in REQUIRED_IDS]
        rows.extend({'id':identity, 'status':'pending', 'evidence':'', 'required_for_stable':False} for identity in OPTIONAL_IDS)
        self.assertEqual(blockers(rows), [])

    def test_missing_required_row_cannot_bypass_gate(self):
        rows=[{'id':identity,'status':'passed','evidence':'fixture'} for identity in REQUIRED_IDS - {'extension-window-topology'}]
        self.assertIn('extension-window-topology', blockers(rows))

    def test_mandatory_item_cannot_be_relabelled_optional(self):
        rows=[{'id':'extension-window-topology','status':'passed','evidence':'fixture','required_for_stable':False}]
        with self.assertRaisesRegex(RuntimeError, 'downgraded'):
            blockers(rows)

    def test_duplicate_row_refused(self):
        rows=[{'id':'current-engine','status':'passed','evidence':'fixture'}] * 2
        with self.assertRaisesRegex(RuntimeError, 'Duplicate'):
            blockers(rows)

if __name__=='__main__':unittest.main()
