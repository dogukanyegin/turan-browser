# Historical 0.1.0 baseline — 4 October 2026

This file records the original baseline. Current engineering engine, artifacts,
tests and blockers are in [TEST_REPORT](../TEST_REPORT.md). Baseline archives are
immutable; current `artifacts/` filenames refer to newer runs. No historical
desktop screenshot is retained or used as current verification evidence.

The full-Chromium plan was replaced as requested. No Chromium/depot_tools checkout
was downloaded. Obsolete source-fetch/build tooling was removed.

Built on Windows 11 Pro x64, VS Build Tools 2026, SDK 10.0.26100.0 and CMake 4.3.1,
using minimal CEF 144.0.36 / Chromium 144.0.7559.264. Only Turan and CEF's small
C++ API wrapper were compiled.

Passed checks:

- CTest URL, UTF-8, persistence, private-history guard and Shield boundary checks.
- Actual sandbox-bootstrap CEF rendering and JavaScript execution.
- Three normal/private/normal tabs with cookie-context isolation/persistence.
- Real download; output bytes matched the local fixture.
- Real blocked-domain request; fixture server received no tracker request.
- Persisted normal history/bookmark; private history excluded.
- Native bookmarks/history/downloads/permissions/settings panels.
- Native tab create/private/close and clean process shutdown.
- Native tab strip background corrected in the original baseline.

Original packaging verification covered ZIP CRC/runtime/default portable profile, installed
runtime, uninstall registration cleanup and user-file preservation. Outputs:
portable ZIP 145.94 MiB; installer 106.55 MiB.

Windows 10 and physical camera/microphone/location are not independently tested;
DoH has not been packet-captured. No password-vault UI or automatic updater is
implemented. App/installer are unsigned. Precise limits are in README.
