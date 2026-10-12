"""Deterministic regression checks for the metadata recovery observation gap."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
from transient_state_read import read_optional_bytes


class TransientStateReadTests(unittest.TestCase):
    def test_missing_primary_is_retriable_but_not_considered_recovered(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "browser-state.txt"
            self.assertIsNone(read_optional_bytes(path))
            self.assertFalse(read_optional_bytes(path) is not None)

    def test_quarantine_gap_and_restored_content(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "browser-state.txt"
            quarantine = path.with_name("browser-state.txt.rejected-sample")
            damaged = b'S 1 1 0 0 1 0\nB "truncated bookmark'
            expected = b'S 1 1 0 0 1 0\nB "Legacy bookmark" "https://legacy.example"\nH "Legacy history" "https://history.example"\n'
            path.write_bytes(damaged)
            path.rename(quarantine)
            self.assertIsNone(read_optional_bytes(path))
            self.assertEqual(quarantine.read_bytes(), damaged)
            path.write_bytes(expected)
            self.assertEqual(read_optional_bytes(path), expected)
            self.assertEqual(quarantine.read_bytes(), damaged)

    def test_invalid_data_is_not_silently_repaired(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "browser-state.txt"
            path.write_bytes(b"invalid")
            self.assertEqual(read_optional_bytes(path), b"invalid")

    def test_permission_error_propagates(self):
        bogus = Mock()
        bogus.read_bytes.side_effect = PermissionError("read denied")
        with self.assertRaises(PermissionError):
            read_optional_bytes(bogus)


if __name__ == "__main__":
    unittest.main()
