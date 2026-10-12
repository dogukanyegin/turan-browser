> **Provenance notice (0.9.0-beta.1 candidate):** The evidence below documents historical 0.2.0-dev engineering runs, not validation of the newly version-labelled Beta binary. New Beta Windows results must be produced and archived before publication.

# Production acceptance report — engineering in progress

Date: 2026-10-11. Target: Turan Browser 1.0 Stable. Current application:
0.2.0-dev, CEF 154.0.34 / Chromium 154.0.8037.98. Local OS: Windows 11 x64.
Windows 10 has not been independently tested. No Stable release is approved.

## Latest fast-track integration

The combined application builds with the pinned prebuilt CEF engine. All nine
CTest groups pass, including the new vault, subscription and updater groups.
Current engineering evidence is bound to a DLL hash in each JSON report; earlier
sections describe preserved historical experiments and failures, not a claim
that every artifact belongs to the latest binary.

| Added check | Verified scope |
| --- | --- |
| Integrated normal Chrome frame | One visible browser root and one genuine extension window with three tabs; reorder, pin, mute, colored groups, automatic windows.update focus, cross-window moves and final-window shutdown. No SetParent or native foreground workaround. Stock engine controls and hidden Turan controller remain an opt-in candidate; full custom UI and engine session metadata are pending |
| Password vault | Native create/save/reauthentication, explicit exact-HTTPS fill/capture/update, isolated input getter against page prototype poisoning, cross-origin/registration/ambiguous/iframe refusals, navigation and same-URL reload cancellation, handler lock/unlock, generator save/fill and absence of synthetic secrets in disk backups/logs pass on both runtimes. Windows Hello, physical OS suspend, automatic save prompts and universal form coverage are pending |
| Filter subscriptions | Native and live official HTTPS tests pass. Both CEF runtimes verify actual ad/tracker/custom requests and server negative controls, cosmetics, real counters, native rollback/disable, declined consent and restart persistence. Default-off valid caches make no subscription update request. The supported bounded syntax skips 3911 unsupported and 309 invalid rules across the downloaded official lists; this is not full ABP/scriptlet support |
| Updater | 17 native test groups, genuine RSA-PSS verification, fixed inventory, six real termination/rollback cases and TLS/HTTP refusals pass. Actual CEF native panel on both runtimes correctly disables unprovisioned actions; forced check fails closed, workers join and existing data remains. A blank caption found by QA was fixed and retested. NorthByte production trust provisioning, CEF-free cold-start preflight recovery, new-engine health rollback and registration refresh remain blockers |
| Extension management | Native Extensions menu opens engine WebUI; real folder selection installs an unpacked synthetic fixture, and API metadata/permissions are visible. Separate toolbar pin and incognito grant/revoke, disable/enable, unpacked version reload and actual native Views removal consent/readback pass. Combining incognito reload and pin in one QA API call caused a transient no-action error; separate controls resolve it. Removal requires consent even when another-extension QA requests showConfirmDialog:false; the test now invokes the real scoped dialog. No full store/distribution acceptance is recorded. Earlier picker/readiness/combined-call/consent failures are retained |

Evidence: `artifacts/fast-track-caption-build.log`,
`artifacts/integrated-chrome-cef.json`, `artifacts/password-vault-cef-default.json`,
`artifacts/password-vault-cef-integrated.json`,
`artifacts/subscriptions-cef-alloy.json`,
`artifacts/subscriptions-cef-chrome-integrated.json`,
`artifacts/update-native-integration.json`, `artifacts/update-panel-cef.json`,
and `artifacts/extensions-lifecycle-cef.json`.

Actual uBOL advertising block/server negative control, Dark Reader style changes
and all three publisher action popups/disable-enable tests pass in the integrated
candidate. Bitwarden verification covers first-run/logged-out UI; account vault
login/autofill remains untested. Chrome Web Store/CRX installation, extension
automatic updates and full private-extension behavior are not certified.
Independent Tor routing/leak/fail-closed mode is not implemented. No new
performance improvement is claimed without matched measurements. Win10 and
long-session/low-memory/accessibility acceptance remain pending. Stable remains
blocked even where new limited fixtures pass.

## Actual tests

