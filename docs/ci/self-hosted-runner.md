<!-- SPDX-License-Identifier: Apache-2.0 -->
# Self-hosted builder — tier-2 warm-cache (roam-36, plan §12.6, personal-machine v1)

The tier-2 fast path: trusted jobs build Chromium **incrementally from a warm base in minutes** on a
self-hosted Apple-silicon runner. When absent (runner gone AND `vars.ROAMEX_CI_CHROMIUM_RUNNER`
unset), CI degrades to roam-5's visible skips — never red.

## v1 posture vs full §12.6 — read this first

This is the **personal-machine v1** (Q(i5)-B: user-provisioned, the operator's own Mac). Delivered:
a standing labeled runner (`self-hosted, macos, chromium-builder`); the pinned checkout
(`~/chromium/src` per `CHROMIUM_PIN`) as the shared warm base; a CI-owned build dir (`out/CI`,
APFS-cloned copy-on-write from the operator's `out/Default`); **declared-channel** base access (the
job touches the base only via the overlay symlink — restored by an EXIT trap — and the idempotent
fail-loud runhook; structurally test-enforced by `test_tier2_job.py`).

**Not delivered (the upgrade path, in order):** a dedicated low-privilege runner user (clean HOME —
no keychain/SSH/`gh` tokens); filesystem-enforced read-only base; JIT/ephemeral runners; per-job VM
snapshots (`tart`/Anka). Adopt these the moment this stops being a single-operator machine.

## Security model (single-operator repo)

- **Host exposure, owned:** CI jobs run as the runner's login user — anything that user can read
  (keychain, SSH keys, `gh` tokens, dotfiles) is reachable by CI-executed code. v1's accepted-risk
  argument: the **trust predicate** below means only code someone with push rights already chose to
  push can reach this runner — on this repo, that is the operator.
- **Trust predicate (enumerated exactly in the workflows, invariant-enforced):** `ci.yml` —
  protected-`main` pushes OR same-repo (non-fork) PRs; `nightly.yml` — the schedule OR a main-only
  manual dispatch. **No catch-alls.** Fork PRs are structurally unable to reach self-hosted labels
  (R15). **Tighten to protected-refs/merge-queue the day this repo gains outside collaborators.**
- **No GitHub secrets on this tier** — tier-3 secrets live only in the protected `release`
  Environment.

## Runbook

```sh
# Provision (idempotent; --dry-run to preview):
bash roamux/build/ci/provision_runner.sh
# Machine-specific env for jobs (written once):
cat > ~/roamux-runner/.env <<ENV
ROAMEX_CHROMIUM_SRC=<abs path to the pinned checkout, e.g. /Users/you/chromium/src>
ROAMEX_DEPOT_TOOLS=<abs path to depot_tools>
ROAMEX_CANONICAL_OVERLAY=<abs path to codes/roamux/roamux — the symlink restore target>
ENV
# Start (session-lifetime; dies on reboot):
cd ~/roamux-runner && nohup ./run.sh >runner.log 2>&1 &
# Persist across reboots (deeper machine mutation — operator choice):
cd ~/roamux-runner && ./svc.sh install && ./svc.sh start
# Enable tier-2 jobs:
gh variable set ROAMEX_CI_CHROMIUM_RUNNER --body roamux-builder-1 --repo <owner>/<repo>
# Decommission (do BOTH — a set variable with a dead runner queues jobs until timeout):
cd ~/roamux-runner && ./config.sh remove --token "$(gh api -X POST repos/<o>/<r>/actions/runners/remove-token --jq .token)"
gh variable delete ROAMEX_CI_CHROMIUM_RUNNER --repo <owner>/<repo>
```

## Cache model

`out/CI` is an APFS clone of the operator's warm `out/Default` (near-instant, ~zero disk until
divergence) made on first job use; ninja then builds incrementally. Refresh = delete `out/CI` (the
next job re-clones). Known v1 contention: the runner shares the machine/checkout with local
development — jobs serialize on the single runner; a PR that *changes* patches leaves the base's
applied set at branch state until the next runhook run converges it; avoid heavy local builds while
a CI job runs.
