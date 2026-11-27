"""Landscape stand (COM-684). Foot: a body wholly behind the plate (no shelf under the glass: the cable comes down
behind the plate, not under it) with the dovetail post on the right, the cable dropping straight into the open tray on the left,
through a notch in the lid's front edge, for a free cable coil, the panel jack behind a thin web in the rear wall, and the click-in
tray lid. The frame stands on the desk; the foot only holds the lean."""

from dataclasses import dataclass

from build123d import Align, Cylinder, Part, Pos, Rot

from . import params as p
from .frames import SCREEN_TO_WORLD, box, dovetail_prism, screen_box, side_profile


@dataclass(frozen=True)
class FootLayout:
    """Derived foot positions shared by the model, the cable path and the checks."""
    x0: float            # foot outer left edge (world X)
    x1: float            # foot outer right edge
    in_x0: float         # tray inner left wall
    in_x1: float         # tray inner right wall
    z_deck: float        # deck height behind the plate (world Z)
    z_cavity_top: float  # underside of the lid
    front_zone_end: float


def layout() -> FootLayout:
    # As wide as the frame and on its centre line (COM-442).
    x0, x1 = p.FRAME_X
    in_x0, in_x1 = x0 + p.WALL, x1 - p.WALL
    z_deck = p.DECK_Z
    return FootLayout(x0, x1, in_x0, in_x1, z_deck, z_deck - p.LID_T, p.FRONT_ZONE_D)


def _desk_body(L: FootLayout) -> Part:
    """Body: a block behind the plate up to the deck, its front face POST_FACE_CLEAR off the plate's back."""
    d_back = p.D_PLATE_BACK + p.POST_FACE_CLEAR
    return box(L.x0, L.x1, -60, p.FOOT_D, 0, L.z_deck) & (
        SCREEN_TO_WORLD * screen_box(-50, 300, -300, 400, d_back, 400))


def build_foot() -> Part:
    L = layout()
    foot = _desk_body(L)

    # Open-topped tray behind the front zone, with a rabbet for the lid.
    tray = box(L.in_x0, L.in_x1, L.front_zone_end, p.FOOT_D - p.WALL, p.FLOOR, L.z_deck + 1)
    rabbet = box(L.in_x0 - p.LID_RABBET, L.in_x1 + p.LID_RABBET, L.front_zone_end - p.LID_RABBET,
                 p.FOOT_D - p.WALL + p.LID_RABBET, L.z_cavity_top, L.z_deck + 1)
    foot -= tray + rabbet
    # V-grooves in the rabbet's front and rear walls for the lid's detent ridges.
    for x0, x1, y_edge, out in lid_detents():
        g = p.LID_DETENT_GROOVE_CLEAR
        foot -= _detent_prism(x0 - g, x1 + g, y_edge, out, p.LID_DETENT_H + g)

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
    post = screen_box(p.DOVE_X - p.POST_W / 2, p.DOVE_X + p.POST_W / 2, -12 - p.RISE, p.POST_H - p.RISE,
                      post_d0, post_d0 + p.POST_T)
    rail = build_rail()
    foot += SCREEN_TO_WORLD * (post + rail)
    # Optional foot-lock, cut after the union: the post's back is buried in the body, so the counterbore carries on
    # rearward as an access bore into the tray, where the M3 × 12 goes in with the lid off.
    foot -= SCREEN_TO_WORLD * lock_bore()
    # Keep the post's buried lower end above the desk.
    foot = foot & box(L.x0 - 1, L.x1 + 1, -50, p.FOOT_D + 1, 0, 300)
    return foot


def lock_floor_depth() -> float:
    """Depth of the foot-lock counterbore's floor: LOCK_POST_LEFT of post left between it and the post front."""
    return p.D_PLATE_BACK + p.POST_FACE_CLEAR + p.LOCK_POST_LEFT


