#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Launch-critical dyld check (roam-121): every @rpath/ dependency of the app's main
framework must resolve THROUGH the framework's own LC_RPATHs to a real file inside the
bundle. The stub executable dlopen()s the framework at startup; one unresolvable
@rpath dep means the installed app aborts ~80 ms after first launch, so this gate runs
in the release pipeline right after the bundle is assembled — seconds, not a
user-visible crash.

Hermetic core: parse_load_commands() and unresolved_deps() are pure (fixture-testable);
only main()/check_framework() shell out to otool.
"""

import argparse
import pathlib
import subprocess
import sys

_DYLIB_CMDS = ("LC_LOAD_DYLIB", "LC_LOAD_WEAK_DYLIB", "LC_REEXPORT_DYLIB")


def parse_load_commands(otool_text):
    """Return (lc_rpaths, rpath_deps) from `otool -l` output.

    rpath_deps: LC_*_DYLIB names beginning with @rpath/ — the deps this check owns.
    lc_rpaths:  LC_RPATH path entries, in order.
    """
    rpaths, deps = [], []
    current_cmd = None
    for line in otool_text.splitlines():
        stripped = line.strip()
        if stripped.startswith("cmd "):
            current_cmd = stripped.split()[1]
        elif stripped.startswith("name ") and current_cmd in _DYLIB_CMDS:
            name = stripped[len("name "):].rsplit(" (offset", 1)[0]
            if name.startswith("@rpath/"):
                deps.append(name)
        elif stripped.startswith("path ") and current_cmd == "LC_RPATH":
            rpaths.append(stripped[len("path "):].rsplit(" (offset", 1)[0])
    return rpaths, deps


def unresolved_deps(deps, rpaths, loader_dir):
    """Return human-readable problems for @rpath deps no LC_RPATH resolves.

    @loader_path is substituted with loader_dir (the directory containing the framework
    binary — identical for every process that loads it, browser and helpers alike).
    Absolute rpaths are honoured; other stems (@executable_path) cannot be proven from
    the binary alone and never count as a resolution.
    """
    loader_dir = pathlib.Path(loader_dir)
    problems = []
    for dep in deps:
        tail = dep[len("@rpath/"):]
        resolved = False
        for rp in rpaths:
            if rp.startswith("@loader_path"):
                base = loader_dir / rp[len("@loader_path"):].lstrip("/")
            elif rp.startswith("/"):
                base = pathlib.Path(rp)
            else:
                continue
            if (base / tail).resolve().exists():
                resolved = True
                break
        if not resolved:
            problems.append(
                f"{dep}: not resolvable via LC_RPATHs {rpaths or '[none]'} "
                f"(loader dir {loader_dir})")
    return problems


def _framework_binary(app):
    hits = sorted(app.glob("Contents/Frameworks/* Framework.framework/Versions/*/"))
    for versioned in hits:
        name = versioned.parents[1].name.removesuffix(".framework")
        candidate = versioned / name
        if candidate.is_file():
            return candidate
    raise SystemExit(f"error: no versioned '* Framework.framework' binary under {app}")


def check_framework(app):
    """Evaluate each arch slice separately — roam-114 proved slices can diverge."""
    binary = _framework_binary(pathlib.Path(app))
    archs = subprocess.run(["lipo", "-archs", str(binary)], capture_output=True,
                           text=True, check=True).stdout.split()
    problems = []
    for arch in archs:
        otool = subprocess.run(["otool", "-arch", arch, "-l", str(binary)],
                               capture_output=True, text=True, check=True).stdout
        rpaths, deps = parse_load_commands(otool)
        problems += [f"[{arch}] {p}"
                     for p in unresolved_deps(deps, rpaths, binary.parent)]
    return binary, problems


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True, type=pathlib.Path)
    args = parser.parse_args()
    binary, problems = check_framework(args.app)
    if problems:
        for p in problems:
            print(f"::error::framework rpath check: {p}", file=sys.stderr)
        return 1
    print(f"framework rpath check OK: {binary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
