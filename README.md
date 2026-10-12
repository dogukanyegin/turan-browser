> **Beta release candidate:** This source is labelled 0.9.0-beta.1, Windows 11 x64 only. No production automatic updater is provisioned. Rebuild and verify the resulting Beta binaries before public distribution. This is not 1.0.0 Stable. The integrated Chromium tab/dock route remains opt-in, not the default launch mode.

# Turan Browser

A C++17 / native Win32 browser shell for Windows 10/11 x64, using **prebuilt CEF**.
No Chromium source checkout, depot_tools, GN, Electron, Qt or ATL is needed.
Node.js 22+ is used only for DevTools integration tests; the application does not
require or distribute Node.js.

Production engineering is in progress on `codex/production-engineering`.
Current beta candidate version: **0.9.0-beta.1**, engine CEF 154.0.34 / Chromium
154.0.8037.98. **This is not the requested 1.0 Stable release.** Next-source audit and the remaining required release blockers are documented in
[finalization status](docs/FINALIZATION_STATUS.md). Known production
blockers and real test results are in [TEST_REPORT](TEST_REPORT.md) and
[source audit](docs/SOURCE_AUDIT.md). Original 0.1.0 archives below are preserved.
New builds add an atomic session journal with backup recovery, closed-tab reopen,
pinning, muting, three coloured groups and drag reordering. Session records exclude
private tabs. Right-click a tab for controls; Ctrl+Shift+T reopens a closed tab.
Settings can disable restoration at startup.

## Run

Extract `dist/TuranBrowser-0.9.0-beta.1-windows-x64-portable.zip` and run
`TuranBrowser/TuranBrowser.exe`, keeping its DLLs/resources together. Or use
`dist/TuranBrowser-0.9.0-beta.1-windows-x64-setup.exe` for an isolated per-user Beta installation. The installer
does not change your default browser.

Portable data lives in `Profile/` beside the executable; installed data lives in
`%LOCALAPPDATA%\TuranBrowserBeta\Profile` for Beta builds.
The original 0.1.0 installed profile remains untouched in TuranBrowser/Profile. Private tabs use a separate in-memory CEF
context and are excluded from Turan history. The last private tab releases that
context. Explicitly saved bookmarks and downloaded files still remain.

## Features

- Original Turan logo, custom native toolbar/tab strip, local new tab page.
- Tabs, omnibox with DuckDuckGo search, back/forward/reload/stop and keyboard shortcuts.
- Find in page, zoom/reset, fullscreen, home, print, PDF export and developer tools.
- Persistent bookmarks/history with native management panels.
- Downloads: save dialog, progress, pause/resume/cancel, reveal folder and persistent normal-profile download history.
- Private tabs with separate cookies/storage.
- Complete closed-profile SHA-256 backups, verified restore with the previous
  profile retained separately, and automatic snapshots before engine changes.
- Asynchronous origin-labelled permission prompts and editable profile policies
  for camera, microphone, location, notifications and clipboard. Private decisions
  stay in memory; screen capture always needs a fresh request decision.
- Turan Shield: bounded ABP network rules, scoped CSS cosmetics, native custom-rule
  editor, per-site exceptions, actual per-page request counts/reasons, PSL-aware
  third-party **network cookie** filtering and HTTPS upgrade.
- Optional Cloudflare DoH; restart after changing it.
- Explicit external Tor Browser sharing with authenticated 15.0.24 code identity
  and private URL/preference isolation. Other versions need catalog renewal;
  Turan Tor connectivity remains pending.
- English/Turkish controls and dark/light native toolbar.
- CEF sandbox bootstrap, portable ZIP and NSIS installer/uninstaller with licenses.

The menu (☰) opens bookmarks, history, downloads, permissions and settings.
It also offers **Open this page in Tor Browser**. Choose the official installation
folder and verify/open; only the displayed HTTP(S) URL is passed. The external
browser handles its own profile and Tor connection. See
[Tor architecture and validation limits](docs/TOR_ARCHITECTURE.md).
The star toggles the current bookmark; Shield opens the current page's event and
site-exception panel, with custom rules and privacy settings. Permissions
can be allowed, blocked or reset in the native Site permissions panel. The active
normal site appears with five controls; private consent never changes this profile
policy. Native Chromium permission scopes still apply to the underlying engine.

