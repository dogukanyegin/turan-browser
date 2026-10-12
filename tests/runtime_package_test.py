"""Distribution allowlist excludes arbitrary runtime neighbours and profile backups."""
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_cef import runtime_files
class RuntimePackageTests(unittest.TestCase):
    def test_only_declared_files_and_no_profile_neighbours(self):
        with tempfile.TemporaryDirectory(prefix='turan-runtime-allowlist-') as temp:
            root=Path(temp);app=root/'app';app.mkdir();inventory=root/'runtime-files.txt'
            (app/'TuranBrowser.exe').write_bytes(b'fixture executable')
            (app/'resources').mkdir();(app/'resources/start.html').write_text('fixture asset')
            inventory.write_text('TuranBrowser.exe\nresources/start.html\n')
            for name in ('Profile','Profile.turan-backups/snapshot/profile','Profile.before-restore-123',
                         'Profile.restore-staging-123','other-private-folder','resources/private-profile'):
                folder=app/name;folder.mkdir(parents=True);(folder/'browser-state.txt').write_text('PRIVATE SENTINEL')
            (app/'private.txt').write_text('PRIVATE SENTINEL')
            names={p.relative_to(app).as_posix() for p in runtime_files(app,inventory)}
            self.assertEqual(names,{'TuranBrowser.exe','resources/start.html'})
            inventory.write_text('../outside.txt\n')
            with self.assertRaises(RuntimeError):runtime_files(app,inventory)
            inventory.write_text('missing.dll\n')
            with self.assertRaises(RuntimeError):runtime_files(app,inventory)
    def test_startup_is_declared_in_release_inventory_and_shortcut(self):
        root=Path(__file__).resolve().parents[1]
        cmake=(root/'CMakeLists.txt').read_text(encoding='utf-8')
        installer=(root/'packaging/windows/installer.nsi').read_text(encoding='utf-8')
        publisher=(root/'tools/update_release.py').read_text(encoding='utf-8')
        self.assertIn('TuranStartup.exe TuranProfileTool.exe',cmake)
        self.assertIn(r'"$INSTDIR\TuranStartup.exe"',installer)
        self.assertIn("'turanstartup.exe'",publisher)

    def test_case_alias_rejected_even_when_both_files_are_absent(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);app=root/'app';app.mkdir();inventory=root/'names.txt'
            inventory.write_text('TuranBrowser.dll\nturanbrowser.dll\n')
            with self.assertRaisesRegex(RuntimeError,'case-alias'):
                runtime_files(app,inventory,required=False)

    def test_executable_script_interpolation_names_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);app=root/'app';app.mkdir();inventory=root/'names.txt'
            inventory.write_text('resources/x$\n')
            with self.assertRaisesRegex(RuntimeError,'Unsafe Windows path'):
                runtime_files(app,inventory,required=False)

    def test_symlink_ancestor_rejected(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t);app=root/'app';app.mkdir();external=root/'other';external.mkdir()
            (external/'secret.txt').write_text('never bundle')
            (app/'resources').symlink_to(external,target_is_directory=True)
            inventory=root/'names.txt';inventory.write_text('resources/secret.txt\n')
            with self.assertRaisesRegex(RuntimeError,'Redirected runtime path'):
                runtime_files(app,inventory)

if __name__=='__main__':unittest.main()
