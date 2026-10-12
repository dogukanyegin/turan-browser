# Password vault trust boundary

`password-vault.dat` is an additive profile-owned file. Existing browser metadata,
CEF cookies, bookmarks and profiles are not converted. The current-user Windows
DPAPI envelope wraps a bounded AES-256-GCM payload. PBKDF2-HMAC-SHA-256 uses a fresh
128-bit salt and 600,000 iterations; the master passphrase requires at least
15 Unicode scalar values. Metadata and individually authenticated password records
are encrypted. Random nonces come from Windows CNG. No encryption key is compiled
into the product.

The native manager saves/updates/searches/deletes credentials, generates unbiased
24-character passwords, locks the vault and changes the master passphrase.
Encrypted `.tvex` import/export uses a separate salted passphrase-derived key;
export never overwrites an existing file. Local DPAPI vault files require the
same Windows account/machine; a portable move to another machine requires explicit
encrypted export/import. No plaintext CSV export is offered.

Every save, fill, delete, import, export and passphrase change requests the current
master passphrase. Create/unlock and derivation run asynchronously. Five minutes
of inactivity, a Windows session-lock notification, suspend or manager destruction
locks the vault. Session-lock/suspend invalidates delayed authentication and pending
page actions. The manager owns no Windows account password and Windows Hello is
not implemented.

Page save/fill occurs only after an explicit native action on a completed main-frame
HTTPS page with a valid certificate and no mixed-content status. URL and completed
navigation timestamp are captured before asynchronous reauthentication. The
broker rechecks security and uses a freshly generated, isolated JavaScript world
with a system-unique execution-context identifier, preventing context-ID reuse
after renderer/document changes. The form must have one visible enabled password
input, one unambiguous username input and a form action on the exact HTTPS origin.
Registration forms, ambiguous forms and iframe-only forms are refused. Filling
never submits the form. Saving a private tab requires an explicit native choice
that states the destination is the normal profile's encrypted vault.

No page-to-native credential bridge is registered. Secrets are typed CDP arguments,
not JavaScript-source literals. Page actions are refused while protocol/network
logging is requested. Native secret allocations and our temporary copies are
zeroed and VirtualLock is best effort. CEF IPC, JavaScript/DOM and native masked
edit controls necessarily hold transient plaintext while a user explicitly saves
or fills. The target site's scripts can read filled fields; the vault is not a
protection against a compromised target site, malicious same-account debugger,
process dump or OS administrator.

Before publishing a new local vault, the previous encrypted file is copied to
`.bak`; write failures preserve the old file. Backups may retain an older master
passphrase after a passphrase change. Full profile backups likewise retain the
keys/passphrases applicable when made. This is an explicit recovery tradeoff,
not a claim that changing a passphrase revokes previously copied encrypted files.
Damaged vaults are preserved and never silently replaced. Import is authenticated,
bounded and all-or-nothing. The feature does not claim universal form compatibility,
cloud sync, Windows Hello or protection against every credential phishing scenario.
