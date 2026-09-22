#!/usr/bin/env python3
"""
convert.py — build-time sprite converter for palette-indexed pixel-art firmware (COM-296).

Reads ONLY the committed Aseprite exports (a sheet PNG + its json-array data file) and
never the .aseprite source, so it runs anywhere Python does — CI included — without
Aseprite installed. Standard library only (no Pillow), for the same reason.

Usage:
    python3 tools/sprites/convert.py \
        --assets apps/tamagotchi-plus/assets \
        --out apps/tamagotchi-plus/include/generated/sprite_assets.h [--strict]

Conventions inside --assets:
    palette.hex          the locked palette: 15 colours, one RRGGBB per line. Index 0 is
                         reserved for "transparent", so it holds 15 colours + transparent =
                         16 entries = 4 bits per pixel.
    palette-<name>.hex   alternate palettes (e.g. palette-sick.hex) with the same 15 slots
                         recoloured. Swapping palettes at runtime recolours every sprite with
                         no extra pixel data.
    <name>.png + <name>.json
                         an Aseprite sheet export (`--sheet ... --data ... --format
                         json-array --list-tags`). Every such pair becomes one SpriteSheet.

Palette lock: every opaque pixel must exactly match a colour in palette.hex. Off-palette
pixels are snapped to the nearest palette colour and reported. With --strict (what the Nx
target uses) any off-palette pixel fails the build instead, so what ships is exactly what
was reviewed in Aseprite. Pixels with alpha < 128 become transparent (index 0); partially
transparent pixels are also reported, since the device has no alpha blending.

Output: one header of C arrays (4bpp packed, two pixels per byte, high nibble first),
per-frame rects/durations, tag ranges and RGB565 palettes, using the types in
include/SpriteTypes.h.
"""

import argparse
import json
import re
import struct
import sys
import zlib
from pathlib import Path

TRANSPARENT = 0
MAX_COLOURS = 15  # + transparent = 16 entries (4bpp)


# ── PNG decoding (stdlib only) ───────────────────────────────────────────────────────────