def lock_bore() -> Part:
    """The optional foot-lock's M3 clearance hole through the post and rail, its counterbore, and the access bore
    from the counterbore rearward into the tray (screen frame)."""
    post_d0 = p.D_PLATE_BACK + p.POST_FACE_CLEAR
    back = post_d0 + p.POST_T
    bore = Pos(p.DOVE_X, p.LOCK_Y, -back - 1) * Cylinder(
        p.M3_CLEAR_D / 2, p.POST_T + p.DOVE_DEPTH + 3, align=(Align.CENTER, Align.CENTER, Align.MIN))
    head_d = lock_floor_depth()
    bore += Pos(p.DOVE_X, p.LOCK_Y, -(head_d + p.LOCK_ACCESS_L)) * Cylinder(
        p.LOCK_HEAD_D / 2, p.LOCK_ACCESS_L, align=(Align.CENTER, Align.CENTER, Align.MIN))
    return bore


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
                          d_root, d_tip, -p.RISE, p.DOVE_RAIL_L - p.RISE)


def lid_outline():
    """(x0, x1, y0, y1) of the lid, LID_CLEAR inside the rabbet on every side."""
    L = layout()
    c = p.LID_CLEAR
    return (L.in_x0 - p.LID_RABBET + c, L.in_x1 + p.LID_RABBET - c,
            L.front_zone_end - p.LID_RABBET + c, p.FOOT_D - p.WALL + p.LID_RABBET - c)


def lid_notch():
    """(x0, x1, y1) of the lid's cable notch, open to its front edge: every cable point within the lid's thickness
    (plus the cable radius), with LID_NOTCH_CLEAR round it. The plug passes the open tray with the lid off."""
    from .cable import build_path
    L = layout()
    r, c = p.CABLE_D / 2, p.LID_NOTCH_CLEAR
    path = build_path()
    pts = [v for v in path.points[: path.bend_end + 1]
           if L.z_cavity_top - r <= v.Z <= L.z_deck + r]
    return (min(v.X for v in pts) - r - c, max(v.X for v in pts) + r + c, max(v.Y for v in pts) + r + c)


def lift_slot_x():
    """(x0, x1) of the lid's lift slot, at its front-right corner."""
    L = layout()
    return L.in_x1 - 14, L.in_x1 - 4


def lid_detents():
    """(x0, x1, y_edge, out) per detent ridge: LID_DETENT_N evenly spaced along each long
    (front and rear) lid edge; `out` is +1 where the ridge points to +Y, -1 to -Y."""
    x0, x1, y0, y1 = lid_outline()
    pitch = (x1 - x0) / p.LID_DETENT_N
    spans = [(x0 + (i + 0.5) * pitch - p.LID_DETENT_L / 2, x0 + (i + 0.5) * pitch + p.LID_DETENT_L / 2)
             for i in range(p.LID_DETENT_N)]
    return [(a, b, y, out) for y, out in ((y0, -1), (y1, 1)) for a, b in spans]


def _detent_prism(x0, x1, y_edge, out, h):
    """Triangular ridge across X: base on the lid edge over the lid's full thickness, apex h out.
    The shallow flanks ramp the snap in and out, and print without supports."""
    L = layout()
    zm, half = (L.z_cavity_top + L.z_deck) / 2, p.LID_T / 2
    base = y_edge - out * 0.01
    pts = [(base, zm - half), (y_edge + out * h, zm), (base, zm + half)]
    # side_profile extrudes along the face normal, so keep the winding anticlockwise (+X).
    return side_profile(pts if out > 0 else pts[::-1], x0, x1)


def build_lid() -> Part:
    """Flat lid for the tray opening, with detent ridges on its long edges. Printed flat."""
    L = layout()
    x0, x1, y0, y1 = lid_outline()
    lid = box(x0, x1, y0, y1, L.z_cavity_top, L.z_deck)
    for a, b, y_edge, out in lid_detents():
        lid += _detent_prism(a, b, y_edge, out, p.LID_DETENT_H)
    # Cable notch in the front edge, where the cable drops into the tray behind the plate.
    nx0, nx1, ny1 = lid_notch()
    lid -= box(nx0, nx1, y0 - 1, ny1, L.z_cavity_top - 1, L.z_deck + 1)
    # Lift slot so it can be prised out.
    sx0, sx1 = lift_slot_x()
    lid -= box(sx0, sx1, L.front_zone_end + 1, L.front_zone_end + 5, L.z_cavity_top - 1, L.z_deck + 1)
    return lid


def lid_print_pose(lid: Part) -> Part:
    return Pos(0, 0, -layout().z_cavity_top) * lid