| Test | Result and scope |
| --- | --- |
| Native filter CTest | PSL private/wildcard/exception/IDN boundaries, ABP anchors/types/party/domain/exceptions/badfilter/important, unsupported constraint rejection, malformed UTF-8, regex budget and cosmetic selection pass |
| Shield CEF fixture | Actual script/image/XHR blocks and server negative controls; scoped/exception/native :has/dynamic cosmetics; malicious CSS cannot request; native site toggle/restart, async editor rejection/backup/apply, subframe scope, sibling/private-suffix cookies and private events/exceptions pass |
| Core CTest | URL handling, UTF-8, history/bookmark persistence, private-history exclusion, Shield boundaries pass |
| Session CTest | Metadata, crash marker, backup fallback, unsafe URL, bounded size and unsupported schema rejection pass |
| Store CTest | Complete legacy parsing, newline/quoted titles, verified backup fallback, damaged-file preservation, oversize/unsafe records, opaque future records, download/privacy and permission rules, write-lock refusal, symbolic-link refusal and hard-link isolation pass |
| Profile CTest | Full ordinary-file/empty-directory snapshots, known SHA-256 vectors, exclusive/live/writer locks, byte/entry/depth limits, cancellation, tampered/traversal/duplicate/reparse/named-stream rejection, retained-copy restore, crash handoff states and engine downgrade guard pass |
| Profile migration fixture | Immutable 0.1.0 engine 144 creates persistent cookies/localStorage; engine 154 snapshots every old file before opening, retains data/bookmarks/history, and verified restore retains the displaced current profile. Reopening 144 from the restored snapshot retains cookie/storage. Busy/tampered/newer successful or attempted engine rejection, interrupted handoff and native close-and-backup action pass on Windows 11. A read-only successful marker leaves the newer pre-start attempt bound after CEF changes the cache |
| Real CEF fixture | Sandboxed rendering/JS, three normal/private/normal tabs, isolated cookies, real download bytes, actual request blocking pass |
| Real CEF session fixture | Clean restart, forced termination recovery, damaged journal fallback, closed-tab reopen, native drag reorder, pin/mute/group metadata, private-session exclusion, legacy data and metadata recovery pass; invalid primary+backup startup preserves both files |
| Download history restart | Actual normal and private download bytes pass. Restarted native manager restores the normal record and disables stale pause/cancel controls; private metadata is absent from normal state |
| Permission fixture | Native consent, profile restart persistence, block/reset and fake camera/microphone API access pass. Private decisions stay separate and are forgotten after last private tab; main/subframe navigation cancels pending media prompts. Captures remain live during observation and revoking microphone permission ends an existing stream. Notification consent/revocation, location denial and location/clipboard query-state allow/reset pass without reading actual position or clipboard |
| TLS fixture | Actual default engine rejection of a fresh local self-signed certificate with ERR_CERT_AUTHORITY_INVALID; HTTPS document never requested and subsequent HTTP navigation recovers. No trust-store changes; expiry/name-mismatch tests remain pending |
| DNS fixture | Global preference readback, real Cloudflare DoH responses, classic-DNS negative control and unavailable-provider failure without classic fallback pass for isolated random example.org names; full packet capture remains pending |
| Native UI fixture | Baseline panels and tab/private creation/closing pass; rerun by build tool |
| Navigation fixture | Actual renderer zoom/reset, two find matches/next result, fullscreen restoration, 31,221-byte CEF PDF and DevTools open/close/shutdown pass |
| MV3 probe | Worker, messaging, local storage, tab/window queries, DOM injection, context-menu create/remove, alarm create/query/clear, notification permission query, permission contains, frame enumeration and actual dynamic DNR blocking pass |
| MV3 native window topology | **FAIL**: two native tabs in one Turan window appear as two extension windows |
| Chromium new-tab command alternative | **FAIL**: GetDefaultClient + IDC_NEW_TAB still produces separate popup/normal extension windows; both tested command dispositions fail |
| Extension tabs.create alternative | **FAIL**: even with the popup window ID specified, the new tab is routed to a separate normal window. Native registration and clean exit pass; one-window topology fails |
| Normal Chromium window alternative | **PASS within probe**: two engine tabs share one normal extension window; the stock engine window is separate from the Turan control window |
| Native normal-frame adapter | **PASS limited lifecycle/identity fixture**: normal engine frame attached to Turan; native and extension activation agree, extension-created tab registered, resize/minimize/fullscreen and renderer input pass; closing one tab preserves shared frame. Private cookies/localStorage and private history remain separate. Stock controls remain visible; automatic window focus is unreliable and production architecture is not accepted |
| Diagnostic startup boundary | Five actual invalid/missing-profile diagnostic launches return 2 without creating a profile, lease or recovery file in an isolated portable runtime |
| uBlock Origin Lite 2026.1006.1931 | Advertising request blocked with no server request, then succeeds and reaches the server when disabled; actual action popup, disable/enable and clean shutdown pass in normal-window probe |
| Dark Reader 4.9.133 MV3 | Dynamic stylesheet changes white/black fixture to rgb(24,26,27)/rgb(232,230,227); disabling/reloading restores white/black. Action popup, disable/enable and shutdown pass in normal-window probe |
| Bitwarden 2026.9.3 MV3 | First-run consent, logged-out account UI, actual action popup on the explicitly focused engine window, disable/enable and shutdown pass. Vault login and autofill are not tested |
| Release gate | Deliberately assigning 1.0.0/stable with unresolved acceptance criteria is rejected |
| Packaging | 0.2.0-dev portable default profile/runtime, per-user development installer/runtime, uninstall registration cleanup and extra-user-file preservation pass on Windows 11; final repackaging is rechecked |

