"""Cradle: a black PLA tray the acrylic case drops into from the top (open top, no top retention).

A flat 7 mm back (wall side down on the bed) carries the dovetail groove; side walls with lips
over the case's border; a floor the case stands on, with the USB-C plug's opening; a TF window in
the right wall; channels in the back for the case's screw heads (they slide down them) and in the
left wall for the LCD flex tab; vents; and an insert in the underside for the detent screw.
"""

from build123d import Axis, chamfer

from . import params as p
from . import reference as ref
from .geom import box, groove_half, step_d, step_x, step_z, trapezoid_z, wall_side_down, zcyl


def window_x():
    """(left, right) X of the case window."""
    return tuple(sorted((step_x(p.STEP_WINDOW_Y[0]), step_x(p.STEP_WINDOW_Y[1]))))


def lip_edges():
    """X of the side lips' inner edges and Z of the bottom lip's top edge."""
    wx0, wx1 = window_x()
    return wx0 - p.LIP_WINDOW_MARGIN, wx1 + p.LIP_WINDOW_MARGIN, step_z(p.STEP_WINDOW_X[0]) - p.LIP_WINDOW_MARGIN


def head_channel_x():
    return sorted({round(step_x(ys), 4) for _, ys in p.STEP_HEADS})


def build_cradle():
    outer = box(-p.X_OUT, p.X_OUT, p.D_BACK0, p.D_FRONT, 0.0, p.Z_TOP)
    front_edges = outer.edges().filter_by(Axis.Z).group_by(Axis.Y)[0] + \
        outer.edges().filter_by(Axis.X).group_by(Axis.Y)[0]
    c = chamfer(front_edges, p.EDGE_CHAMFER)

    # Case pocket: open at the top so the case slides in from above.
    c -= box(-p.X_IN, p.X_IN, p.D_BACK1, p.D_LIP0, p.Z_CASE0, p.Z_TOP + 1)
    # Front opening between the lips (open at the top too: no top lip).
    lx0, lx1, lz = lip_edges()
    c -= box(lx0, lx1, p.D_LIP0 - 1, p.D_FRONT + 1, lz, p.Z_TOP + 1)

    # Screw-head channels in the back's inner face, from the lower heads' pockets up and out the top.
    z_low = min(step_z(xs) for xs, _ in p.STEP_HEADS) - p.HEAD_POCKET_D / 2
    head_floor = p.D_CASE_BACK - p.HEAD_H - p.HEAD_POCKET_EXTRA
    for x in head_channel_x():
        c -= box(x - p.HEAD_POCKET_D / 2, x + p.HEAD_POCKET_D / 2, head_floor, p.D_BACK1 + 0.01,
                 z_low, p.Z_TOP + 1)

    # Flex-tab relief in the left wall, from the tab's final place up and out the top.
    (fx0, _), _, (fz0, fz1) = p.STEP_FLEX_TAB
    c -= box(-p.X_IN - p.FLEX_RELIEF_DEPTH, -p.X_IN + 0.01,
             step_d(fz0) - p.FLEX_RELIEF_MARGIN, step_d(fz1) + p.FLEX_RELIEF_MARGIN,
             step_z(fx0) - p.FLEX_RELIEF_MARGIN, p.Z_TOP + 1)

    # TF window in the right wall, round the case's TF slot.
    sz0, sz1 = p.STEP_SPACER_Z
    tx0, tx1 = p.STEP_TF_SLOT_X
    c -= box(p.X_IN - 0.01, p.X_OUT + 1, step_d(sz0) - p.TF_MARGIN_D, step_d(sz1) + p.TF_MARGIN_D,
             step_z(tx0) - p.TF_MARGIN_Z, step_z(tx1) + p.TF_MARGIN_Z)

    # USB-C plug opening through the floor.
    ux, ud = ref.usb_axis()
    hw, ht = p.PLUG_W / 2 + p.PLUG_CLEAR, p.PLUG_T / 2 + p.PLUG_CLEAR
    c -= box(ux - hw, ux + hw, ud - ht, ud + ht, -1, p.FLOOR_T + 0.01)

    # Dovetail groove: open at the bottom, closed at PLATE_TOP where it sits on the rail's top end.
    c -= trapezoid_z(groove_half, p.D_BACK0 - 0.5, p.D_GROOVE_FLOOR, -1, p.PLATE_TOP)

    # Vents through the back, above the plate (the groove ends at PLATE_TOP, below them).
    for side in (-1, 1):
        x0, x1 = side * p.VENT_X0, side * (p.VENT_X0 + p.VENT_W)
        z = p.VENT_Z[0]
        while z + p.VENT_H <= p.VENT_Z[1] + 1e-6:
            c -= box(x0, x1, p.D_BACK0 - 1, p.D_BACK1 + 1, z, z + p.VENT_H)
            z += p.VENT_PITCH

    # Detent insert, up into the underside.
    c -= zcyl(detent_x(), p.DETENT_D, -1, p.INSERT_HOLE_DEPTH, p.INSERT_HOLE_D)
    return c


def detent_x():
    return (p.EAR_X[0] + p.EAR_X[1]) / 2


def cradle_print_pose(cradle):
    """Wall side down: the groove opens onto the bed and its floor bridges 18 mm."""
    return wall_side_down(cradle, p.D_BACK0)


def fit_frame(cradle, depth=4.0):
    """Coupon 1: the cradle's front `depth` mm (lips, side walls, bottom lip), in the cradle's own pose
    (wall side down, lips on top), so it also shows how the lips' 3.5 mm overhang prints. Turn it over
    onto the case to check the side play and that the lips sit on the border, clear of the window."""
    return wall_side_down(cradle & box(-100, 100, p.D_FRONT - depth, p.D_FRONT + 1, -10, 300), p.D_FRONT - depth)


def bottom_slice(cradle, top=p.COUPON2_TOP):
    """Coupon 2: the cradle's bottom `top` mm (floor, plug opening, lower head channels, lower TF window,
    detent insert), as printed."""
    return cradle_print_pose(cradle & box(-100, 100, -1, 60, -10, top))

