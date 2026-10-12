# Extension architecture experiments

The original Win32 shell embeds Alloy browsers. Production extension management
is absent. The isolated Chrome-style probe enables a trusted unpacked local MV3
fixture on the prebuilt CEF 154 engine while retaining its sandbox.

The fixture exercises specific runtime/storage/tabs/windows/scripting/contextMenus/
alarms/notifications/permissions/webNavigation/declarativeNetRequest operations
and service-worker execution. It checks that the DNR-blocked request never reaches
the local server. See TEST_REPORT.md for exact scope and failures.

Two independent CreateBrowser calls with the same Win32 parent produce two
Chromium Browser windows: chrome.windows sees one window per Turan tab. This
fails the one-native-window/multiple-tabs contract. The normal path remains Alloy
until a controlled architecture addresses this and preserves Turan data/branding.

An additional compiled candidate uses IDC_NEW_TAB and GetDefaultClient to manage
Chromium-created browsers. Both CURRENT_TAB and NEW_FOREGROUND_TAB dispositions
were tried. The actual extension report identifies the original embedded browser
as type "popup" and the new Chromium browser as type "normal"; the two remain
separate extension windows. This candidate also fails the topology acceptance
test and is available only through the disposable-profile chrome-tabs probe.
Evidence: artifacts/mv3-topology-chrome-tabs.json.

The third candidate, chrome-native-tabs, creates a normal Chromium window by
omitting the child/Views host. IDC_NEW_TAB adds a real tab to the same Browser.
OnAfterCreated may reuse the initial client, so callback routing now uses actual
CefBrowser identifiers. The real topology fixture passes with two tabs in one
normal extension window. **The engine's stock window is separate from the Turan
Win32 control window.** This earlier candidate performs no reparenting/cropping.
This validates the engine tab model, not integration with the custom Turan UI.
It still requires an explicit disposable profile and is not the normal path.

On that candidate, pinned publisher MV3 builds of uBlock Origin Lite and Dark
Reader pass actual network-blocking/theme/action-popup/disable/enable operations.
Bitwarden's consent/logged-out popup and disable/enable checks pass after first-run
initialization and explicit engine-window focus; vault use remains untested.
Initial popup failures are retained. See TEST_REPORT.md and artifacts/thirdparty-extensions.json. The test
extensions are not bundled with Turan, and no signed CRX/Web Store installation
or production permission workflow is claimed.

