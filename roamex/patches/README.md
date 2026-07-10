<!-- SPDX-License-Identifier: Apache-2.0 -->
# `roamex/patches` — minimal upstream patches (roam-2 / plan §12.2)

In-function hook points and generated-table edits that **neither** an additive `//roamex` file **nor** a
`chromium_src` override can reach — e.g. a one-line `ROAMEX_*` macro insert, an `IDC_*` command-id enum
entry, a macOS accelerator table row, `RegisterProfilePrefs`, or a `BrowsingDataRemover` registration.

## The runhook (roam-2)

```sh
python3 roamex/build/apply_patches.py --chromium-src ~/chromium/src            # apply (idempotent)
python3 roamex/build/apply_patches.py --chromium-src ~/chromium/src --check   # verify, no mutation
```

Patches apply in **name order** (`NNNN-slug.patch`, `git diff` format). Already-applied patches are
skipped (`[applied]`); one that neither is applied nor applies cleanly **fails loudly naming the patch**
(§12.5 — the rebase signal). §12.4's target state invokes this at `gclient sync` time; until then it
runs manually / from the build gate.

## Current inventory

| Patch | Kind | What it does |
|---|---|---|
| `0001-gn-all-add-roamex-targets.patch` | **persistent** | wires `//roamex` + the `//roamex:roamex_tests` group into `gn_all` (§12.4) — new test targets join the group, never this patch |
| `0002-chromium-src-include-redirect.patch` | **persistent** | enables `chromium_src` shadowing (one `include_dirs` line, §12.2 mechanism 2) |
| `0003-sample-marker.patch` | **sample** | one-line inert marker proving the runhook end-to-end (roam-2 test) |
| `0004-register-profile-prefs.patch` | **persistent** | the §12.2 registrar hook (roam-3): `roamex::prefs::RegisterProfilePrefs` call + include in `browser_prefs.cc`, plus the `//roamex/common` dep edge on its owning GN target |
| `0005-tab-menu-tab-strip-position.patch` | **persistent** | the §12.2 tab-strip context-menu hook (roam-6): flag-gated "Tab strip position (Roamex)" submenu (member + `Build()` call in `TabMenuModel`) plus the `//roamex/browser/ui/tabs` dep edge |
| `0006-settings-appearance-tab-strip-position.patch` | **persistent** | the settings WebUI insertion (roam-6; **maintainer-authorized §12.2 mechanism revision**, see issue #6 — at pin M149 the `chromium_src` include-redirect cannot reach `build_webui()` resource lists, so the insertion lands as this minimal patch): Appearance-page row + `settings_private` allowlist entry + `roamexTabStripPositionEnabled` loadTimeData, plus the `//roamex/common` dep edge on `//chrome/browser/ui` |
| `0007-browser-layout-tab-strip-placement.patch` | **persistent** | the §12.2 tab-strip-region placement hook (roam-7), at the pin's reworked coordinates (`BrowserViewTabbedLayoutImpl::CalculateProposedLayout`, not the plan's pre-rework `BrowserViewLayout::Layout()`): flag-gated bottom-band docking via `roamex::ComputeBottomStripLayout`, the `BrowserView` live-switch observer member + instantiation, and the `//roamex/browser/ui/tabs` dep edge on `//chrome/browser/ui`. Upstream vertical tabs win structurally (coexistence rule D1, tested) |
| `0008-vertical-tab-strip-roamex-placement.patch` | **persistent** | vertical-strip reuse (roam-8; **maintainer-authorized issue-surface revision**, see issue #8 — upstream M149 ships the vertical strip, so Roamex maps placement left/right onto it instead of building `RoamexVerticalTabStrip`): controller creation-gate OR (`roamex::kTabStripPosition`), display-gate OR (`ShouldDisplayVerticalTabsForPlacement`), placement-pref observer with a display-flip guard (no spurious strip re-init), right-dock bounds mirror in the tabbed layout, plus `//roamex/common` dep edges. Roamex reads upstream vertical-tabs state, never writes it. **Amended by roam-9**: the roamex-driven dock side is physical/RTL-invariant (ProposedLayout coordinates are logical, so the side flips under RTL) |
| `0009-tab-uid-hooks.patch` | **persistent** | the durable-tab-uid hooks (roam-10, the §12.2 session-state row at pin coordinates): per-tab helper creation in `TabFeatures::Init`; restore hand-off + closed-tab capture beside the glic extra-data precedents (`browser_tabrestore.cc`, `browser_live_tab_context.cc`); `TabUidServiceFactory` registration in `EnsureBrowserContextKeyedServiceFactoriesBuilt`; `sources +=`/dep edge on the tab target (chrome-facing helper/factory are roamex-owned files compiled into the upstream target — no GN cycle). **Amended by roam-11**: the same attach/discard hunks also create/transfer the initial-url capture helper, and the sources list gains it |
| `0010-reload-initial-url-command.patch` | **persistent** | the §12.2 command rows (roam-12): `IDC_RELOAD_INITIAL_URL` (34059), `BrowserCommandController` execute + enabled-state (init + tab-state refresh), the macOS `Ctrl+Cmd+R` accelerator row (collision audit 2026-07-08: no Ctrl+Cmd `VKEY_R` conflict — re-check on uprev), and the `//chrome/browser/ui` `sources +=` for the additive command pair |
| `0011-shared-shortcut-settings.patch` | **persistent** | the §4.3 shared shortcut surface (roam-13): the mac key-event dispatch hook in `CommandForKeyEvent` (pref-overridable Roamex bindings — this is where roam-12's default chord actually dispatches), the settings handler registration (`RoamexShortcutsHandler`, the §12.2-declared additive handler), the `roamexShortcutsEnabled` loadTimeData + Appearance-page panel/recorder (roam-6 mechanism), and the `chrome/browser` mac `sources +=` for the roamex-owned registry-mac/handler files |
| `0009-tab-uid-hooks.patch` | *(amended, roam-14)* | sibling `TabInitialUrlHelper::PopulateExtraData` (close-time) and `SetPendingRestoredInitialUrl` (AddRestoredTab: reopen AND session restore) calls beside the uid ones — the initial URL rides roam-10's extra-data channels |
| `0012-initial-url-edit-ui.patch` | **persistent** | the §4.5 edit surface (roam-14): the `DuplicateTabAt` inherit-value+lock hook (§4.2), the tab-menu `MaybeAppendInitialUrlSubMenu` (own delegate; "Edit initial URL…" dialog + "Set initial URL to current page"), and the `//chrome/browser/ui/tabs` `sources +=` for the menu/dialog files |

| `0013-edge-chromium-importer.patch` | **persistent** | the ImporterList family (roam-15): `TYPE_EDGE_CHROMIUM` + `VISIT_SOURCE_EDGE_IMPORTED`, the IPC ImporterType range raise (both maxes), the `#if IS_MAC` `CreateImporterByType` case, the flag-gated macOS `DetectRoamexEdgeProfiles` (calls the pure `roamex::DetectEdgeSourceProfile`), the `in_process_importer_bridge` VisitSource map, the UMA bucket + `enums.xml` value 8, the `sql/histograms.xml` DatabaseTag, and the `chrome/utility` + `chrome/browser/importer` + `chrome/test` BUILD wiring |

| `0014-edge-secrets-keychain-oscrypt.patch` | **persistent** | the §5.2 secrets surface (roam-16): a `roamex::MakeCryptoPassKey()` friend in `crypto/subtle_passkey.h` (for the Edge-key PBKDF2), `ProfileWriter::AddCookies` (plaintext CanonicalCookies → CookieManager, re-encrypting under Roamex), and the test BUILD wiring (`chrome/browser/importer` secret-stage source_set + its `chrome/test` registration) |
| `0015-tab-visit-journal-factory.patch` | **persistent** | the E4 settled-visit journal (roam-21): `SettledVisitJournalFactory` registration in `EnsureBrowserContextKeyedServiceFactoriesBuilt`; `sources +=`/dep edge on the tab target (the chrome-facing factory is a roamex-owned file compiled into the upstream target — no GN cycle; the pure service + SQLite store live in `//roamex/browser/tab_visit`); and the `sql/histograms.xml` `RoamexSettledVisitJournal` DatabaseTag variant for the store |
| `0022-brave-style-profiles-name-only-creation.patch` | **persistent** | the E5 name-only creation hook (roam-29): `profile_picker_ui.cc` `AddFlags` wraps `signInProfileCreationFlowSupported` in `roamex::profiles::AllowSigninProfileCreationStep(...)` (flag-on → false → the picker WebUI's `computeStep` resolves NEW_PROFILE straight to the local name+avatar/theme path; flag-off → pure pass-through), plus the `//roamex/browser/profiles` dep edge on `source_set("profile_impl")` |
| `0023-signin-surface-suppression-on-next-startup.patch` | **persistent** | the E5 default surface suppression (roam-30): the AccountConsistencyModeManager startup derivation reads `roamex::signin::IsSigninAllowedOnNextStartup(prefs)` in place of the raw `kSigninAllowedOnNextStartup` read (flag on → `roamex.signin.optional_entry_point_enabled` AND upstream, default off ⇒ `kSigninAllowed` derives false and every §7.3 surface suppresses; flag off → pure pass-through), plus the `//roamex/browser/signin` dep edge on `source_set("impl")` |

Each patch is **tiny, reviewed, and fails loudly on rebase**. Keep the surface minimal — it is the
rebase-cost surface tracked by §7.6/§12.5; the authoritative hook inventory is plan **§12.2**.
