"""Wall plate: a flat plate with the dovetail rail, two #6 countersunk wood screws through the rail,
and the detent ear at its bottom end. The plate itself is the drilling template."""

import math

from . import params as p
from .geom import box, dcone, dcyl, rail_half, trapezoid_z, wall_side_down, zcyl


def ear_z():
    """(bottom, top) Z of the detent ear; its top clears the cradle's underside by DETENT_GAP."""
    top = -p.DETENT_GAP
    return top - p.EAR_T, top


def csk_depth():
    """Depth of the #6 countersink cone (head Ø to shank clearance Ø)."""
    return (p.WOOD_HEAD_D - p.WOOD_CLEAR_D) / 2 / math.tan(math.radians(p.WOOD_CSK_ANGLE / 2))


def build_plate(shrink=0.0):
    ez0, ez1 = ear_z()
    pl = box(-p.PLATE_W / 2, p.PLATE_W / 2, 0.0, p.D_PLATE_FACE, ez0, p.PLATE_TOP)
    pl += trapezoid_z(lambda d: rail_half(d, shrink), p.D_PLATE_FACE - 0.01, p.D_RAIL_TIP, p.RAIL_BOTTOM, p.PLATE_TOP)
    pl += box(*p.EAR_X, 0.0, p.EAR_D, ez0, ez1)
    for z in p.WOOD_SCREW_Z:
        top = p.D_RAIL_TIP - p.WOOD_HEAD_SINK
        pl -= dcyl(0.0, z, -1, p.D_RAIL_TIP + 1, p.WOOD_CLEAR_D)
        pl -= dcone(0.0, z, top - csk_depth(), top, p.WOOD_CLEAR_D, p.WOOD_HEAD_D)
        pl -= dcyl(0.0, z, top - 0.01, p.D_RAIL_TIP + 1, p.WOOD_HEAD_D)
    pl -= zcyl((p.EAR_X[0] + p.EAR_X[1]) / 2, p.DETENT_D, ez0 - 1, ez1 + 1, p.M3_CLEAR_D)
    return pl


def plate_print_pose(plate):
    """Wall side down, rail and ear up."""
    return wall_side_down(plate, 0.0)
