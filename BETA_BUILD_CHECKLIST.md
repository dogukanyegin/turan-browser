# Turan Browser 0.9.0 Beta 1 — Windows 11 release checklist

The previous 0.2.0-dev binary passed Windows engineering tests; **these hashes and results do not certify this Beta binary**.

## Build (Windows 11 x64, Visual Studio C++ Build Tools and Python 3)

From the project root:

```powershell
python tools/build_cef.py all
python tests/integrated_chrome_cef.py
python tools/windows_acceptance.py --runtime .\out-cef\app\Release --extended --soak-minutes 60
```

`--soak-minutes 60` is a real 60-minute live test; keep the computer awake until it completes. If Windows acceptance fails, do not publish the package.

## Required manual checks

1. Verify normal Beta portable UI/navigation/session recovery. Separately, in a disposable profile launch the opt-in `--turan-runtime=chrome-integrated --profile=<disposable-profile>` candidate and verify real tabs, Turan dock, focus, uBlock Origin Lite and Dark Reader; preserve one visible Chromium root. Never imply the opt-in candidate is the default browser UI.
2. Open a disposable Beta profile, restart the browser, and verify private browsing does not persist history/cookies in the normal profile.
3. Confirm updater panel says unconfigured and Check/Download/Install stay disabled. Do **not** create/distribute a test RSA private key or claim live automatic updates.
4. Inspect `dist/PACKAGE_INTEGRITY.json`, `dist/SHA256SUMS.txt`, and `dist/TEST_REPORT.md`. Recompute SHA-256 of installer and portable ZIP independently.
5. Publish as Beta **Windows 11 x64 only**; disclose stock Chromium toolbar and partially tested Chrome extension behaviors. Do not describe this release as security-audited or Stable.

## Expected outputs

- `dist/TuranBrowser-0.9.0-beta.1-windows-x64-portable.zip`
- `dist/TuranBrowser-0.9.0-beta.1-windows-x64-setup.exe`
- `dist/TuranBrowser-0.9.0-beta.1-source.zip`

If beta installer smoke detects existing `TuranBrowserBeta` registration, the build intentionally refuses to uninstall/overwrite that installation in the disposable QA step. Test in a clean machine or remove previous test Beta only after backing up its user data.
