#!/usr/bin/env python3
"""Make the Cartridge Store icon and screenshot.

  assets/store/icon.png        64x64: the drum grid in the kit's colours
  assets/store/screenshot.png  320x200 gameplay frame

The screenshot is a real frame of the cartridge. The preferred source is the
QEMU firmware capture (build/qemu-preview/groove.png, written by
`scripts/qemu_preview.py --script preview`, 320x240 with the firmware
bands, cropped here to the 320x200 game viewport). When that file does not
exist the host render of the same screen is used (build/shots/compose.png,
written by `tests/run_tests.sh shots`). Pass --host to force the host
render, which is deterministic. Both images are palettised because the
Store trailer counts towards the 64 KiB cartridge slot.
"""
from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]

# Colours of src/drumfight.c (C_BG, C_CELL, C_GOLD, VOICE_COLORS).
BG, CELL, GOLD = (8, 10, 28), (30, 36, 76), (255, 204, 40)
VOICES = [(236, 64, 64), (255, 150, 40), (250, 226, 60), (170, 232, 70),
          (60, 210, 130), (70, 220, 230), (70, 150, 255), (236, 110, 220)]
# Half a bar of a piazza groove, one row per voice.
ICON_ROWS = ["x..x..x.", "....x...", "x.x.x.x.", "..x...x.", "x..x..x.", ".x.....x", "x..x..x.", "....x..."]


def icon() -> Image.Image:
    img = Image.new("RGB", (64, 64), BG)
    draw = ImageDraw.Draw(img)
    draw.rectangle((0, 0, 63, 63), outline=GOLD, width=2)
    for v, row in enumerate(ICON_ROWS):
        for s, c in enumerate(row):
            x, y = 5 + s * 7, 5 + v * 7
            draw.rectangle((x, y, x + 5, y + 5), fill=VOICES[v] if c == "x" else CELL)
    return img


def main() -> int:
    out = ROOT / "assets/store"
    out.mkdir(parents=True, exist_ok=True)
    icon().quantize(colors=16).save(out / "icon.png", optimize=True)
    qemu = ROOT / "build/qemu-preview/groove.png"
    host = ROOT / "build/shots/compose.png"
    if qemu.exists() and "--host" not in sys.argv:
        frame, source = Image.open(qemu).convert("RGB").crop((0, 20, 320, 220)), "QEMU firmware capture"
    elif host.exists():
        frame, source = Image.open(host).convert("RGB"), "host render"
    else:
        raise SystemExit("no screenshot source: run tests/run_tests.sh shots or scripts/qemu_preview.py first")
    assert frame.size == (320, 200), frame.size
    frame.quantize(colors=32, dither=Image.Dither.NONE).save(out / "screenshot.png", optimize=True)
    print(f"screenshot source: {source}")
    for f in sorted(out.glob("*.png")):
        print(f"{f.relative_to(ROOT)}: {f.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
