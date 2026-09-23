#!/usr/bin/env python3
"""
gen-egg.py — authors the tamagotchi-plus egg sprite frames (COM-299).

The egg is drawn procedurally rather than generated or hand-painted because every crack
stage must sit on a *pixel-identical* shell: only crack pixels may differ between stages,
or the egg appears to breathe as it cracks. Re-running this with a different SHELL preset
(e.g. SHELL_SPECIAL) regenerates the whole set consistently.

Writes PNG frames + manifest.json under <assets>/src/egg and <assets>/src/eggshell, which
png-to-aseprite.lua then imports. Local-only helper: needs Pillow, like preview.py. The
firmware build and CI never run this — they read the committed Aseprite exports.

Usage:
    python3 tools/sprites/gen-egg.py --assets apps/tamagotchi-plus/assets
"""

import argparse
import json
import math
import random
from pathlib import Path

from PIL import Image

# Locked palette slots (see assets/palette.hex / include/PaletteIndex.h)
NAVY  = (0x1d, 0x2b, 0x53)
PLUM  = (0x7e, 0x25, 0x53)
BROWN = (0xab, 0x52, 0x36)
DGREY = (0x5f, 0x57, 0x4f)
CREAM = (0xff, 0xf1, 0xe8)
PEACH = (0xff, 0xcc, 0xaa)

# Shell presets. SHELL is the hatching egg; SHELL_SPECIAL is the rounder "special edition"
# shell kept so a variant egg can reuse every crack stage without redrawing anything.
SHELL         = dict(w=40, h=46, top=5, amax=16.0, top_exp=2.0, bot_exp=2.1, yw=0.58)
SHELL_SPECIAL = dict(w=40, h=46, top=6, amax=16.5, top_exp=2.1, bot_exp=2.1, yw=0.55)

SPECKLES = [(0.28, 0.22), (0.68, 0.12), (0.42, 0.44), (0.86, 0.34),
            (0.14, 0.58), (0.58, 0.70), (0.90, 0.78), (0.30, 0.88)]

# One master fissure, plus branches that join at later stages. Normalised to the egg's box.
MASTER = [(0.60, 0.06), (0.69, 0.18), (0.55, 0.27), (0.66, 0.37), (0.50, 0.46),
          (0.59, 0.56), (0.44, 0.64), (0.51, 0.74), (0.40, 0.82)]
BRANCHES = [[(0.55, 0.27), (0.40, 0.30), (0.30, 0.38)],
            [(0.59, 0.56), (0.74, 0.59), (0.83, 0.66)],
            [(0.50, 0.46), (0.36, 0.44), (0.26, 0.49)]]

# stage -> (fraction of MASTER revealed, branch count, hole?, eyes?)
STAGES = [(0.00, 0, False, False),   # 0 pristine
          (0.22, 0, False, False),   # 1 hairline
          (0.45, 1, False, False),   # 2 spreading
          (0.70, 2, False, False),   # 3 branching
          (1.00, 3, True,  False),   # 4 a piece breaks away
          (1.00, 3, True,  True)]    # 5 the creature looks out

HOLE_W, HOLE_H, HOLE_FX, HOLE_FY = 9, 5, 0.46, 0.30
# One creature, one face: the eyes keep the same spacing whether they are seen through
# the stage-5 hole or the wider opening after the lid comes off. Widening them for the
# bigger opening made the face appear to grow at the moment of hatching.
EYE_DX = (-2, 2)
# Slightly over 1 so the ellipse keeps its corner pixels: an exact unit test drops them
# and the hole reads as a diamond rather than an oval at this size.
HOLE_TOLERANCE = 1.05
CUT_FY = 0.42          # where the lid breaks off
LID_CROP_H = 26        # lid frames are cropped to this many rows

# Blink: long hold open, quick half, brief closed. Played ping-pong, so the half frame is
# reused on the way back up and only three frames are stored.
BLINK_MS = [4200, 70, 130]
BLINK_EYES = [CREAM, DGREY, NAVY]