The matching CEF implementation converts Views-hosted TYPE_NORMAL browsers to
TYPE_POPUP to suppress standard controls:
[CEF 154 ChromeBrowserHostImpl::CreateBrowser](https://github.com/chromiumembedded/cef/blob/14c5a08/libcef/browser/chrome/chrome_browser_host_impl.cc).
This explains why a Views transition by itself is insufficient; a real tab-model
and UI integration contract still needs to be established.

A fourth real-engine experiment calls `chrome.tabs.create` from the MV3 worker with the embedded popup's explicit `windowId`. Chromium still creates a separate normal window. Both browsers register in the native shell and shutdown exits with code 0, but the one-window extension topology fails. This also rules out simply replacing IDC_NEW_TAB with the extension tab-creation API in this prebuilt engine. Run `python tests/extension_tab_model_cef.py`; its current exit code 1 records the unresolved acceptance failure in artifacts/extension-api-tab-model.json.

A fifth candidate, `chrome-reparent-tabs`, preserves that genuine normal frame
and attaches its HWND to the native Turan owner. The separate adapter validates
same process/UI thread and DPI context, removes popup/overlapped styles and uses
Win32 SetParent. This uses public Windows operations, but the Chromium frame
class and native ownership assumptions still require upstream compatibility QA.
[SetParent documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent)
describes the style and DPI requirements. No engine checkout or patched binary
is required for this experiment.

Actual engine tests pass one normal extension window/two Turan tabs, native
selection, extension-created tabs, extension selection reflected in Turan,
renderer resize/text input, native minimize/restore/fullscreen, shared-frame
single-tab close, private cookie/storage isolation, private-history exclusion
and exit. Text input is tested through the renderer's DevTools input interface;
physical keyboard/IME/accessibility are not certified by it. A synchronous
OnGotFocus->Select->SetFocus attempt hung tabs.update. Posting the native update
and avoiding engine selection in the callback fixed the reproduced hang.

The actual publisher packages also pass their specific blocking/theme/popup/
disable/enable fixtures on this adapter after native root/tab activation. These
tests still require genuine chrome.action.openPopup, not an extension-page tab.
However, windows.update({focused:true}) alone did not reliably activate the child
engine frame. This failure remains visible in the native-host report and earlier
publisher failure artifacts. Native activation is a tested narrower interaction,
not proof of full chrome.windows focus semantics. The upstream
[HWNDMessageHandler::IsActive](https://github.com/chromium/chromium/blob/154.0.8037.98/ui/views/win/hwnd_message_handler.cc)
compares the active HWND with its engine frame; matching native hierarchy alone
is insufficient.

**Stock engine controls remain visible.** Their replacement, activation/window
ownership semantics, real multi-window/private extension workflows, engine/native
reorder/pin/group synchronization, high DPI, accessibility, IME and lifecycle
management are open production requirements. This candidate is diagnostic only,
requires an explicit disposable profile, and is guarded by the build option
TURAN_ENABLE_NATIVE_CHROME_PROBE. The default runtime is unchanged.

Alternatives under evaluation:

1. Chrome-style CEF Views with a single real Chromium Browser/tab model and
   custom native controls. Must test control over tabs, popups, branding and
   profile callbacks; changing Views alone is not proof.
2. A narrowly scoped CEF engine patch exposing real Browser tab-strip attachment
   and management APIs, built on a suitable Windows CI host. This changes the
   prebuilt dependency, requires engine supply-chain maintenance and must not
   trigger a full Chromium checkout on this 8 GiB local computer.
3. A Chromium fork built remotely only if the CEF API boundary cannot meet the
   required browser contracts. This is a larger maintenance and infrastructure
   decision; no such checkout/build has been started locally.

CEF/Chromium open-source licensing must remain in every engine package. No Chrome
brand assets or proprietary services may be added by the Turan shell.

Primary references:

- [CEF runtime styles](https://github.com/chromiumembedded/cef/blob/master/include/internal/cef_types_runtime.h)
  describe the single Chrome BrowserView constraint in a Views window.
- [Runtime extension load API request](https://github.com/chromiumembedded/cef/issues/3877)
  shows that editing extension preferences alone is insufficient.
- [Chrome extension distribution documentation](https://developer.chrome.com/docs/extensions/how-to/distribute)
  restricts self-hosted Windows Chrome installation to enterprise policy and
  treats unpacked loading as a trusted-development workflow. These Chrome rules
  are not proof of CEF store compatibility. Turan direct Web Store install,
  CRX3 verification, updates and contractual compatibility remain unverified.

No bypass of CRX signatures, permissions or store restrictions is implemented.
Further API, account/vault and complete management workflows remain pending.

## New unmodified normal-frame candidate

`chrome-integrated` uses the prebuilt engine's real normal window as the only
visible browser frame. `src/ui/engine_window` attaches Turan icon/title and a
native system-menu command; it does not call SetParent or alter frame styles.
The existing hidden controller dispatches Turan's native panels. Actual tabs,
groups, reorder/pin/mute, cross-window moves and automatic extension window focus
pass `tests/integrated_chrome_cef.py` without QA foreground compensation.
This addresses the reparent experiment's native activation mismatch while
preserving Chromium's real TabStripModel and extension window identity.

The existing client registers engine-created tabs through GetDefaultClient and
defers focus callbacks to avoid reentrancy. An active browser's real frame owns
permission, vault, subscription and updater panels. The original start page is
used for native new-tab navigation. Extension management opens the genuine
privileged `chrome://extensions/` WebUI; external pages have no Turan bridge.
Unpacked folder loading is subject to the actual native picker and engine
permission checks. The lifecycle test is kept separate from publisher action
tests and does not pretend API uninstall without confirmation is native consent QA.

The default Alloy product is retained. Final custom toolbar integration,
engine/native session metadata, private extension scenarios, store/CRX3 trust,
automatic extension updates and broad compatibility are still required before
production acceptance. The new candidate does not require a full Chromium tree
or a rebuilt CEF DLL. No remote engine build has been started.
