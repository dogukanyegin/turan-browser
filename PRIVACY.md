# Implemented privacy behaviour

The native password vault stores encrypted profile-local credentials. Capturing
or filling a supported form requires an explicit native action and authentication;
private-form saving has separate consent. CEF/DOM must temporarily hold a filled
secret. No password sync or plaintext export is provided. Historical backups
retain older encrypted vaults; changing a master password does not revoke those
copies. See docs/PASSWORD_VAULT.md.

EasyList/EasyPrivacy subscriptions are optional and disabled by default. Native
consent explains the publisher URL/license and that WinHTTP uses Windows TLS
and direct DNS outside browser DoH/proxy controls. No browsing URLs, cookies or
credentials accompany those requests. Unsupported filter rules are skipped and
reported. This transport must not be presented as Tor-routed networking; Turan
has no independent Tor connection mode. See docs/FILTER_SUBSCRIPTIONS.md.

The updater has no provisioned publication address/trust key by default and
cannot install an unauthenticated package. Its native preparation/apply helper
does not upload profile contents. See docs/UPDATE_ARCHITECTURE.md.

The normal profile stores preferences, bookmarks, history and the normal-session
journal, permission decisions and normal download metadata locally. Download
records contain source URLs, filenames, paths, byte counts and completion times.
Portable data uses Profile beside the executable; installed
development data uses LocalAppData/TuranBrowserDevelopment/Profile; the original
0.1.0 profile remains at LocalAppData/TuranBrowser/Profile. HTTP(S) URLs in these files may
contain sensitive query strings. Application logs are local CEF warning logs.

Private tabs use an in-memory request context. Their navigation is excluded from
history and session journals; private cookies are not copied into the normal
context. Private download records and permission decisions are not persisted in
the normal profile. Explicitly saved bookmarks and downloaded files remain on disk. Private tabs
do not promise protection from websites, network observers or fingerprinting.

Searches entered in the omnibox are sent to DuckDuckGo. Selecting DoH sends DNS
queries to Cloudflare after restart through Chromium's secure DNS preferences.
Bootstrap resolution of the provider and other network channels are not a promise
of zero DNS disclosure; full packet-level leak testing remains pending.
These are optional/user-triggered network
services, not Turan telemetry. Normal browsing sends requests to visited sites.
Background networking/sync/component updates are disabled and a small set of
Google service hosts is blocked; this is not an exhaustive network audit.

Shield preserves its curated starter domains and supports explicit custom ABP
network/CSS rules. Per-page counters and at most 100 truncated request/rule
records per registered tab stay in memory and reset on navigation/close. A private
tab has separate counters and in-memory site exceptions; normal site exceptions
and custom rules (including a previous-rule backup) are saved in the normal
profile. The editor is disabled in private tabs. Cookie filtering uses a pinned
Public Suffix List, including private suffixes, for network send/save callbacks.
Shield site exceptions do not turn off cookie blocking. No filter-subscription
server is contacted by the application yet. Procedural/scriptlet rules, full ABP
compatibility, storage partitioning and fingerprinting reduction remain pending.
Strict CSP can prevent cosmetic styles. Permission requests ask locally
and unsupported types are denied. Normal-profile microphone, camera, location,
notification and clipboard decisions can be edited/reset. Private decisions are
held only for the private session, and are forgotten when its last tab closes.
Screen sharing requires a new decision per request. Engine permission scopes and
the OS's device access controls also apply.

Validated metadata backups and quarantined damaged files may retain older
history, bookmarks, downloads or permission records. Account for these files
when making a complete backup or manually removing a profile.
Complete engine-upgrade and manually requested snapshots also retain CEF cookies,
site databases, local storage and logs. Restore keeps the displaced current
profile under `Profile.before-restore-*`. Incomplete copies are retained on
failure. These local directories are not automatically deleted or uploaded;
include them in any later data deletion or backup retention decision. Backups
are byte copies with hash manifests, not a separate encrypted vault.

Chrome-style extension testing requires an explicitly supplied disposable
profile. It is not enabled for normal users. Extension permissions and data flows
must be reviewed before real third-party extensions are installed.

The explicit external Tor Browser action passes only the displayed HTTP(S) URL
after code-catalog validation. URLs can contain query tokens; the panel explains
this before Open. No Turan cookies, passwords or profile data are imported, and
Turan does not alter external settings/profile files. The external application
manages its own data/connection. Only normal launches save the chosen folder;
private launches do not save URL/folder choices. Code identity does not certify
user-modified external profiles/extensions. Only catalog 15.0.24 is supported;
later versions require renewal.

No Turan Tor connectivity/anonymity, password sync or remote backup is implemented or
claimed. No sensitive profile data is intentionally sent to NorthByte servers.
The privacy claims above cover implemented code, not yet a complete packet-level
audit of the upstream engine.
