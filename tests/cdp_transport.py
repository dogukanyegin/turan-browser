"""Strict local CDP adapter for disposable CEF QA targets.

Only retry *pre-send* connect failures after fresh target identity discovery.
Never retry protocol errors, commands already sent, or missing/ambiguous targets.
"""
import json
import re
import subprocess
import time
import urllib.request
from pathlib import Path
from urllib.parse import urlsplit

LOCAL_WS = re.compile(r'^ws://127\.0\.0\.1:\d+/devtools/')


def _fresh_target(original):
    url = original.get('webSocketDebuggerUrl', '')
    if not LOCAL_WS.match(url):
        raise RuntimeError('Non-local DevTools URL refused')
    port = urlsplit(url).port
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/json/list', timeout=3) as response:
        rows = json.load(response)
    # A semantic match is required; never use an unrelated extension/page.
    matches = [row for row in rows
               if row.get('type') == original.get('type')
               and row.get('url') == original.get('url')
               and LOCAL_WS.match(row.get('webSocketDebuggerUrl', ''))]
    if len(matches) == 1:
        return matches[0]
    if len(matches) > 1:
        same = [row for row in matches if row.get('id') == original.get('id')]
        if len(same) == 1:
            return same[0]
    return None


def cdp(target, method, params=None, script=None):
    script = Path(script or Path(__file__).with_name('cdp_call.mjs'))
    original = dict(target)
    current = original
    for attempt in range(3):
        ws = current.get('webSocketDebuggerUrl', '')
        if not LOCAL_WS.match(ws):
            raise RuntimeError('Non-local DevTools URL refused')
        try:
            completed = subprocess.run(
                ['node', str(script)], input=json.dumps({'url': ws, 'method': method, 'params': params or {}}),
                text=True, encoding='utf-8', capture_output=True, timeout=36, check=False)
        except subprocess.TimeoutExpired as exc:
            raise RuntimeError('CDP transport process timeout; command outcome unknown, not retried') from exc
        if completed.returncode == 0:
            return json.loads(completed.stdout)
        detail = completed.stderr.strip()[:1200]
        # Retry only when the transport explicitly confirms the command was NOT sent.
        if completed.returncode != 31 or not detail.startswith('CDP_CONNECT_FAILED '):
            raise RuntimeError(f'CDP command failed (no replay): {detail}')
        if attempt == 2:
            break
        time.sleep(0.2)
        refreshed = _fresh_target(original)
        if refreshed is None:
            raise RuntimeError('CDP target disappeared or became ambiguous before send; no other target substituted')
        current = refreshed
    raise RuntimeError('CDP connection unavailable after verified pre-send reconnect attempts')
