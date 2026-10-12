"""Only genuine chrome://extensions page targets satisfy the native UI QA."""
from urllib.parse import urlsplit


def is_extensions_manager(row):
    if not isinstance(row, dict) or row.get('type') != 'page':
        return False
    url = row.get('url')
    if not isinstance(url, str):
        return False
    try:
        parts = urlsplit(url)
    except ValueError:
        return False
    return (parts.scheme == 'chrome' and parts.netloc == 'extensions'
            and parts.path in ('', '/'))


def find_extensions_manager(rows):
    return next((row for row in rows if is_extensions_manager(row)), None)


def summarize_debug_targets(rows):
    """No arbitrary sites, extension IDs, tokens or user URLs in evidence."""
    result = []
    for row in rows[:30]:
        if not isinstance(row, dict):
            continue
        url = row.get('url', '')
        if not isinstance(url, str):
            url = ''
        if url.startswith('chrome://'):
            classification = 'extensions-manager' if is_extensions_manager(row) else 'other-internal'
        elif url.startswith('chrome-extension://'):
            classification = 'extension'
        elif url.startswith('http://127.0.0.1:'):
            classification = 'loopback-fixture'
        else:
            classification = 'other'
        result.append({'type': row.get('type', 'unknown'), 'urlClass': classification})
    return result
