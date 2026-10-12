"""Static guard for native Chromium system-menu -> Turan command IDs.
This is NOT an engine/UI smoke test; Windows integration fixture tests actual menu.
"""
from pathlib import Path
import re
import unittest
ROOT=Path(__file__).resolve().parents[1]

class MenuContractTests(unittest.TestCase):
    def test_command_values_align_with_native_window_dispatch(self):
        ui=(ROOT/'src/ui/engine_window.cpp').read_text()
        for name,value in {'kShield':107,'kExtensions':214,'kDownloads':202,
                           'kHistory':201,'kSettings':204,'kUpdates':216,
                           'kPermissions':203,'kBookmarks':200,'kSavePdf':247,'kPrint':246}.items():
            self.assertRegex(ui,rf'{{{name},{value},L"')
        match=re.findall(r'constexpr UINT (k\w+)=0x([0-9A-Fa-f]+);',ui)
        values=[int(v,16) for _,v in match]
        self.assertEqual(len(values),len(set(values)))
        self.assertTrue(all(v<0xf000 and v&15==0 for v in values))
        self.assertIn('WM_SYSCOMMAND',ui)

if __name__=='__main__':unittest.main()
