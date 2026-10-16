"""Small geometry helpers shared by the parts, reference models and checks."""

from build123d import Align, Box, Cylinder, Plane, Polygon, Pos, Rot, SlotOverall, extrude

from . import params as p


def box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box from min/max coordinates."""
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=(Align.MIN, Align.MIN, Align.MIN))


def dbox(x0, x1, y0, y1, d0, d1):
    """Box from x, y and depth ranges (depth = distance behind the front face, z = -depth)."""
    return box(x0, x1, y0, y1, -max(d0, d1), -min(d0, d1))


def dcyl(x, y, d0, d1, dia):
    """Cylinder along the depth axis, from depth d0 to d1."""
    lo, hi = min(d0, d1), max(d0, d1)
    return Pos(x, y, -hi) * Cylinder(dia / 2, hi - lo, align=(Align.CENTER, Align.CENTER, Align.MIN))


def stadium_y(xc, dc, w, h, y0, y1):
    """Stadium (rounded-end slot) w wide across x and h tall in depth, running along y from y0 to y1."""
    lo, hi = min(y0, y1), max(y0, y1)
    # Rot(X=-90) turns the sketch's extrusion direction (+Z) into +Y.
    return Pos(xc, lo, -dc) * Rot(X=-90) * extrude(SlotOverall(w, h), hi - lo)


def prism_y(profile_xz, y0, y1):
    """Extrude a closed (x, z) profile along y from y0 to y1.

    The extrusion follows the face normal, whose sign depends on the profile's winding,
    so place the result by its bounding box rather than assuming a direction.
    """
    solid = extrude(Plane.XZ * Polygon(*profile_xz, align=None), amount=abs(y1 - y0))
    return Pos(0, min(y0, y1) - solid.bounding_box().min.Y, 0) * solid


class ChargerFrame:
    """Maps charger-board coordinates onto the enclosure.

    bx runs up the remote (USB-C end at the bottom), by across it, centred; h is height
    above the component face, which faces the back cover (+depth).
    """

    def __init__(self, ch: p.Charger):
        self.ch = ch
        self.y0 = p.charger_y0(ch)
        self.d_comp = p.D_CHARGER_FRONT + ch.pcb_t

    def x(self, by):
        return by - self.ch.width / 2

    def y(self, bx):
        return self.y0 + bx

    def d(self, h):
        return self.d_comp + h

    def box(self, bx0, bx1, by0, by1, h0, h1):
        return dbox(self.x(by0), self.x(by1), self.y(bx0), self.y(bx1), self.d(h0), self.d(h1))
