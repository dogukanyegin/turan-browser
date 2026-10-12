"""Actual CEF subscription filtering/native controls on owned loopback fixtures.

Cache seeds model a previously consented, validated profile. No official-list
downloads or test-only CEF network bypass are required. Production cache SHA,
parser, scheduler, immutable policy publication and native controls are used.
"""
import argparse
import contextlib
import ctypes
from ctypes import wintypes
import hashlib
import http.server
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import time
import urllib.request

from ui_smoke import windows, u
from navigation_cef import text, until
from thirdparty_extensions_cef import evaluate, cdp
from process_tree import hold_children, wait_children

ROOT = Path(__file__).resolve().parents[1]
ADS = '[Adblock Plus 2.0]\n! Owned synthetic subscription QA\n||ads.subscription.example^$xhr\n127.0.0.1##.subscription-ad\n||unsupported.subscription.example^$redirect=noopjs\n'
OLD_ADS = '[Adblock Plus 2.0]\n! Owned previous subscription QA\n||previous.subscription.example^$xhr\n127.0.0.1##.subscription-ad\n'
TRACKERS = '[Adblock Plus 2.0]\n! Owned synthetic tracker subscription QA\n||tracker.subscription.example^$xhr\n127.0.0.1##.tracking-pixel\n'
CUSTOM = '||custom.subscription.example^$xhr\n'


def cache(profile, index, body, backup=False):
    stamp = int(time.time()) - (1 if backup else 0)
    value = f'TURAN-FILTER-CACHE-1\n{stamp}\n{hashlib.sha256(body.encode()).hexdigest()}\n{body}'
    (profile / (f'shield-subscription-{index}.cache' + ('.bak' if backup else ''))).write_bytes(value.encode())


@contextlib.contextmanager
def owned_fixture():
    temporary = tempfile.TemporaryDirectory(prefix='turan-subscriptions-cef-')
    try:
        yield Path(temporary.name)
    except BaseException:
        temporary._finalizer.detach()
        raise
    else:
        temporary.cleanup()


def child_texts(hwnd):
    values = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    @callback_type
    def collect(child, unused):
        values.append(text(child))
        return True
    u.EnumChildWindows.argtypes = [wintypes.HWND, callback_type, wintypes.LPARAM]
    u.EnumChildWindows(hwnd, collect, 0)
    return '\n'.join(values)


