"""Front shell: bezel, side and bottom walls, Freenove bosses, charger standoffs and snap hooks, charge port.

The top end is open behind the bezel: its end wall is part of the back cover, so taking the
cover off exposes the Freenove's USB-C, RESET and BOOT for flashing.
"""

from build123d import Axis, Part, Plane, Pos, RectangleRounded, Rot, Rectangle, extrude, fillet, loft

from . import params as p
from . import reference as ref
from .geom import ChargerFrame, dbox, dcyl, prism_y, stadium_y

INNER_R = 1.5


def outline(ch: p.Charger):
    """(outer length, inner top y)."""
    _, _, y_inner_top, y_outer_top = p.layout(ch)
    return y_outer_top, y_inner_top


def outer_body(ch: p.Charger) -> Part:


    """The remote's outer form: rounded in plan, with rounded front and back edges."""
    length, _ = outline(ch)
    body = Pos(0, length / 2, -p.D_BACK_OUTER) * extrude(RectangleRounded(p.OUTER_W, length, p.OUTER_R), p.D_BACK_OUTER)
    faces = body.faces().sort_by(Axis.Z)
    return fillet(faces[-1].edges() + faces[0].edges(), p.EDGE_FILLET)


def cavity(ch: p.Charger, grow: float = 0.0) -> Part:
    """Inside of the walls, from behind the front wall to past the back (open for the cover).

    It runs on past the open top end, so its rounded top corners don't leave slivers of wall there.
    """
    _, y_top = outline(ch)
    y_end = y_top + 5.0
    w = 2 * p.INNER_HALF_W + 2 * grow
    ln = y_end - p.Y_INNER_BOTTOM + 2 * grow
    sk = Pos(0, (p.Y_INNER_BOTTOM + y_end) / 2, -p.D_BACK_OUTER - 1) * RectangleRounded(w, ln, INNER_R + grow)
    return extrude(sk, p.D_BACK_OUTER + 1 - p.FRONT_T)


def screen_opening(ch: p.Charger) -> Part:
    """The visible window, straight through the bezel, plus a 45° chamfer on the front."""
    y0, _, _, _ = p.layout(ch)
    yc = y0 + p.VA_FROM_BOTTOM + p.VA_L / 2
    straight = dbox(-p.VA_W / 2, p.VA_W / 2, yc - p.VA_L / 2, yc + p.VA_L / 2, -1, p.FRONT_T + 0.5)
    c = p.BEZEL_CHAMFER
    e = 0.2   # carry the cone a little past the face so the cut is clean
    lo = Plane.XY.offset(-c) * Pos(0, yc) * Rectangle(p.VA_W, p.VA_L)
    hi = Plane.XY.offset(e) * Pos(0, yc) * Rectangle(p.VA_W + 2 * (c + e), p.VA_L + 2 * (c + e))
    return straight + loft([lo, hi])


def hook(f: ChargerFrame, bx_c: float, side: int) -> Part:
    """Cantilever snap hook on one long edge of the charger, rooted in the front wall.

    Profile drawn in (x, z) for the +x side and mirrored for −x; extruded HOOK_W along y.
    """
    ch = f.ch
    xi = ch.width / 2 + p.CHARGER_EDGE_CLEAR          # arm inner face (|x|)
    xo = xi + p.HOOK_T
    xt = ch.width / 2 - p.HOOK_BARB                    # barb tip
    z_root = -p.FRONT_T + 0.01
    z_catch = -(f.d_comp + 0.05)                       # barb's catching face, just behind the component face
    reach = xi - xt
    z_top = z_catch - 0.4 - reach                      # 45° lead-in from the arm top to the tip
    prof = [(xi, z_root), (xo, z_root), (xo, z_top), (xi, z_top), (xt, z_top + reach), (xt, z_catch), (xi, z_catch)]
    if side < 0:
        prof = [(-x, z) for x, z in prof]
    return prism_y(prof, f.y(bx_c) - p.HOOK_W / 2, f.y(bx_c) + p.HOOK_W / 2)


def hook_strain(ch: p.Charger) -> tuple:
    """(strain, flex length, deflection) for one snap of a hook arm."""
    f = ChargerFrame(ch)
    length = f.d_comp + 0.05 - p.FRONT_T
    defl = p.CHARGER_EDGE_CLEAR + p.HOOK_BARB
    return 1.5 * p.HOOK_T * defl / length ** 2, length, defl


