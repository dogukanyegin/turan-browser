"""Build orchestration can report missing historical baseline without inventing metrics."""
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_cef import performance_report

class BuildSourceTests(unittest.TestCase):
    def test_missing_baseline_is_not_reported_as_performance_success(self):
        body=performance_report({'status':'not_tested','reason':'no immutable old archive'})
        self.assertIn('not verified',body)
        self.assertIn('not supplied',body)
        self.assertNotIn('| Immutable 0.1.0',body)

if __name__=='__main__':unittest.main()
