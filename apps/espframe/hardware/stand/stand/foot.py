"""Foot: felted ledge, dovetail post, cable tunnel and open tray for a free cable coil,
a solid front, the panel jack behind a thin web in the rear wall, and the tray lid."""

from dataclasses import dataclass

from build123d import Align, Cylinder, Part, Pos, Rot

from . import params as p
from .frames import SCREEN_TO_WORLD, box, dovetail_prism, screen_box, screen_point, side_profile


@dataclass(frozen=True)
class FootLayout:
    """Derived foot positions shared by the model, the cable path and the checks."""
    x0: float            # foot outer left edge (world X)
    x1: float            # foot outer right edge
    in_x0: float         # tray inner left wall
    in_x1: float         # tray inner right wall
    y_front: float       # front face of the lip (world Y)
    z_deck: float        # deck height behind the plate (world Z)
    z_cavity_top: float  # underside of the lid
    front_zone_end: float
    coil_cx: float       # stadium coil centre X


def layout() -> FootLayout:
    side = (p.FOOT_W - p.GLASS_W) / 2
    x0, x1 = -side, p.GLASS_W + side
    in_x0, in_x1 = x0 + p.WALL, x1 - p.WALL
    front = screen_point(0, -p.SEAT, -(p.FRAME_FACE_GAP + p.FRAME_FACE_T + p.LEDGE_OVER))
    z_deck = screen_point(0, -p.SEAT, p.D_PLATE_BACK + p.POST_FACE_CLEAR).Z
    rc = p.COIL_W / 2 + p.CABLE_D / 2
    coil_cx = p.PORT_X + rc + (p.COIL_LEN - p.COIL_W) / 2
    return FootLayout(x0, x1, in_x0, in_x1, front.Y, z_deck, z_deck - p.LID_T,
                      p.FRONT_ZONE_D, coil_cx)


def _sp(y, d):
    v = screen_point(0, y, d)
    return (v.Y, v.Z)


