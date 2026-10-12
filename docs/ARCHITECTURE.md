# CEF architecture and Git/Gitea structure

```text
CMakeLists.txt                C++17 shell + CEF API wrapper
cef.lock.json                 pinned minimal binary dependencies
src/main.cpp                  sandbox bootstrap entry and profile setup
src/core/model.{h,cpp}        URL/search and starter domain/privacy policy
src/core/store.cpp           bounded legacy metadata/download/permission records
src/core/state_file.{h,cpp}   exclusive/atomic metadata IO and rejected-file preservation
src/core/session.{h,cpp}      normal-tab recovery journals
src/core/permissions.{h,cpp}  bounded origin/type decisions
src/core/profile_backup.*    full snapshots, hashes, restore journal, engine migration
src/core/profile_worker.*    engine-free shutdown helper launch and result UI
src/profile_tool.cpp         small TuranProfileTool.exe backup process
src/browser/client.{h,cpp}    engine lifecycle/network/cookie/download/permission hooks
src/browser/network.{h,cpp}   validated global secure-DNS preferences
src/ui/window.{h,cpp}         native Win32 tabs/toolbar/panels
src/ui/native_chrome_host.*   opt-in same-thread normal-engine HWND adapter
src/ui/engine_window.*        unmodified normal-frame Turan branding/native menu adapter
src/security/*               DPAPI/AES-GCM vault and move-only secret memory
src/browser/credential_forms.* isolated-world HTTPS form broker
src/ui/credential_window.*    explicit native vault/capture/fill controls
src/privacy/filter_subscription.* bounded optional list transport/cache/rollback
src/ui/filter_subscription_window.* native list consent/status/actions
src/update/*                 signed manifest, inventory, transaction/helper/recovery
src/ui/update_window.*        native preparation UI and joined cancellable workers
resources/                    original assets, new tab, Shield starter rules
tools/build_cef.py            checked download/build/test/package
tools/brand_bootstrap.py      icon resource branding of copied bootstrap
tests/core_tests.cpp          model/persistence/privacy CTest
tests/smoke_cef.py             actual CEF integration fixture
tests/ui_smoke.py              native panels/tab/window test
packaging/windows/            NSIS installer + generated owned-file list
.gitea/                       review templates
.deps/                        ignored dependency cache
out-cef/                      ignored build/runtime
dist/                         ignored portable ZIP/installer/checksums
artifacts/                    ignored test reports (no desktop capture)
```

Chromium/CEF sources are not forked or compiled. The stable CEF API is the engine
boundary. Upgrade the binary lock, compile against its new headers, run tests and
repackage. Keep core policy/data independent of CEF.

The UI thread owns windows/tabs/data. Network callbacks use a shared policy with
atomic options and locked domain rules; they do not access a destroyed UI window.
Callbacks route by the actual browser identifier; Chrome probes can reuse a
client between browsers. CEF approves close before its child window is
destroyed; shutdown waits for every browser OnBeforeClose callback.

The native-host experiment retains a normal Chromium Browser/TabStripModel
without changing upstream code. It checks process/thread, frame class and DPI
awareness before Win32 reparenting, restores style if attachment fails, resizes
each shared frame once and leaves engine destruction to CEF. Focus notifications
post native-row updates rather than re-entering the engine's tab mutation stack.
It remains an explicit-profile experiment: stock controls, window activation,
multiple-window ownership, tab reordering metadata and DPI/accessibility contracts
are not production-accepted. Production uses the existing Alloy path.

The normal context persists; private tabs share a separate empty-cache context.
The last private tab releases it. Metadata is bounded and replaced atomically.
No privileged JS bridge is exposed to websites. The local start page has a
restrictive CSP; shell navigation allows HTTP/HTTPS/about:blank and its trusted
start page, not arbitrary local files or JavaScript/data URLs. Certificate errors
retain CEF's default rejection.

Full-profile snapshots are taken while CEF is closed. An exclusive sibling lease
and Chromium's native lock prevent cooperating writers/live-engine copies.
File data is bounded, hashed with Windows CNG and flushed before commit. Restore
uses sibling staging/previous directories and a validated handoff journal.
The native backup action uses a small CEF-free executable because CEF shutdown
alone did not release every open profile file in the integration test.

The original 0.1.0 is preserved on `main`; engineering work is on
`codex/production-engineering`. No remote was pushed/configured:

```powershell
git add .
git commit -m "Build Turan native shell with prebuilt CEF"
git remote add origin https://YOUR_GITEA/OWNER/turan-browser.git
git push -u origin codex/production-engineering
```

Replace the remote/identity. Store source/locks in Git and package/checksums in
Gitea releases. A native Windows runner can run build, ui_smoke, package; an
external runner/CI has not been connected. Keep signing keys away from PR jobs.

## Native Shield boundary

privacy/filter_engine is independent C++ with immutable compiled policies,
RE2 memory/program budgets and necessary-literal indexes. privacy/public_suffix
loads the pinned original PSL with IDN and wildcard/exception/private entries.
privacy/shield adapts the original model API without changing browser-state.txt.
The curated domain source becomes anchored third-party rules; custom rules and
normal site exceptions use separate bounded atomic files. Failed custom builds
retain the published engine and source; a previous custom source is backed up.
Invalid startup input is preserved and its fallback is visible in the panel.

CEF IO callbacks use only shared Shield state, never raw Window pointers.
Cookie first party stays distinct from the requesting document used by $domain.
Per-browser request events are bounded, ephemeral and forgotten on close.
Navigation generations prevent late old-request callbacks from polluting new
page counters; oversized resource inputs are blocked with an explicit limit reason. Private
site exceptions never enter the normal persisted set. Compilation from the native
editor runs asynchronously; closing its panel waits for the bounded task so profile
leases cannot be released during writes. The CSS layer serializes selectors as
JSON, validates selectors/style rules through Chromium, and publishes only
display:none!important declarations; no filter scripts, redirects or native API
bridge are executed. CSP remains effective. Full subscriptions and procedural
filter compatibility remain explicit acceptance gaps.
