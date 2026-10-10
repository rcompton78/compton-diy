"""Frame helpers: screen frame (plate) to world frame (foot / desk)."""

from build123d import Box, Location, Polygon, Plane, Pos, Rot, Vector, Align, extrude

from . import params as p

# Screen frame: x across, y up the glass from its bottom edge, z = -depth behind
# the glass front. Rotating by (90 - tilt) about X maps it onto the world frame.
# The ledge surface is the screen-frame plane y = -SEAT (the frame's bottom rail sits
# on felt on it), and the point (y = -SEAT, depth 0) lands at world (Y=0, Z=LEDGE_H).
SCREEN_TO_WORLD: Location = Pos(0, 0, p.LEDGE_H) * Rot(X=90 - p.TILT_DEG) * Pos(0, p.SEAT, 0)


def screen_point(x: float, y: float, depth: float) -> Vector:
    """World position of a screen-frame point given as (x, up, depth behind glass)."""
    return (SCREEN_TO_WORLD * Pos(x, y, -depth)).position


def box(x0, x1, y0, y1, z0, z1):
    """Axis-aligned box from min/max coordinates."""
    return Pos(x0, y0, z0) * Box(x1 - x0, y1 - y0, z1 - z0, align=(Align.MIN, Align.MIN, Align.MIN))


def screen_box(x0, x1, y0, y1, d0, d1):
    """Box in the screen frame from x, up and depth ranges (depth = distance behind the glass front)."""
    return box(x0, x1, y0, y1, -max(d0, d1), -min(d0, d1))


def dovetail_prism(x_c, mouth_w, floor_w, d_mouth, d_floor, y0, y1):
    """Trapezoid prism in the screen frame, running along y.

    Width mouth_w at depth d_mouth and floor_w at depth d_floor, centred on x_c.
    """
    pts = [
        (x_c - mouth_w / 2, -d_mouth),
        (x_c + mouth_w / 2, -d_mouth),
        (x_c + floor_w / 2, -d_floor),
        (x_c - floor_w / 2, -d_floor),
    ]
    # Plane.XZ maps local (u, v) to world (x, z) and extrudes along -Y.
    solid = extrude(Plane.XZ * Polygon(*pts, align=None), amount=y1 - y0)
    bb = solid.bounding_box()
    return Pos(0, y1 - bb.max.Y, 0) * solid


def side_profile(points_yz, x0, x1):
    """Extrude a world-frame side profile (list of (Y, Z)) across X from x0 to x1."""
    solid = extrude(Plane.YZ * Polygon(*points_yz, align=None), amount=x1 - x0)
    return Pos(x0, 0, 0) * solid