def main(runtime):
    exe = ROOT / 'out-cef/app/Release/TuranBrowser.exe'
    proof = {'runtime': runtime, 'passed': False,
             'scope': 'Windows 11 actual CEF loopback requests, cosmetics, native subscription controls and restart; synthetic pre-consented cache, no live publisher fetch',
             'dllSha256': hashlib.sha256(exe.with_suffix('.dll').read_bytes()).hexdigest()}
    requests = []
    class Server(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            requests.append({'host': self.headers.get('Host', ''), 'path': self.path})
            if self.path.startswith('/data'):
                data = b'owned fixture network response'
            else:
                data = b'<title>Turan subscription QA</title><div class="subscription-ad">advert</div><div class="tracking-pixel">tracking</div><div class="ordinary">ordinary</div>'
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.send_header('Cache-Control', 'no-store')
            self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            try:
                self.wfile.write(data)
            except (BrokenPipeError, ConnectionResetError):
                pass
        def log_message(self, *args):
            pass
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f'http://127.0.0.1:{server.server_port}'
    process = None
    controller = None
    panel = None
    handles = []
    try:
      with owned_fixture() as fixture:
        proof['fixture'] = str(fixture)
        with socket.socket() as available:
            available.bind(('127.0.0.1', 0))
            debug_port = available.getsockname()[1]

        def start(profile, name):
            nonlocal process, controller, panel
            address = base + '/' + name
            arguments = [str(exe), '--profile=' + str(profile), '--url=' + address,
                         '--remote-debugging-port=' + str(debug_port),
                         '--host-resolver-rules=MAP *.subscription.example 127.0.0.1', '--no-proxy-server']
            if runtime != 'alloy':
                arguments += ['--turan-runtime=' + runtime]
            process = subprocess.Popen(arguments, cwd=exe.parent)
            controller = until(lambda: windows(process.pid, 'TuranWindow'))[0]
            def targets():
                try:
                    return json.load(urllib.request.urlopen(f'http://127.0.0.1:{debug_port}/json/list', timeout=2))
                except OSError:
                    return []
            page = until(lambda: next((p for p in targets() if p['type'] == 'page' and p['url'] == address), None))
            until(lambda: evaluate(page, 'document.readyState') == 'complete')
            # The manager's cache parsing/publication starts asynchronously at
            # native creation; avoid issuing its menu command before completion.
            time.sleep(1.2)
            u.PostMessageW(controller, 0x111, 215, 0)
            panel = until(lambda: windows(process.pid, 'TuranFilterSubscriptionsPanel'))[0]
            until(lambda: 'Validated subscription policy is active' in text(u.GetDlgItem(panel, 4118)), timeout=25)
            proof['engine'] = cdp(page, 'Browser.getVersion')
            return page

        def stop():
            nonlocal process, controller, panel, handles
            handles = hold_children(process.pid)
            u.PostMessageW(controller, 0x10, 0, 0)
            assert process.wait(timeout=20) == 0
            wait_children(handles)
            handles = []
            process = None
            controller = None
            panel = None

        def fetch(page, host, path):
            address = f'http://{host}:{server.server_port}{path}'
            return evaluate(page, f'fetch({json.dumps(address)},{{cache:"no-store"}}).then(r=>r.text()).then(()=>true).catch(()=>false)')

        def reload(page):
            cdp(page, 'Page.reload')
            until(lambda: evaluate(page, 'document.readyState') == 'complete')
            time.sleep(.3)

        def hidden(page, selector):
            return evaluate(page, 'getComputedStyle(document.querySelector(' + json.dumps(selector) + ')).display') == 'none'

        # Cached filters are not active, and do not initiate updates, before
        # explicit native consent. Use a separate fresh default-off profile.
        fresh = fixture / 'default-off'
        fresh.mkdir()
        cache(fresh, 0, ADS)
        cache(fresh, 1, TRACKERS)
        seeded = (fresh / 'shield-subscription-0.cache').read_bytes()
        page = start(fresh, 'default-off')
        assert u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF0, 0, 0) == 0
        assert not u.IsWindowEnabled(u.GetDlgItem(panel, 4113))
        assert 'Unsupported rules skipped: 1' in text(u.GetDlgItem(panel, 4118))
        assert fetch(page, 'ads.subscription.example', '/data-default-off')
        assert not hidden(page, '.subscription-ad') and not hidden(page, '.tracking-pixel')
        assert not (fresh / 'shield-subscriptions.state').exists()
        assert (fresh / 'shield-subscription-0.cache').read_bytes() == seeded
        proof['cachedListsDefaultOffWithoutConsent'] = True
        stop()

        opted = fixture / 'pre-consented'
        opted.mkdir()
        (opted / 'browser-state.txt').write_text('S 1 1 0 0 1 0\nB "Preserved legacy bookmark" "https://legacy.example/"\n', encoding='utf-8')
        (opted / 'shield-subscriptions.state').write_bytes(b'TURAN-SUBSCRIPTIONS-1\n1\n1\n')
        (opted / 'shield-custom.txt').write_text(CUSTOM, encoding='utf-8')
        cache(opted, 0, ADS)
        cache(opted, 0, OLD_ADS, True)
        cache(opted, 1, TRACKERS)
        page = start(opted, 'enabled')
        reload(page)
        until(lambda: hidden(page, '.subscription-ad') and hidden(page, '.tracking-pixel'))
        assert not hidden(page, '.ordinary')
        assert not fetch(page, 'ads.subscription.example', '/data-ad-blocked')
        assert not fetch(page, 'tracker.subscription.example', '/data-tracker-blocked')
        assert not fetch(page, 'custom.subscription.example', '/data-custom-blocked')
        assert fetch(page, 'control.subscription.example', '/data-negative-control')
        assert fetch(page, 'unsupported.subscription.example', '/data-unsupported-rule-negative')
        observed = [row['path'] for row in requests]
        assert all(path not in observed for path in ('/data-ad-blocked', '/data-tracker-blocked', '/data-custom-blocked'))
        assert '/data-negative-control' in observed and '/data-unsupported-rule-negative' in observed
        proof['actualBlockedRequestsAndServerNegativeControls'] = True
        proof['actualCosmeticsAfterReloadAndOrdinaryNegative'] = True
        proof['unsupportedConstraintNotRelaxed'] = True
        proof['nativeListDetails'] = text(u.GetDlgItem(panel, 4118))

        u.PostMessageW(controller, 0x111, 107, 0)
        shield = until(lambda: next((h for h in windows(process.pid, 'TuranPanel') if u.GetDlgItem(h, 80)), None))
        counts = text(u.GetDlgItem(shield, 80))
        assert 'Ads: 1' in counts and 'Trackers: 1' in counts and 'Custom: 1' in counts, counts
        proof['actualShieldCounters'] = counts
        u.PostMessageW(shield, 0x10, 0, 0)

        # Rollback is real cache/policy replacement, not merely a UI status.
        assert u.IsWindowEnabled(u.GetDlgItem(panel, 4114))
        u.SendMessageW(u.GetDlgItem(panel, 4114), 0xF5, 0, 0)
        until(lambda: OLD_ADS.encode() in (opted / 'shield-subscription-0.cache').read_bytes())
        until(lambda: fetch(page, 'ads.subscription.example', '/data-after-rollback'))
        assert not fetch(page, 'previous.subscription.example', '/data-previous-list-blocked')
        assert not any(row['path'] == '/data-previous-list-blocked' for row in requests)
        proof['nativeRollbackReplacesActualPolicy'] = True

        assert u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF0, 0, 0) == 1
        u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF5, 0, 0)
        until(lambda: (opted / 'shield-subscriptions.state').read_text() == 'TURAN-SUBSCRIPTIONS-1\n0\n1\n')
        until(lambda: fetch(page, 'previous.subscription.example', '/data-after-ad-disable'))
        assert not fetch(page, 'tracker.subscription.example', '/data-tracker-still-blocked')
        assert not fetch(page, 'custom.subscription.example', '/data-custom-still-blocked')
        reload(page)
        assert not hidden(page, '.subscription-ad') and hidden(page, '.tracking-pixel')
        proof['nativeDisableRemovesListKeepsOtherListsAndCustom'] = True

        u.SendMessageW(u.GetDlgItem(panel, 4111), 0x14E, 1, 0)  # CB_SETCURSEL
        u.SendMessageW(panel, 0x111, 4111 | (1 << 16), 0)  # CBN_SELCHANGE
        assert u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF0, 0, 0) == 1
        u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF5, 0, 0)
        until(lambda: (opted / 'shield-subscriptions.state').read_text() == 'TURAN-SUBSCRIPTIONS-1\n0\n0\n')
        until(lambda: fetch(page, 'tracker.subscription.example', '/data-after-tracker-disable'))
        reload(page)
        assert not hidden(page, '.subscription-ad') and not hidden(page, '.tracking-pixel')
        assert not fetch(page, 'custom.subscription.example', '/data-custom-only-blocked')
        proof['disableAllPreservesCustomAndReloadClearsCosmetics'] = True

        # A native enable attempt requires explicit publisher/transport consent.
        # Decline it in this owned QA profile: no real updater request is started.
        consent_before = (opted / 'shield-subscriptions.state').read_bytes()
        u.PostMessageW(u.GetDlgItem(panel, 4112), 0xF5, 0, 0)
        consent = until(lambda: windows(process.pid, '#32770'))[0]
        notice = child_texts(consent)
        assert 'https://easylist.to/easylist/easyprivacy.txt' in notice
        assert 'outside the browser' in notice and 'direct DNS' in notice and 'cookies' in notice
        proof['nativeConsentNotice'] = notice
        u.PostMessageW(consent, 0x111, 7, 0)  # IDNO
        until(lambda: not windows(process.pid, '#32770'))
        assert (opted / 'shield-subscriptions.state').read_bytes() == consent_before
        assert u.SendMessageW(u.GetDlgItem(panel, 4112), 0xF0, 0, 0) == 0
        assert fetch(page, 'tracker.subscription.example', '/data-after-consent-decline')
        proof['nativeConsentDeclinePreservesDisabledState'] = True

        # No publisher key/endpoint is configured: real update controls must be
        # disabled and explain that authenticated distribution is not provisioned.
        u.PostMessageW(controller, 0x111, 216, 0)
        updater = until(lambda: windows(process.pid, 'TuranSecureUpdateWindow'))[0]
        assert all(not u.IsWindowEnabled(u.GetDlgItem(updater, id)) for id in (301, 302, 303))
        update_notice = text(u.GetDlgItem(updater, 305))
        assert 'not been provisioned' in update_notice and 'Unauthenticated' in update_notice
        proof['unconfiguredUpdaterFailsClosed'] = update_notice
        u.PostMessageW(updater, 0x10, 0, 0)
        stop()

        page = start(opted, 'restart-disabled')
        assert fetch(page, 'previous.subscription.example', '/data-restart-ad-allowed')
        assert fetch(page, 'tracker.subscription.example', '/data-restart-tracker-allowed')
        assert not fetch(page, 'custom.subscription.example', '/data-restart-custom-blocked')
        assert not hidden(page, '.subscription-ad') and not hidden(page, '.tracking-pixel')
        assert 'Preserved legacy bookmark' in (opted / 'browser-state.txt').read_text()
        proof['restartConsentAndLegacyDataPreserved'] = True
        stop()
        proof['requests'] = requests
        proof['passed'] = True
    except BaseException as error:
        proof['failure'] = str(error)
        proof['requests'] = requests
        if panel:
            proof['panelAtFailure'] = text(u.GetDlgItem(panel, 4118))
        raise
    finally:
        if process and process.poll() is None:
            handles = handles or hold_children(process.pid)
            subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'], capture_output=True)
            process.wait(timeout=20)
        if handles:
            wait_children(handles)
        server.shutdown()
        server.server_close()
        (ROOT / 'artifacts' / f'subscriptions-cef-{runtime}.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(proof, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime', choices=('alloy', 'chrome-integrated'), default='alloy')
    main(parser.parse_args().runtime)
