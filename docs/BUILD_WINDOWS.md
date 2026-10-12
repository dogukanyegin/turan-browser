# Windows x64 build with prebuilt CEF

| Dependency | Requirement |
| --- | --- |
| OS | Windows 10/11 x64 |
| Compiler | VS 2022+ C++ desktop/x64 tools; tested Build Tools 2026 |
| SDK | Windows 10/11 SDK; tested installed 10.0.26100.0 |
| CMake | 3.22+; tested VS bundled 4.3.1 |
| Python | 3.10+ |
| Node.js | 22+ for DevTools integration tests only; not shipped/required by the application |
| CEF | Locked minimal Windows64 154.0.34 / Chromium 154.0.8037.98 |
| Native filter | RE2 2025-11-05 + Abseil 20260817.0, SHA-256 pinned in filter.lock.json; no Rust runtime |
| Public suffixes | Original MPL-2.0 list pinned by commit/hash in filter.lock.json |
| OpenSSL | TLS fixture only; tested Git for Windows bundled OpenSSL. No certificate is added to the OS trust store |
| NSIS | Locked portable compiler 3.13; packaging only |

No ATL/MFC or Chromium toolchain/source tree is required. The earlier 100 GB
checkout requirement no longer applies. Allow a few GB for dependencies, build
output and packages.

```powershell
python tools/build_cef.py deps
python tools/build_cef.py build
python tests/ui_smoke.py
python tools/build_cef.py package
```

CEF's upstream SHA-1, additionally measured SHA-256, archive URL/size and NSIS SHA-256 are pinned in
`cef.lock.json`. Archives are checked before extraction and unsafe path/link
members rejected. Debug engines, debug symbols and source mirrors are not fetched.
The small CEF C++ API wrapper and pinned native RE2/Abseil filter libraries are
compiled; the actual Chromium engine is prebuilt. Native dependency extraction
rejects traversal and links, except a specifically omitted RE2 Python LICENSE
symlink that is unnecessary for C++ compilation. Full license texts are bundled.
The QA corpus is kept only in .deps/filter-corpus and is not shipped or enabled.

Direct CMake after obtaining the binary package:

```powershell
python tools/filter_deps.py
cmake -S . -B out-cef -A x64 -DCEF_ROOT="C:/path/to/extracted/cef_binary_package"
cmake --build out-cef --config Release --parallel 2
ctest --test-dir out-cef -C Release --output-on-failure
python tests/smoke_cef.py
```

Output: `out-cef/app/Release/TuranBrowser.dll` and sandbox bootstrap
`TuranBrowser.exe`, plus the small engine-free `TuranProfileTool.exe` for
post-exit profile backups and `TuranUpdateTool.exe` for authenticated update
transactions. Windows DPAPI/CNG, WinHTTP and WTS are operating-system dependencies;
no new credential-server, cryptographic runtime DLL or Chromium checkout is needed.
Matching browser base names are required. Never pass `--no-sandbox`.
`build_cef.py` applies the Turan icon and compiled client version to our local
bootstrap copy after building. `release.json` supplies the application version.
The build runs nine model/session/store/profile/filter/Tor/vault/subscription/updater
CTest groups, native UI, actual CEF privacy/download/session
recovery, real Shield network/cosmetic/private/PSL-cookie checks, pinned official
EasyList/EasyPrivacy QA corpus compilation, complete profile upgrade/rollback, permission, DNS and certificate-rejection fixtures, isolated MV3 and publisher-extension probes.
It also tests the integrated normal-frame candidate, both runtimes' explicit
password form workflows and optional subscription publication, and actual native
updater panels/helper refusal. Production updater provisioning requires an
external public-only header passed as `-DTURAN_UPDATE_TRUST_HEADER=...`; private
keys must stay outside source/packages. See docs/UPDATE_ARCHITECTURE.md.
DNS checks require access to Cloudflare DoH and only resolve random test names
under example.org. A source/binary hash manifest prevents
packaging changed sources without rebuilding. Stable labels are rejected while
`release-acceptance.json` has failed or pending criteria.

Additional diagnostics:

```powershell
python tests/extensions_cef.py --runtime chrome-native-tabs --topology
python tests/thirdparty_extensions_cef.py
python tests/native_chrome_host_cef.py
python tests/thirdparty_extensions_cef.py --runtime chrome-reparent-tabs
python tests/diagnostic_guard_cef.py
python tests/package_smoke.py
python tests/permissions_cef.py
python tests/dns_cef.py
python tests/tls_cef.py
python tests/profile_migration_cef.py
```

The third-party script downloads only pinned publisher MV3 test ZIPs into
.deps/test-extensions, verifies SHA-256 and archive paths, and uses disposable
profiles. Those extensions are not distributed in the app. Bitwarden's initial
popup failures are retained; its logged-out popup passes after consent and explicit
window focus. Account/vault/autofill checks remain pending for production approval.
DevTools transport binds to localhost and is enabled only by explicit test launch
arguments. Navigation PDF testing uses a guarded local fixture chooser callback;
native Save-dialog interaction remains unverified.
Run interactive engine/UI fixtures serially; concurrent windows can affect focus
and popup results. The compiled native Chrome adapter is opt-in, requires an
explicit profile and retains stock controls. `TURAN_ENABLE_NATIVE_CHROME_PROBE=OFF`
rejects that runtime at startup; the default product path remains Alloy. The full
engineering test pipeline includes this experiment and requires it compiled in.
Media consent tests use Chromium's fake device flag in disposable test profiles.
The production app does not enable fake devices. Permission queries do not read
the user's clipboard or position. NetLog files contain only the isolated test
engine's requests; the build manifest includes small verification summaries and
does not redistribute raw network logs or third-party popup/font contents.

Packaging reuses the app output without another uncompressed staging copy.
`portable.flag` is included only in the ZIP, selecting a relative profile. The
installer excludes the flag/profile, uses LocalAppData, creates Start menu
shortcuts and an HKCU uninstall entry. Its uninstaller deletes only owned files
and empty directories; profiles and extra user files remain. Close Turan before
install/update/uninstall. Automatic upgrades are not implemented.

`dist/SHA256SUMS.txt` records outputs. Apply Authenticode signatures with your own
certificate after resource branding and before final package hashes. No certificate
was supplied, so current artifacts are unsigned.

The [official CEF binary documentation](https://chromiumembedded.github.io/cef/general_usage#using-a-binary-distribution)
describes wrapper/runtime requirements. CEF's required filenames remain intact.
