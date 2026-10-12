"""Ensure only Windows-11/offline-update scope can replace two old requirements."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from release_gate import (validate,scoped_blockers,SCOPE_EXPECTED_REQUIRED,
                          SCOPE_EXPECTED_OPTIONAL,SCOPE_NAME)

ROOT=Path(__file__).resolve().parents[1]
class ReleaseScopeTests(unittest.TestCase):
    def test_current_dev_metadata_parses(self):
        self.assertEqual(validate(ROOT)['stable'],False)

    def test_required_criteria_preserved_except_explicit_scope(self):
        rows=[{'id':id,'status':'passed','evidence':'fixture'} for id in SCOPE_EXPECTED_REQUIRED]
        rows.extend({'id':id,'status':'pending','evidence':'','required_for_stable':False}
                    for id in SCOPE_EXPECTED_OPTIONAL)
        self.assertEqual(scoped_blockers(rows),[])
        altered=[dict(row) for row in rows]
        next(row for row in altered if row['id']=='extension-window-topology')['required_for_stable']=False
        with self.assertRaisesRegex(RuntimeError,'downgraded'):scoped_blockers(altered)

    def test_release_scope_modification_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            release=json.loads((ROOT/'release.json').read_text());release.update(version='1.0.0',major=1,minor=0,patch=0,stable=True,channel='stable')
            (root/'release.json').write_text(json.dumps(release))
            acceptance=json.loads((ROOT/'release-acceptance.json').read_text())
            (root/'release-acceptance.json').write_text(json.dumps(acceptance))
            scope=json.loads((ROOT/'release-scope.json').read_text());scope['productionUpdates']='unverified_auto_enabled'
            (root/'release-scope.json').write_text(json.dumps(scope))
            with self.assertRaisesRegex(RuntimeError,'reviewed'):validate(root)

    def test_scope_still_rejects_unresolved_stable(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            release=json.loads((ROOT/'release.json').read_text());release.update(version='1.0.0',major=1,minor=0,patch=0,stable=True,channel='stable')
            (root/'release.json').write_text(json.dumps(release))
            (root/'release-acceptance.json').write_text((ROOT/'release-acceptance.json').read_text())
            (root/'release-scope.json').write_text((ROOT/'release-scope.json').read_text())
            with self.assertRaisesRegex(RuntimeError,'extension-window-topology'):validate(root)

if __name__=='__main__':unittest.main()