class Shell:
    def __init__(self, p):
        self.p = p
        self.W, self.H = p['w'], p['h']
        self.cx = (self.W - 1) / 2.0
        self.top = p['top']
        self.bot = self.H - 3
        self.amax = p['amax']
        self.yw = self.top + (self.bot - self.top) * p['yw']
        self.solid = self._silhouette()

    def _half_width(self, y):
        if y < self.yw:
            u = (self.yw - y) / (self.yw - self.top)
            return self.amax * max(0.0, 1 - u ** self.p['top_exp']) ** 0.5
        v = (y - self.yw) / (self.bot - self.yw)
        return self.amax * max(0.0, 1 - v ** self.p['bot_exp']) ** 0.5

    def _silhouette(self):
        s = [[False] * self.W for _ in range(self.H)]
        for y in range(self.top, self.bot + 1):
            hw = self._half_width(y)
            for x in range(self.W):
                if abs(x - self.cx) <= hw:
                    s[y][x] = True
        return s

    def edge(self, x, y, rad=1):
        return any(not (0 <= x + dx < self.W and 0 <= y + dy < self.H) or not self.solid[y + dy][x + dx]
                   for dx in range(-rad, rad + 1) for dy in range(-rad, rad + 1))

    def interior(self, x, y):
        return (0 <= x < self.W and 0 <= y < self.H and self.solid[y][x] and not self.edge(x, y, 1))

    def to_px(self, fx, fy):
        return (int(round(self.cx - self.amax + fx * 2 * self.amax)),
                int(round(self.top + fy * (self.bot - self.top))))

    def shaded(self):
        """cream body, peach shadow on a curved terminator, brown base rim, navy outline"""
        lx = self.cx - self.amax * 0.72
        ly = self.top + (self.bot - self.top) * 0.22
        r = max(self.amax, (self.bot - self.top) * 0.5) * 1.34
        px = [[None] * self.W for _ in range(self.H)]
        for y in range(self.H):
            for x in range(self.W):
                if self.solid[y][x]:
                    px[y][x] = CREAM if math.hypot(x - lx, y - ly) < r else PEACH
        for y in range(self.H):
            for x in range(self.W):
                if (self.solid[y][x] and not self.edge(x, y, 1) and self.edge(x, y, 2)
                        and math.hypot(x - lx, y - ly) > r + 1.0):
                    px[y][x] = BROWN
        for fx, fy in SPECKLES:
            x, y = self.to_px(fx, fy)
            if self.interior(x, y):
                px[y][x] = PLUM
        for y in range(self.H):
            for x in range(self.W):
                if self.solid[y][x] and self.edge(x, y, 1):
                    px[y][x] = NAVY
        return px


def _line(a, b):
    (x0, y0), (x1, y1) = a, b
    dx, dy = abs(x1 - x0), abs(y1 - y0)
    sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
    err, pts = dx - dy, []
    while True:
        pts.append((x0, y0))
        if (x0, y0) == (x1, y1):
            return pts
        e2 = 2 * err
        if e2 > -dy:
            err -= dy; x0 += sx
        if e2 < dx:
            err += dx; y0 += sy


def _path(sh, norm_pts):
    out = []
    for i in range(len(norm_pts) - 1):
        seg = _line(sh.to_px(*norm_pts[i]), sh.to_px(*norm_pts[i + 1]))
        out += seg if i == 0 else seg[1:]
    return out


def _image(sh, px):
    im = Image.new('RGBA', (sh.W, sh.H), (0, 0, 0, 0))
    d = im.load()
    for y in range(sh.H):
        for x in range(sh.W):
            if px[y][x]:
                d[x, y] = px[y][x] + (255,)
    return im


