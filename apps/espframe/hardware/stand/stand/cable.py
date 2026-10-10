"""Cable path from the plug exit to the panel jack, sampled as world-frame points.

Path: plug exit -> one R bend from "down the screen" to flat-rearward -> along the
tunnel floor -> one free stadium coil in the tray (rising so the crossover
stacks) -> into the back of the jack body.
"""

import math
from dataclasses import dataclass

from build123d import Vector

from . import params as p
from .frames import screen_point


@dataclass
class CablePath:
    points: list            # world Vectors, ~1 mm apart
    length: float           # mm, plug exit to jack body
    min_radius: float       # tightest bend along the path, mm
    bend_end: int           # index of the last point of the plug bend
    drop: float             # straight drop below the plug before the bend (spare room), mm
    loop_start: int         # index of the first loop point
    loop_end: int


def _arc(cx, cy, r, a0, a1, z0, z1, n=None):
    n = n or max(8, int(abs(a1 - a0) * r))
    out = []
    for i in range(n + 1):
        t = i / n
        a = a0 + (a1 - a0) * t
        out.append((cx + r * math.cos(a), cy + r * math.sin(a), z0 + (z1 - z0) * t))
    return out


def _line(a, b, n=None):
    d = math.dist(a, b)
    n = n or max(2, int(d))
    return [tuple(a[k] + (b[k] - a[k]) * i / n for k in range(3)) for i in range(n + 1)]


def build_path() -> CablePath:
    R = p.BEND_R
    r = p.CABLE_D / 2
    x = p.PORT_X
    e0 = screen_point(x, p.PORT_MOUTH_Y - p.PLUG_OUT, p.PORT_DEPTH)
    z_floor = p.FLOOR + r
    # Straight drop down the screen (-S, -C) so the R bend ends with the cable on the tray floor.
    # The drop length is the spare room under the plug: negative would mean the bend can't fit.
    drop = (e0.Z - R * (1 + p.S) - z_floor) / p.C
    e = Vector(x, e0.Y - max(drop, 0) * p.S, e0.Z - max(drop, 0) * p.C)
    straight = _line((x, e0.Y, e0.Z), (x, e.Y, e.Z)) if drop > 0.5 else [(x, e0.Y, e0.Z)]
    # Bend in the YZ plane: start heading down the screen; centre is R towards the rear.
    cy, cz = e.Y + R * p.C, e.Z - R * p.S
    a0 = math.atan2(e.Z - cz, e.Y - cy)            # ≈ 170°
    a1 = math.radians(270)
    bend = straight[:-1] + [(x, yy, zz) for (yy, zz, _) in _arc(cy, cz, R, a0, a1, 0, 0)]
    z_top = z_floor + p.CABLE_D                     # second pass rides over the first at the crossover
    end_bend = bend[-1]
    run = _line(end_bend, (x, p.COIL_Y, z_floor))

    rc = p.COIL_W / 2 + r
    ls = p.COIL_LEN - p.COIL_W
    c1, c2 = x + rc, x + rc + ls
    seg_len = [math.pi * rc / 2, ls, math.pi * rc, ls, math.pi * rc / 2]
    total = sum(seg_len)
    zs = [z_floor]
    for s in seg_len:
        zs.append(zs[-1] + (z_top - z_floor) * s / total)
    loop = []
    loop += _arc(c1, p.COIL_Y, rc, math.pi, math.pi / 2, zs[0], zs[1])
    loop += _line((c1, p.COIL_Y + rc, zs[1]), (c2, p.COIL_Y + rc, zs[2]))
    loop += _arc(c2, p.COIL_Y, rc, math.pi / 2, -math.pi / 2, zs[2], zs[3])
    loop += _line((c2, p.COIL_Y - rc, zs[3]), (c1, p.COIL_Y - rc, zs[4]))
    loop += _arc(c1, p.COIL_Y, rc, -math.pi / 2, -math.pi, zs[4], zs[5])

    # S-curve to the inner end of the jack body: cosine profile in X (gentlest jog for its
    # length), Z eased up to the jack height. The jack sits low enough that the cable
    # stays under the lid the whole way.
    jx, jy, jz = p.JACK_X, p.FOOT_D - p.JACK_WEB_T - p.JACK_BODY_L, p.JACK_Z
    sx, sy, sz = loop[-1]
    exitp = []
    n = int(jy - sy)
    for i in range(n + 1):
        t = i / n
        s = (1 - math.cos(math.pi * t)) / 2
        exitp.append((sx + (jx - sx) * s, sy + (jy - sy) * t, sz + (jz - sz) * s))

    pts = bend + run[1:] + loop[1:] + exitp[1:]
    vecs = [Vector(*q) for q in pts]
    length = sum((vecs[i + 1] - vecs[i]).length for i in range(len(vecs) - 1))
    return CablePath(vecs, length, _min_radius(vecs), len(bend) - 1, drop, len(bend) + len(run) - 2,
                     len(bend) + len(run) + len(loop) - 3)


def _min_radius(v):
    """Smallest circumradius over point triples a few mm apart (ignores straight runs)."""
    best = math.inf
    k = 3
    for i in range(k, len(v) - k):
        a, b, c = v[i - k], v[i], v[i + k]
        ab, bc, ca = (b - a).length, (c - b).length, (a - c).length
        cross = (b - a).cross(c - a).length
        if cross < 1e-6:
            continue
        best = min(best, ab * bc * ca / (2 * cross))
    return best
