<!-- SPDX-License-Identifier: Apache-2.0 -->
# Vendored Sparkle (gitignored; fetched by pin)

`Sparkle.framework` + `bin/` release tooling land here via
`roamux/build/fetch_sparkle.py` — **pinned to Sparkle 2.9.4**, SHA-256
`ce89daf967db1e1893ed3ebd67575ed82d3902563e3191ca92aaec9164fbdef9`, verified
before extraction (plan §13.6/K4; R16 supply-chain: the pin is the trust
anchor). Everything in this directory except this README is **gitignored** —
run the fetch script after checkout (see BOOTSTRAP.md); flag-on GN builds
(`roamux_enable_sparkle=true`) fail loudly when the framework is absent.

Sparkle is MIT-style licensed — the upstream `LICENSE` is extracted alongside
the framework. Uprev = change the pin (version + hash) in `fetch_sparkle.py`,
re-run with `--force`, and re-validate the I-6.2 bundle smoke + fixture tests.
