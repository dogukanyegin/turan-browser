# Turan Browser 0.9.0 Beta 1 — candidate source release

**Status:** Beta candidate, not yet Windows-rebuilt from this version-labelled source. Windows 11 x64 only; not 1.0.0 Stable.

- Based on the last 0.2.0-dev engineering build that passed nine CTest groups, 33 Python tests (10 skipped), 11 Windows acceptance scenarios and the integrated Chromium tab/native Turan dock fixture on Windows 11. These results belong to the engineering binary, **not** this version-labelled Beta binary.
- Version metadata changed consistently to `0.9.0-beta.1` (Windows file version `0.9.0.1`, channel `beta`); separate `TuranBrowserBeta` application registration, shortcuts and default profile.
- The existing native default interface and Shield remain in place. The Chromium normal-window tab/MV3 integration and attached Turan dock are an **opt-in candidate**, not the default browser UI; the bespoke Turan tab/omnibox shell is not fully replacing stock Chromium controls.
- The unprovisioned network updater stays disabled. RSA-PSS verification/offline publication tooling exists, but no production publisher key or HTTPS update endpoint is configured.
- Beta is experimental: no Windows 10 claims, no Store/CRX auto-update certification, no guarantee of full Bitwarden account login/autofill or independent Tor anonymity.
- A new Windows build, extended acceptance, Beta installer/portable verification, package SHA-256 and real browser UI review are required before external Beta release.

---

## Previous engineering release notes (historical)

# Production engineering — 0.2.0-dev

This is an intermediate engineering build, not Turan Browser 1.0 Stable.

Latest engineering changes preserve the existing project and prebuilt engine:

- Added an unmodified top-level Chrome-runtime candidate with real shared engine
  tabs, cross-window moves, automatic extension window focus and Turan native panels.
- Added profile-local DPAPI/AES-GCM password storage, master authentication,
  explicit secure form capture/fill, encrypted import/export and lock controls.
- Added optional EasyList/EasyPrivacy subscriptions with explicit consent,
  bounded HTTPS transport, validated cache/backup and asynchronous Shield publication.
- Added RSA-PSS/SHA-256 update verification, exact runtime inventory enforcement,
  CEF-independent apply helper, journal recovery and joined/cancellable native workers.
- Added actual-engine and native regression tests. These are limited engineering
  results; the acceptance report records remaining production blockers.

- Upgraded the pinned prebuilt engine to CEF 154.0.34 / Chromium 154.0.8037.98.
- Preserved and committed the original 0.1.0 source; corrected the Git rule that
  accidentally ignored src/.
- Added normal-session journals, backup fallback, clean/crash recovery and startup
  restoration preference without changing legacy bookmark/history records.
- Added closed-tab reopen, pinning, muting, three coloured tab groups and native
  drag ordering. Private tabs are excluded from recovery and closed-tab history.
- Fixed synchronous Chrome-runtime shutdown invalidating the tab iteration.
- Added an independent native RE2/PSL Shield engine, tested ABP subset, scoped CSS
  cosmetics, per-site exceptions, asynchronous custom-rule editor with rollback,
  actual request reasons and isolated private counters. Strict CSP cosmetics,
  procedural filters and subscriptions remain incomplete.
- Added the explicit external Tor Browser launcher with authenticated 15.0.24
  file catalog, strict URL/local-path/argv checks, asynchronous verification and
  private preference isolation. Real normal/private external windows pass. Other
  Tor versions need catalog renewal; separate Tor connectivity remains pending.
- Added real MV3 API/DNR probes; identified incompatible native window topology.
- Added a modular, opt-in same-thread Win32 adapter for the real normal Chrome
  tab model. Native and extension tab activation, renderer resize/input,
  fullscreen, shared-frame tab close, private storage/history isolation and
  publisher action popups after native activation pass. Deferred focus callbacks
  fix an actual tab-update reentrancy hang. Stock controls and automatic window
  focus remain production blockers; the default Alloy path is retained.
- Reject unsupported diagnostic invocations before profile recovery/leases or
  metadata writes; actual disposable portable launches verify this boundary.
