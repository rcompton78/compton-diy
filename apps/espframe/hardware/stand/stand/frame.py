"""Picture frame (style B, classic bevel, 21 mm face), printed face-down in wood-look PLA.

The opening is the active area plus a small reveal, and the face is the same width all
round, so the uneven black bezel reads as an even border. The frame hangs on four tabs
on the back-plate (screws from behind into inserts in wall bosses) and its bottom rail
sits on the felted ledge of the foot. It never touches the glass: felt on the back of the
face stays FELT_CLEAR off the glass, and the walls clear the glass edges.
"""

from build123d import Align, Cylinder, Part, Plane, Pos, Rectangle, Rot, extrude, loft

from . import params as p
from .frames import screen_box
from .plate import frame_tab_spans


def _rect_at(x0, x1, y0, y1, z):
    return Plane.XY.offset(z) * Pos((x0 + x1) / 2, (y0 + y1) / 2) * Rectangle(x1 - x0, y1 - y0)


def build_frame() -> Part:
    """Frame in the screen frame (z = -depth behind the glass front; the face is at z > 0)."""
    (ox0, ox1), (oy0, oy1) = p.FRAME_X, p.FRAME_Y
    (px0, px1), (py0, py1) = p.OPEN_X, p.OPEN_Y
    z_back = p.FRAME_FACE_GAP                       # face rear surface (towards the glass)
    z_front = z_back + p.FRAME_FACE_T
    ch, bev = p.FRAME_OUTER_CH, p.FRAME_BEVEL

    # Face: straight outer band, then a 45° outer chamfer to the front surface.
    face = extrude(_rect_at(ox0, ox1, oy0, oy1, z_back), z_front - ch - z_back)
    face += loft([_rect_at(ox0, ox1, oy0, oy1, z_front - ch),
                  _rect_at(ox0 + ch, ox1 - ch, oy0 + ch, oy1 - ch, z_front)])
    # Opening with a 45° bevel opening out towards the viewer.
    face -= extrude(_rect_at(px0, px1, py0, py1, z_back - 1), z_front - z_back + 2)
    face -= loft([_rect_at(px0, px1, py0, py1, z_front - bev),
                  _rect_at(px0 - bev - 0.01, px1 + bev + 0.01, py0 - bev - 0.01, py1 + bev + 0.01, z_front + 0.01)])

    # Walls from the face back to the plate's back face.
    w = p.FRAME_WALL
    walls = extrude(_rect_at(ox0, ox1, oy0, oy1, -p.D_PLATE_BACK), p.D_PLATE_BACK + z_back)
    walls -= extrude(_rect_at(ox0 + w, ox1 - w, oy0 + w, oy1 - w, -p.D_PLATE_BACK - 1), p.D_PLATE_BACK + z_back + 2)
    frame = face + walls

    # Bosses for the M3 × 4 inserts, tied to the side walls, face at the plate's front face.
    r = p.FRAME_BOSS_D / 2
    for x0, x1, xc in frame_tab_spans():
        wall_x = ox0 + w if xc < p.GLASS_W / 2 else ox1 - w
        for ty in p.TAB_Y:
            boss = Pos(xc, ty, -p.D_PLATE_FRONT) * Cylinder(r, p.FRAME_BOSS_L, align=(Align.CENTER, Align.CENTER, Align.MIN))
            web = screen_box(min(xc, wall_x), max(xc, wall_x), ty - r, ty + r,
                             p.D_PLATE_FRONT - p.FRAME_BOSS_L, p.D_PLATE_FRONT)
            frame += boss + web
            frame -= Pos(xc, ty, -p.D_PLATE_FRONT - 0.01) * Cylinder(
                p.INSERT_HOLE_D / 2, p.FRAME_INSERT_DEPTH, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Cable notch through the bottom rail, behind the glass, where the plug lead drops into the foot.
    frame -= screen_box(p.PORT_X - p.FRAME_NOTCH_W / 2, p.PORT_X + p.FRAME_NOTCH_W / 2, oy0 - 1, oy0 + w + 0.5,
                        p.SLOT_T[0] - 1, p.D_PLATE_BACK + 1)
    # Notch round the foot's dovetail rail, which rises from the ledge through the bottom rail.
    frame -= screen_box(p.DOVE_X - p.DOVE_FLOOR_W / 2 - 1.0, p.DOVE_X + p.DOVE_FLOOR_W / 2 + 1.0, oy0 - 1, oy0 + w + 0.5,
                        p.D_PLATE_BACK - p.DOVE_DEPTH - 0.5, p.D_PLATE_BACK + 1)
    return frame


def frame_print_pose(frame: Part) -> Part:
    """Face down on the bed: bevel and outer chamfer are 45° overhangs, no supports."""
    flipped = Rot(X=180) * frame
    bb = flipped.bounding_box()
    return Pos(0, 0, -bb.min.Z) * flipped
