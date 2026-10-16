"""Back cover: closes the shell, carries the cell pocket, hooks in at the top and screws down at the bottom."""

from build123d import Align, Cone, Part, Pos, RectangleRounded, extrude

from . import params as p
from . import reference as ref
from .geom import ChargerFrame, dbox, dcyl
from .shell import INNER_R, cover_boss_xy, outline, tongue_slot_x


def cover_outline(ch: p.Charger):
    """(x0, x1, y0, y1) of the cover plate: the shell's inside less COVER_CLEAR all round."""
    _, y_top = outline(ch)
    c = p.COVER_CLEAR
    return -p.INNER_HALF_W + c, p.INNER_HALF_W - c, p.Y_INNER_BOTTOM + c, y_top - c


def pocket_ribs(ch: p.Charger) -> Part:
    """Ribs round the cell pocket. The bottom rib is full width but low: the lead passes over it."""
    pk = ref.cell_pocket(ch)
    t, d0, d1 = p.RIB_T, p.D_COVER_INNER - p.RIB_H, p.D_COVER_INNER + 0.01
    x0, x1, y0, y1 = pk.x0 - t, pk.x1 + t, pk.y0 - t, pk.y1 + t
    ribs = dbox(x0, pk.x0, y0, y1, d0, d1) + dbox(pk.x1, x1, y0, y1, d0, d1) + dbox(x0, x1, pk.y1, y1, d0, d1)
    ribs += dbox(x0, x1, y0, pk.y0, p.D_COVER_INNER - p.BOTTOM_RIB_H, d1)
    return ribs


def build_cover(ch: p.Charger) -> Part:
    x0, x1, y0, y1 = cover_outline(ch)
    plate = Pos((x0 + x1) / 2, (y0 + y1) / 2, -p.D_BACK_OUTER) * extrude(
        RectangleRounded(x1 - x0, y1 - y0, INNER_R - p.COVER_CLEAR), p.COVER_T)
    cover = plate + pocket_ribs(ch)

    # Tongues on the top edge, on the inner face, reaching into the shell's slots.
    for tx in tongue_slot_x():
        cover += dbox(tx - p.TONGUE_W / 2, tx + p.TONGUE_W / 2, y1 - 0.01, y1 + p.COVER_CLEAR + p.TONGUE_L,
                      p.D_COVER_INNER - p.TONGUE_T, p.D_COVER_INNER + 0.01)

    # Two M3 countersunk screws at the bottom into the shell's boss inserts.
    for x, y in cover_boss_xy():
        cover -= dcyl(x, y, p.D_COVER_INNER - 0.1, p.D_BACK_OUTER + 0.1, p.M3_CLEAR_D)
        r = p.CSK_D / 2
        cover -= Pos(x, y, -p.D_BACK_OUTER - 0.01) * Cone(r, r - p.CSK_DEPTH, p.CSK_DEPTH + 0.01,
                                                         align=(Align.CENTER, Align.CENTER, Align.MIN))

    # Paperclip holes over RESET and BOOT, and a window over the charger's CHG LED.
    for _, x, y in ref.button_xy(ch):
        cover -= dcyl(x, y, p.D_COVER_INNER - 0.1, p.D_BACK_OUTER + 0.1, p.PINHOLE_D)
    f = ChargerFrame(ch)
    lx, ly = f.x(ch.chg_led[1]), f.y(ch.chg_led[0])
    cover -= dcyl(lx, ly, p.D_COVER_INNER - 0.1, p.D_BACK_OUTER + 0.1, p.LED_HOLE_D)
    return cover


def cover_print_pose(cover: Part) -> Part:
    """Outer face down on the bed: ribs and tongues build up, countersinks open onto the bed."""
    return Pos(0, 0, p.D_BACK_OUTER) * cover
