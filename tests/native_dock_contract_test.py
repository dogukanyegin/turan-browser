"""Static invariants; runtime ownership and command routing use integrated_chrome_cef.py."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]

class NativeDockContract(unittest.TestCase):
    def test_no_engine_reparenting_or_focus_override(self):
        text=(ROOT/'src/ui/engine_window.cpp').read_text()
        self.assertIn('TuranEngineTools',text)
        self.assertIn('WS_EX_NOACTIVATE',text)
        self.assertIn('GW_OWNER',text)
        self.assertIn('WM_NCDESTROY',text)
        self.assertIn('SWP_NOACTIVATE',text)
        self.assertNotIn('SetParent(',text)
        self.assertNotIn('SetActiveWindow(',text)
        self.assertNotIn('SetForegroundWindow(',text)

    def test_native_controls_call_existing_dispatch(self):
        text=(ROOT/'src/ui/engine_window.cpp').read_text()
        for value in (107,214,200,201,202,203,204,216,247,246,108):
            self.assertRegex(text,rf'\{{0,{value},L"')
        self.assertIn('self->command_(frame,command)',text)
        self.assertIn('self->ToggleDock(frame)',text)

if __name__=='__main__':unittest.main()
