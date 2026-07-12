<!-- SPDX-License-Identifier: Apache-2.0 -->
# Roamux mac app icon — the bold "X" (roam-103)

The Roamux application icon: a four-color "X" of golden-ratio-widened bars (stroke-width
90.608 = 1.618 × 56) around a white hub. Designed in the project workspace (`docs/icons/`,
`README-bold.md` there documents the geometry and verification); this directory vendors the
payloads the build consumes, so the repo is self-sufficient.

## Files

| File | Consumed by | Notes |
|---|---|---|
| `app.icns` | `chrome_app_icon` bundle_data (patch 0029) → `Contents/Resources/app.icns` | full iconset 16→1024 incl @2x, packed with `iconutil`; pairs with `CFBundleIconFile=app.icns` |
| `Assets.car` | `chrome_asset_catalog` bundle_data (patch 0029) → `Contents/Resources/Assets.car` | compiled asset catalog; pairs with `CFBundleIconName=AppIcon` — **this is the payload modern macOS actually displays** |
| `Assets.xcassets/` | source for `Assets.car` | `AppIcon.appiconset` mirrors upstream's 8-entry shape (16/32/128/256 pt @1x+2x) |
| `icon-bold.svg` | — | the design source of truth |

Upstream ships the icon through BOTH channels (`chrome/app/theme/chromium/mac/`), and the
`CFBundleIconName`/`Assets.car` channel wins on modern macOS — replacing `app.icns` alone changes
nothing visible. Patch `0029-mac-app-icon-roamux.patch` rewires both `bundle_data` targets here.
The same targets also feed the Alerts helper app (its Info.plist carries the same two keys), so
the helper is branded too — intended.

Known, accepted trade-off: upstream's checked-in `Assets.car` additionally carries compiled
**Icon Composer** layers (macOS 26 adaptive/dark/tinted treatment). No Icon Composer source exists
for the bold X, so this catalog ships classic imagery only and macOS 26 applies its generated
treatment. If that ever disappoints, author an `.icon` source as a follow-up.

## Regeneration (design change only)

1. Edit the SVG master (workspace `docs/icons/icon-bold.svg`; keep this copy in sync) and re-export
   the PNGs (`docs/icons/build-bold.sh`).
2. `app.icns`: rebuild via `docs/icons/build-bold.sh` (iconutil path) and copy in.
3. Refresh `Assets.xcassets/AppIcon.appiconset/appicon_{16,32,64,128,256,512}.png`, then compile:

   ```sh
   cd roamux/app/resources/icons/mac
   xcrun actool Assets.xcassets --compile /tmp/car-out --platform macosx \
     --minimum-deployment-target 12.0 --app-icon AppIcon \
     --output-partial-info-plist /tmp/car-out/partial.plist
   cp /tmp/car-out/Assets.car Assets.car
   ```

   `--minimum-deployment-target 12.0` = the pin's `mac_deployment_target`
   (`build/config/mac/mac_sdk.gni`); re-check at uprev. The partial plist is discarded — upstream's
   Info.plist already sets `CFBundleIconName=AppIcon`. Last compiled with Xcode 26.6 (17F113);
   `actool` output is not byte-stable across Xcode versions, which is fine — the checked-in file is
   the contract (`roamux/build/check_app_icon.py` compares the built bundle against it byte-wise).
