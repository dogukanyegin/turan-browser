# Turan Browser 1.0 — Windows 11-only offline-update release contract

**Engineering contract, NOT release approval.** The candidate remains 0.2.0-dev until the remaining required acceptance items have been independently closed. The source `release.json` has `release_scope=win11-x64-offline-updates-v1`; `release-scope.json` pins what that means. The release gate explicitly rejects a change to that contract without code review.

## Chromium/Turan native UI

Normal Chromium windows in `--turan-runtime=chrome-integrated` are owned by Chromium, not Win32 `SetParent`. The engine therefore remains authoritative for real tab and extension window IDs, focus, cross-window moves and extension popups. The new `TuranEngineTools` native **owned** dock sits inside the same screen area without reparenting/replacing the Chrome window. It starts collapsed as a `T` control; clicking `T`, Ctrl+Shift+M or choosing *Toggle Turan tools* in the system menu expands/collapses it. Shield, extensions, bookmarks, history, downloads, permissions, settings, secure updates, PDF export and printing forward to the existing Turan native functions. Extra actions remain under *More commands*.

The dock is DPI-aware, follows Chromium move/resize/minimize and collapses rather than overflowing small browser windows. The hidden native controller still stores Turan-specific state, and Chromium's **stock browser chrome remains visible**. Consequently, this is a materially improved one-root integration candidate, **not** a completed full custom Turan tab strip/omnibox. Replacing those would require changes in upstream Chromium/CEF or a new supported integration contract. The production extension-window-topology/custom-UI gate stays open. The default normal user runtime remains unchanged.

## Update policy

For this scoped 1.0 contract, **no publisher key or HTTPS endpoint is provisioned and updater network actions remain disabled**. A user-invoked update panel must report why updates are unavailable rather than fetching unsigned software. `TuranStartup.exe` still performs CEF-free interrupted-transaction preflight before the browser DLL is loaded. **Source/portable/installer files remain unsigned by NorthByte unless separately Authenticode-signed.**

An offline release is acceptable only if `offline-updater-fail-closed` passes against the exact release DLL, the release clearly documents that updates require manually installed new signed releases, and the supported browser engine receives a maintained security-update process. Neither a test signing key nor an example `.invalid` endpoint can be a production key. Supplying a `TURAN_UPDATE_TRUST_HEADER` to a Stable offline build is rejected in CMake.

To enable real network updates in a later release: (1) procure and secure a real NorthByte RSA key outside this source tree, (2) provision only its public key and canonical HTTPS manifest endpoint, (3) sign manifests using the existing RSA-PSS/SHA-256 publisher tool, (4) prepare a static deployment directory with `tools/update_distribution.py assemble`, (5) verify that directory, (6) deploy **payload files first**, then manifest and signature last to the exact declared HTTPS paths; (7) rerun TLS, tamper, rollback, cold-start and new-engine health regression tests. Key custody, hosting, bandwidth, rollover and real client testing cannot be completed in an offline source environment.

## Scope changes in the release gate

For this **explicitly reviewed** scope only, the original Windows 10+11 package item is replaced by `windows11-x64-package-acceptance`. Windows 10 support is not claimed. The original online updater deployment item is replaced by `offline-updater-fail-closed` (a live online service is not claimed). Both replacements are **pending**, not self-certified. All other security, performance, integrated UI, extension and migration requirements remain mandatory. The `release_gate.py` default tests still reject silent downgrades and changed criteria IDs.

## Windows acceptance to rerun after recompilation

Build and test the exact patched source in your Windows folder. Run `python tools/build_cef.py all` and `python tools/windows_acceptance.py --extended --soak-minutes 60`. On a machine with pinned CEF and fixture extensions available run `python tests/integrated_chrome_cef.py`: that test now checks real dock ownership, command dispatch and Chrome tab/window topology. Recheck the native menus while uBlock Origin Lite, Dark Reader and Bitwarden popups are open. Native UI tests and those engine fixtures were **not** run in this source-only environment.

Current manual hardware reports may describe successful user observations; do not convert that into independent verification or blindly attach it to another DLL hash. Do not change version/channel to Stable before outstanding non-scoped gates are accepted.
