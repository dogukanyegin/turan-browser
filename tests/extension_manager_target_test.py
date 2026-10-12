"""Regression coverage for Chromium extension-manager URL variants and safe diagnostics."""
import unittest
from extension_manager_target import find_extensions_manager, is_extensions_manager, summarize_debug_targets


class ManagerTargetTests(unittest.TestCase):
    def test_chrome_canonical(self):
        self.assertTrue(is_extensions_manager({'type': 'page', 'url': 'chrome://extensions/'}))

    def test_no_trailing_slash(self):
        self.assertTrue(is_extensions_manager({'type': 'page', 'url': 'chrome://extensions'}))

    def test_query_and_fragment(self):
        self.assertTrue(is_extensions_manager({'type': 'page', 'url': 'chrome://extensions/?id=synthetic#foo'}))

    def test_other_chrome_webui_is_rejected(self):
        self.assertFalse(is_extensions_manager({'type': 'page', 'url': 'chrome://settings/'}))

    def test_extension_scheme_is_rejected(self):
        self.assertFalse(is_extensions_manager({'type': 'page', 'url': 'chrome-extension://extensions/'}))

    def test_spoof_and_other_path_rejected(self):
        self.assertFalse(is_extensions_manager({'type': 'page', 'url': 'https://example.invalid/?to=chrome://extensions/'}))
        self.assertFalse(is_extensions_manager({'type': 'page', 'url': 'chrome://extensions/foo'}))
        self.assertFalse(is_extensions_manager({'type': 'page', 'url': 'chrome://extensions.evil/'}))

    def test_wrong_target_type_rejected(self):
        self.assertFalse(is_extensions_manager({'type': 'service_worker', 'url': 'chrome://extensions/'}))
        self.assertFalse(is_extensions_manager({'url': 'chrome://extensions/'}))
        self.assertFalse(is_extensions_manager('bad-row'))

    def test_selector_finds_only_true_page(self):
        rows=[{'type': 'service_worker', 'url': 'chrome://extensions/'}, {'type': 'page', 'url': 'chrome://extensions/?foo=bar'}]
        self.assertIs(find_extensions_manager(rows),rows[1])

    def test_diagnostics_do_not_expose_arbitrary_urls(self):
        rows=[{'type':'page','url':'https://example.invalid/secret?password=redacted'},
              {'type':'page','url':'chrome://extensions/'},
              {'type':'service_worker','url':'chrome-extension://private-id/worker.js'}]
        info=summarize_debug_targets(rows)
        self.assertEqual([item['urlClass'] for item in info], ['other','extensions-manager','extension'])
        self.assertNotIn('password',str(info))
        self.assertNotIn('private-id',str(info))

if __name__=='__main__':
    unittest.main()