def cover_boss_xy():


    """(x, y) of the two cover-screw bosses at the bottom corners."""
    bx = p.INNER_HALF_W - p.COVER_BOSS_INSET
    return [(-bx, p.COVER_BOSS_Y), (bx, p.COVER_BOSS_Y)]


def end_region(ch: p.Charger, grow: float = 0.0) -> Part:
    """Everything above the side walls' ends and behind the bezel: the cover's end wall lives here."""
    length, y_top = outline(ch)
    return dbox(-p.OUTER_W, p.OUTER_W, y_top + grow, length + 5, p.FRONT_T + grow, p.D_BACK_OUTER + 5)


def top_slot(ch: p.Charger, sx: int) -> Part:
    """Slot in a side wall's inner face, open at its top end, for one of the cover's tongues."""
    _, y_top = outline(ch)
    c = p.TOP_SLOT_CLEAR
    x0, x1 = p.INNER_HALF_W - 0.5, p.INNER_HALF_W + p.TOP_TONGUE_IN + c
    if sx < 0:
        x0, x1 = -x1, -x0
    d0, d1 = p.TOP_TONGUE_D
    return dbox(x0, x1, y_top - p.TOP_TONGUE_L - 0.5, y_top + 1, d0 - c, d1 + c)


def build_shell(ch: p.Charger) -> Part:


    """Front shell in the assembled frame (front face at z = 0)."""
    _, y_top = outline(ch)
    shell = outer_body(ch) - cavity(ch)

    # Screen opening.
    shell -= screen_opening(ch)

    # Freenove mount bosses: from the bezel back to the PCB front, M3 × 4 insert from behind.
    for x, y in ref.fn_hole_xy(ch):
        shell += dcyl(x, y, p.FRONT_T - 0.01, p.D_PCB_FRONT, p.BOSS_D)
        shell -= dcyl(x, y, p.D_PCB_FRONT - (p.INSERT_L + 0.6), p.D_PCB_FRONT + 0.01, p.INSERT_HOLE_D)

    # Charger: standoffs under its four holes with locating pegs, and two snap hooks.
    f = ChargerFrame(ch)
    for bx, by in ch.holes:
        x, y = f.x(by), f.y(bx)
        shell += dcyl(x, y, p.FRONT_T - 0.01, p.D_CHARGER_FRONT, p.CHARGER_STANDOFF_D)
        shell += dcyl(x, y, p.D_CHARGER_FRONT - 0.01, f.d_comp + p.PEG_PROUD, p.PEG_D)
    for bx_c, side in ch.hooks:
        shell += hook(f, bx_c, side)

    # Cover screw bosses, merged into the side walls, M3 × 4 inserts from the back.
    for x, y in cover_boss_xy():
        shell += dcyl(x, y, p.FRONT_T - 0.01, p.D_COVER_INNER, p.COVER_BOSS_D)
        shell -= dcyl(x, y, p.D_COVER_INNER - (p.COVER_SCREW_L - p.COVER_T + 1.0), p.D_COVER_INNER + 0.01, p.INSERT_HOLE_D)

    # Cover-support ledges on both side walls, with a 45° underside so they print unsupported.
    w = p.LEDGE_W
    for sx in (-1, 1):
        xw = sx * p.INNER_HALF_W
        prof = [(xw, -p.D_COVER_INNER), (xw - sx * w, -p.D_COVER_INNER), (xw, -p.D_COVER_INNER + w)]
        prof = [(x + sx * 0.01, z) for x, z in prof]
        # They stop short of the cover's tongues, which slide down past them.
        y0, y1 = p.COVER_BOSS_Y + p.COVER_BOSS_D / 2, y_top - p.TOP_TONGUE_L - 1.0
        shell += prism_y(prof, y0, y1)

    # Open top end (the cover's end wall closes it), leaving the bezel running to the end as a lip.
    shell -= end_region(ch)

    # Slots for the cover's two tongues in the side walls' inner faces.
    for sx in (-1, 1):
        shell -= top_slot(ch, sx)

    # Charge port: sized for a generous cable overmold, which reaches through the wall to the mouth.
    ow, oh = p.PLUG_OVERMOLD
    cw, chh = ow + 2 * p.PLUG_CLEAR, oh + 2 * p.PLUG_CLEAR
    x, d, _ = ref.charger_usb_axis(ch)
    shell -= stadium_y(x, d, cw, chh, -2, p.Y_INNER_BOTTOM + 1.0)
    return shell


def shell_print_pose(shell: Part) -> Part:
    """Bezel face down on the bed: walls, bosses and hooks build straight up."""
    return Rot(X=180) * shell
