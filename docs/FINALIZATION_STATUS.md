# Turan Browser 1.0.0 — Finalization status (11 October 2026)

**Engineering status: 0.2.0-dev. NOT a Stable release.**

The original source ZIP and Windows portable ZIP uploaded for this audit match the
`source_sha256` recorded in the original portable build manifest. All 37
manifested binary runtime file SHA-256 values and ZIP CRC were verified.
The uploaded NSIS EXE's SHA-256 was recorded, **but its installed payload was
not validated**. Those checks establish file consistency, not functional behavior.

## Source corrections in this next-source package

- Release acceptance: explicitly optional password vault and independent Tor mode
  no longer masquerade as mandatory Stable blockers. Mandatory IDs cannot be
  removed or demoted by changing JSON; `1.0.0-dev` also cannot bypass the gate.
- The Turan Shield custom-rule failure path now handles a missing per-rule
  diagnostic safely; it will show a validation error rather than indexing an
  empty diagnostic list.
- The native privacy-settings description now acknowledges optional validated
  filter subscriptions rather than incorrectly saying they are absent.
- New offline `tools/verify_distribution.py` verifies packaged source provenance,
  ZIP integrity, safe member names, and hashed runtime files. The package
  builder runs the verifier on finished outputs and retains its JSON result.
- A Windows build from standalone new sources no longer fails solely because
  the historic 0.1.0 archive was not shipped. Engine-migration/performance
  comparisons are instead **NOT TESTED** when that baseline is unavailable.
  This omission **does not** pass migration, performance, or Stable acceptance.

## Required acceptance not yet completed

1. Native save dialog and real printer workflow.
2. Multi-window and multi-profile isolation.
3. Production integration of true Chromium/Manifest V3 window and tab topology.
4. Complete MV3 extension lifecycle, permissions and real third-party tests.
5. Full required Turan Shield / subscription acceptance coverage.
6. Production-signed and rollback-verified automatic updates.
7. Completed network, privacy, security and hostile-input checks.
8. Independent Windows 10 and Windows 11 installer/portable testing.
9. Memory, startup, stability, performance and accessibility testing.
10. Security assessment confirming no known open critical/high-risk findings.
11. Physical camera, microphone, screen sharing and notifications.
12. Investigation and resolution of unexplained startup regression.

Optional and not Stable blockers: a full built-in password manager and a separate
Tor connectivity/anonymity mode. Other extant core tests were already reported
as passed in the historical manifest but have **not** been rerun here on Windows.

## Rebuilding on Windows

Use a disposable Windows 11 x64 build environment with Visual Studio C++ tools,
Windows SDK, CMake, Python and Node.js installed. Unpack this next-source ZIP.
To rerun historical engine migration and comparative performance, separately
place the verified immutable `TuranBrowser-0.1.0-windows-x64-portable.zip` in
`dist/`. Do not use any other archive as a substitute baseline.

```powershell
python -m unittest discover -s tests -p "*test.py" -v
python tools/build_cef.py all
python tools/release_gate.py
```

The `all` command performs Windows C++ compilation, CTest, existing live-engine
integration probes and dev packaging, then verifies package integrity. It does
**not** automatically certify Stable acceptance. The code is not compiled or
Windows-tested by the present Linux-based source audit. Previously uploaded
0.2.0 binaries must **not** be represented as if built from this modified
source; their build hash belongs to the original source ZIP.

Do not rename 0.2.0 development installers or edit manifest fields to simulate
a 1.0.0 Stable deliverable. Obtain verified production signing/update keys and
rerun full end-to-end Windows checks before any Stable deployment.

## Follow-up engineering security/build iteration — 11 October 2026

- Added a canonical Windows-safe path policy shared by the packager, signed-update manifest producer and distribution verifier. The policy rejects case-insensitive ZIP/inventory aliases, reserved device names, alternate data-stream syntax, a file shadowing a directory and symlink/reparse-point runtime ancestors.
- Added an isolated RSA-PSS publisher fixture using ephemeral 3072-bit keys, verification with OpenSSL and tampered-signature rejection. This is not publisher-key provisioning for release.
- Expanded platform-independent source tests from 14 to 24 passing tests. The original uploaded 0.2.0 source and portable still pass the stricter read-only package verifier.
- Added `.github/workflows/windows-engineering-build.yml` to run source tests, CMake/CEF integration, packaging, and immutable-source/binary verification on a Windows engineering runner. No GitHub Actions job has been executed in this audit.
- Added a post-packaging Windows extracted-portable/installer/uninstaller smoke stage. It is now required by `build_cef.py all`, but has not run here. Historical 0.1.0 migration is explicitly NOT TESTED when its immutable baseline is absent.
- Original 0.2.0 EXE, DLL and installer remain unchanged and **do not match** this revised source. There is no new Windows executable from this iteration.

See `docs/WINDOWS_ENGINEERING_BUILD.md` for operational commands and verification limitations. All previously listed Stable blockers remain open until evidence is collected from a new Windows build and independently reviewed.