The MV3 probe validates specific operations, not every operation in those API
namespaces. Notification display, event delivery over long sessions, extension
installation/removal/update, incognito permission workflows and tab moves remain
unverified. Third-party checks use pinned publisher artifacts in disposable
profiles, not normal Turan profiles or Chrome Web Store installation. Loading
alone does not satisfy compatibility acceptance. Bitwarden's initial action-popup
failures were followed by a passing initialized/focused-window check; evidence is in
artifacts/thirdparty-extensions.json. Additional account/session extension testing
is pending.

The same publisher packages also pass their specific action popup, block/theme
and disable/enable cases on chrome-reparent-tabs after native root/tab activation.
That narrower interaction does **not** close the automatic chrome.windows focus
failure. Raw evidence is artifacts/thirdparty-extensions-chrome-reparent-tabs.json
and artifacts/native-chrome-host-cef.json; the latter explicitly records automatic
focus acceptance separately from its passing lifecycle/identity checks. Incognito
cookie/storage/history tests do not certify incognito extension permissions.

The PDF engine test selects its temporary destination through CefDialogHandler
only with an explicit profile, local 127.0.0.1 fixture and test flag. Normal
users receive CEF's asynchronous platform chooser. Attempts to automate the
native Save dialog with Win32 messages/UI Automation did not complete. This
separate interaction remains **unverified**, not passed by the callback fixture.
Evidence: artifacts/navigation-native-save-dialog-failure.json. Physical printer
output, print preview and independent PDF viewer interaction remain pending.

## Failures found and disposition

- The first native-host publisher run failed its focus-ready check despite real
  uBOL blocking and Dark Reader theming. A subsequent serial run reproduced the
  failure. Activating Turan's actual native root/tab permits genuine action popups;
  automatic windows.update focus remains unreliable. Initial evidence is retained
  in artifacts/thirdparty-extensions-reparent-initial-focus-failure.json.
- Extension-driven tab activation initially left Turan's selected row stale.
  Calling Select/SetFocus from OnGotFocus then caused a real DevTools timeout in
  tabs.update. Deferred native-row updates avoid re-entering Chromium's tab model;
  the bidirectional activation case now passes. Initial failures are retained in
  artifacts/native-chrome-host-initial-selection-failure.json and
  artifacts/native-chrome-host-reentrant-focus-failure.json.

- A real read-only engine-marker test reproduced 115 profile files after CEF
  startup while the marker still said engine 144 and no browser window opened.
  Recording `turan-engine-attempt.txt` before initialization now preserves the
  engine bound if startup or final marker commit fails. Successful commit clears
  it; malformed/newer attempts stop startup. Initial evidence is retained in
  artifacts/engine-readonly-marker-failure.json and the fixed scenario is in
  artifacts/profile-migration-cef.json.

- Initial restore failed because Windows refused renaming a directory containing
  an open lease file. The lease now sits beside the profile and remains open
  across directory moves; the restore and crash-state tests pass.
- The first native close-and-backup attempt failed copying a still-open profile
  file after CefShutdown. The utility now runs in a separate engine-free process,
  acknowledges the browser handle before teardown and waits for successful
  process exit. The initial helper race queried an exited process image and
  terminated without a result dialog; a readiness event removes this race.
  The actual menu path now passes, including all file hashes. Initial failure
  evidence is retained in artifacts/profile-migration-native-backup-initial-failure.json.