- Validated a normal Chromium-window tab model separately; custom Turan UI
  integration remains unresolved. Tested actual uBOL blocking and Dark Reader
  theming/action popups and Bitwarden consent/logged-out popup. Actual Bitwarden
  vault/autofill and normal Turan extension management remain unverified.
- Added find, zoom, fullscreen, stop, home, print, PDF export and developer-tools
  controls. Engine PDF output passes; native Save-dialog automation is unverified.
- Isolated development installation/registration/default profiles from 0.1.0.
- Added complete metadata validation, verified backup recovery, preservation of
  damaged files and startup refusal when no valid metadata copy exists. Atomic
  writers reject reparse-point leaf files and avoid shared temporary filenames.
- Added persistent normal download history and real restart tests; private files
  remain saved without persisting their download metadata to the normal profile.
- Added complete closed-profile snapshots, SHA-256 validation, restore staging,
  retained previous profiles and interrupted-handoff recovery. Engine changes
  back up profiles before CEF opens them; newer engine markers refuse downgrade.
  Real CEF tests preserve cookies/local storage through 144 -> 154 and restored
  144. A small engine-free helper implements the native close-and-backup action.
  A pre-start engine-attempt marker protects failed startup/final marker writes.
- Restricted portable/installer payloads to a generated runtime allowlist so
  neighbouring profiles, backups and arbitrary files cannot enter releases.
- Added asynchronous native consent, editable/resettable site permission policies,
  private-session permission isolation and real CEF revoke/restart tests. Tests
  use fake media devices and do not read the user's clipboard or location.
- Replaced DoH command-line switches with real global preferences and verified
  DoH responses/fail-closed fixture routing using Chromium NetLog.
- Removed desktop capture from the native test harness.
- Removed the explicit client-side phishing-disable switch; no functioning
  reputation provider is claimed.
- Added common version metadata, client/bootstrap version branding, source/binary
  build fingerprints and a Stable acceptance gate.
- Avoided unconditional idle toolbar redraws. Limited local process-tree
  comparison results are in TEST_REPORT.md; no general speed increase is claimed.

Full extension distribution/management, profile UX, complete password/form coverage,
advanced Shield syntax, Tor connectivity, update publication/bootstrap recovery and comprehensive platform/security tests
remain production blockers listed in TEST_REPORT.md and release-acceptance.json.


## Finalization engineering patch — October 11, 2026 (source only)

- Added `TuranStartup.exe`, a CEF-free recovery-first launcher; Start-menu
  shortcuts and portable launch guidance now use it.
- Added a Windows integration fixture for corrupt journal refusal, absent DLL
  refusal and preservation of user data.
- Included the launcher in the explicit runtime inventory and publication
  allowlist; the distribution verifier requires it in newly built sources.
- Added an independent Windows 10/11 workstation reporting utility for
  real-platform smoke evidence; Windows Server CI cannot substitute.
- No new Win32 binary has been compiled or executed in the Linux editing
  environment. No production signing keys, publisher endpoint, full window
  integration or physical-device/printer acceptance is claimed.

### 2026-10-11 release-hardening source iteration (still 0.2.0-dev)

- Expose Turan native commands through the genuine integrated Chromium frame's
  Windows system menu without changing Chromium tab/window ownership.
- Sign and verify RSA-PSS release metadata in staging, require matching
  provisioned publisher public key for stable-channel publication, reject
  duplicate release targets and unsafe/incomplete production payload inventories.
- Add extended isolated Windows workstation probes, local long-session
  sampling, real manual peripheral/accessibility observation and distinct
  Windows 10/11 evidence reconciliation. These additions are **not** Windows
  runtime results or a Stable release certificate.

## Engineering candidate — integrated tools and offline publication guard

- Added an opt-in, DPI-aware owned Turan command dock to real normal Chromium
  windows without changing parent/style, CEF extension window IDs or Chrome tab
  focus semantics. This does **not** replace the stock Chromium toolbar.
- Added offline RSA-PSS/SHA-256 static deployment staging, verification,
  signature/key tamper rejection and extra-file checks. No server created.
- Pinned an explicitly reviewed Windows 11 x64 + disabled-unprovisioned-updater
  scope with mandatory substitute acceptance gates; production Stable is still
  blocked by unfinished required engineering and Windows verification.
