<!-- SPDX-License-Identifier: Apache-2.0 -->
# 0001. Chromium overlay strategy

## Status
Accepted

## Context
Roamux is a small, feature-focused derivative of Chromium maintained by a solo developer. Forking
Chromium outright makes rebasing onto upstream security releases (every ~4 weeks) prohibitively costly.
We need our features to live alongside upstream with the smallest possible rebase surface (§12).

## Decision
We will build Roamux as an **overlay** on an unmodified upstream Chromium checkout: net-new code lives in
additive `//roamux` targets; upstream is touched only through a declared `chromium_src` include-redirect
override or a minimal, fail-loud `patches/` entry recorded in the §12.2 hook inventory — never edited in
place. A per-milestone re-pin (Q(i4)-A) tracks the latest stable Chromium.

## Consequences
- **Easier:** upstream security uprevs (re-apply a tiny patch set; run the override-staleness gate,
  roam-2); reasoning about "what is ours" (everything under `roamux/`).
- **Harder:** reaching in-function upstream hooks (needs a patch, which carries rebase cost); some
  behaviors need a full-file `chromium_src` copy that must be watched for upstream drift.
