"""Windows-safe, canonical distribution paths shared by packager and verifier.

Windows filenames are case-insensitive by default, and device names/ADS can
create overwrite or data-exfiltration hazards even in otherwise valid ZIPs.
Only ASCII names used by Turan/CEF's explicit runtime inventory are accepted.
"""
import re

_ALLOWED = re.compile(r'[A-Za-z0-9_.\-/]+\Z', re.ASCII)
_RESERVED = re.compile(r'(?:CON|PRN|AUX|NUL|CLOCK\$|COM[1-9]|LPT[1-9])(?:\..*)?\Z', re.IGNORECASE)


def safe_windows_relative(name):
    if not isinstance(name, str) or not name or len(name) > 240 or not _ALLOWED.fullmatch(name):
        return False
    for part in name.split('/'):
        if not part or part in ('.', '..') or part.endswith('.') or _RESERVED.fullmatch(part):
            return False
    return True


def validate_inventory(names):
    """Reject duplicate paths, case aliases, and files shadowing directories."""
    folded = set()
    all_paths = list(names)
    if not all_paths:
        raise ValueError('Empty runtime inventory')
    for name in all_paths:
        if not safe_windows_relative(name):
            raise ValueError('Unsafe Windows path: ' + repr(name))
        canonical = name.casefold()
        if canonical in folded:
            raise ValueError('Duplicate/case-alias Windows path: ' + name)
        folded.add(canonical)
    for name in all_paths:
        parents = name.casefold().split('/')[:-1]
        for i in range(1, len(parents)+1):
            if '/'.join(parents[:i]) in folded:
                raise ValueError('Runtime file shadows directory: ' + name)
    return all_paths
