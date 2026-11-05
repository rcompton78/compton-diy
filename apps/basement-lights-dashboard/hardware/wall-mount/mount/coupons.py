"""Coupon 3: the dovetail fit at DOVE_TRIAL_CLEARS, printed in the real parts' poses.

One groove block (a 30 mm length of the cradle's back, groove sized for zero clearance, wall side
down like the cradle) and one rail block per trial clearance (a length of the plate with its rail,
narrowed by that clearance per flank, wall side down like the plate). Notches on each rail block's
top edge count the trial: 1 notch = DOVE_TRIAL_CLEARS[0], 2 = [1], 3 = [2]. Slide each rail into
the groove: the one that slides freely without rocking is the SLIDE_CLEAR to use.
"""

from . import params as p
from .geom import box, groove_half, rail_half, trapezoid_z, wall_side_down

LENGTH = 30.0
BLOCK_W = 34.0


def groove_block():
    blk = box(-BLOCK_W / 2, BLOCK_W / 2, p.D_BACK0, p.D_BACK1, 0, LENGTH)
    blk -= trapezoid_z(lambda d: groove_half(d, 0.0), p.D_BACK0 - 0.5, p.D_GROOVE_FLOOR, -1, LENGTH + 1)
    return wall_side_down(blk, p.D_BACK0)


def rail_block(clear, notches):
    blk = box(-BLOCK_W / 2, BLOCK_W / 2, 0.0, p.D_PLATE_FACE, 0, LENGTH)
    blk += trapezoid_z(lambda d: rail_half(d, clear), p.D_PLATE_FACE - 0.01, p.D_RAIL_TIP, 0, LENGTH)
    for k in range(notches):
        x = BLOCK_W / 2 - 1 - 3 * k    # outboard of the rail, and cut no higher than the plate, so the flanks stay whole
        blk -= box(x - 1.2, x, -1, p.D_PLATE_FACE + 0.01, LENGTH - 2, LENGTH + 1)
    return wall_side_down(blk, 0.0)


def dovetail_coupon():
    """All four blocks side by side on one plate."""
    from build123d import Pos
    out = groove_block()
    for i, c in enumerate(p.DOVE_TRIAL_CLEARS):
        out += Pos((i + 1) * (BLOCK_W + 6), 0, 0) * rail_block(c, i + 1)
    return out
