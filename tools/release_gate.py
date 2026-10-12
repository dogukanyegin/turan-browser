"""Refuse stable-labelled build/package while mandatory acceptance criteria remain open.

A test being marked passed in this metadata is not sufficient production proof:
Windows, Chromium, extension and installer evidence must be independently checked.
"""
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Required release coverage must not be silently reduced by editing the JSON.
# Password-vault completion and stand-alone Tor routing are explicitly optional.
REQUIRED_IDS = frozenset({
    'current-engine', 'sandbox-baseline', 'history-bookmarks-downloads',
    'persistent-download-history', 'native-save-dialog-and-printer-workflow',
    'session-recovery', 'multi-window-profile-isolation',
    'extension-window-topology', 'mv3-real-extensions-and-management',
    'shield-abp-cosmetics-subscriptions', 'persistent-site-permissions',
    'authenticated-update-rollback', 'network-and-security-test-suite',
    'windows10-and-windows11-package-tests', 'performance-stability-accessibility',
    'no-unresolved-critical-or-high-security-findings',
    'physical-media-screen-sharing-and-notification-delivery',
    'full-profile-backup-and-engine-migration', 'startup-regression-investigation',
    'shield-tested-network-cosmetic-subset',
    'external-official-tor-browser-launch',
    'encrypted-vault-supported-explicit-forms',
    'optional-validated-filter-subscriptions',
    'authenticated-update-transaction-fixtures',
})
OPTIONAL_IDS = frozenset({'password-manager', 'separate-tor-connectivity-isolation'})
# A reviewed scope variant may replace ONLY these two gates, never the actual
# extension/UI/security acceptance. Do not permit arbitrary JSON downgrades.
SCOPE_NAME='win11-x64-offline-updates-v1'
SCOPE_REQUIRED=frozenset({'windows11-x64-package-acceptance','offline-updater-fail-closed'})
SCOPE_REPLACED=frozenset({'windows10-and-windows11-package-tests','authenticated-update-rollback'})
SCOPE_EXPECTED_REQUIRED=(REQUIRED_IDS-SCOPE_REPLACED)|SCOPE_REQUIRED
SCOPE_EXPECTED_OPTIONAL=OPTIONAL_IDS|SCOPE_REPLACED


def scoped_blockers(criteria):
    rows={}
    for row in criteria:
        identity=row.get('id')
        if not isinstance(identity,str) or not identity or identity in rows:
            raise RuntimeError('Duplicate or invalid acceptance criterion identifier')
        if identity not in SCOPE_EXPECTED_REQUIRED | SCOPE_EXPECTED_OPTIONAL:
            raise RuntimeError('Unexpected acceptance criterion: '+identity)
        if row.get('status') not in ('passed','pending','failed'):
            raise RuntimeError('Invalid acceptance status for '+identity)
        if row.get('required_for_stable',True) is not (identity in SCOPE_EXPECTED_REQUIRED):
            raise RuntimeError('Scoped Stable requirements may not be downgraded: '+identity)
        rows[identity]=row
    missing=sorted(SCOPE_EXPECTED_REQUIRED-rows.keys())
    unresolved=sorted(identity for identity in SCOPE_EXPECTED_REQUIRED & rows.keys()
                      if rows[identity]['status']!='passed' or not rows[identity].get('evidence'))
    return missing+unresolved


def validate_scope(root,release):
    if release.get('release_scope')!=SCOPE_NAME:
        raise RuntimeError('Unsupported release scope')
    scope=json.loads((root/'release-scope.json').read_text(encoding='utf-8'))
    expected={
      'schema':1,'profile':SCOPE_NAME,'targetVersion':'1.0.0',
      'supportedPlatforms':['windows11-x64'],
      'productionUpdates':'disabled_until_provisioned',
      'extensionArchitecture':'chromium_normal_tabs_without_reparenting',
      'customBrowserUI':'required_for_release_not_yet_accepted',
    }
    if any(scope.get(k)!=v for k,v in expected.items()):
        raise RuntimeError('The reviewed Windows 11/offline-update scope was modified')
    return scope


def blockers(criteria):
    rows = {}
    for row in criteria:
        identity = row.get('id')
        if not isinstance(identity, str) or not identity or identity in rows:
            raise RuntimeError('Duplicate or invalid acceptance criterion identifier')
        if identity not in REQUIRED_IDS | OPTIONAL_IDS:
            raise RuntimeError('Unexpected acceptance criterion: ' + identity)
        if row.get('status') not in ('passed', 'pending', 'failed'):
            raise RuntimeError('Invalid acceptance status for ' + identity)
        if row.get('required_for_stable', True) is not (identity in REQUIRED_IDS):
            raise RuntimeError('Stable requirements may not be downgraded: ' + identity)
        rows[identity] = row
    missing = sorted(REQUIRED_IDS - rows.keys())
    unresolved = sorted(identity for identity in REQUIRED_IDS & rows.keys()
                        if rows[identity]['status'] != 'passed' or not rows[identity].get('evidence'))
    return missing + unresolved


def validate(root=ROOT):
    release = json.loads((root / 'release.json').read_text(encoding='utf-8'))
    expected = '.'.join(str(release[key]) for key in ('major', 'minor', 'patch'))
    base = release['version'].split('-', 1)[0].split('+', 1)[0]
    if base != expected:
        raise RuntimeError('Version metadata mismatch')
    channel=release.get('channel')
    if channel not in ('development','beta','stable'):
        raise RuntimeError('Unsupported release channel')
    if channel=='beta':
        match=re.fullmatch(r'\d+\.\d+\.\d+-beta\.([1-9]\d*)',release['version'])
        if not match or release.get('stable') is not False or release.get('revision')!=int(match.group(1)):
            raise RuntimeError('Beta version/channel/revision metadata mismatch')
    elif '-beta.' in release['version']:
        raise RuntimeError('Beta version requires beta channel')
    stable = release['stable'] or release['channel'] == 'stable' or base == '1.0.0'
    criteria = json.loads((root / 'release-acceptance.json').read_text(encoding='utf-8'))['criteria']
    if not criteria:
        raise RuntimeError('Missing release acceptance criteria')
    scoped=release.get('release_scope') is not None
    if scoped:validate_scope(root,release)
    blocked = scoped_blockers(criteria) if scoped else blockers(criteria)
    if stable and (blocked or not release['stable'] or release['channel'] != 'stable'):
        raise RuntimeError('Stable release prohibited: ' + ', '.join(blocked) if blocked else 'Stable metadata mismatch')
    return release


if __name__ == '__main__':
    print(json.dumps(validate(), indent=2))