def build_foot() -> Part:
    L = layout()

    # Body: side profile extruded across the width. The ledge is a flat seat (with felt)
    # under the picture frame's bottom rail, from just in front of the frame face back
    # to the plate; no lip, so nothing touches the glass.
    seat_front = _sp(-p.SEAT, -(p.FRAME_FACE_GAP + p.FRAME_FACE_T + p.LEDGE_OVER))
    profile = [
        (seat_front[0], 0.0),
        (p.FOOT_D, 0.0),
        (p.FOOT_D, L.z_deck),
        _sp(-p.SEAT, p.D_PLATE_BACK + p.POST_FACE_CLEAR),
        seat_front,
    ]
    foot = side_profile(profile, L.x0, L.x1)

    # Open-topped tray behind the front zone, with a rabbet for the lid.
    tray = box(L.in_x0, L.in_x1, L.front_zone_end, p.FOOT_D - p.WALL, p.FLOOR, L.z_deck + 1)
    rabbet = box(L.in_x0 - p.LID_RABBET, L.in_x1 + p.LID_RABBET, L.front_zone_end - p.LID_RABBET,
                 p.FOOT_D - p.WALL + p.LID_RABBET, L.z_cavity_top, L.z_deck + 1)
    foot -= tray + rabbet

    # Cable tunnel under the wedge, from the deck slot back into the tray.
    foot -= box(p.PORT_X - 6, p.PORT_X + 6, L.y_front + p.WALL + 2, L.front_zone_end + 1,
                p.FLOOR, L.z_cavity_top)

    # Deck slot through the ledge, big enough for the plug to pass during assembly,
    # and covering the cable's bend through the deck.
    slot = SCREEN_TO_WORLD * screen_box(p.PORT_X - p.SLOT_W / 2, p.PORT_X + p.SLOT_W / 2,
                                        -30 - p.SEAT, 1, p.SLOT_T[0], p.SLOT_T[1])
    foot -= slot & box(L.x0 - 1, L.x1 + 1, -50, 200, p.FLOOR, 100)

    # Rear wall at the jack: a flange recess in the inside face leaves a thin web; the
    # receptacle pokes through a window in it, and two clearance holes take the jack's own
    # screws from outside into the flange's threaded holes.
    cx = p.JACK_X
    pw, ph = p.JACK_POCKET
    foot -= box(cx - pw / 2, cx + pw / 2, p.FOOT_D - p.WALL - 1, p.FOOT_D - p.JACK_WEB_T,
                p.JACK_Z - ph / 2, p.JACK_Z + ph / 2)
    ww, wh = p.JACK_WINDOW
    foot -= box(cx - ww / 2, cx + ww / 2, p.FOOT_D - p.WALL, p.FOOT_D + 1, p.JACK_Z - wh / 2, p.JACK_Z + wh / 2)
    for dx in (-p.JACK_SCREW_PITCH / 2, p.JACK_SCREW_PITCH / 2):
        foot -= Pos(cx + dx, p.FOOT_D + 1, p.JACK_Z) * Rot(X=90) * Cylinder(
            p.M3_CLEAR_D / 2, p.WALL + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Dovetail post and rail behind the plate (screen frame, then placed).
    post_d0 = p.D_PLATE_BACK + p.POST_FACE_CLEAR
    post = screen_box(p.DOVE_X - p.POST_W / 2, p.DOVE_X + p.POST_W / 2, -12 - p.SEAT, p.POST_H - p.SEAT,
                      post_d0, post_d0 + p.POST_T)
    rail = build_rail()
    # Optional foot-lock: counterbored M3 clearance hole through post and rail.
    lock_len = p.POST_T + p.DOVE_DEPTH + 2
    lock = Pos(p.DOVE_X, p.LOCK_Y, -(post_d0 + p.POST_T) - 1) * Cylinder(
        p.M3_CLEAR_D / 2, lock_len + 1, align=(Align.CENTER, Align.CENTER, Align.MIN))
    lock += Pos(p.DOVE_X, p.LOCK_Y, -(post_d0 + p.POST_T) - 1) * Cylinder(
        p.LOCK_HEAD_D / 2, p.POST_T - p.LOCK_POST_LEFT + 1, align=(Align.CENTER, Align.CENTER, Align.MIN))
    foot += SCREEN_TO_WORLD * (post + rail - lock)
    # Keep the post's buried lower end above the desk.
    foot = foot & box(L.x0 - 1, L.x1 + 1, -50, p.FOOT_D + 1, 0, 300)
    return foot


def build_rail(clear: float = p.SLIDE_CLEAR) -> Part:
    """Male dovetail rail on the post front face, in the screen frame."""
    import math
    post_d0 = p.D_PLATE_BACK + p.POST_FACE_CLEAR
    taper = (p.DOVE_FLOOR_W - p.DOVE_MOUTH_W) / p.DOVE_DEPTH
    flank = math.atan(taper / 2)                 # flank angle from the depth axis
    shrink = 2 * clear / math.cos(flank)          # width loss for a perpendicular gap of `clear`

    def groove_w(d):
        return p.DOVE_MOUTH_W + taper * (p.D_PLATE_BACK - d)

    d_root, d_tip = post_d0 + 0.5, p.D_PLATE_BACK - p.DOVE_DEPTH + max(clear, p.DOVE_TIP_CLEAR)
    return dovetail_prism(p.DOVE_X, groove_w(d_root) - shrink, groove_w(d_tip) - shrink,
                          d_root, d_tip, -p.SEAT, p.DOVE_RAIL_L - p.SEAT)


def build_lid() -> Part:
    """Flat lid for the tray opening. Printed flat."""
    L = layout()
    c = p.LID_CLEAR
    lid = box(L.in_x0 - p.LID_RABBET + c, L.in_x1 + p.LID_RABBET - c,
              L.front_zone_end - p.LID_RABBET + c, p.FOOT_D - p.WALL + p.LID_RABBET - c,
              L.z_cavity_top, L.z_deck)
    # Lift slot so it can be prised out.
    lid -= box(L.in_x1 - 14, L.in_x1 - 4, L.front_zone_end + 1, L.front_zone_end + 5, L.z_cavity_top - 1, L.z_deck + 1)
    return lid


def lid_print_pose(lid: Part) -> Part:
    return Pos(0, 0, -layout().z_cavity_top) * lid
