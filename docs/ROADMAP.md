# Future work

The current shell is implemented and tested as described in README. Remaining:

- Full ABP syntax, procedural filters/scriptlets and publisher-authenticated list
  updates. Optional HTTPS EasyList/EasyPrivacy consent/cache/rollback/subscriptions
  are implemented and tested. The subset, native CSS cosmetics, custom editor, site exceptions
  and actual per-page request records now work. Strict CSP cosmetics remain limited.
- Full storage partitioning controls. PSL classification with private entries and
  actual sibling/private-suffix cookie boundaries are now tested.
- Physical permission-device/screen-sharing tests and notification delivery.
  Persistent origin/type policies and editable native site settings are tested.
- Broader form/automatic-save coverage and physical lock/suspend tests beyond
  the implemented DPAPI/AES-GCM vault and explicit HTTPS capture/fill workflow.
- Production update trust provisioning, scheduling, CEF-free startup recovery,
  new-engine health rollback and registration/retention. Signed native
  transactions, helper coordination and interrupted rollback fixtures now pass.
- Multi-window session ownership, tab overflow/search/suspension and richer
  keyboard/accessibility QA. Single-window recovery/reopen, native reordering,
  pin/mute/group metadata are implemented and integration-tested.
- Correct extension tab/window model, extension lifecycle/permissions and real
  MV3 extension acceptance. Embedded, command-driven and explicit tabs.create candidates fail window
  topology; the normal Alloy path is retained.
  The opt-in native normal-frame adapter now passes limited tab/resize/close,
  private-context and publisher popup fixtures. Stock controls, unreliable
  automatic engine activation, multiple-window ownership and complete management
  still prevent adopting it as the production architecture.
  The newer unmodified chrome-integrated frame fixes tested automatic focus and
  shared-tab identity and passes real unpacked lifecycle/native removal consent.
  Custom UI/session contracts, store/CRX/extension automatic updates and broad
  private behavior are still pending.
- Complete translations and new-tab/page theme control.
- Windows 10 hardware tests, physical media/location tests and packet-level DoH audit.
- Authenticode signatures for application and installer.
- Full profile-selection/guest UX, range-based data clearing and backup browsing.
  Closed-profile snapshots, verified retained-copy restore and real engine
  144 -> 154 -> restored 144 migration are implemented; Windows 10, very large
  profiles, cross-account portability and power-loss validation remain pending.
- Preserving named NTFS streams, EFS and custom ACLs in full-profile backups;
  the current bounded snapshot format refuses unsupported streams/encrypted
  files instead of losing their data or origin markings.

Continue using prebuilt CEF packages; do not restore the Chromium source build path.
