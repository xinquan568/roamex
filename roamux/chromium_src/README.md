<!-- SPDX-License-Identifier: Apache-2.0 -->
# `roamux/chromium_src` — upstream overrides (roam-2 / plan §12.2)

Whole-file / header **overrides** that shadow an upstream Chromium file via an include-path redirect.
Mirror the upstream path exactly, e.g. `roamux/chromium_src/chrome/common/chrome_isolated_world_ids.h`
(the roam-2 sample, a full upstream copy + one inert marker).

## How the redirect works (v1)

`patches/0002-chromium-src-include-redirect.patch` prepends `//roamux/chromium_src` to GN's
`default_include_dirs`, ahead of the source root — first match wins, so any `#include "<path>"` of a
mirrored path resolves to our copy **tree-wide**. This serves:

- **full-file header replacement** (carry the upstream copy + our delta), and
- the **`#define`-redirect + subclass** trick at class/header boundaries.

**v1 boundary — not yet served:** replacing a `.cc` an upstream target already lists in its own
`sources` (needs a `redirect_cc`-style compiler launcher; first needed by E1's frame-view override) and
arbitrary method interception (never supported — that is what minimal `patches/` are for, §12.2).

## Staleness gate (§12.5)

Every override's *pristine* upstream content is fingerprinted in `../build/override_signatures.json`
(recorded via `git show <CHROMIUM_PIN>:<path>` — never the patched working tree):

```sh
python3 roamux/build/check_override_staleness.py --chromium-src ~/chromium/src            # gate
python3 roamux/build/check_override_staleness.py --chromium-src ~/chromium/src --update   # after a reviewed uprev
```

The gate **fails loudly** on any override whose upstream changed since recording, any override missing
from the manifest, or an unresolvable/UNPINNED pin (no HEAD fallback). On a milestone uprev: re-review
each flagged copy, refresh it, then `--update`.

**Use sparingly** — an override carries a copy of upstream (the highest rebase-tax surface). Prefer
additive `//roamux` code; record every override here AND in the §12.2 inventory.
