> **Installation scope:** Windows 11 x64 Beta only. Do not install over an existing Turan Stable or Development profile; the Beta namespace is separate.

# Windows installation

Current Beta candidate is 0.9.0-beta.1; 1.0 Stable is not approved. Windows
10/11 x64 are targets; the local validation host is Windows 11.

Extract the matching portable archive into a writable user directory and run
TuranBrowser/TuranBrowser.exe. Keep all DLLs, resources and locale files together.
The portable.flag marker selects Profile next to the executable. No administrator
permission or machine-wide registration is required.

The Beta setup.exe installs per user into
LocalAppData/Programs/TuranBrowserBeta. It creates a separate
"Turan Browser Beta" Start-menu folder and TuranBrowserBeta HKCU
uninstall entry. Its default profile lives in
LocalAppData/TuranBrowserBeta/Profile. The existing 0.1.0 installation,
shortcuts and LocalAppData/TuranBrowser/Profile are preserved. A Stable build,
after acceptance, uses the original TuranBrowser namespace.

Close all Turan processes before replacing application files. Preserve Profile,
browser-state.txt and session-v1 files. Back up the complete profile before an
engine upgrade. Older Chromium engines must not be launched against a profile
cache already opened by a newer engine; use the corresponding backed-up profile
when rolling back.

Keep `TuranUpdateTool.exe` with the application. The native secure-update panel
is disabled until a publisher public key and HTTPS manifest endpoint are compiled
into the build. It is not an operational NorthByte automatic update service yet;
see docs/UPDATE_ARCHITECTURE.md for bootstrap recovery and health-check blockers.

The password vault is additive profile data. Do not discard `password-vault.dat`
or its retained backup when upgrading. Current-user DPAPI means copying the
portable folder to another Windows account/machine does not transfer access to
the vault; use its explicitly encrypted export/import workflow. See
docs/PASSWORD_VAULT.md. Optional filter consent/cache files are also profile data.

## Complete profile backup and restore

The menu action **Back up full profile and close** saves the normal session,
closes browsing and starts the bundled `TuranProfileTool.exe`. This small utility
does not load CEF. It acknowledges an inherited browser-process handle, waits for
that process to exit, acquires an exclusive profile lease and creates a verified
snapshot. Keep this executable with the rest of the application.

For explicit destinations, close Turan and use the main executable:

```powershell
.\TuranBrowser.exe "--profile=C:\Users\YOUR_USER\Turan\Profile" "--backup-profile=C:\Users\YOUR_USER\TuranBackups\Snapshot"
.\TuranBrowser.exe "--profile=C:\Users\YOUR_USER\Turan\Profile" "--restore-profile=C:\Users\YOUR_USER\TuranBackups\Snapshot"
```

Replace these example paths with your actual directories. The backup destination
must be new and outside the profile. Operations require plain local directories;
UNC/network paths, reparse points, traversal names and live profiles are refused.
EFS-encrypted files and named NTFS streams are currently refused rather than
decrypted or silently omitted. The snapshot format covers ordinary file bytes,
empty directories and file timestamps; it does not preserve custom ACLs or all
NTFS metadata. Such profiles need a backup method that preserves that metadata
before an engine upgrade; wider filesystem support remains an engineering task.
The default maximum is 20 GiB / 100,000 entries / 64 levels. Failures keep any
incomplete copies under a `.partial-*` or `.restore-staging-*` name; these are
not valid completed backups. No existing destination is overwritten.

Each completed snapshot contains `profile/` and `backup-manifest.txt` with file
sizes and SHA-256 hashes. Restore verifies the manifest and every file, builds a
separate staged copy, then moves the current profile to a sibling
`Profile.before-restore-*` directory before installing the restored copy. An
interrupted handoff is checked before next startup. Ambiguous recovery states
stop startup and preserve all copies. This mechanism has been tested with
process-crash states; power-loss behaviour on every filesystem is not certified.

The recorded CEF/Chromium version is in `turan-engine.txt`. A different engine
first makes a complete snapshot in the sibling `Profile.turan-backups/` folder;
the successful-version marker changes only after CEF initializes. A separate
`turan-engine-attempt.txt` is flushed before CEF startup; failed initialization or
a failed successful-marker write leaves this conservative engine bound in place.
After a successful marker commit it is removed. A newer successful or attempted
engine is refused by an older build. Unmarked legacy cache profiles are backed up before
first migration. The real-engine fixture verifies 144 -> 154 cookies, local
storage, bookmarks/history and restoration followed by reopening engine 144.
The original 0.1.0 executable cannot enforce the new downgrade marker: always
restore its pre-upgrade snapshot before using it.

Backups and previous profiles contain sensitive data, are not automatically
deleted, and need sufficient free space. Keep them in a protected user directory.
SHA-256 detects changed bytes; it is not a signature or proof of a backup's origin.
Windows-protected engine data may depend on the original Windows account/machine;
cross-account or cross-machine cookie/password portability is not claimed.

browser-state.bak contains a verified previous copy of Turan metadata. A damaged
primary is kept as browser-state.txt.rejected-* during successful recovery. If
both primary and backup are invalid, startup stops without rewriting either;
restore browser-state.txt from your own backup while Turan is closed. These files
cover Turan metadata, not the complete CEF cache/cookie database. New D/Q records
add download history and permission policies while retaining legacy S/B/H records.
Use a full pre-upgrade profile backup when returning to 0.1.0, whose writer does
not retain these newer records.

Uninstallation removes installer-owned application files and empty directories,
not the separate profile or extra user-created files. The archives' hashes are
in SHA256SUMS.txt. Engineering binaries have no Authenticode signature.

Build dependencies and commands: docs/BUILD_WINDOWS.md. The original 0.1.0 source,
portable and installer archives remain unchanged for regression comparison.


## Update preflight (new engineering source, requires rebuild)

The rebuilt Windows installer creates a Start-menu shortcut to
`TuranStartup.exe`, a DLL-independent recovery launcher. The portable ZIP also
includes it. Launch `TuranStartup.exe` to check the signed transaction journal
before CEF is loaded. If recovery cannot authenticate, startup stops and the
transaction is preserved; do not bypass the check with TuranBrowser.exe.
The preflight does not enable production updates without NorthByte public-key
configuration and a verified server. Previous 0.2.0-dev executable packages
do not contain this new functionality until recompiled.