Profile metadata is validated as a whole. A verified previous state is retained
in browser-state.bak; damaged metadata is preserved under a .rejected-* name when
recovered. With no valid copy the application stops before opening the profile.
This metadata backup does not replace a complete profile backup for engine upgrades.
Use **Back up full profile and close** in the menu. An engine-free utility waits
for the browser process to finish before copying the profile. On engine changes,
startup automatically snapshots the closed profile into its sibling
`Profile.turan-backups/` directory. A newer recorded engine prevents downgrade.
See [backup and restore instructions](INSTALLATION.md). Backups contain sensitive
profile data and are not uploaded; choose a protected local user directory.

| Shortcut | Action |
| --- | --- |
| Ctrl+L | Address bar |
| Ctrl+T / Ctrl+W | New / close tab |
| Ctrl+Shift+N | Private tab |
| Ctrl+D | Bookmark |
| Ctrl+H / Ctrl+J | History / downloads |
| F5 / Escape | Reload / stop loading |
| Ctrl+F | Find in page |
| Ctrl++ / Ctrl+- / Ctrl+0 | Zoom in / out / reset |
| F11 / F12 | Fullscreen / developer tools |
| Ctrl+P | Print |

## Build

Install Visual Studio 2022+ C++ x64 tools and a Windows SDK, CMake 3.22+ and
Python 3.10+. VS-bundled CMake is detected automatically. Release x64 only.

```powershell
python tools/build_cef.py build
python tests/ui_smoke.py
python tools/build_cef.py package
```

`build` downloads the locked **minimal binary** package, compiles only Turan and
the small CEF API wrapper and pinned native RE2/Abseil libraries, then runs
CTest and actual CEF integration checks.
`package` downloads the small portable NSIS compiler if needed. `.deps/`,
`out-cef/`, `dist/`, and `artifacts/` are local and ignored. Only en-US/tr engine
locales are packaged, and compressed dependency caches are removed after packaging.

[Build/dependencies](docs/BUILD_WINDOWS.md) · [Architecture](docs/ARCHITECTURE.md) ·
[Verification](docs/STATUS.md) · [Licenses](THIRD_PARTY_NOTICES.md)

## Remaining limits of the engineering build

Shield supports a tested subset of ABP rules and native CSS cosmetics; full
subscriptions, procedural filters and scriptlets remain pending. Unknown options
reject custom-rule saves; regex execution is bounded by RE2. Strict page CSP can
block cosmetics. Cookie filtering covers network send/save callbacks, not complete
storage partitioning or fingerprinting defence. Site comparison includes the pinned
Public Suffix List and its private entries. HTTPS-only upgrades HTTP with loopback
exemptions; its separate exception editor remains pending. Secure DNS now
uses Chromium global preferences. A NetLog fixture verifies Cloudflare DoH
responses and no classic fallback when that provider is blocked; this is not a
complete packet-level leak audit. Geolocation depends on the engine/provider
and Windows permissions. Hardware camera/microphone tests remain outstanding.

The native menu now includes an encrypted password vault with explicit form
capture/fill, optional filter subscriptions and authenticated update preparation.
See [vault](docs/PASSWORD_VAULT.md), [subscriptions](docs/FILTER_SUBSCRIPTIONS.md)
and [updater](docs/UPDATE_ARCHITECTURE.md) for supported operations and limitations.
The updater defaults to disabled until a NorthByte public trust root and HTTPS
publication endpoint are provisioned. Cold-start bootstrap recovery and new-engine
health rollback remain incomplete; manual replacement must preserve the profile.
Background networking, sync, component updates and
extensions are disabled in the normal Alloy path, and known Google service hosts
resolve to NXDOMAIN. There are no Google API/OAuth keys or Chrome branded UI.
This is not a claim of an exhaustive upstream network audit. Required CEF names
such as `chrome_elf.dll` remain for binary compatibility.

Tested on Windows 11 x64; Windows 10 x64 is targeted but not tested on a second
machine. App and installer are unsigned. Remaining improvements are in
[ROADMAP](docs/ROADMAP.md).

