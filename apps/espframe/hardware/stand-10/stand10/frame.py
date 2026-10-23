"""Picture frame (D-sym), printed face-down in wood-look PLA.

The outer edge sits tight to the shell (FRAME_OUT outside it on the left and top) and the face
runs in over the whole black glass border to the picture, so only the picture shows, with a small
reveal. The right side matches the left face width, so the picture is centred across. The face and
side walls run down past the shell to felt on the desk, cut level, so the frame stands on the desk
in front of the foot; there's no bottom wall. The frame hangs on the back-plate (screws from behind
into inserts in wall bosses) and never touches the glass: felt on the back of the face stays
FELT_CLEAR off it.
"""

from build123d import Align, Cylinder, Part, Plane, Pos, Rectangle, Rot, extrude, loft

from . import params as p
from .frames import SCREEN_TO_WORLD, box, screen_box
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
    # The face and side walls run on down, then get cut level at the desk felt below.
    fy0 = -60.0

    # Face: straight outer band, then a 45° outer chamfer to the front surface on the top and
    # sides. The bottom edge stays square, so the face comes right down to the desk.
    face = extrude(_rect_at(ox0, ox1, fy0, oy1, z_back), z_front - ch - z_back)
    face += loft([_rect_at(ox0, ox1, fy0, oy1, z_front - ch),
                  _rect_at(ox0 + ch, ox1 - ch, fy0, oy1 - ch, z_front)])
    # Opening with a 45° bevel opening out towards the viewer.
    face -= extrude(_rect_at(px0, px1, py0, py1, z_back - 1), z_front - z_back + 2)
    face -= loft([_rect_at(px0, px1, py0, py1, z_front - bev),
                  _rect_at(px0 - bev - 0.01, px1 + bev + 0.01, py0 - bev - 0.01, py1 + bev + 0.01, z_front + 0.01)])

    # Side and top walls from the face back to the plate's back face; no bottom wall (COM-431).
    w = p.FRAME_WALL
    walls = extrude(_rect_at(ox0, ox1, fy0, oy1, -p.D_PLATE_BACK), p.D_PLATE_BACK + z_back)
    walls -= extrude(_rect_at(ox0 + w, ox1 - w, fy0 - 1, oy1 - w, -p.D_PLATE_BACK - 1), p.D_PLATE_BACK + z_back + 2)
    frame = face + walls

    # Bosses for the M3 × 4 inserts, tied to the side walls, face at the plate's front face.
    r = p.FRAME_BOSS_D / 2
    for x0, x1, xc in frame_tab_spans():
        wall_x = ox0 + w if xc < p.SHELL_W / 2 else ox1 - w
        for ty in p.TAB_Y:
            boss = Pos(xc, ty, -p.D_PLATE_FRONT) * Cylinder(r, p.FRAME_BOSS_L, align=(Align.CENTER, Align.CENTER, Align.MIN))
            web = screen_box(min(xc, wall_x), max(xc, wall_x), ty - r, ty + r,
                             p.D_PLATE_FRONT - p.FRAME_BOSS_L, p.D_PLATE_FRONT)
            frame += boss + web
            frame -= Pos(xc, ty, -p.D_PLATE_FRONT - 0.01) * Cylinder(
                p.INSERT_HOLE_D / 2, p.FRAME_INSERT_DEPTH, align=(Align.CENTER, Align.CENTER, Align.MIN))

    return frame & (SCREEN_TO_WORLD.inverse() * box(-50, 250, -100, 300, p.DESK_FELT_T, 400))


def frame_print_pose(frame: Part) -> Part:
    """Face down on the bed: bevel and outer chamfer are 45° overhangs, no supports."""
    flipped = Rot(X=180) * frame
    bb = flipped.bounding_box()
    return Pos(0, 0, -bb.min.Z) * flipped
