# Roamux

A personal-use desktop browser for **macOS**, built as a **Chromium/C++ overlay** (`src/roamux` over
upstream Chromium). Apache-2.0. Four features + Brave-style profiles:

1. Configurable tab-strip position (top / bottom / left / right)
2. Per-tab pinned "initial URL" with dual reload (`Cmd+R` / `Ctrl+Cmd+R`)
3. Import from Microsoft Edge (macOS)
4. Tab visit-order navigation (append-only, per-profile, persisted)
5. Brave-style local profiles (name-only, no forced sign-in) + hidden optional sign-in
   · Sparkle auto-update

## Build

Roamux is a Chromium overlay — the Chromium source is **not** committed here; it's fetched with
`depot_tools`. See **[`BOOTSTRAP.md`](BOOTSTRAP.md)** for prerequisites (≥ ~150 GB free disk, Xcode) and
the fetch → overlay → `gn gen` → `autoninja` flow.

The full feature proposal & execution plan lives in the parent workspace at
`docs/discussion/2026-07-05-roamux-browser-features/` (see §7.9 governance, §11–§12 build/overlay, §14
issue breakdown). Work is tracked as GitHub issues **roam-1 … roam-40** (epics E0–E7).

## Layout

```
roamux/            the overlay (placed at src/roamux/ at build time)
  browser/         net-new browser code (tabs UI, tab-visit, importer, sign-in)
  common/          shared helpers (features, prefs)
  chromium_src/    upstream overrides (§12.2 — used sparingly)
  patches/         minimal upstream patches (§12.2)
  build/args/      GN arg files (reference.gn = unbranded default)
  app/             branding, packaging, Sparkle (M6)
  test/            fixtures + the E0 smoke test
.github/workflows/ CI (§12.6 tiers): per-PR lint+hermetic gate; capability-gated Chromium jobs (visible skips until roam-36); scheduled nightly; release posture bound to the protected `release` Environment
```
