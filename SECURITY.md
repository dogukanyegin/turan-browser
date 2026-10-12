# Security status and release controls

Turan 1.0 Stable is not yet released. Current engineering artifacts are unsigned
and have unresolved production acceptance requirements; see TEST_REPORT.md.

The CEF bootstrap sandbox is required and settings.no_sandbox remains false.
Chromium is supplied by the pinned prebuilt CEF distribution. Downloads are
checked against the lock before extraction; archive traversal and symbolic links
are rejected. The local real-engine TLS fixture verifies default rejection of a fresh untrusted self-signed certificate and subsequent navigation recovery. This does not cover every TLS failure mode.

Normal and private contexts are separate. Session journals persist only normal
HTTP(S) tabs, validate schema/size/schemes and fall back to a previously validated
backup. Explicit private downloads and bookmarks are retained by user choice.
Private download metadata and consent decisions are not written to the normal
profile. Metadata parsing rejects malformed/oversize records; valid backups are
not replaced by damaged bytes. Writes use exclusive temporary files, flushing
and replacement, and refuse reparse-point leaf files. This is not a defence
against an attacker controlling the same Windows account and profile directory.
No privileged JavaScript management bridge is exposed to external websites.
Complete profile operations refuse live Chromium locks, redirections, unsafe
relative names, oversized inventories and SHA-256 mismatches. A sibling lease
remains held during restore directory moves. The closed-profile helper inherits
only a process handle and readiness event, validates the parent executable and
waits for successful process exit; it does not load the browser engine. The
current profile is retained in a separate directory before restore commits.
Process-crash recovery and real engine 144 -> 154 -> restored 144 data retention
are tested on Windows 11. Hash manifests are unsigned; they do not authenticate
the creator or protect against a same-account attacker who can alter both data
and manifest. Use trusted local backups and protected directories.
The attempted engine version is recorded before CEF initialization so a failed
startup cannot leave the profile's downgrade bound at an older successful version.
The real-engine fixture covers a read-only successful marker: CEF cache changes,
the successful marker remains old, and the newer attempt is retained.

The explicit phishing-disable switch has been removed. This is not evidence of
a functioning reputation provider. Malware/phishing protection, production update publication/recovery,
full extension management and broad security testing remain incomplete. Persistent
permission controls pass actual CEF consent/revoke/reset tests with fake media
devices; hardware and screen-sharing tests remain pending. Do not treat the
starter Shield domain list as a malware detector.

DoH is configured through Chromium's global preference manager with readback.
Failure to apply the requested configuration prevents browser-window creation.
The isolated NetLog fixture checks real DoH replies, classic-DNS negative control
and failure without classic fallback when the DoH provider is unavailable.
It does not certify all DNS/IPv6/WebRTC channels or Tor anonymity.

release.json, release-acceptance.json and tools/release_gate.py prevent an
incomplete build from being packaged with the Stable label. Verified builds
record source and binary hashes; packaging rejects changed sources/binaries.
Production update signing keys and Authenticode certificates are not present.
Native RSA-PSS/SHA-256 update verification and interrupted-transaction rollback
are implemented and fixture-tested. Publication defaults to disabled. Recovery
inside the browser DLL cannot repair a missing DLL before the CEF bootstrap
loads it; external preflight recovery and new-engine health rollback are release
blockers. See docs/UPDATE_ARCHITECTURE.md.
The password vault uses current-user DPAPI around authenticated encryption with
master authentication. Form capture/fill is explicitly initiated, exact HTTPS
origin-bound and restricted to supported main-frame login forms. It refuses
unsafe TLS, cross-origin actions, ambiguous forms and stale documents. No
Windows Hello or complete password-form compatibility is claimed. See
docs/PASSWORD_VAULT.md for memory and historical-backup limits.
Never put signing keys or sensitive profiles in source control.
Portable archives, installer inputs and runtime fingerprints use CMake's explicit
runtime-file inventory. Arbitrary neighbouring files, profiles, retained restore
directories and backups are excluded. Packaging never recursively archives the
whole executable directory.

Security reporting and production signing/distribution channels must be assigned
by NorthByte before public release. A repository remote has not been configured.

Microphone/camera consent updates Chromium content settings in the requesting browser context before continuing capture. Private decisions use the off-the-record context; revocation is tested to end an active fake microphone stream. Main and subframe navigations invalidate pending media consent. Physical capture and long WebRTC sessions remain unverified.

Shield filter inputs are bounded and UTF-8 validated. Unsupported options reject
the complete rule instead of dropping constraints. The custom editor rejects
any unsupported/invalid rule and retains the previous immutable engine. RE2
provides bounded regular expressions; backreferences/lookarounds are rejected.
Selectors are data, parsed by Chromium before CSS publication, and may publish
only display:none!important. Scriptlets, CSS declarations, remote resource
redirects and privileged renderer calls are not executed. Page CSP can prevent
cosmetics and is not bypassed. This is not full ABP or malware protection.

HTTP(S)/WS subresource filter input above 64 KiB is blocked with an explicit
Turan-limits reason; it cannot silently bypass the engine. Main documents remain
outside ad blocking. Per-site user exceptions explicitly override this resource
limit. Navigation generations discard delayed events from the previous page.

The external Tor Browser launcher verifies the pinned 15.0.24 code catalog,
rejects altered/unexpected loadable files, validates local non-reparse code paths
and uses an explicit executable with Windows argv quoting. No shell, shortcut or
Turan data import is used. Mutable external profile/Data and updater staging
remain Tor Browser's responsibility. Catalog renewal follows external updates.
Same-user adversarial filesystem races and Tor network/anonymity properties are
not certified. See docs/TOR_ARCHITECTURE.md for the trust boundary and test scope.


### CEF-free startup recovery boundary (engineering patch)

`TuranStartup.exe` verifies interrupted signed updates before loading
`TuranBrowser.dll`; broken or unauthenticated transactions block launch while
leaving profile files untouched. The legacy direct executable and OS-level
file associations can bypass this extra guard, and current engineering builds
have no publisher trust root. Do not treat this as a completed automatic
production recovery chain until Windows acceptance and entrypoint coverage
are verified.