def stage_image(sh, stage, eye=None):
    reveal, branches, hole, eyes = STAGES[stage]
    px = sh.shaded()
    master = _path(sh, MASTER)
    for (x, y) in master[:int(len(master) * reveal)]:
        if sh.interior(x, y):
            px[y][x] = NAVY
    for b in BRANCHES[:branches]:
        for (x, y) in _path(sh, b):
            if sh.interior(x, y):
                px[y][x] = NAVY
    if hole:
        hx, hy = sh.to_px(HOLE_FX, HOLE_FY)
        cells = []
        for dy in range(-(HOLE_H // 2), HOLE_H - HOLE_H // 2):
            for dx in range(-(HOLE_W // 2), HOLE_W - HOLE_W // 2):
                if (dx / (HOLE_W / 2.0)) ** 2 + (dy / (HOLE_H / 2.0)) ** 2 <= HOLE_TOLERANCE:
                    x, y = hx + dx, hy + dy
                    if sh.interior(x, y):
                        px[y][x] = NAVY; cells.append((x, y))
        if eyes and eye is not None:
            for dx in EYE_DX:
                if (hx + dx, hy) in cells:
                    px[hy][hx + dx] = eye
    return _image(sh, px)


def split(sh, seed=7):
    """break the top off: returns (lid, base) images, both still outlined"""
    whole = stage_image(sh, 5, eye=None)
    cut = int(round(sh.top + CUT_FY * (sh.bot - sh.top)))
    rnd = random.Random(seed)
    prof, y = [], 0
    for _ in range(sh.W):
        if rnd.random() < 0.55:
            y = max(-2, min(2, y + rnd.choice([0, 0, 1, -1, 2, -2])))
        prof.append(y)
    src = whole.load()
    lid = Image.new('RGBA', (sh.W, sh.H), (0, 0, 0, 0)); ld = lid.load()
    base = Image.new('RGBA', (sh.W, sh.H), (0, 0, 0, 0)); bd = base.load()
    for x in range(sh.W):
        cy = cut + prof[x]
        for y in range(sh.H):
            if src[x, y][3]:
                (ld if y < cy else bd)[x, y] = src[x, y]
    # single navy pixel along each new edge: outlining every column gives uniform sawteeth
    for x in range(sh.W):
        col = [y for y in range(sh.H) if ld[x, y][3]]
        if col: ld[x, max(col)] = NAVY + (255,)
        col = [y for y in range(sh.H) if bd[x, y][3]]
        if col: bd[x, min(col)] = NAVY + (255,)
    return lid, base


def opened_base(base, eye=CREAM, depth=3):
    b = base.copy(); d = b.load(); W, H = b.size
    tops = {}
    for x in range(W):
        col = [y for y in range(H) if d[x, y][3]]
        if col: tops[x] = min(col)
    for x, t in tops.items():
        for y in range(t + 1, min(t + 1 + depth, H)):
            if d[x, y][3]:
                d[x, y] = NAVY + (255,)
    if eye is not None and tops:
        cx = (min(tops) + max(tops)) // 2
        xs = [cx + dx for dx in EYE_DX if cx + dx in tops]
        if len(xs) == len(EYE_DX):
            # Both eyes share one row. Keying each off its own column's rim would follow
            # the ragged break line and leave them on different rows — a diagonal streak
            # rather than a face. Sit just under the deeper of the two rim points, and
            # carry each column's dark interior down to meet it so the eyes read as being
            # inside the shell rather than painted on it.
            row = max(tops[x] for x in xs) + 1
            for x in xs:
                for y in range(tops[x] + 1, row + 1):
                    if 0 <= y < H and d[x, y][3]:
                        d[x, y] = NAVY + (255,)
                if 0 <= row < H and d[x, row][3]:
                    d[x, row] = eye + (255,)
    return b


def tilt(img, amount):
    W, H = img.size
    out = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    s, o = img.load(), out.load()
    for y in range(H):
        sh = int(round(amount * (y - H / 2) / (H / 2)))
        for x in range(W):
            if s[x, y][3] and 0 <= x + sh < W:
                o[x + sh, y] = s[x, y]
    return out


def write_tag(root, tag, frames, durations, direction='forward'):
    d = root / tag
    d.mkdir(parents=True, exist_ok=True)
    entries = []
    for i, im in enumerate(frames):
        name = f"{i:02d}.png"
        im.save(d / name)
        entries.append({"file": f"{tag}/{name}", "ms": durations[i]})
    return {"name": tag, "direction": direction, "frames": entries}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--assets', required=True, type=Path)
    ap.add_argument('--special', action='store_true',
                    help='use the rounder special-edition shell preset')
    args = ap.parse_args()

    sh = Shell(SHELL_SPECIAL if args.special else SHELL)

    egg_root = args.assets / 'src' / 'egg'
    tags = []
    for s in range(5):
        tags.append(write_tag(egg_root, f"stage{s}", [stage_image(sh, s)], [1000]))
    tags.append(write_tag(egg_root, "stage5",
                          [stage_image(sh, 5, eye=e) for e in BLINK_EYES],
                          BLINK_MS, direction='pingpong'))

    lid, base = split(sh)
    tags.append(write_tag(egg_root, "open",
                          [opened_base(base, eye=e) for e in BLINK_EYES],
                          BLINK_MS, direction='pingpong'))
    (egg_root / 'manifest.json').write_text(json.dumps({"tags": tags}, indent=2) + "\n")

    shell_root = args.assets / 'src' / 'eggshell'
    lid_frames = [tilt(lid, a).crop((0, 0, sh.W, LID_CROP_H)) for a in (0.0, 2.0, 3.4)]
    shell_tags = [write_tag(shell_root, "tumble", lid_frames, [70, 70, 70])]
    (shell_root / 'manifest.json').write_text(json.dumps({"tags": shell_tags}, indent=2) + "\n")

    n = sum(len(t['frames']) for t in tags)
    print(f"egg:      {n} frames across {len(tags)} tags -> {egg_root}")
    print(f"eggshell: {len(lid_frames)} frames -> {shell_root}")


if __name__ == '__main__':
    main()
