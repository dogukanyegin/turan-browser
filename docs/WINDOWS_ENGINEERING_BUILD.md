# Turan Browser — Windows engineering build and release boundary

**Status: 0.2.0-dev; the source in this package has not been compiled on Windows.**

The source includes a Windows build-and-integration workflow at
`.github/workflows/windows-engineering-build.yml`. When this source is placed
in a GitHub repository, manually trigger **Turan Browser Windows engineering
build** (or push to the configured branches). Its job runs on `windows-2025`
with MSVC, pinned CEF, Python and Node. Successful runs retain artifacts for
14 days; failed runs retain non-secret test logs where available.

You can run the equivalent on a disposable, GUI-capable Windows x64 workstation:

```powershell
python -m unittest discover -s tests -p '*test.py' -v
python tools/build_cef.py all
python tools/release_gate.py
```

The `all` command downloads dependencies only from the pinned lockfiles,
compiles CEF-wrapper/Turan native components, executes CTest and live
Chromium/CEF fixtures, creates the setup EXE/portable ZIP/source ZIP,
verifies runtime SHA-256 and ZIP integrity, then executes
`tests/package_smoke.py` to smoke-test extracted portable runtime, per-user
installer, uninstall registration and preservation of a user-created file.
It does not silently run the old 0.1.0 migration test when the exact historical
baseline is absent; that requirement remains **NOT TESTED**.

GitHub-hosted Windows Server is **not** an actual Windows 10 + Windows 11
validation matrix. A headless runner might also fail native dialog/desktop
tests; treat such failures as failed/unverified tests, not as passes.
Independently test installer and portable files on real Windows 10 and 11
machines, with interactive print/save UI, physical media, screen sharing,
extensions, update rollback, low-memory use and assistive technologies.

## Publisher credentials and security

The repository contains no production signing keys. The update producer uses
RSA-PSS SHA-256 with explicit public-key provisioning, but production server,
private publisher key management, Authenticode certificate and signed-release
rollback behavior remain open. **Do not put secrets in GitHub artifacts or
source ZIPs.** The signing tests generate temporary disposable keys only.

The engineering workflow does **not** create a GitHub Release, publish a
Stable installer, change `release.json`, claim Windows 10 support or alter the
Stable gate. `release_gate.py` continues to reject 1.0.0 with unresolved
mandatory criteria. Any new EXE must be paired with the *same* source digest,
not the earlier 0.2.0 portable package.

## Package-path protection

`tools/safe_paths.py` rejects invalid Windows device names, traversal,
alternate data streams and case-insensitive path collisions. The package
builder, update publisher and ZIP verifier share these checks. Runtime
inventory loading also rejects symlink or reparse-point ancestors. These are
packaging and publisher protections—not a replacement for Windows runtime QA.


## 2026-10-11: CEF-free recovery and workstation QA

**New binary:** `TuranStartup.exe` is a CEF-independent launcher linked only to
Windows system libraries and the authenticated update component. It performs
transaction recovery *before* `TuranBrowser.exe` can load `TuranBrowser.dll`.
If the journal is corrupt, the update is unprovisioned or the runtime is missing,
startup fails closed and user profiles are not deliberately altered. The
installed Start-menu shortcut targets `TuranStartup.exe`. Portable users must
launch **TuranStartup.exe**, not the legacy Chromium bootstrap executable,
for the recovery preflight. Directly opening `TuranBrowser.exe` bypasses this
new preflight, so this is a documented residual risk until all entry points
are covered. The bootstrap itself retains its existing recovery check only
*after* CEF has loaded. Production signed trust material is still absent.

On a newly rebuilt Windows x64 tree:

```powershell
python tests/startup_preflight.py --runtime .\out-cef\app\Release
python tools/windows_acceptance.py --runtime .\out-cef\app\Release
```

Run the workstation acceptance tool **separately on real Windows 10 and
Windows 11 x64 workstations**; GitHub Windows Server runners do not count as
these operating systems. Its JSON captures OS product type and OS build,
actual binaries' SHA-256, and isolated-profile UI/CEF smoke results. Manual
physical-device, printing, accessibility, long-duration, hostile-download and
production key/server checks remain NOT TESTED. Do not edit the release gate
to claim any of those tests have passed. The 0.2.0-dev source changes must
be recompiled; an earlier `dist/` cannot prove this launcher works.