def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path):
    """Decodes a non-interlaced PNG into (width, height, rows of (r, g, b, a) tuples).

    Supports every colour type Aseprite exports (RGBA for RGB-mode sprites, palette +
    tRNS for indexed-mode sprites) plus greyscale/RGB, at 8-bit depth (and 1/2/4-bit
    for indexed/greyscale)."""
    data = Path(path).read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG file")

    pos, idat, plte, trns = 8, bytearray(), None, None
    width = height = bit_depth = colour_type = interlace = None
    while pos < len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            width, height, bit_depth, colour_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"PLTE":
            plte = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat += chunk
        elif ctype == b"IEND":
            break

    if interlace:
        raise ValueError(f"{path}: interlaced PNGs are not supported")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(colour_type)
    if channels is None:
        raise ValueError(f"{path}: unsupported PNG colour type {colour_type}")
    if bit_depth != 8 and not (colour_type in (0, 3) and bit_depth in (1, 2, 4)):
        raise ValueError(f"{path}: unsupported bit depth {bit_depth} for colour type {colour_type}")

    raw = zlib.decompress(bytes(idat))
    bits_pp = channels * bit_depth
    stride = (width * bits_pp + 7) // 8
    bpp = max(1, bits_pp // 8)  # filter byte distance
    prev = bytearray(stride)
    rows, i = [], 0
    for _ in range(height):
        ftype = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 0xFF
            elif ftype == 2:
                line[x] = (line[x] + b) & 0xFF
            elif ftype == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif ftype == 4:
                line[x] = (line[x] + _paeth(a, b, c)) & 0xFF
        prev = line

        if bit_depth < 8:
            per_byte, mask = 8 // bit_depth, (1 << bit_depth) - 1
            samples = [(line[x // per_byte] >> (8 - bit_depth * (x % per_byte + 1))) & mask
                       for x in range(width)]
        else:
            samples = None

        px = []
        for x in range(width):
            if colour_type == 6:
                px.append(tuple(line[x * 4:x * 4 + 4]))
            elif colour_type == 2:
                r, g, b = line[x * 3:x * 3 + 3]
                px.append((r, g, b, 255))
            elif colour_type == 3:
                idx = samples[x] if samples else line[x]
                r, g, b = plte[idx]
                alpha = trns[idx] if trns is not None and idx < len(trns) else 255
                px.append((r, g, b, alpha))
            elif colour_type == 4:
                v, alpha = line[x * 2:x * 2 + 2]
                px.append((v, v, v, alpha))
            else:  # 0: greyscale
                v = samples[x] * 255 // ((1 << bit_depth) - 1) if samples else line[x]
                px.append((v, v, v, 255))
        rows.append(px)
    return width, height, rows


# ── Palettes ─────────────────────────────────────────────────────────────────────────────

def read_palette(path):
    """Reads a palette .hex file into a list of exactly 15 (r, g, b) tuples."""
    colours = []
    for n, line in enumerate(Path(path).read_text().splitlines(), 1):
        line = line.split(";")[0].strip().lstrip("#")  # ";" starts a comment; "#" prefix optional
        if not line:
            continue
        if not re.fullmatch(r"[0-9a-fA-F]{6}", line):
            raise ValueError(f"{path}:{n}: expected RRGGBB, got {line!r}")
        colours.append(tuple(int(line[i:i + 2], 16) for i in (0, 2, 4)))
    if len(colours) != MAX_COLOURS:
        raise ValueError(f"{path}: expected exactly {MAX_COLOURS} colours "
                         f"(index 0 is reserved for transparent), found {len(colours)}")
    return colours


def rgb565(c):
    """Packs an (r, g, b) tuple into RGB565."""
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def nearest(palette, c):
    """Returns the index into palette of the colour closest to c."""
    # "Redmean" weighted distance: cheap and much closer to perceived difference than plain RGB.
    r, g, b = c
    best, best_d = 0, None
    for i, (pr, pg, pb) in enumerate(palette):
        rm = (r + pr) / 2
        d = (2 + rm / 256) * (r - pr) ** 2 + 4 * (g - pg) ** 2 + (2 + (255 - rm) / 256) * (b - pb) ** 2
        if best_d is None or d < best_d:
            best, best_d = i, d
    return best


# ── Sheet conversion ─────────────────────────────────────────────────────────────────────

class Report:
    def __init__(self):
        self.off_palette = {}  # (sheet, rgb) -> count
        self.partial_alpha = {}  # sheet -> count

    def problems(self):
        return bool(self.off_palette or self.partial_alpha)

    def print(self, strict):
        level = "error" if strict else "warning"
        for (sheet, rgb), count in sorted(self.off_palette.items()):
            snapped = "" if strict else " (snapped to nearest palette colour)"
            print(f"{level}: {sheet}: {count} px of off-palette colour #{bytes(rgb).hex()}{snapped}",
                  file=sys.stderr)
        for sheet, count in sorted(self.partial_alpha.items()):
            print(f"{level}: {sheet}: {count} px partially transparent (alpha thresholded at 128)",
                  file=sys.stderr)


def convert_sheet(name, png_path, json_path, palette, report):
    """Converts one Aseprite sheet export into 4bpp frame data, frame rects/durations and
    tags. Records off-palette and partially transparent pixels in report."""
    _, _, pixels = read_png(png_path)
    meta = json.loads(Path(json_path).read_text())
    frames_json = meta["frames"]
    if isinstance(frames_json, dict):
        raise ValueError(f"{json_path}: expected --format json-array (frames as a list), got json-hash")
    lookup = {c: i for i, c in enumerate(palette)}

    frames, blob = [], bytearray()
    size = None
    for f in frames_json:
        fx, fy, fw, fh = (f["frame"][k] for k in ("x", "y", "w", "h"))
        if f.get("rotated"):
            raise ValueError(f"{json_path}: rotated frames are not supported")
        src = f["sourceSize"]
        size = size or (src["w"], src["h"])
        if (src["w"], src["h"]) != size:
            raise ValueError(f"{json_path}: frames have different source sizes")
        ox, oy = f["spriteSourceSize"]["x"], f["spriteSourceSize"]["y"]

        offset = len(blob)
        for y in range(fh):
            row = []
            for x in range(fw):
                r, g, b, a = pixels[fy + y][fx + x]
                if 0 < a < 255:
                    report.partial_alpha[name] = report.partial_alpha.get(name, 0) + 1
                if a < 128:
                    idx = TRANSPARENT
                else:
                    i = lookup.get((r, g, b))
                    if i is None:
                        key = (name, (r, g, b))
                        report.off_palette[key] = report.off_palette.get(key, 0) + 1
                        i = nearest(palette, (r, g, b))
                    idx = i + 1
                row.append(idx)
            if fw % 2:
                row.append(TRANSPARENT)
            blob.extend((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2))
        frames.append({"offset": offset, "x": ox, "y": oy, "w": fw, "h": fh,
                       "duration": int(f.get("duration", 100))})

    tags = []
    for t in meta.get("meta", {}).get("frameTags", []):
        direction = {"forward": 0, "reverse": 1, "pingpong": 2}.get(t.get("direction", "forward"), 0)
        tags.append({"name": t["name"], "from": t["from"], "to": t["to"], "direction": direction})
    return {"name": name, "w": size[0], "h": size[1], "frames": frames, "tags": tags, "blob": bytes(blob)}


# ── Header emission ──────────────────────────────────────────────────────────────────────

def c_ident(name):
    ident = re.sub(r"\W", "_", name).upper()
    return ident if not ident[0].isdigit() else "_" + ident


def emit_header(sheets, palettes, source_names):
    """Renders the converted sheets and palettes as the C header text."""
    out = [
        "// Generated by tools/sprites/convert.py from the committed Aseprite exports in",
        "// assets/ — do not edit. Regenerate with: pnpm nx run tamagotchi-plus:gen-assets",
        f"// Sources: {', '.join(source_names)}",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        '#include "SpriteTypes.h"',
        "",
        "namespace assets {",
        "",
    ]
    for pname, colours in palettes:
        ident = c_ident(pname)
        vals = ", ".join(f"0x{rgb565(c):04X}" for c in colours)
        out.append(f"// Palette '{pname}': index 0 = transparent, indices 1..15 = colours (RGB565).")
        out.append(f"static const Palette PALETTE_{ident} = {{\"{pname}\", {{0x0000, {vals}}}}};")
        out.append("")

    total = 0
    for s in sheets:
        ident = c_ident(s["name"])
        blob = s["blob"]
        total += len(blob)
        out.append(f"// Sheet '{s['name']}': {s['w']}x{s['h']} px, {len(s['frames'])} frames, "
                   f"{len(blob)} bytes of 4bpp pixel data.")
        out.append(f"static const uint8_t {ident}_PIXELS[{len(blob)}] = {{")
        for i in range(0, len(blob), 20):
            out.append("    " + ", ".join(f"0x{b:02X}" for b in blob[i:i + 20]) + ",")
        out.append("};")
        out.append(f"static const SpriteFrame {ident}_FRAMES[{len(s['frames'])}] = {{")
        for f in s["frames"]:
            out.append(f"    {{{ident}_PIXELS + {f['offset']}, {f['x']}, {f['y']}, {f['w']}, {f['h']}, "
                       f"{f['duration']}}},")
        out.append("};")
        if s["tags"]:
            out.append(f"static const SpriteTag {ident}_TAGS[{len(s['tags'])}] = {{")
            for t in s["tags"]:
                out.append(f"    {{\"{t['name']}\", {t['from']}, {t['to']}, "
                           f"static_cast<TagDirection>({t['direction']})}},")
            out.append("};")
        tags_ref = f"{ident}_TAGS" if s["tags"] else "nullptr"
        out.append(f"static const SpriteSheet {ident} = {{\"{s['name']}\", {s['w']}, {s['h']}, "
                   f"{len(s['frames'])}, {ident}_FRAMES, {len(s['tags'])}, {tags_ref}}};")
        out.append("")

    out.append(f"// Total pixel data across all sheets (flash/rodata), for the perf notes in the README.")
    out.append(f"static const uint32_t TOTAL_PIXEL_BYTES = {total};")
    out.append("")
    out.append("}  // namespace assets")
    out.append("")
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--assets", required=True, type=Path, help="directory with palette*.hex and <name>.png/.json exports")
    ap.add_argument("--out", required=True, type=Path, help="header file to write")
    ap.add_argument("--strict", action="store_true", help="fail on off-palette or partially transparent pixels")
    args = ap.parse_args()

    primary = args.assets / "palette.hex"
    palette = read_palette(primary)
    palettes = [("normal", palette)]
    for alt in sorted(args.assets.glob("palette-*.hex")):
        palettes.append((alt.stem[len("palette-"):], read_palette(alt)))

    report = Report()
    sheets, sources = [], [primary.name]
    for json_path in sorted(args.assets.glob("*.json")):
        png_path = json_path.with_suffix(".png")
        if not png_path.exists():
            raise SystemExit(f"error: {json_path} has no matching sheet {png_path.name}")
        sheets.append(convert_sheet(json_path.stem, png_path, json_path, palette, report))
        sources += [png_path.name, json_path.name]
    if not sheets:
        raise SystemExit(f"error: no <name>.png + <name>.json sheet exports found in {args.assets}")

    if report.problems():
        report.print(args.strict)
        if args.strict:
            raise SystemExit("error: palette lock violated — fix the sprite in Aseprite (or re-run "
                             "png-to-aseprite.lua, which maps frames onto the locked palette) and re-export")

    header = emit_header(sheets, palettes, sources)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if not args.out.exists() or args.out.read_text() != header:
        args.out.write_text(header)
    total = sum(len(s["blob"]) for s in sheets)
    print(f"convert.py: {len(sheets)} sheet(s), {sum(len(s['frames']) for s in sheets)} frame(s), "
          f"{len(palettes)} palette(s), {total} bytes of pixel data -> {args.out}")


if __name__ == "__main__":
    main()
