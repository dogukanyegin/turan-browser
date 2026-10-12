# External Tor Browser and separate connectivity work

## Implemented external launcher

The native menu opens an explicit sharing panel, captures the selected page URL
and displays what will be sent. Verify checks the installation without starting
it. Open passes only that HTTP(S) URL to the external application. Credentials,
internal/file/script schemes, controls, non-canonical Unicode, quotes,
backslashes and URLs above 16 KiB are refused. No renderer bridge exists.

Known local Desktop, LocalAppData, ProgramFiles and Downloads Tor Browser folders
and the normal profile's explicit folder preference are candidates, not trusted
installations. The native chooser selects Browser/firefox.exe. No shortcuts,
registry commands or recursive personal-file searches are used. UNC/device,
relative, alternate-stream and reparse-point code paths are refused.

The currently authenticated Windows x64 catalog is Tor Browser 15.0.24.
tor-browser.lock.json records the official origin, size, SHA-256, published
primary OpenPGP fingerprint EF6E286DDA85EA2A4BA7DE684E2C6E8793298290 and release
subkey 022DA248432D2A0E0F54E65E316C1FACD62D07D9. The compiled catalog checks all
172 immutable files outside TorBrowser/Data, including executable/DLL/JA/pref
code. Mutable external profile/Data and updater staging are Tor Browser's own
responsibility. Unexpected loadable code outside them is refused. Catalog
identity does not certify user-modified external profile settings/extensions.

The official release notes describe an expired Windows installer certificate;
the package's Firefox executable itself is unsigned. Filename/version strings
are therefore not used as authenticity evidence. Primary references:

- https://blog.torproject.org/new-release-tor-browser-15024/
- https://support.torproject.org/tor-browser/getting-started/verifying-tor-browser/
- https://dist.torproject.org/torbrowser/15.0.24/

Catalog maintenance authenticates the package before a fresh bounded extraction
and uses a dedicated GnuPG home, never the user's keyring. GnuPG/7z are optional
developer tools, not normal build/runtime dependencies:

```powershell
python tools/tor_catalog.py --gpg 'C:/Program Files/Git/usr/bin/gpg.exe'
python tests/tor_browser_cef.py --tor-root '.deps/tor-qa/catalog-extracted-<generated-id>'
```

Other versions and in-place Tor updates fail clearly until the catalog is renewed
from an authenticated current release. Turan does not prevent external updates,
download a browser automatically or fall back to an outdated version. No Tor
binary, profile, logo, GnuPG or extractor is included in Turan packages.

Hashing runs outside CEF's UI thread. Code handles deny write/delete sharing until
CreateProcess returns. The fixed executable receives quoted Windows arguments,
--new-tab and the URL, no shell, inherited handles, profile arguments or environment
changes. Its Browser working directory follows the official installer's
argument-free Firefox shortcut. Turan neither imports its data nor changes the
external profile/preferences. The launched browser naturally manages its own
files and connection. A Windows launch request does not prove Tor connectivity.

These checks guard accidental/untrusted choices and ordinary file tampering,
not every race with an adversarial same-user process able to modify Turan, its
directory ancestors or OS configuration. Independent review remains pending.

Only successful normal launches save the chosen root in a bounded atomic
tor-browser-path.txt record. Private launches save neither URL nor folder choice.
Existing full backups include this file; legacy browser-state formats do not
change. URLs can contain query secrets; sharing is explicit and the panel says so.

## Separate Turan Tor connectivity: not implemented or enabled

The official Windows x64 Expert Bundle currently contains Tor 0.4.9.13 in bundle
15.0.24; the standalone source daemon release is 0.4.9.14:
https://download.torproject.org/tor/. Recheck before selecting a daemon. A future
module must validate detached signatures, pin every dependency and preserve its
licenses. The external QA browser retains its original notices and is not shipped.

Chromium documents remote target-name resolution for SOCKS5 and TCP URL traffic,
not UDP. Its implicit local bypass rules also need an explicit decision; setting
a proxy alone does not prove isolation:
https://chromium.googlesource.com/chromium/src/net/+/HEAD/docs/proxy.md

The proposed module uses a separate ephemeral process/profile, not a normal
context proxy toggle. Establish a verified local SOCKS5 endpoint before loading
documents, forbid DIRECT/PAC/system fallback and implicit local bypasses, disable
extensions and non-proxied WebRTC/STUN/UDP/QUIC/prefetch/DoH/external handlers,
and remain stopped on proxy failure. Daemon readiness, ownership, authenticated
control-port access, crash/kill handling and cleanup need actual tests. Evaluate
packet-level enforcement without disabling CEF sandboxing or making silent
administrator changes. These are design requirements, not implemented guarantees.

An isolated SOCKS fixture must prove remote-name requests, IPv4/IPv6/local targets,
WebSocket routing, no direct fallback on disconnect, no STUN/UDP and isolated
cookies/storage/history. Actual bootstrap, .onion and packet capture then need
separate tests. Fingerprinting/side channels require independent review. None
of these properties follows from the external launcher result. There is no
Turan Tor mode or Tor Browser-equivalence claim in the current build.

## Evidence

Six CTest groups include strict URL/argv round trips, preference validation,
candidate detection and unsigned/missing/network-root refusal. Real CEF controls
check native missing-path refusal and disabled internal-page sharing. Owned-package
QA verifies 172 files, same-size tampering, unexpected DLL refusal, actual normal
and private MozillaWindowClass windows, the URL argument, no profile flags/import
and normal/private persistence. Firefox's Windows launcher can hand off to a
child: tests verify the returned PID or its freshly created child, exact executable
path and creation time before terminating only owned QA processes.
artifacts/tor-browser-full-cef.json records the exact tested Turan DLL.
Bootstrap/routing/leak/anonymity tests are outside this evidence.
