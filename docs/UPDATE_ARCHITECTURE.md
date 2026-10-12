# Authenticated Turan runtime updates

The native `src/update` component and CEF-free `TuranUpdateTool.exe` implement
publisher-authenticated update preparation, runtime replacement and interrupted
transaction recovery. The native panel uses explicit user actions. External web
pages have no update API. This development build has **no NorthByte publication
endpoint or publisher key** and therefore cannot check/install production
updates. It does not contain a test trust root. No Authenticode signature is
claimed.

## Trust and publication

The publisher public key, HTTPS manifest address, release channel and monotonic
installed sequence are compiled into the browser and helper through an external
build header. `tools/update_release.py provision` accepts an RSA 3072/4096-bit
public PEM key, exponent 65537, and emits a public-only C++ header. The private
key stays outside source and distribution. `publish` signs exact ASCII/LF
manifest bytes using RSA-PSS/SHA-256 with a 32-byte salt. The corresponding CNG
verifier rejects changed metadata, signatures and other keys.

`TURAN_UPDATE_TRUST_HEADER` names the publisher configuration header.
`TURAN_UPDATE_INVENTORY_HEADER` names a generated header declaring
`turan::update::inventory::paths`, compiled from the explicit CMake runtime file
inventory. Neither is a profile setting. The manifest must contain exactly those
runtime paths. A valid publisher signature does not authorize arbitrary files
below `resources`, profile/vault/Tor state, the uninstaller, `portable.flag`,
absolute paths, directory traversal, device names or alternate data streams.
Inventory changes require a separately validated full installer. Do not provision
a fixture/test key as a production publisher.

The signed format has a schema, four-part numeric version, strictly increasing
release sequence, channel, `portable`/`installer` kind, issued/expiry timestamps,
HTTPS payload base and sorted file inventory. Metadata is limited to 1 MiB,
4096 entries, 2 GiB per file and 4 GiB total. Validity is at most 31 days. Checks
reject expired/not-yet-valid metadata, channel changes, version downgrades,
sequence replays and distribution-kind changes. A signed local receipt preserves
the installed sequence/version across subsequent runs. HTTPS uses Windows
certificate validation, TLS 1.2, no redirects, no cookies or automatic logon,
15-second operation timeouts and a 15-minute streaming budget per file. Payloads
must match both signed SHA-256 and exact lengths. No archive extraction or
downloaded installer execution is involved.

## Replacement and recovery

Preparation creates a fresh random sibling staging directory, bound to the
normalized local application root. New files are created exclusively. UNC/device
paths, reparse points in parent/leaf paths, hardlinked payloads, additional staged
files and insufficient disk space are refused. Read/hash operations use handles
that deny concurrent writes/deletes. Runtime replacement renames the exact
validated file handle rather than trusting an earlier pathname check. Directory
handles prevent parent renames during the operation. This protects against remote
malicious payloads and accidental filesystem redirections; it does not promise
security against an attacker already controlling the Windows account/process.

The user initiates installation. The currently shipped helper is copied and
hash-checked in staging; a downloaded executable is not launched. The helper
verifies the initiating process's full executable path, PID and creation time,
waits for it to exit and requests native user confirmation. Busy runtime files
refuse installation. No elevation is requested.

Before replacing any file the updater flushes a rollback plan containing original
sizes/hashes and a destination-bound transaction journal. It moves old runtime
files into backup and promotes verified new files one at a time. On replacement
or final verification failure it validates the entire rollback state first, then
restores originals. A changed backup/target or redirected path stops recovery
without deleting ambiguous data. Profiles, cookies, saved preferences, password
vaults, Tor profiles, arbitrary saved files, uninstaller and portable marker are
never transaction targets. Successful updates retain verified old binaries and a
signed receipt; failed stages are also retained for diagnostics. Retention cleanup
and uninstall registration/version refresh require further integration.

`Recover(app, policy, error)` restores an uncommitted transaction and is
idempotent. A committed journal is archived after new-runtime verification. The
function should run before profile/CEF initialization. The older direct CEF bootstrap
loads `TuranBrowser.dll` before Turan's own main function. A power interruption
between moving that DLL and promoting its replacement can prevent ordinary
startup from reaching recovery.** The retained external
`.turan-update-<id>/apply-helper.exe --recover --app <absolute application root>`
provides recovery, but a CEF-free preflight launcher or equivalent external
recovery entrypoint must be integrated and tested before this is considered a
fully automatic production updater. This is a Stable acceptance blocker, not a
passed capability.

The native panel cancels preparation when it closes. Cancellation is checked
between network stages/chunks and payload files; a synchronous WinHTTP operation
can take its configured timeout before returning. Workers own their state and
never post to a stale native window. `WaitForUpdateWorkers()` must run after the
CEF message loop and before shutdown/DLL unload: it cancels and joins every owned
worker. Background exceptions produce an error instead of terminating the
process. Preparation cannot apply an update by itself. The native installation
callback executes only on the UI thread. No periodic scheduling is claimed yet.

## Validation

`tests/update-native` is an independent small CMake project. Its C++ test creates
a disposable CNG signing key in memory and verifies genuine RSA-PSS signatures,
known SHA-256 vectors, key/signature/metadata mutation, authenticated unsafe
inventories, immutable inventory boundaries, expiry/channel/package kind,
version/sequence replay, staged SHA-256 tamper, extra files, hardlinks and busy
runtime refusal. It performs real child-process termination at old-file removal,
new-file promotion and receipt stages, then verifies byte-identical restoration
of prior runtime and preservation of synthetic profile/vault/cookie/uninstaller
data. Changed interrupted targets are preserved and recovery stops. Both
portable and installed replacement paths are exercised. The real native panel
has accurate unconfigured status with inactive update actions.

These tests exercise native transaction fixtures, not a production NorthByte
release server, Windows 10 or a complete running CEF upgrade. Real publication,
signing-key custody, key rotation, installer registration refresh, startup
recovery bootstrap, post-update engine health rollback and operational update
retention still need release engineering/acceptance validation. Never use this
document or these fixture results as grounds for a 1.0 Stable label.


## CEF-free launch preflight (engineering integration)

The `TuranStartup.exe` program now checks and recovers an authenticated
transaction journal before starting the CEF bootstrap. It uses the same
`PublisherPolicy` and `Recover` code as the native updater; an unprovisioned
build refuses a pending update rather than trusting any unsigned journal.
`--check-only` performs real preflight without spawning a CEF process, and the
Windows fixture tests invalid journal refusal and preservation of unrelated
user files. Installer shortcuts target this launcher. Direct invocations
of `TuranBrowser.exe` still bypass preflight and are not a certified
fully-enforced entrypoint. This integration is not yet Windows-runtime
validated and does not provision trust keys, refresh installer registration,
or prove an upgraded Chromium engine is healthy.

## Publisher-side deployment preparation

For the explicitly offline updater scope, see [RELEASE_1_0_SCOPE.md](RELEASE_1_0_SCOPE.md). Signed static hosting preparation (without touching a server) and verification commands are in [UPDATE_STATIC_DEPLOYMENT.md](UPDATE_STATIC_DEPLOYMENT.md). The CEF-free `TuranStartup.exe` preflight implementation exists; all new binaries still need Windows verification and the direct `TuranBrowser.exe` path is not an enforced preflight entrypoint.
