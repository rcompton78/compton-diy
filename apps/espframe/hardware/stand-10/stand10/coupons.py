"""Test coupons: the 6-hole template, one riser, the receptacle plates and the dovetail fit.

Print the template and the riser first: they check the board's measured hole pattern, rear-box
outline, UART port position and screw length against the real board before the big parts.
"""

from build123d import Align, Cylinder, Part, Pos, Rot

from . import params as p
from .foot import build_rail
from .frames import box, dovetail_prism


def template_on_back() -> Part:
    """The hole template where it lies on the board, in the screen frame: on the flat back, TEMPLATE_T thick.
    Six rings drop over the six bosses; a band rings the rear box TEMPLATE_BOX_CLEAR off it, with a notch in its
    bottom edge under the UART port. Two columns join the rings, the bottom rung ties the bottom pair, short stubs
    tie the middle pair to the band and a stub ties the band to the bottom rung. No rung crosses the box, and there's
    none at the top, where the LCD-FPC bracket sits between the top holes (checked: no overlap with the board)."""
    t, bar = p.TEMPLATE_T, p.TEMPLATE_BAR
    xs = sorted({x for x, _ in p.HOLES})
    ys = sorted({y for _, y in p.HOLES})
    ring_od = p.BOSS_D + 0.4 + 2 * 2.0
    c, w = p.TEMPLATE_BOX_CLEAR, 3.0
    (bx0, bx1), (by0, by1) = p.BOX_X, p.BOX_Y
    parts = [box(x - bar / 2, x + bar / 2, ys[0], ys[-1], 0, t) for x in xs]
    parts.append(box(xs[0], xs[-1], ys[0] - bar / 2, ys[0] + bar / 2, 0, t))
    parts += [Pos(x, y, 0) * Cylinder(ring_od / 2, t, align=(Align.CENTER, Align.CENTER, Align.MIN)) for x, y in p.HOLES]
    parts.append(box(bx0 - c - w, bx1 + c + w, by0 - c - w, by1 + c + w, 0, t)
                 - box(bx0 - c, bx1 + c, by0 - c, by1 + c, -1, t + 1))
    parts.append(box(xs[0], bx0 - c - w + 0.5, ys[1] - bar / 2, ys[1] + bar / 2, 0, t))
    parts.append(box(bx1 + c + w - 0.5, xs[-1], ys[1] - bar / 2, ys[1] + bar / 2, 0, t))
    mid = sum(p.BOX_X) / 2
    parts.append(box(mid - bar / 2, mid + bar / 2, ys[0], by0 - c - w + 0.5, 0, t))
    out = parts[0]
    for s in parts[1:]:
        out += s
    for x, y in p.HOLES:
        out -= Pos(x, y, -1) * Cylinder((p.BOSS_D + 0.4) / 2, t + 2, align=(Align.CENTER, Align.CENTER, Align.MIN))
    rw = p.JACK_RECEPTACLE[0]
    out -= box(p.PORT_X - rw / 2, p.PORT_X + rw / 2, by0 - c - w - 1, by0 - c + 0.01, -1, t + 1)
    # Lying on the flat back: its top face against the back, at z = -BODY_T.
    return Pos(0, 0, -p.BODY_T - t) * out


def template_coupon() -> Part:
    """The hole template in print pose: flat, mirrored to the rear view so it reads the right way round
    lying on the back."""
    mirrored = Rot(Y=180) * template_on_back()
    bb = mirrored.bounding_box()
    return Pos(-bb.min.X, -bb.min.Y, -bb.min.Z) * mirrored


def riser_coupon() -> Part:
    """One riser on a 3 mm square of plate, as on the back-plate: back face on the bed. It should drop over a
    boss, and the M2.5 × 20 + washer should pull it down without bottoming in the insert."""
    from .plate import riser_stack
    s = 22.0
    part = box(-s / 2, s / 2, -s / 2, s / 2, 0, p.PLATE_T) + Pos(0, 0, p.PLATE_T) * riser_stack()
    part -= Pos(0, 0, -1) * Cylinder(p.M25_CLEAR_D / 2, p.PLATE_T + p.RISER_H + p.RISER_SEAT[1] + 2,
                                     align=(Align.CENTER, Align.CENTER, Align.MIN))
    return part


def receptacle_coupon() -> Part:
    """Three thin plates (1–3 notches = RECEPTACLE_TRIAL_T thick), each with the receptacle
    window and two M3 clearance holes at the measured flange pitch. The jack is the 7"'s, so
    this is only a sanity check: plate 2 (1.2 mm) should end flush with the receptacle."""
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
    """One groove block and one rail block per trial clearance, all in print pose (as the 7"):
    the groove block groove-down like the back-plate, each rail block standing on end so the
    rail runs up Z like the foot post. 1–3 notches on the top end count the trial."""
    length = 20.0
    w = 24.0
    taper = (p.DOVE_FLOOR_W - p.DOVE_MOUTH_W) / p.DOVE_DEPTH
    groove_blk = box(0, w, 0, length, 0, 8)
    groove_blk -= Pos(w / 2 - p.DOVE_X, 0, 0) * dovetail_prism(
        p.DOVE_X, p.DOVE_MOUTH_W - 0.5 * taper, p.DOVE_FLOOR_W, 0.5, -p.DOVE_DEPTH, -1, length + 1)
    parts = groove_blk if include_groove else None
    post_t = 9.0
    for i, c in enumerate(p.DOVE_TRIAL_CLEARS):
        post = box(0, w, 0, length, 0, post_t)
        rail = Pos(w / 2 - p.DOVE_X, p.RISE, post_t + (p.D_PLATE_BACK + p.POST_FACE_CLEAR)) * build_rail(c)
        rail = rail & box(-5, w + 5, 0, length, post_t - 0.5, 30)
        blk = post + rail
        for k in range(i + 1):
            blk -= box(2 + 3 * k, 3.2 + 3 * k, length - 1.2, length + 1, -1, post_t + 1)
        standing = Rot(X=90) * blk
        bb = standing.bounding_box()
        x0 = w + 8 + i * (w + 6)
        placed = Pos(x0 - bb.min.X, -bb.min.Y + 30, -bb.min.Z) * standing
        parts = placed if parts is None else parts + placed
    return parts
