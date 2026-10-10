"""Test coupons: receptacle plates (the rear-wall web at three thicknesses), and the dovetail fit at three clearances."""

from build123d import Align, Cylinder, Part, Pos, Rot

from . import params as p
from .foot import build_rail
from .frames import box, dovetail_prism


def receptacle_coupon() -> Part:
    """Three thin plates (1–3 notches = RECEPTACLE_TRIAL_T thick), each with the receptacle
    window and two M3 clearance holes at the measured flange pitch. Screw one onto the
    flange with the jack's own screws: the holes check the pitch, and the plate whose face
    the metal receptacle ends flush with gives its real protrusion."""
    w, h = p.JACK_FLANGE_W + 6, 14.0
    ww, wh = p.JACK_WINDOW
    out = None
    for i, t in enumerate(p.RECEPTACLE_TRIAL_T):
        x0 = i * (w + 6)
        plate = box(x0, x0 + w, 0, h, 0, t)
        plate -= box(x0 + w / 2 - ww / 2, x0 + w / 2 + ww / 2, h / 2 - wh / 2, h / 2 + wh / 2, -1, t + 1)
        for dx in (-p.JACK_SCREW_PITCH / 2, p.JACK_SCREW_PITCH / 2):
            plate -= Pos(x0 + w / 2 + dx, h / 2, -1) * Cylinder(
                p.M3_CLEAR_D / 2, t + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))
        for k in range(i + 1):
            plate -= box(x0 + 2 + 3 * k, x0 + 3.2 + 3 * k, h - 1.5, h + 1, -1, t + 1)
        out = plate if out is None else out + plate
    return out


def dovetail_coupon(include_groove: bool = True) -> Part:
    """One groove block and one rail block per trial clearance, all in print pose.

    The groove block prints groove-down, exactly like the back-plate (the groove floor is
    a 14 mm bridge). Each rail block prints standing on its end so the rail runs up Z,
    exactly like the rail on the foot post: printed flat, the rail's angled flanks come out
    as undersized layer steps and the fit reads too loose. Notches on the top end count
    the trial: 1 notch = DOVE_TRIAL_CLEARS[0], 2 = [1], and so on.
    """
    length = 20.0
    w = 24.0
    taper = (p.DOVE_FLOOR_W - p.DOVE_MOUTH_W) / p.DOVE_DEPTH
    groove_blk = box(0, w, 0, length, 0, 8)
    groove_blk -= Pos(w / 2 - p.DOVE_X, 0, 0) * dovetail_prism(
        p.DOVE_X, p.DOVE_MOUTH_W - 0.5 * taper, p.DOVE_FLOOR_W, 0.5, -p.DOVE_DEPTH, -1, length + 1)
    parts = groove_blk if include_groove else None
    post_t = 9.0
    for i, c in enumerate(p.DOVE_TRIAL_CLEARS):
        # Built lying down (post z 0..post_t, rail on top running along Y), then stood on end.
        post = box(0, w, 0, length, 0, post_t)
        rail = Pos(w / 2 - p.DOVE_X, p.SEAT, post_t + (p.D_PLATE_BACK + p.POST_FACE_CLEAR)) * build_rail(c)
        rail = rail & box(-5, w + 5, 0, length, post_t - 0.5, 30)
        blk = post + rail
        for k in range(i + 1):   # notches across the top end (y = length) of the post
            blk -= box(2 + 3 * k, 3.2 + 3 * k, length - 1.2, length + 1, -1, post_t + 1)
        standing = Rot(X=90) * blk                      # Y (rail direction) -> Z
        bb = standing.bounding_box()
        x0 = w + 8 + i * (w + 6)
        placed = Pos(x0 - bb.min.X, -bb.min.Y + 30, -bb.min.Z) * standing
        parts = placed if parts is None else parts + placed
    return parts
