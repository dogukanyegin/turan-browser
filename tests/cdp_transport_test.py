"""CEF-free unit tests: pre-send retries cannot mask a failed product assertion."""
import json
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import cdp_transport

TARGET = {'id': 'old', 'type': 'service_worker',
          'url': 'chrome-extension://example/worker.js',
          'webSocketDebuggerUrl': 'ws://127.0.0.1:9222/devtools/page/old'}
NEW = dict(TARGET, id='new', webSocketDebuggerUrl='ws://127.0.0.1:9222/devtools/page/new')


def result(status, stdout='', stderr=''):
    return subprocess.CompletedProcess(['node'], status, stdout, stderr)


class CdpTransportTests(unittest.TestCase):
    @patch.object(cdp_transport.subprocess, 'run', return_value=result(0, '{"value":true}'))
    def test_single_success(self, run):
        self.assertEqual(cdp_transport.cdp(TARGET, 'Runtime.evaluate'), {'value': True})
        self.assertEqual(run.call_count, 1)

    @patch.object(cdp_transport.time, 'sleep')
    @patch.object(cdp_transport, '_fresh_target', return_value=NEW)
    @patch.object(cdp_transport.subprocess, 'run', side_effect=[
        result(31, stderr='CDP_CONNECT_FAILED target restarted'),
        result(0, '{"value":true}')])
    def test_refresh_same_identity_before_send(self, run, refresh, sleep):
        self.assertEqual(cdp_transport.cdp(TARGET, 'Runtime.evaluate'), {'value': True})
        self.assertEqual(run.call_count, 2)
        self.assertIn('/new', json.loads(run.call_args.kwargs['input'])['url'])

    @patch.object(cdp_transport.time, 'sleep')
    @patch.object(cdp_transport, '_fresh_target', return_value=None)
    @patch.object(cdp_transport.subprocess, 'run', return_value=result(31, stderr='CDP_CONNECT_FAILED refused'))
    def test_missing_target_fails(self, run, refresh, sleep):
        with self.assertRaisesRegex(RuntimeError, 'disappeared'):
            cdp_transport.cdp(TARGET, 'Runtime.evaluate')
        self.assertEqual(run.call_count, 1)

    @patch.object(cdp_transport, '_fresh_target')
    @patch.object(cdp_transport.subprocess, 'run', return_value=result(32, stderr='CDP_RESPONSE_CLOSED target ended'))
    def test_no_replay_after_send(self, run, refresh):
        with self.assertRaisesRegex(RuntimeError, 'no replay'):
            cdp_transport.cdp(TARGET, 'Runtime.evaluate')
        self.assertEqual(run.call_count, 1)
        refresh.assert_not_called()

    @patch.object(cdp_transport, '_fresh_target')
    @patch.object(cdp_transport.subprocess, 'run', return_value=result(32, stderr='CDP_PROTOCOL_ERROR forbidden'))
    def test_protocol_error_never_retried(self, run, refresh):
        with self.assertRaisesRegex(RuntimeError, 'no replay'):
            cdp_transport.cdp(TARGET, 'Runtime.evaluate')
        refresh.assert_not_called()

    @patch.object(cdp_transport.subprocess, 'run')
    def test_nonlocal_rejected_before_spawn(self, run):
        with self.assertRaisesRegex(RuntimeError, 'Non-local'):
            cdp_transport.cdp(dict(TARGET, webSocketDebuggerUrl='ws://evil.example:9222/devtools/page/x'), 'Runtime.evaluate')
        run.assert_not_called()

    @patch.object(cdp_transport.urllib.request, 'urlopen')
    def test_target_discovery_requires_exact_extension_url(self, open_url):
        response = open_url.return_value.__enter__.return_value
        response.read.return_value = json.dumps([dict(NEW, url='chrome-extension://other/worker.js')]).encode()
        self.assertIsNone(cdp_transport._fresh_target(TARGET))


if __name__ == '__main__':
    unittest.main()