For architecture testing only, `--turan-runtime=chrome --profile=DISPOSABLE_PATH`
enables Chrome-style CEF and loading trusted unpacked fixtures. Normal user
profiles are not switched to this path. The MV3 worker/API/DNR probe passes, but
native tabs are exposed as separate extension windows, so full extension
compatibility is not claimed. Physical media, screen capture and actual notification
delivery remain unverified despite passing consent/state tests.
The additional `chrome-tabs` command-driven candidate also fails the window
topology test; it is a diagnostic path, not a released extension feature.

The newer `--turan-runtime=chrome-integrated --profile=DISPOSABLE_PATH` candidate
keeps Chromium's genuine normal frame as the only visible browser window and
uses its real tab strip, omnibox and extension toolbar. Turan's original icon,
caption, start page and native panels are attached without reparenting the frame.
Ctrl+Shift+M or the window system menu opens Turan controls; Extensions opens the
engine's privileged `chrome://extensions/` manager. External pages have no Turan
management bridge. The candidate supports tested native tab/window operations
and publisher action popups, but custom UI, engine session metadata, full private
extension behavior, Chrome Web Store/CRX distribution and automatic extension
updates still require acceptance. The default existing Alloy architecture is
retained while these contracts are completed.

## Türkçe

CEF hazır motoruyla çalışan C++ tarayıcıdır; Chromium kaynakları indirilmez veya
derlenmez. ZIP'i çıkartıp `TuranBrowser.exe` çalıştırın ya da setup kullanın.
Menü → Settings → Türkçe ile dili seçin; motor dili için yeniden başlatın.

Turan code/assets: [MIT](LICENSE). Dependencies retain their own licenses.

`chrome-native-tabs` validates a real normal Chromium tab model with a separate
stock engine window. It is a diagnostic path, not the custom Turan architecture.
On it, pinned uBOL and Dark Reader builds pass actual blocking/theming/action
popup checks. Bitwarden consent/logged-out popup checks also pass on this probe;
its account page alone is not proof
of vault/autofill compatibility. Details and remaining failures: TEST_REPORT.md.

The opt-in `chrome-reparent-tabs` experiment attaches that genuine normal engine
frame to Turan's Win32 root. Native/extension tab activation, resize/fullscreen,
single-tab close and private cookie/storage/history tests pass. Publisher action
popups pass after native root/tab activation. **Stock engine controls remain
visible, and automatic extension window focus is not reliable.** This is not the
production UI or a shipped extension manager. It requires an explicit disposable
`--profile` and can be omitted with `TURAN_ENABLE_NATIVE_CHROME_PROBE=OFF`.


### Windows build automation (11 October 2026 source iteration)

For the new offline package hardening, ephemeral RSA-PSS QA, Windows CI and
installer smoke workflow, see `docs/WINDOWS_ENGINEERING_BUILD.md`. These
source-side checks do not convert the development release into 1.0.0 Stable
or retroactively validate the original executable.


### CEF-free recovery launcher (engineering edition)

After rebuilding this source, open **`TuranStartup.exe`** instead of
`TuranBrowser.exe` to run the authenticated interrupted-update preflight before
CEF loads. The Windows installer shortcut uses the launcher. Portable users
should also start `TuranStartup.exe`. The original bootstrap executable remains
included for CEF child processes and may bypass the external preflight if
launched directly. This is still 0.2.0-dev, not a production-approved Stable
release. Publisher signing keys and a production update endpoint are not
provisioned. See `docs/UPDATE_ARCHITECTURE.md`.

### Release finishing and workstation evidence (October 2026)

See [`docs/RELEASE_FINISHING.md`](docs/RELEASE_FINISHING.md) for the updated
integrated-Chromium system menu, publisher verification, Windows 10/11
integration suite, isolated local soak and actual hardware/accessibility QA.
**Production release remains blocked** until independent evidence is reviewed.

## Windows 11 release contract and integrated tools

The candidate now includes an **opt-in** Chromium-owned normal window with a
collapsible native Turan tools dock; Chromium remains authoritative for tabs and
extension popups. The standard Chrome strip is not yet replaced. Production
updates stay disabled without an independently provisioned NorthByte publisher
public key and HTTPS endpoint. The scoped 1.0 release target is Windows 11 x64
only, with explicit fail-closed offline-update acceptance. See
[release scope](docs/RELEASE_1_0_SCOPE.md) and
[static HTTPS publisher staging](docs/UPDATE_STATIC_DEPLOYMENT.md).
