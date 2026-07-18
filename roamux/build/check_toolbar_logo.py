#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Toolbar-logo gate (roam-158): assert the current-channel-logo PNGs are the real Roamux glyph.

chrome://theme/current-channel-logo (the settings toolbar, chrome://version, the settings drawer,
the management/extensions banners, the searchbox pedal icon) serves IDR_ROAMUX_PRODUCT_LOGO_32,
registered by roamux/app/resources/theme/roamux_theme_resources.grd from the per-scale sources
next to that grd:

  default_100_percent/product_logo_32.png   32x32  (1x)
  default_200_percent/product_logo_32.png   64x64  (2x)

This checker proves the committed assets are the real transparent Roamux glyph — structurally, by
decoding the PNG pixel data — so a 1x1 placeholder, an opaque tile, or a flat one-colour square
can never silently ship (the roam-140 stub that roam-157 had to clean up). A glyph must have a
transparent field (the mark floats on the toolbar), a non-trivial amount of opaque ink, and at
least two distinct ink colours (the four palette rays). Pure Python, no Chromium build; tier-1.

Regeneration (from the canonical glyph SVG). qlmanage flattens the glyph onto an opaque white
canvas (this gate caught that on the first roam-158 render), so rasterize with a browser engine:
wrap the SVG in a bare HTML page as <img src="roamux_glyph.svg" width="N" height="N"> with a
transparent body, then for N in {32, 64}:
  chrome --headless=new --disable-gpu --force-device-scale-factor=1 \
    --default-background-color=00000000 --window-size=N,N \
    --screenshot=<out>.png file://<wrapper>.html
and place the renders as default_100_percent/product_logo_32.png (32) and
default_200_percent/product_logo_32.png (64). Source of truth:
roamux/app/resources/settings_about/roamux_glyph.svg.

Usage: check_toolbar_logo.py [--theme-dir <dir>] [--json]
"""

import argparse
import json
import pathlib
import struct
import sys
import zlib

_PNG_MAGIC = b"\x89PNG\r\n\x1a\n"

# (per-scale subdir, expected square pixel size)
EXPECTED = (
    ("default_100_percent", 32),
    ("default_200_percent", 64),
)
FILENAME = "product_logo_32.png"

# Structural floors for "real glyph, not a stub". The 4 rays at the glyph's
# stroke width cover roughly a fifth of the canvas; the transparent field
# dominates. Floors are deliberately loose — they reject degenerate art, they
# do not pin the design.
MIN_TRANSPARENT_FRACTION = 0.10  # alpha == 0
MIN_OPAQUE_FRACTION = 0.02       # alpha >= 128
MIN_DISTINCT_INK_COLOURS = 2     # distinct RGB among opaque pixels


class CheckError(Exception):
    """A structural defect in a checked PNG."""


def _chunks(data):
    if not data.startswith(_PNG_MAGIC):
        raise CheckError("not a PNG (bad magic)")
    pos = len(_PNG_MAGIC)
    while pos + 12 <= len(data):
        (length,) = struct.unpack_from(">I", data, pos)
        ctype = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if len(body) != length:
            raise CheckError(f"truncated {ctype!r} chunk")
        yield ctype, body
        pos += 12 + length


def _ihdr(data):
    for ctype, body in _chunks(data):
        if ctype == b"IHDR":
            width, height, depth, colour, _comp, _filt, interlace = struct.unpack(
                ">IIBBBBB", body)
            return width, height, depth, colour, interlace
        break
    raise CheckError("IHDR is not the first chunk")


def _unfilter(raw, width, height):
    """Reverse PNG scanline filtering for 8-bit RGBA (4 bytes/pixel)."""
    stride = width * 4
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        if pos + 1 + stride > len(raw):
            raise CheckError("pixel data shorter than IHDR dimensions")
        ftype = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride
        if ftype == 1:  # Sub
            for i in range(4, stride):
                line[i] = (line[i] + line[i - 4]) & 0xFF
        elif ftype == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(stride):
                left = line[i - 4] if i >= 4 else 0
                line[i] = (line[i] + (left + prev[i]) // 2) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(stride):
                a = line[i - 4] if i >= 4 else 0
                b = prev[i]
                c = prev[i - 4] if i >= 4 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        elif ftype != 0:
            raise CheckError(f"unsupported PNG filter type {ftype}")
        out += line
        prev = line
    return bytes(out)


def _rgba_pixels(data):
    width, height, depth, colour, interlace = _ihdr(data)
    if depth != 8 or colour != 6:
        raise CheckError(
            f"expected 8-bit RGBA (depth 8, colour type 6), got depth {depth}"
            f" colour type {colour} — regenerate per the recipe in this"
            " script's docstring")
    if interlace != 0:
        raise CheckError("interlaced PNG — regenerate without interlacing")
    idat = b"".join(body for ctype, body in _chunks(data) if ctype == b"IDAT")
    if not idat:
        raise CheckError("no IDAT chunk")
    raw = zlib.decompress(idat)
    pixels = _unfilter(raw, width, height)
    return width, height, [tuple(pixels[i:i + 4]) for i in range(0, len(pixels), 4)]


def check_glyph_png(path, expected_px):
    """Failure strings for one per-scale glyph PNG (empty list = pass)."""
    path = pathlib.Path(path)
    if not path.is_file():
        return [f"{path}: missing"]
    try:
        width, height, pixels = _rgba_pixels(path.read_bytes())
    except (CheckError, zlib.error, struct.error) as err:
        return [f"{path}: {err}"]
    failures = []
    if (width, height) != (expected_px, expected_px):
        failures.append(
            f"{path}: {width}x{height}, expected {expected_px}x{expected_px}")
    total = len(pixels)
    transparent = sum(1 for p in pixels if p[3] == 0)
    opaque = [p for p in pixels if p[3] >= 128]
    if transparent < MIN_TRANSPARENT_FRACTION * total:
        failures.append(
            f"{path}: transparent field is {transparent}/{total} pixels —"
            " a glyph floats on transparency (opaque tile / filled stub?)")
    if len(opaque) < MIN_OPAQUE_FRACTION * total:
        failures.append(
            f"{path}: only {len(opaque)}/{total} opaque pixels — no visible ink"
            " (blank or near-empty stub?)")
    if len({p[:3] for p in opaque}) < MIN_DISTINCT_INK_COLOURS:
        failures.append(
            f"{path}: fewer than {MIN_DISTINCT_INK_COLOURS} distinct ink"
            " colours — the glyph's palette rays are missing (flat stub?)")
    return failures


def check_theme_dir(theme_dir):
    """Failure strings across both per-scale glyph PNGs (empty list = pass)."""
    theme_dir = pathlib.Path(theme_dir)
    failures = []
    for subdir, px in EXPECTED:
        failures.extend(check_glyph_png(theme_dir / subdir / FILENAME, px))
    return failures


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--theme-dir", type=pathlib.Path,
        default=pathlib.Path(__file__).resolve().parents[1]
        / "app" / "resources" / "theme",
        help="directory holding the per-scale glyph dirs"
        " (default: roamux/app/resources/theme)")
    parser.add_argument("--json", action="store_true",
                        help="emit a JSON report instead of text")
    args = parser.parse_args(argv)

    failures = check_theme_dir(args.theme_dir)
    if args.json:
        print(json.dumps({"ok": not failures, "failures": failures}, indent=2))
    else:
        for failure in failures:
            print(f"FAIL: {failure}", file=sys.stderr)
        if not failures:
            print("toolbar-logo check: OK")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
