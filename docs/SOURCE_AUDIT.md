# Turan Browser production engineering audit

Date: 2026-10-10. Baseline commit: `2a8f9b1`. The original 0.1.0 source,
portable and installer archives remain unchanged in `dist/`.

## Verified baseline and architecture

The existing implementation is preserved: C++17, Win32 child windows, CEF
bootstrap sandbox, one persistent request context and a separate memory-only
private context. The small CEF wrapper is compiled locally; Chromium is not.
`browser-state.txt` stores preferences, bookmarks and history. No schema migration
is needed for the engine upgrade. Existing caches must not be tested with an
older engine after upgrading; architecture probes use disposable profiles.

The source directory was accidentally ignored by Git. That ignore rule has been
corrected and all original source files committed before engineering changes.

| Area | Actual implementation at baseline | Production gap |
| --- | --- | --- |
| Tabs | Up to 32 Win32-hosted Alloy browsers, stable IDs | Windows, drag, pin, groups, mute, recovery, suspension absent |
| Navigation | HTTP(S), search, back/forward/reload, keyboard shortcuts | Find, autocomplete, save, zoom and fullscreen UI absent |
| History/bookmarks | Atomic text-file persistence, private history excluded | Timestamps, import/export, backup and migration absent |
| Downloads | Native save dialog, pause/resume/cancel/folder | Persistent download history, dangerous-download checking absent |
| Permissions | Explicit session prompts, default deny | Persistent origin/type controls and hardware tests absent |
| Private mode | In-memory context, shared between private tabs | No distinct private window or guest/profile selector |
| Shield | Third-party domain denylist, real request counter | ABP, cosmetics, subscriptions, exemptions, event reasons absent |
| Third-party cookies | Network access filter | Public suffix handling and script/storage policy incomplete |
| Extensions | Disabled; Alloy browser style | No extension manager or verified MV3 API support |
| Password manager | None | Entire vault/autofill/security workflow absent |
| Tor | None | Official-browser launch and isolated connectivity absent |
| Internal pages | Bundled file start page, native panels | Privileged `turan://` protocol and bridge absent |
| Updates | Engine pinned in dependency lock | No authenticated application updater or production endpoint |
| Sandbox/TLS | Required bootstrap; default certificate rejection | Isolation/leak/hostile download tests incomplete |
| Distribution | Real NSIS per-user setup and portable ZIP | Production signing, Windows 10 validation and stable gate absent |
| UI | Dark/light toolbar and partial Turkish labels | High DPI, accessibility, complete translation and modern panels absent |
| Performance | No comparative measurements | Must measure against immutable portable 0.1.0 archive |

## Immediate engineering decisions

1. Upgrade only the prebuilt minimal engine to the highest stable Chromium major
   in the official CEF index, pin its SHA-256 and rerun baseline tests.
2. Keep the normal Alloy path until an isolated Chrome-style probe establishes
   real MV3 service-worker, DOM injection, tab/window mapping and DNR enforcement.
   Do not equate removing `disable-extensions` with extension support.
3. Remove the explicit client-side phishing-disable switch. This does **not**
   establish a working reputation service. No effective phishing/Safe Browsing
   protection is claimed until its provider, data flows and tests are verified.
4. Keep profiles and the six-field `S` record intact. New records/modules must
   tolerate older readers, create backups where migrating, and exclude private
   sessions from persisted state.
5. No 1.0 Stable version or release artifacts until the full acceptance matrix
   passes. Known absent capabilities are release blockers, not successful tests.

## Primary architecture references

- [CEF architecture](https://github.com/chromiumembedded/cef/blob/master/docs/architecture.md):
  Chrome UI layer includes extensions and browser functionality; Alloy exposes
  additional embedder callbacks. Runtime style is per browser/window.
- [CEF runtime extension loading issue](https://github.com/chromiumembedded/cef/issues/3877):
  preference edits alone are not a supported runtime install mechanism.
- [Official binary index](https://cef-builds.spotifycdn.com/index.json): engine
  selection evidence saved as `artifacts/cef-index-selection.json`.

These describe upstream capabilities, not proof that the Turan shell exposes
them correctly. `tests/extensions_cef.py` tests the running sandboxed binary.
