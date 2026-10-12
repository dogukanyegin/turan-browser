# Turan Browser — release-hardening engineering iteration

**11 October 2026 · Current product version: 0.2.0-dev · Stable NOT approved**

This iteration **changes real source code** and adds actionable test harnesses. It
is not a completed Chromium fork, production signing deployment, or physical
peripheral certification. Keep any existing end-user profile, old 0.2.0 binary,
and raw failure evidence intact.

## What was actually changed

1. `EngineWindows`: native system-menu commands inside the **real** Chromium
   top-level frame for Turan Shield, extension manager, downloads, history,
   bookmarks, permissions, settings, updates, PDF save and printing. The
   menu dispatches to existing Turan native handlers and preserves Chromium's
   TabStripModel, HWND identity, tab/window focus and sandbox; it does **not**
   reparent Chromium or replace its complete stock toolbar. Real Windows test
   added to `tests/integrated_chrome_cef.py`.
2. Update manifest producer now signs into a temporary directory and verifies
   the actual RSA-PSS signature *before* promoting final files, refuses reuse
   of an output filename, refuses signer/provisioned-public-key mismatch,
   mandates the expected public key for the stable channel and requires the
   launcher/update helper in a complete runtime inventory. Unconfigured builds
   stay safely unconfigured. **No company signing key or HTTPS endpoint has
   been fabricated or packaged.**
3. Extended real Windows workstation tests cover Chromium integrated tabs,
   unpacked MV3 extension installation/lifecycle, uBOL/Dark Reader/Bitwarden
   logged-out UI, sessions, fake-media permissions, PDF engine, DNS and TLS.
   The optional `--soak-minutes` workload uses an isolated profile, a local
   HTTP server and process-tree samples, retaining incremental evidence.
   It does not contact arbitrary web sites.
4. Manual evidence wizard covers actual PDF native Save As, **physical paper
   printing**, camera/microphone, screen sharing, OS notifications, keyboard,
   screen reader and 200% DPI/high contrast. It never auto-passes untested
   operations and collects no camera/microphone recordings.
5. Cross-machine evidence checker requires **distinct real Windows 10 and
   Windows 11** workstation results and identical Turan DLL SHA-256 values.
   It intentionally cannot approve Stable without the remaining architectural,
   signing, security and human release decisions.

## Apply PATCH to your current Windows build directory

Back up your current working source directory (or use Git). Use the last
successfully built `TuranBrowser-0.2.0-dev-finalization-SOURCE-20261011` tree
with `.deps` and `out-cef` intact. Extract the patch ZIP into the root of that
same tree, preserving the relative `src/`, `tests/`, `tools/`, `docs/` paths.
Do **not** place the files into a nested directory. This patch does not include
user profiles or executables.

```powershell
Expand-Archive -LiteralPath "$HOME\Downloads\TuranBrowser-0.2.0-dev-release-hardening-PATCH-20261011.zip" -DestinationPath . -Force
python -m unittest discover -s tests -p '*test.py'
python tools/build_cef.py all
python tools/windows_acceptance.py --runtime .\out-cef\app\Release --extended
```

**A full local Windows test and build are needed after these C++ changes.**
The above tests can take substantial resources. With the same folder, cached
CEF will be reused when its verified archive files remain available. If the
extended acceptance fails, upload its JSON and relevant failure output; do not
relabel the release.

### Long-session test (real work, optional duration)

```powershell
python tools/windows_acceptance.py --runtime .\out-cef\app\Release --extended --soak-minutes 60
```

This writes:

- `artifacts/windows-workstation-acceptance.json`
- `artifacts/soak-workstation.json`

A long-session test alone does not establish acceptable CPU or RAM usage. The
soak script records installed RAM, process-tree private/working bytes, CPU,
request count and actual duration for engineering interpretation. The test
has **not** been run from the current Linux authoring environment.

### Physical device, printing and accessibility checks

```powershell
python tools/device_acceptance.py --runtime .\out-cef\app\Release
```

The operator must actually perform each test. If there is no physical printer,
record `not_tested`; do not mark virtual PDF printing as physical printing.
Store the observation JSON only; never upload private audio/video or unredacted
screen captures. This is human-observed evidence, not a security audit.

### Windows 10 and Windows 11 comparison

Copy the updated release runtime to **a real Windows 10 x64 workstation** and
repeat the extended suite and device wizard there. Save the reports under
distinct names. The existing Windows 11 result does not count as Windows 10,
and a GitHub Windows Server CI runner does not count as either consumer OS.

```powershell
python tools/release_evidence.py --win10 .\evidence\win10.json --win11 .\evidence\win11.json --devices10 .\evidence\win10-devices.json --devices11 .\evidence\win11-devices.json
```

The checker reports blockers; it **does not automatically change**
`release-acceptance.json` or the product's version number.

## Actual publisher provisioning — NorthByte operational prerequisite

The product already contains an RSA-PSS verifier and signed transactional update
machinery. The company must secure and maintain its **own** RSA 3072/4096
private signing key outside the repository/runtime, export its public PEM key,
and control a **real** HTTPS update/payload origin. A hardware-backed/offline
signing environment and operational key rotation policy are recommended.
**Do not send any private key to ChatGPT or put one in a ZIP or Git repo.**

Only a public key may be embedded in an external build header:

```powershell
python tools/update_release.py provision --openssl "C:\Path\openssl.exe" --public-key "C:\Secure\publisher-public.pem" --manifest-url "https://YOUR-ACTUAL-DOMAIN.example/updates/stable/manifest" --channel stable --sequence 1 --output "C:\Secure\generated\turan_update_public.h"
```

The URL above is a **placeholder**, not a deployed server. Configure the
CMake cache with `-DTURAN_UPDATE_TRUST_HEADER=C:/Secure/generated/turan_update_public.h`,
then rebuild and validate the exact runtime inventory against the deployed
HTTPS payloads, the signature, interruption/rollback and upgraded CEF health.
For production publication, `publish` now requires `--expected-public-key` to
match the signing key. It refuses to overwrite existing manifest or signature
files: publish each release to a new filename and deploy the verified pair
alongside immutable payloads in an atomic server-side activation procedure.
Do not expose a partially uploaded release to clients. Windows Authenticode
signing, installer registration refresh, key rotation, multi-engine post-update
health checking and operator incident recovery remain separate tasks.

## Remaining blockers that CANNOT be marked resolved by this ZIP

- Full custom Turan toolbar within the real Chromium window, long-term engine
  session metadata and extension tab/window integration contract. System menu
  commands improve the visible native integration **but do not close P0**.
- Production endpoint, publisher custody, live complete update, rollback
  from unhealthy new Chromium, Authenticode and registration refresh.
- Chrome Web Store/direct CRX compatibility, authenticated Bitwarden vault
  login/autofill and comprehensive extension permission/update tests.
- Full multi-profile/multi-window product guarantees, Windows 10 and physical
  device certification, independent high-risk security audit.
- Full supported ABP/Shield expectations, low-memory, physical accessibility
  and multi-hour stability acceptance.

A green Python or CTest result does not turn 0.2.0-dev into 1.0 Stable.
