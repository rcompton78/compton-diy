"""Back-plate: 3 mm plate filling the frame's back, integral risers on the four M3 points, dovetail groove spine."""

from build123d import Align, Cylinder, Pos, RectangleRounded, SlotOverall, extrude, Part

from . import params as p
from .frames import dovetail_prism, screen_box


def frame_tab_spans():
    """(x0, x1, boss centre x) for the left and right screw points, screen frame."""
    inner_l = p.FRAME_X[0] + p.FRAME_WALL
    inner_r = p.FRAME_X[1] - p.FRAME_WALL
    r = p.FRAME_BOSS_D / 2
    return ((inner_l + p.PLATE_EDGE_CLEAR, inner_l + p.FRAME_BOSS_D, inner_l + r),
            (inner_r - p.FRAME_BOSS_D, inner_r - p.PLATE_EDGE_CLEAR, inner_r - r))


def plate_outline():
    """(x0, x1, y0, y1): the plate fills the frame's inner walls, less PLATE_EDGE_CLEAR."""
    c, w = p.PLATE_EDGE_CLEAR, p.FRAME_WALL
    return (p.FRAME_X[0] + w + c, p.FRAME_X[1] - w - c, p.FRAME_Y[0] + w + c, p.FRAME_Y[1] - w - c)


def build_plate() -> Part:
    """Back-plate in the screen frame (z = -depth behind the glass front)."""
    # Fills the frame out to its walls, so the back is closed with no gap round the plate.
    x0, x1, y0, y1 = plate_outline()
    outline = Pos((x0 + x1) / 2, (y0 + y1) / 2, -p.D_PLATE_BACK) * RectangleRounded(x1 - x0, y1 - y0, p.PLATE_R)
    plate = extrude(outline, p.PLATE_T)

    # Risers: from the plate front face forward to the brass spacer tops.
    for x, y in p.HOLES:
        plate += Pos(x, y, -p.D_PLATE_FRONT) * Cylinder(
            p.RISER_D / 2, p.RISER_H, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Spine: local thickening towards the panel that carries the dovetail groove.
    plate += screen_box(p.SPINE_X[0], p.SPINE_X[1], 0, p.SPINE_TOP_Y,
                        p.D_PLATE_FRONT - p.SPINE_T, p.D_PLATE_FRONT)

    # Screw holes over the picture frame's wall bosses (M3 × 8 + washer into an M3 × 4 insert).
    for x0, x1, xc in frame_tab_spans():
        for ty in p.TAB_Y:
            plate -= Pos(xc, ty, -p.D_PLATE_BACK - 1) * Cylinder(
                p.M3_CLEAR_D / 2, p.PLATE_T + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # M3 clearance holes through riser and plate.
    for x, y in p.HOLES:
        plate -= Pos(x, y, -p.D_PLATE_BACK - 1) * Cylinder(
            p.M3_CLEAR_D / 2, p.PLATE_T + p.RISER_H + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Dovetail groove, open at the bottom edge so the plate slides down onto the rail.
    # The prism starts 0.5 behind the back face (extrapolating the flank) so the cut is clean.
    taper = (p.DOVE_FLOOR_W - p.DOVE_MOUTH_W) / p.DOVE_DEPTH
    plate -= dovetail_prism(p.DOVE_X, p.DOVE_MOUTH_W - 0.5 * taper, p.DOVE_FLOOR_W,
                            p.D_PLATE_BACK + 0.5, p.D_PLATE_BACK - p.DOVE_DEPTH,
                            -1.0, p.DOVE_GROOVE_L)

    # Vent slots.
    sl, sw = p.VENT_SLOT
    for i in range(p.VENT_COLS):
        vx = p.GLASS_W / 2 + (i - (p.VENT_COLS - 1) / 2) * p.VENT_PITCH_X
        for vy in p.VENT_ROWS_Y:
            plate -= Pos(vx, vy, -p.D_PLATE_BACK - 1) * extrude(SlotOverall(sl, sw), p.PLATE_T + 2)

    # Cable notch in the bottom edge: the plug lead bends rearward just under the plate.
    nw, nh = p.PLATE_NOTCH
    plate -= screen_box(p.PORT_X - nw / 2, p.PORT_X + nw / 2, -1, nh, p.D_PLATE_FRONT - 1, p.D_PLATE_BACK + 1)

    # Optional foot-lock: M3 × 4 insert pressed in from the groove floor, through the spine.
    floor_d = p.D_PLATE_BACK - p.DOVE_DEPTH
    plate -= Pos(p.DOVE_X, p.LOCK_Y, -floor_d - 0.01) * Cylinder(
        p.INSERT_HOLE_D / 2, floor_d - (p.D_PLATE_FRONT - p.SPINE_T) + 0.5,
        align=(Align.CENTER, Align.CENTER, Align.MIN))
    return plate


def plate_print_pose(plate: Part) -> Part:
    """Back face on the bed: risers, spine and groove undercut all print without supports."""
    return Pos(0, 0, p.D_PLATE_BACK) * plate
