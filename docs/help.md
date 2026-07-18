<!-- SPDX-License-Identifier: Apache-2.0 -->
# Getting help with Roamux

Roamux is a Chromium-based browser for macOS, built as a small overlay
(`src/roamux`) over upstream Chromium. It adds tab-workflow features (vertical
tab strip placement, durable tab identity, settled-visit navigation), an Edge
profile importer, and Sparkle-based updates, while staying as close to stock
Chromium as possible.

This page is the in-product help target — the "Get help with Roamux" row on
`chrome://settings/help` lands here (roam-161).

## Reporting a problem

Open an issue: **https://github.com/xinquan568/roamux/issues**

Please include:

- the Roamux version from `chrome://settings/help` (both lines — the Roamux
  marketing version and the Chromium base), and
- for update problems, the dimmed grey detail line under the update status (it
  carries the raw Sparkle error while Roamux is in alpha — that line exists
  precisely to make your report actionable).

## Known limitations of the personal-alpha distribution

- **Unsigned:** builds are not Apple-signed or notarized (a deliberate
  personal-alpha decision). macOS Gatekeeper will warn on first launch;
  right-click → Open, or `xattr -dr com.apple.quarantine Roamux.app`.
- **Updates:** delivered via Sparkle from the project's appcast. Update
  packages are EdDSA-signed; there is no Mac App Store channel.
- **macOS only**, Apple silicon first.

## More

- [README](../README.md) — project overview and goals
- [CONTRIBUTING](../CONTRIBUTING.md) — building, the overlay/patch discipline,
  and the roam-N issue convention
