<!-- SPDX-License-Identifier: Apache-2.0 -->
# Roamux — bootstrap & build (E0 foundation)

Roamux is a **Chromium/C++ overlay** — the code in this repo (`roamux/`) is layered onto an upstream
Chromium checkout; the Chromium source is **not** committed here (it's fetched with `depot_tools`).
See the execution plan §11–§12 (`docs/discussion/2026-07-05-roamux-browser-features/FINAL.html`).

## Prerequisites (build machine)

| Need | Why |
|---|---|
| **≥ ~150 GB free disk** | Chromium checkout (~40 GB) + build output (~60–100 GB). **This is the gating requirement.** |
| **Xcode (FULL — not just CLT) + Command Line Tools** | Chromium's macOS build runs `xcodebuild`, which the Command Line Tools alone do **not** provide (`gn gen` fails at `sdk_info.py` otherwise). Install full Xcode from the App Store or developer.apple.com, then: `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer && sudo xcodebuild -license accept && sudo xcodebuild -runFirstLaunch`. Verify: `xcodebuild -version`. |
| **Xcode Metal Toolchain** | A separate ~700 MB Xcode component; GPU-adjacent Chromium code compiles `.metal` shaders and fails without it (`cannot execute tool 'metal'`). Install: `xcodebuild -downloadComponent MetalToolchain` (no sudo). Discovered building the browser-prefs layer (roam-3). |
| **`depot_tools` on `PATH`** | `gclient`, `gn`, `autoninja`, `fetch`. Installed by the bootstrap step below. |
| **macOS on Apple silicon** | Target is macOS-only (universal2 later; §11.10). |

## 1. depot_tools (done once)

```sh
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git ~/depot_tools
echo 'export PATH="$HOME/depot_tools:$PATH"' >> ~/.zshrc && source ~/.zshrc
gclient --version    # sanity
```
> In this workspace, depot_tools was cloned to `./depot_tools` and added to `~/.zshrc`. `gn`/`autoninja`
> resolve to the copy Chromium vendors into `src/` after the first sync — until then `gn --version` on the
> bare `depot_tools` reports "not found", which is expected.

## 2. Fetch Chromium (heavy — ~40 GB, hours; run OUTSIDE this repo)

```sh
mkdir -p ~/chromium && cd ~/chromium
fetch --no-history chromium          # downloads the tree, runs the first gclient sync
cd src
git fetch --tags
git checkout <LATEST_STABLE_MILESTONE_TAG>   # Q(i4)-A: latest stable milestone; re-pin every cycle
gclient sync -D                      # sync deps to the pinned tag; vendors gn/ninja
```
Record the exact tag in `roamux/build/CHROMIUM_PIN` when you pick it.

## 3. Place the Roamux overlay into the Chromium tree + wire it into the build graph

The overlay is `codes/roamux/roamux/` → it must appear as `src/roamux/` in the checkout, **and** a root
target must reference it (GN only loads `//roamux/BUILD.gn` if something depends on it):
```sh
ln -s /abs/path/to/codes/roamux/roamux ~/chromium/src/roamux
# Apply ALL managed patches (0001 gn_all wiring, 0002 chromium_src redirect, ...) — idempotent, fail-loud:
python3 ~/chromium/src/roamux/build/apply_patches.py --chromium-src ~/chromium/src

# Roamux (roam-32): vendor the pinned Sparkle.framework (needed by roamux_enable_sparkle=true
# builds — the flag-on GN targets fail loudly without it; hash-verified, see plan §13.6/R16).
python3 ~/chromium/src/roamux/build/fetch_sparkle.py
```
(A symlink keeps the git repo as the source of truth; a `gclient` custom-solution/DEPS entry is the
alternative once the overlay stabilizes. The `patches/` entry becomes a `gclient` runhook later, §12.5.)

## 4. Configure + build (targeted — do NOT build `all`)

```sh
cd ~/chromium/src
mkdir -p out/Default && cp roamux/build/args/reference.gn out/Default/args.gn  # args.gn keeps comments/newlines
gn gen out/Default
autoninja -C out/Default roamux_unittests   # the E0 hello-world test target (roam-1); first build is ~hours
out/Default/roamux_unittests                # both RoamuxSmokeTest cases should pass
```
> Use `out/Default/args.gn` — **not** `--args="$(tr '\n' ' ' …)"`; collapsing the arg file to one line turns
> its first `#` comment into a comment that swallows the rest. Full Chromium builds are hours — always build
> the **touched target + its test target**, never `all` (CI trust tiers, §12.6). A `test()` target needs a
> linked `main` (`//base/test:run_all_unittests`) — already wired in `roamux/BUILD.gn`.

## Status of this repo (E0 foundation)

Bootstrap completed on the build machine:
- ✅ depot_tools installed + persisted; full Xcode 26.6 active.
- ✅ **Built green** — `roamux_unittests` compiles and both `RoamuxSmokeTest` cases pass (roam-1 acceptance met).
- ✅ Overlay symlinked into `src/roamux`; the `//roamux` build-graph wiring is
  `roamux/patches/0001-gn-all-add-roamux-targets.patch`.
- ✅ **Pinned to the latest stable milestone `149.0.7827.201` (M149)** per Q(i4)-A — see `roamux/build/CHROMIUM_PIN`.
  (First build was verified on tip 152.0.7936.0, then re-pinned to stable.)

Next: E0 roam-2..5, then E7 (governance), then the feature epics via `/issue2pr chain --label <epic>`.
