#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Roamux patch runhook (execution plan §12.2 mechanism 3, §12.5).

Applies every ``roamux/patches/*.patch`` to a Chromium checkout, in name order.
Idempotent and **stack-aware** (roam-77): the applied-state detector forward-
simulates the whole stack from pristine (``git show HEAD:<path>``) content in a
scratch tree and matches the working tree against the per-patch snapshots —
per-patch ``git apply --reverse --check`` probes misread a fully-applied tree
whenever a later patch inserts lines adjacent to an earlier patch's hunk (the
0008×0020 / 0006×0024 shape). Fails loudly, naming the patch or the diverged
file, when the tree matches no stack state.

Usage:
  apply_patches.py --chromium-src ~/chromium/src [--patches DIR] [--check]

``--check`` verifies (applied or cleanly appliable) without mutating the tree.
"""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile


def _run(cmd, **kwargs):
    return subprocess.run(cmd, capture_output=True, **kwargs)


def _touched_paths(patch):
    """Repo-relative paths a patch touches, via git's own parser."""
    result = _run(["git", "apply", "--numstat", str(patch)])
    if result.returncode != 0:
        _fail(patch, result)
        return None
    paths = []
    for line in result.stdout.decode("utf-8").splitlines():
        added, deleted, path = line.split("\t", 2)
        if added == "-" or deleted == "-":
            print(f"FAIL: binary patch unsupported: {patch.name} ({path})",
                  file=sys.stderr)
            return None
        paths.append(path)
    return paths


def _pristine(chromium_src, path):
    """The file's content at HEAD (the pinned base), or None if absent."""
    result = _run(["git", "-C", str(chromium_src), "show", f"HEAD:{path}"])
    return result.stdout if result.returncode == 0 else None


def _read_tree(root, paths):
    """Snapshot {path: bytes-or-None} for the given paths under root."""
    snapshot = {}
    for path in paths:
        file = pathlib.Path(root) / path
        snapshot[path] = file.read_bytes() if file.is_file() else None
    return snapshot


def _write_tree(root, snapshot):
    for path, content in snapshot.items():
        if content is None:
            continue
        file = pathlib.Path(root) / path
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_bytes(content)


def _simulate(chromium_src, patches, union):
    """Forward-apply the stack in a scratch tree from pristine content.

    Returns [state0 (pristine), state1, ..., stateN] — content snapshots over
    the union of touched paths — or None after a loud failure (the stack does
    not apply in order; the same failure a fresh checkout would hit).
    """
    scratch = tempfile.mkdtemp(prefix="roamux-patch-sim-")
    try:
        _write_tree(scratch, {p: _pristine(chromium_src, p) for p in union})
        snapshots = [_read_tree(scratch, union)]
        for patch in patches:
            result = _run(["git", "apply", str(patch)], cwd=scratch)
            if result.returncode != 0:
                _fail(patch, result)
                return None
            snapshots.append(_read_tree(scratch, union))
        return snapshots
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chromium-src", required=True, type=pathlib.Path,
                        help="path to the Chromium src/ checkout")
    parser.add_argument("--patches", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent.parent / "patches",
                        help="directory holding *.patch files (default: roamux/patches)")
    parser.add_argument("--check", action="store_true",
                        help="verify only; do not mutate the tree")
    args = parser.parse_args()

    # Resolve now: the scratch simulation runs git apply with a different cwd,
    # so relative --patches paths must be absolute before that (roam-77 review).
    patches = sorted(args.patches.resolve().glob("*.patch"))
    if not patches:
        print(f"no patches found under {args.patches}", file=sys.stderr)
        return 0

    union = []
    for patch in patches:
        paths = _touched_paths(patch)
        if paths is None:
            return 1
        union.extend(p for p in paths if p not in union)

    snapshots = _simulate(args.chromium_src, patches, union)
    if snapshots is None:
        return 1

    worktree = _read_tree(args.chromium_src, union)
    applied_count = None
    for k in range(len(patches), -1, -1):
        if worktree == snapshots[k]:
            applied_count = k
            break
    if applied_count is None:
        diverged = sorted(p for p in union
                          if worktree.get(p) != snapshots[-1].get(p))
        print("FAIL: the tree matches no stack state (neither pristine nor "
              "any applied prefix) — diverged file(s): "
              f"{', '.join(diverged[:5])}", file=sys.stderr)
        print("Rebase or fix the patch (§12.5: patches fail loudly on rebase).",
              file=sys.stderr)
        return 1

    for i, patch in enumerate(patches, start=1):
        if i <= applied_count:
            print(f"[applied]   {patch.name}")
        elif args.check:
            print(f"[appliable] {patch.name}")
        else:
            result = _run(["git", "-C", str(args.chromium_src), "apply",
                           str(patch)])
            if result.returncode != 0:
                _fail(patch, result)
                return 1
            print(f"[apply]     {patch.name}")
    return 0


def _fail(patch, result):
    print(f"FAIL: patch neither applied nor cleanly appliable: {patch.name}", file=sys.stderr)
    stderr = result.stderr
    if stderr:
        if isinstance(stderr, bytes):
            stderr = stderr.decode("utf-8", "replace")
        print(stderr, file=sys.stderr)
    print("Rebase or fix the patch (§12.5: patches fail loudly on rebase).", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main())