- A full-build publisher check again failed Bitwarden action-popup startup. Its onboarding DOM was empty before render, and a following attempt accessed a null document body. The fixture now waits for a real onboarding/login screen and focused engine window instead of fixed sleeps; it closes the onboarding tab and still requires a genuine action popup. Failure evidence is preserved in artifacts/thirdparty-extensions-full-build-popup-failure.json. A tab fallback is never accepted as an action-popup pass.
- The private fake-microphone fixture repeatedly failed with `AbortError: Failed due to shutdown`. Engine logs showed an accepted/opened stream being cancelled. Turan's media decision was not synchronized with Chromium's content setting; Chromium subscribes captures to permission-result changes. Applying microphone/camera decisions to the actual browser request context before continuing consent fixed the reproduced case. Subsequent tests check a live capture, revocation ending the stream, repeated private capture, restart and main/subframe prompt cancellation. Original failure evidence remains in artifacts/permissions-private-abort-failure.json. Physical-device and prolonged WebRTC tests are still pending. The relevant upstream code is [MediaStreamManager::PermissionChangedCallback](https://raw.githubusercontent.com/chromium/chromium/154.0.8037.98/content/browser/renderer_host/media/media_stream_manager.cc).
- The profile-corruption fixture initially assumed an IDOK button on a native message box; Windows exposed IDCANCEL. A failed launch also left its owned process alive and caused a later DLL link failure. The fixture now records the launched process before waiting for a window and closes only its owned native dialog/process tree. These were test-control failures; the corrupt profile files remained intact.
- The first restart permission check read a DevTools target before its document had committed. It now waits for the renderer's actual location and ready state.
- DNS fixture classification initially mistook generic query events for classic transport and failed-attempt response events for parsed DNS replies. Checking Chromium's DNS transaction source corrected the assertions to distinct transport events and response `rcode` data. The original classification failure artifacts are retained; they were test-analysis errors, not evidence of a proven network fix.
- Initial Chrome-style shutdown exited with 0xc0000005. Chrome can synchronously
  call OnBeforeClose while CloseBrowser executes; it invalidated the tab vector
  being iterated. A browser-reference snapshot and deferred final native teardown
  fixed the crash. The subsequent single- and two-tab probes exited with code 0.
  Original failure evidence is retained in artifacts/mv3-probe-initial-shutdown-failure.json.
- Extension window topology remains a production architecture blocker. Simply
  enabling extensions or changing the browser style does not fix it.
  The additional command-driven candidate also failed; its real result is
  artifacts/mv3-topology-chrome-tabs.json. It is not enabled for normal profiles.
- Omitting the embedded child host permits a real normal Chromium tab model.
  Browser callbacks can reuse a CefClient, so events are now routed by actual
  CefBrowser identifier. The normal-window probe passes the topology fixture.
  It leaves standard Chromium UI in a separate window and is not the custom
  Turan product architecture. A supported UI/tab integration contract is still
  required; this result narrows the problem rather than proving CEF incapable.
- An early third-party harness treated a popup loaded as an ordinary extension
  tab as an actual action popup. uBOL correctly returned "not a website" there.
  The corrected test uses chrome.action.openPopup. UTF-8 subprocess decoding
  also fixes a Turkish Windows decoding failure. Bitwarden requires completion of
  first-run consent before popup use. The harness now explicitly focuses the
  actual engine window, passes its window ID and recognizes routed popup URLs.
  Original failures remain retained; a page fallback alone does not count as success.
- File export/download selection now uses CEF's asynchronous native chooser
  instead of a blocking GetSaveFileName call on the browser UI thread.
- The optional native UI test screenshot used desktop capture and could capture
  another foreground app. Desktop screenshot capture was removed from the
  engineering test; no such image is used as verification evidence.
- The first engineering installer test refused to overwrite existing 0.1.0
  Start-menu shortcuts. Development installation, registration and default
  installed profile now use a separate namespace. Original registration and
  profile remain untouched. The development installer/portable test now passes
  on Windows 11; Windows 10 and pre-existing-profile upgrade tests remain pending.
- The first session unit-test cleanup assertion compared equivalent paths with
  differing trailing separators. The safety check now uses filesystem
  equivalence and passes.
- The first cross-process drag test attempted TCM_GETITEMRECT with a process-local
  pointer. Windows does not marshal this control message. The corrected native
  mouse test uses fixed-width tab coordinates and validates persisted ordering.
- Repeated testing found a creation-time mute race: a command before OnAfterCreated
  was ignored. Desired mute state is now retained and applied when CEF is ready.
- Profile cleanup could race with CEF child termination. Session tests now retain
  process handles and verify descendant exit before deleting their disposable
  profiles. An initial process-tree benchmark also included an unrelated compiler
  helper through a reused historical parent PID; executable and creation-time
  checks corrected the instrumentation. No unrelated process was terminated.

## Security and outstanding acceptance

No sandbox-disable flag was used. Default TLS rejection remains in place. Unsafe
session navigation schemes and oversized/schema-mismatched journals are rejected.
The explicit phishing-disable switch was removed, but functioning reputation
protection is not established or advertised.

Broad DNS/WebRTC/IPv6 leak capture, Tor routing/fail-closed tests, hostile download tests,
extension permission boundaries, privileged internal pages, full profile
isolation, large-profile/cross-account/power-loss backup validation, named-stream/EFS/custom-ACL backup support, physical media/screen capture, DPAPI vault/authentication, signed updates/rollback, Windows 10 and
long-duration/low-memory/high-DPI/accessibility tests remain pending.

Full ABP/procedural/subscription Shield, multi-window support, full profile management,
password/autofill, Tor features, internal pages and authenticated updater remain
unimplemented production requirements. These have not been silently removed from
scope. Machine-readable blockers are in release-acceptance.json.

## Performance

The 0.1.0 archives are the immutable baseline. tests/performance_cef.py alternates
two baseline/current runs, with fresh profiles, local static pages, 2 seconds of
settling and 5 seconds of CPU/memory sampling. All sampled processes were readable.
The verified build generates `PERFORMANCE_REPORT.md` from the measured DLL's
paired samples. It is bundled with portable/installer distributions, copied to
dist/ and included as verification/PERFORMANCE_REPORT.md in the source archive.
Exact paired samples and DLL fingerprints are in build-manifest.json under
verification.performance_comparison. A different DLL forces a fresh benchmark;
measurements from an earlier binary are not presented as the packaged result.

The host has 12 logical processors; divide one-core CPU percentages by 12 for
machine-normalized values. Summed working sets count shared pages repeatedly and
are not physical RAM consumption. Baseline files are freshly extracted while the
current app path has been used by tests; OS cache/scanning and engine/shell
changes are confounded. Prior startup medians included 0.99 versus 3.75 seconds,
1.25 versus 1.36 seconds and 1.22 versus 3.95 seconds. The 7.172-second current
first launch remains unexplained. Later pairs were smaller, which does not prove
a startup defect has been fixed. This does not establish a general browsing-speed
improvement. Realistic-site, prolonged and low-memory testing remains required.
Raw results are in artifacts/performance-comparison.json; initial unsettled
samples are retained in artifacts/performance-initial-no-settle.json.

Local raw evidence is saved in artifacts/. Selected non-sensitive verification results are embedded in build-manifest.json; raw network logs and popup/font text are not bundled. Binary/source fingerprints are in
the verified build's build-manifest.json and the source archive's verification/
directory. A successful engineering build does not
approve the full production release.

The Shield fixture explicitly verifies a remaining cosmetic limitation: a page
with style-src none prevents injected cosmetic styles. Its CSP is not disabled.
The event panel counts actual canceled resource callbacks, not DOM elements or
assumed advertisements. Curated source categories are explicit; user rules count
under Custom. Runtime logs are capped/truncated and never written to the profile.
The source does not claim full EasyList syntax from successful corpus compilation.

Pinned official corpus (easylist/easylist gh-pages commit
a494fcbca03b60c89ccd0c3e989bc9a1d38e93f0) accepts 109,006 network and 24,072
cosmetic candidates; 3,906 unsupported and 309 invalid rules are not applied.
The first compile exceeded the fixed 8-million-instruction budget (retained in
artifacts/filter-corpus-initial-budget-failure.json). Using byte matching for
ASCII rules over CEF-serialized URLs removes unnecessary Unicode expansion;
explicit regexes/Unicode literals retain UTF-8 matching and the budget remains.
A subsequent compile passed in 5.24 seconds; 1,000 synthetic requests took a
96.3-microsecond median / 103-microsecond p95 and produced exactly 500 blocks
with 500 negative controls. These measurements describe the filter QA process,
not overall browser speed or complete EasyList compatibility. Fresh packaged-build
measurements, memory peaks, source hashes and representative rejected-rule
diagnostics are embedded under verification.filter_corpus in build-manifest.json.
The lists themselves are QA-only cache inputs, never packaged or activated.

The first complete Shield regression run failed the native Chrome probe's private
page navigation. The test entered the omnibox before asynchronous CefBrowser
creation enabled it; the instrumented reproduction records
privateEditEnabledBeforeWait=false. Waiting for the actual native control to be
ready passes the private cookie/storage/history checks. The original failure is
retained in artifacts/native-chrome-host-shield-regression-failure.json; this
fixes test readiness, not the unresolved production extension architecture.

Shield rejects HTTP(S)/WS subresources whose URL or document exceeds its 64 KiB
input budget with an explicit Custom/Turan-limits reason, rather than silently
bypassing filtering. Site exceptions remain the user's override. Navigation
generations keep late callbacks from adding old requests to a new page's event
list. These cases are verified in native filter/model tests.

The oversized exception-URL case also passes in real CEF: a normally exempt
/allowed request with a 64 KiB query is canceled before it reaches the fixture
server, and the native event row reports [URL budget]. The pinned PSL source has
explicit LF checkout attributes so Windows Git cannot silently alter its locked
SHA-256. Dependency integrity checks run before CMake.

Waiting for an enabled private omnibox alone reproduced the navigation failure.
The native edit was unfocused, so initial address events could overwrite injected
WM_SETTEXT data before Enter. The test now performs a real native edit click
before entry, and three successive fresh-process runs pass private navigation and
actual cookie/storage/history isolation. The second failure is preserved in
artifacts/native-chrome-host-readiness-wait-failure.json; raw repeat evidence is
artifacts/native-host-focused-entry-{0,1,2}.json. This does not resolve automatic
Chrome window focus or certify full private extension permissions.

A later full run reproduced the same unfocused native-entry race in the session
fixture before /second reached its journal. The failure log is preserved at
artifacts/session-unfocused-navigation-failure.log. A common native omnibox
helper now waits for readiness and clicks the edit before entry; session,
permission, extension-topology and paired-performance fixtures use it. Three
successive session runs pass, including actual private-server navigation and
its native title before asserting metadata exclusion. No retries conceal failed
product assertions or manufacture journal contents.

The common-helper integration initially omitted its import in the permission
fixture (artifacts/permissions-helper-import-failure.json). The import is corrected;
the isolated permission fixture and Chrome reparent topology fixture pass with
the actual native-focus helper. This was a harness error, not a browser permission
failure; it was retained and corrected before rebuilding the distribution.

The external Tor Browser integration passes six CTest groups and real native CEF
controls. The official 15.0.24 package's OpenPGP signature and signed SHA-256 were
verified against the published primary key and current subkey. Its Firefox binary
is unsigned; Turan embeds a 172-file authenticated code catalog, not a filename
heuristic. Missing/internal/credential URLs, same-size code tampering and added
loadable DLLs are rejected. Actual normal/private Tor Browser windows and the
synthetic URL arguments pass; no profile flags/data import are used. Private
URLs/folder choices stay out of Turan metadata, and legacy bookmarks remain.
artifacts/tor-browser-full-cef.json records the exact DLL and per-case results.
This does not validate bootstrap, routing, DNS/STUN/IPv6 leaks or anonymity.
Different/updated external Tor versions require an authenticated catalog renewal.

Initial launcher QA failures are retained: tor-panel-stale-object-failure.json
(a source edit overlapped an incremental compilation; the object was rebuilt),
tor-fixture-home-presentation-failure.json (the native new-tab bar intentionally
shows an empty string rather than its file URL), tor-fixture-child-discovery-failure.json
and tor-fixture-launcher-handoff-failure.json (Firefox's Windows launcher handed
off to a new browser PID). The corrected harness checks the exact executable and
fresh process creation time, observes actual MozillaWindowClass windows and
cleans only its owned QA processes. Failed fixture directories/logs are retained;
cleanup no longer masks the original assertion. No Tor network success is inferred.

## New source-only engineering changes — pending Windows binary QA

New native `TuranEngineTools` collapsible dock, release scope and publisher
staging tool were added after the prior Windows build. Static source tests and
synthetic offline RSA-PSS tests passed in the development container. No C++
Windows build or CEF GUI tests of these newly changed sources are claimed.
The release remains 0.2.0-dev. Do not reuse the previous DLL fingerprints as
proof for the newly changed binary.
