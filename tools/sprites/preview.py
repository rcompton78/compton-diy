#!/usr/bin/env python3
"""
preview.py — upscaled previews of the committed sprite exports, for reviewing art in chat
(or anywhere) before flashing.

Runs the sheets through convert.py's exact palette-lock/indexing path, so the preview
shows what the device will draw, not what the source PNG looked like. For every sheet it
writes, under --out:

    <sheet>.png          every frame side by side, one row per palette (normal, sick, ...)
    <sheet>-<tag>.gif    each tag animated at its authored frame durations

Local-only helper: needs Pillow (`pip install pillow`); the firmware build/CI never run it.

Usage:
    python3 tools/sprites/preview.py --assets apps/tamagotchi-plus/assets --out build/sprite-preview
"""

import argparse
import json
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).parent))
import convert  # noqa: E402

BACKDROP = (0x83, 0x76, 0x9C)  # the scene's lavender sky, so outlines read as on-device


def frame_image(sheet, frame, colours, scale):
    """Renders one converted frame on the scene backdrop, upscaled by scale."""
    im = Image.new("RGB", (sheet["w"], sheet["h"]), BACKDROP)
    px = im.load()
    blob, stride = sheet["blob"], (frame["w"] + 1) // 2
    for y in range(frame["h"]):
        for x in range(frame["w"]):
            b = blob[frame["offset"] + y * stride + x // 2]
            idx = (b & 0x0F) if x & 1 else (b >> 4)
            if idx:
                px[frame["x"] + x, frame["y"] + y] = colours[idx - 1]
    return im.resize((sheet["w"] * scale, sheet["h"] * scale), Image.NEAREST)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--scale", type=int, default=8, help="upscale factor (device is 4x)")
    args = ap.parse_args()

    palettes = [("normal", convert.read_palette(args.assets / "palette.hex"))]
    for alt in sorted(args.assets.glob("palette-*.hex")):
        palettes.append((alt.stem[len("palette-"):], convert.read_palette(alt)))
    args.out.mkdir(parents=True, exist_ok=True)

    report = convert.Report()
    for json_path in sorted(args.assets.glob("*.json")):
        name = json_path.stem
        sheet = convert.convert_sheet(name, json_path.with_suffix(".png"), json_path, palettes[0][1], report)
        cell_w, cell_h = sheet["w"] * args.scale, sheet["h"] * args.scale
        pad = args.scale

        strip = Image.new("RGB", (len(sheet["frames"]) * (cell_w + pad) + pad, len(palettes) * (cell_h + pad) + pad),
                          (0x10, 0x10, 0x10))
        for row, (_, colours) in enumerate(palettes):
            for col, f in enumerate(sheet["frames"]):
                strip.paste(frame_image(sheet, f, colours, args.scale),
                            (pad + col * (cell_w + pad), pad + row * (cell_h + pad)))
        strip.save(args.out / f"{name}.png")
        written = [f"{name}.png"]

        for tag in sheet["tags"]:
            order = list(range(tag["from"], tag["to"] + 1))
            if tag["direction"] == 1:
                order.reverse()
            elif tag["direction"] == 2 and len(order) > 1:
                order += order[-2:0:-1]
            frames = [frame_image(sheet, sheet["frames"][i], palettes[0][1], args.scale) for i in order]
            durations = [sheet["frames"][i]["duration"] for i in order]
            gif = args.out / f"{name}-{tag['name']}.gif"
            frames[0].save(gif, save_all=True, append_images=frames[1:], duration=durations, loop=0, disposal=2)
            written.append(gif.name)
        print(f"preview: {name}: {', '.join(written)}")

    if report.problems():
        report.print(strict=False)
    print(f"preview: wrote to {args.out}")


if __name__ == "__main__":
    main()
