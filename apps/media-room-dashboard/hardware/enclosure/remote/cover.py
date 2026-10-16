"""Back cover: closes the back and the top end, carries the cell pocket.

Its end wall closes the shell's open top end, so with the cover off the Freenove's USB-C,
RESET and BOOT are exposed for flashing. Two tongues on the end wall slide down into slots
in the side walls (holding the top), and two screws hold the bottom.
"""

from build123d import Align, Cone, Part, Pos, RectangleRounded, extrude

from . import params as p
from . import reference as ref
from .geom import ChargerFrame, dbox, dcyl
from .shell import INNER_R, cover_boss_xy, outer_body, outline


def cover_outline(ch: p.Charger):
    """(x0, x1, y0, y1) of the cover plate: the shell's inside less COVER_CLEAR, running up into the end wall."""
    _, y_top = outline(ch)
    c = p.COVER_CLEAR
    return -p.INNER_HALF_W + c, p.INNER_HALF_W - c, p.Y_INNER_BOTTOM + c, y_top + p.END_GAP + 0.01


def pocket_ribs(ch: p.Charger) -> Part:
    """Ribs round the cell pocket. The bottom rib is full width but low: the lead passes over it."""
    pk = ref.cell_pocket(ch)
    t, d0, d1 = p.RIB_T, p.D_COVER_INNER - p.RIB_H, p.D_COVER_INNER + 0.01
    x0, x1, y0, y1 = pk.x0 - t, pk.x1 + t, pk.y0 - t, pk.y1 + t
    ribs = dbox(x0, pk.x0, y0, y1, d0, d1) + dbox(pk.x1, x1, y0, y1, d0, d1) + dbox(x0, x1, pk.y1, y1, d0, d1)
    ribs += dbox(x0, x1, y0, pk.y0, p.D_COVER_INNER - p.BOTTOM_RIB_H, d1)
    return ribs


def end_wall(ch: p.Charger) -> Part:
    """The remote's top end: the outer body above the side walls, behind the bezel lip."""
    _, y_top = outline(ch)
    region = dbox(-p.OUTER_W, p.OUTER_W, y_top + p.END_GAP, y_top + p.TOP_WALL + 5,
                  p.FRONT_T + p.END_GAP, p.D_BACK_OUTER + 5)
    return outer_body(ch) & region


def top_tongues(ch: p.Charger) -> Part:
    """A tongue on each side, reaching into the side wall's slot, carried by a web from the plate."""
    _, y_top = outline(ch)
    y1 = y_top + p.END_GAP + 0.01
    y0 = y1 - 0.01 - p.TOP_TONGUE_L
    d0, d1 = p.TOP_TONGUE_D
    out = None
    for sx in (-1, 1):
        xi = p.INNER_HALF_W - p.COVER_CLEAR - p.FIN_T        # web inner face
        xo = p.INNER_HALF_W + p.TOP_TONGUE_IN                 # tongue tip, inside the slot
        tongue = dbox(sx * xi, sx * xo, y0, y1, d0, d1)
        web = dbox(sx * xi, sx * (p.INNER_HALF_W - p.COVER_CLEAR), y0, y1, d1 - 0.01, p.D_COVER_INNER + 0.01)
        part = tongue + web
        out = part if out is None else out + part
    return out


def build_cover(ch: p.Charger) -> Part:
    x0, x1, y0, y1 = cover_outline(ch)
    plate = Pos((x0 + x1) / 2, (y0 + y1) / 2, -p.D_BACK_OUTER) * extrude(
        RectangleRounded(x1 - x0, y1 - y0, INNER_R - p.COVER_CLEAR), p.COVER_T)
    cover = plate + end_wall(ch) + pocket_ribs(ch) + top_tongues(ch)

    # Two M3 countersunk screws at the bottom into the shell's boss inserts.
    for x, y in cover_boss_xy():
        cover -= dcyl(x, y, p.D_COVER_INNER - 0.1, p.D_BACK_OUTER + 0.1, p.M3_CLEAR_D)
        r = p.CSK_D / 2
        cover -= Pos(x, y, -p.D_BACK_OUTER - 0.01) * Cone(r, r - p.CSK_DEPTH, p.CSK_DEPTH + 0.01,
                                                         align=(Align.CENTER, Align.CENTER, Align.MIN))

    # A window over the charger's CHG LED.
    f = ChargerFrame(ch)
    lx, ly = f.x(ch.chg_led[1]), f.y(ch.chg_led[0])
    cover -= dcyl(lx, ly, p.D_COVER_INNER - 0.1, p.D_BACK_OUTER + 0.1, p.LED_HOLE_D)
    return cover


def slide_in() -> float:
    """How far up the cover is laid in before it slides down into place."""
    return p.TOP_TONGUE_L + 0.5


def cover_print_pose(cover: Part) -> Part:
    """Outer face down on the bed: end wall, ribs and tongue webs build up, countersinks open onto the bed."""
    return Pos(0, 0, p.D_BACK_OUTER) * cover
