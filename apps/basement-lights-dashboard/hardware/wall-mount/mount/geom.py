"""Small geometry helpers in the world frame (X right, Z up, Y into the wall; d = distance off the wall)."""

import math

from build123d import Align, Box, Cone, Cylinder, Plane, Polygon, Pos, Rot, extrude

from . import params as p

MIN3 = (Align.MIN, Align.MIN, Align.MIN)
CEN_MIN = (Align.CENTER, Align.CENTER, Align.MIN)


def box(x0, x1, d0, d1, z0, z1):
    """Box from X, off-wall and Z ranges (any order)."""
    x0, x1 = sorted((x0, x1))
    d0, d1 = sorted((d0, d1))
    z0, z1 = sorted((z0, z1))
    return Pos(x0, -d1, z0) * Box(x1 - x0, d1 - d0, z1 - z0, align=MIN3)


def zcyl(x, d, z0, z1, dia):
    """Vertical cylinder at (x, d) from z0 to z1."""
    return Pos(x, -d, z0) * Cylinder(dia / 2, z1 - z0, align=CEN_MIN)


def dcyl(x, z, d0, d1, dia):
    """Cylinder along the wall normal at (x, z), from d0 to d1 off the wall."""
    return Pos(x, -d0, z) * Rot(X=90) * Cylinder(dia / 2, d1 - d0, align=CEN_MIN)


def dcone(x, z, d0, d1, dia0, dia1):
    """Cone along the wall normal: Ø dia0 at d0, Ø dia1 at d1."""
    return Pos(x, -d0, z) * Rot(X=90) * Cone(dia0 / 2, dia1 / 2, d1 - d0, align=CEN_MIN)


def trapezoid_z(half_w, d0, d1, z0, z1, x_c=0.0):
    """Prism along Z whose cross-section has half-width half_w(d) at d0 and d1 (linear between)."""
    pts = [(x_c - half_w(d0), -d0), (x_c + half_w(d0), -d0), (x_c + half_w(d1), -d1), (x_c - half_w(d1), -d1)]
    # Explicit +Z: the polygon's winding (d runs to -Y) would otherwise extrude it downwards.
    return Pos(0, 0, z0) * extrude(Plane.XY * Polygon(*pts, align=None), amount=z1 - z0, dir=(0, 0, 1))


# --- Dovetail profile -------------------------------------------------------------------------------
FLANK = math.atan2((p.RAIL_TIP_W - p.RAIL_ROOT_W) / 2, p.RAIL_D)   # flank angle off the wall normal


def rail_half(d, shrink=0.0):
    """Rail half-width at d off the wall, narrowed by a perpendicular `shrink` per flank."""
    return p.RAIL_ROOT_W / 2 + (d - p.D_PLATE_FACE) * math.tan(FLANK) - shrink / math.cos(FLANK)


def groove_half(d, clear=p.SLIDE_CLEAR):
    """Groove half-width: the rail's flank moved out by `clear` measured square to the flank."""
    return rail_half(d) + clear / math.cos(FLANK)


# --- STEP (landscape case) to world -----------------------------------------------------------------
def step_x(ys):
    """World X of a STEP ys (the STEP's top edge turns to the left side)."""
    return -(ys - p.STEP_YC)


def step_z(xs):
    """World Z of a STEP xs (the STEP's left edge, USB-C, turns to the bottom)."""
    return p.Z_CASE_MID + xs


def step_d(zs):
    """Distance off the wall of a STEP zs (case back plate at D_CASE_BACK)."""
    return p.D_CASE_BACK + (zs - p.STEP_CASE_BACK_Z)


def step_box(xs, ys, zs):
    """World box of a STEP axis-aligned box given as (xs range, ys range, zs range)."""
    return box(step_x(ys[0]), step_x(ys[1]), step_d(zs[0]), step_d(zs[1]), step_z(xs[0]), step_z(xs[1]))


# --- Print poses ------------------------------------------------------------------------------------
def wall_side_down(solid, d_bed):
    """Lay a part on the bed with its face at `d_bed` off the wall down (the wall side), parts towards the room up."""
    return Pos(0, 0, -d_bed) * Rot(X=-90) * solid

