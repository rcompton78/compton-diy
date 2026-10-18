"""Reference (non-printed) parts: the boards, the cell, plug keep-outs and the wire runs.

Used for preview renders and fit checks only.
"""

from dataclasses import dataclass

import numpy as np
from build123d import Part, Pos, RectangleRounded, extrude

from . import params as p
from .geom import ChargerFrame, dbox, dcyl, stadium_y


# --- Freenove FNK0104 ------------------------------------------------------------------
def fn_hole_xy(ch: p.Charger):
    """(x, y) of the Freenove's four mounting holes."""
    y0, y1, _, _ = p.layout(ch)
    hx = p.PCB_W / 2 - p.HOLE_INSET
    return [(sx * hx, y) for y in (y0 + p.HOLE_INSET, y1 - p.HOLE_INSET) for sx in (-1, 1)]


def fn_pcb(ch: p.Charger) -> Part:


    """The Freenove's PCB, with its mounting holes."""
    y0, _, _, _ = p.layout(ch)
    pcb = Pos(0, y0 + p.PCB_L / 2, -p.D_PCB_BACK) * extrude(RectangleRounded(p.PCB_W, p.PCB_L, p.PCB_R), p.PCB_T)
    for x, y in fn_hole_xy(ch):
        pcb -= dcyl(x, y, p.D_PCB_FRONT - 1, p.D_PCB_BACK + 1, p.HOLE_D)
    return pcb


def fn_glass(ch: p.Charger) -> Part:
    """Touch glass + LCD stack, hanging off the PCB front (it floats GLASS_FLOAT behind the bezel)."""
    y0, _, _, _ = p.layout(ch)
    g0 = y0 + p.GLASS_FROM_BOTTOM
    return dbox(-p.GLASS_W / 2, p.GLASS_W / 2, g0, g0 + p.GLASS_L, p.D_PCB_FRONT - p.STACK_T, p.D_PCB_FRONT)


def fn_back_keepout(ch: p.Charger) -> Part:
    """Everything behind the PCB, up to the drawing's 4.7 mm max part height, over the whole footprint."""
    y0, _, _, _ = p.layout(ch)
    return Pos(0, y0 + p.PCB_L / 2, -p.D_PARTS_BACK) * extrude(RectangleRounded(p.PCB_W, p.PCB_L, p.PCB_R), p.BACK_PARTS_H)


def fn_usb_envelope(ch: p.Charger) -> Part:
    """Room reserved for the USB-C receptacle, whatever its real size."""
    _, y1, y_top, _ = p.layout(ch)
    w, h = p.FN_USB_ENV_W, p.FN_USB_ENV_H
    return dbox(p.FN_USB_X - w / 2, p.FN_USB_X + w / 2, y1 - p.FN_USB_L - 1, y_top, p.D_PCB_BACK, p.D_PCB_BACK + h)


def top_access_zone(ch: p.Charger) -> Part:
    """The back of the board's top end (USB-C, RESET, BOOT), out past the back: open once the cover's off."""
    _, y1, y_top, _ = p.layout(ch)
    hw = p.PCB_W / 2 - 1.5
    return dbox(-hw, hw, y1 - p.TOP_ACCESS, y_top + 30, p.D_PCB_BACK, p.D_BACK_OUTER + 30)


def bat_envelope(ch: p.Charger) -> Part:
    x, y = bat_socket_xy(ch)
    ex, ey, eh = p.BAT_ENVELOPE
    return dbox(x - ex / 2, min(x + ex / 2, p.INNER_HALF_W - 0.1), y - ey / 2, y + ey / 2, p.D_PCB_BACK, p.D_PCB_BACK + eh)


def terminal_pins(ch: p.Charger) -> Part:
    f = ChargerFrame(ch)
    bx0, bx1, by0, by1, ln = ch.terminal_pins
    return f.box(bx0, bx1, by0, by1, -ch.pcb_t - ln, -ch.pcb_t)


def fn_usb_mouth_y(ch: p.Charger) -> float:
    _, y1, _, _ = p.layout(ch)
    return y1 + p.FN_USB_OVERHANG


def fn_usb(ch: p.Charger) -> Part:


    """Nominal USB-C receptacle on the Freenove's back, for renders."""
    y_mouth = fn_usb_mouth_y(ch)
    w = p.FN_USB_W
    return dbox(p.FN_USB_X - w / 2, p.FN_USB_X + w / 2, y_mouth - p.FN_USB_L, y_mouth,
                p.D_PCB_BACK, p.D_PCB_BACK + p.FN_USB_H)


def fn_usb_axis_d() -> float:


    """Depth of the Freenove USB-C's axis."""
    return p.D_PCB_BACK + p.FN_USB_H / 2


def bat_socket_xy(ch: p.Charger):


    """(x, y) of the Freenove's BAT socket centre."""
    _, y1, _, _ = p.layout(ch)
    return p.PCB_W / 2 - p.BAT_FROM_EDGE, y1 - p.BAT_FROM_TOP


def fn_bat_socket(ch: p.Charger) -> Part:
    x, y = bat_socket_xy(ch)
    sx, sy, sh = p.BAT_SOCKET
    return dbox(x - sx / 2, x + sx / 2, y - sy / 2, y + sy / 2, p.D_PCB_BACK, p.D_PCB_BACK + sh)


def fn_screw_heads(ch: p.Charger) -> Part:
    """M3 × 6 pan heads on the PCB back."""
    out = None
    for x, y in fn_hole_xy(ch):
        h = dcyl(x, y, p.D_PCB_BACK, p.D_PCB_BACK + 2.1, 5.5)
        out = h if out is None else out + h
    return out


def freenove(ch: p.Charger) -> Part:
    """PCB, glass and the parts that stand out from the 4.7 mm keep-out (for renders)."""
    return fn_pcb(ch) + fn_glass(ch) + fn_usb(ch) + fn_bat_socket(ch) + fn_screw_heads(ch)


def active_area(ch: p.Charger) -> Part:
    """The lit pixels, extruded forward of the glass: nothing may cover them."""
    y0, _, _, _ = p.layout(ch)
    a0 = y0 + p.AA_FROM_BOTTOM
    return dbox(-p.AA_W / 2, p.AA_W / 2, a0, a0 + p.AA_L, -10, p.D_PCB_FRONT - p.STACK_T)


# --- Charger ------------------------------------------------------------------------------
def charger(ch: p.Charger) -> Part:
    """The charger board with its USB-C, JST-PH socket, screw terminal and the terminal's solder pins."""
    f = ChargerFrame(ch)
    pcb = f.box(0, ch.length, 0, ch.width, -ch.pcb_t, 0)
    for bx, by in ch.holes:
        pcb -= dcyl(f.x(by), f.y(bx), f.d(-ch.pcb_t) - 1, f.d(0) + 1, ch.hole_d)
    usb = f.box(ch.usb[0], ch.usb[1], ch.usb[2], ch.usb[3], 0, ch.usb_h)
    jst = f.box(ch.jst[0], ch.jst[1], ch.jst[2], ch.jst[3], 0, ch.jst_h)
    term = f.box(ch.terminal[0], ch.terminal[1], ch.terminal[2], ch.terminal[3], 0, ch.terminal_h)
    return pcb + usb + jst + term + terminal_pins(ch)


def charger_usb_axis(ch: p.Charger):
    """(x, depth) of the charge port's axis, and the y of its mouth."""
    f = ChargerFrame(ch)
    return f.x((ch.usb[2] + ch.usb[3]) / 2), f.d(ch.usb_h / 2), f.y(ch.usb[0])


def jst_plug_zone(ch: p.Charger) -> Part:
    """The cell lead's JST-PH plug in the charger's socket, plus room for its lead to turn."""
    f = ChargerFrame(ch)
    w, h, ln = p.JST_PLUG
    bxc = (ch.jst[0] + ch.jst[1]) / 2
    x0 = f.x(ch.jst[3])
    return dbox(x0, x0 + ln + p.JST_BEND, f.y(bxc) - w / 2, f.y(bxc) + w / 2, f.d(0) + 0.3, f.d(0) + 0.3 + h)


def terminal_screw_column(ch: p.Charger) -> Part:
    """Screwdriver access above the terminal block's screws, to the cover's inner face."""
    f = ChargerFrame(ch)
    t = ch.terminal
    return f.box(t[0] + 1.5, t[1] - 1.5, t[2] + 0.5, t[3] - 0.5, ch.terminal_h, p.D_COVER_INNER - f.d_comp - 0.01)


# --- Cell -----------------------------------------------------------------------------------
@dataclass
class Pocket:
    """Inner faces of the cell pocket's ribs."""
    x0: float   # inner faces of the ribs
    x1: float
    y0: float
    y1: float


def cell_pocket(ch: p.Charger) -> Pocket:
    """Cell pocket inner faces: against the left wall (the wire channel runs down the right),
    its top rib RIB_FROM_PCB_TOP below the PCB's top end."""
    x0 = -p.INNER_HALF_W + p.LEDGE_W + p.COVER_CLEAR + p.RIB_T   # left rib clears the cover ledge
    x1 = x0 + p.CELL_W + 2 * p.CELL_CLEAR
    _, pcb_top, _, _ = p.layout(ch)
    y1 = pcb_top - p.RIB_FROM_PCB_TOP - p.RIB_T   # top rib ends RIB_FROM_PCB_TOP below the PCB's top end
    return Pocket(x0, x1, y1 - (p.CELL_L + 2 * p.CELL_CLEAR), y1)


def cell(ch: p.Charger) -> Part:
    """The cell, centred in its pocket and taped to the cover (unswollen)."""
    pk = cell_pocket(ch)
    xc, yc = (pk.x0 + pk.x1) / 2, (pk.y0 + pk.y1) / 2
    back = p.D_COVER_INNER - p.CELL_TAPE_T
    return dbox(xc - p.CELL_W / 2, xc + p.CELL_W / 2, yc - p.CELL_L / 2, yc + p.CELL_L / 2, back - p.CELL_T, back)


def cell_swollen(ch: p.Charger) -> Part:
    pk = cell_pocket(ch)
    xc, yc = (pk.x0 + pk.x1) / 2, (pk.y0 + pk.y1) / 2
    back = p.D_COVER_INNER - p.CELL_TAPE_T
    return dbox(xc - p.CELL_W / 2, xc + p.CELL_W / 2, yc - p.CELL_L / 2, yc + p.CELL_L / 2,
                back - p.CELL_POCKET_T, back)


# --- USB plug overmolds ----------------------------------------------------------------------
def overmold(xc, dc, y_mouth, outward) -> Part:
    """A cable overmold seated against the receptacle mouth, running 20 mm out of the case."""
    w, h = p.PLUG_OVERMOLD
    return stadium_y(xc, dc, w, h, y_mouth, y_mouth + 20 * outward)


def fn_overmold(ch: p.Charger) -> Part:
    return overmold(p.FN_USB_X, fn_usb_axis_d(), fn_usb_mouth_y(ch), +1)


def charge_overmold(ch: p.Charger) -> Part:
    x, d, y = charger_usb_axis(ch)
    return overmold(x, d, y, -1)


# --- Wire runs (x, y, depth) -----------------------------------------------------------------
def pigtail_path(ch: p.Charger) -> np.ndarray:
    """MX1.25 pigtail: charger 4.5V-max terminal → behind the Freenove's antenna end → up the
    right-hand channel → forward onto the BAT socket. The last two points lie on the board's back."""
    f = ChargerFrame(ch)
    xw = f.x(sum(ch.terminal_wire_by) / 2)
    y_term = f.y(ch.length)
    d_wire = f.d(ch.terminal_wire_h)
    pk = cell_pocket(ch)
    x_chan = (pk.x1 + p.RIB_T + p.INNER_HALF_W) / 2
    d_run = (p.D_PARTS_BACK + p.D_COVER_INNER) / 2 + 1.0
    bx, by = bat_socket_xy(ch)
    y0 = p.pcb_y0(ch)
    pts = [
        (xw, y_term, d_wire),
        (xw, y_term + 3.0, d_wire),
        (xw, y0 - 1.0, d_run),
        (xw, y0 + 4.0, d_run),
        (x_chan, pk.y0 - 4.0, d_run),
        (x_chan, by, d_run),
        (bx + p.BAT_SOCKET[0] / 2 + 0.8, by, p.D_PCB_BACK + p.BAT_SOCKET[2] / 2 + 1.0),
        (bx + p.BAT_SOCKET[0] / 2, by, p.D_PCB_BACK + p.BAT_SOCKET[2] / 2),
    ]
    return np.array(pts)


def cell_lead_d() -> float:
    """Depth of the lead where it leaves the cell: mid-thickness of the cell's end."""
    back = p.D_COVER_INNER - p.CELL_TAPE_T
    return back - p.CELL_T / 2


def cell_lead_paths(ch: p.Charger) -> dict:
    """JST-PH cell lead runs, out of the protection-PCB end (bottom) → over the low bottom rib →
    behind the antenna end → down into the bay beside the charger → into the JST-PH socket.

    The exit is measured at CELL_LEAD_FROM_EDGE (≈ centred); the wires may also be taped along
    the end to either corner first, so all three runs are returned.
    """
    f = ChargerFrame(ch)
    c = cell(ch).bounding_box()
    d_mid = cell_lead_d()
    x_exit = c.max.X - p.CELL_LEAD_FROM_EDGE
    bxc = (ch.jst[0] + ch.jst[1]) / 2
    x_plug = f.x(ch.jst[3]) + p.JST_PLUG[2]
    d_plug = f.d(0) + 0.3 + p.JST_PLUG[1] / 2
    off = p.WIRE_D / 2 + 0.1          # wire centreline just off the cell's end face
    tail = [
        (x_plug + 2.5, f.y(ch.length) + 2.0, d_mid),
        (x_plug + 3.5, f.y(bxc) + 5.0, d_plug + 1.0),
        (x_plug + 0.5, f.y(bxc), d_plug),
    ]
    runs = {}
    for name, x_turn in (("centre", x_exit), ("via left corner", c.min.X + 1.0), ("via right corner", c.max.X - 1.0)):
        pts = [(x_exit, c.min.Y, d_mid), (x_exit, c.min.Y - off, d_mid)]
        if x_turn != x_exit:
            pts.append((x_turn, c.min.Y - off, d_mid))
        pts.append((x_turn, c.min.Y - 6.0, d_mid))
        runs[name] = np.array(pts + tail)
    return runs


def cell_lead_path(ch: p.Charger) -> np.ndarray:
    """The straight (centre) run, for renders."""
    return cell_lead_paths(ch)["centre"]


def path_length(pts: np.ndarray) -> float:
    return float(np.sum(np.linalg.norm(np.diff(pts, axis=0), axis=1)))


def pigtail_min_length(ch: p.Charger) -> int:
    """Shortest MX1.25 pigtail to buy (socket to bare ends): run + strip + slack, rounded up to 5 mm."""
    need = path_length(pigtail_path(ch)) + p.PIGTAIL_STRIP + p.PIGTAIL_SLACK
    return int(-(-need // 5) * 5)


def path_xyz(pts: np.ndarray) -> np.ndarray:
    """(x, y, depth) → world (x, y, z)."""
    out = pts.copy()
    out[:, 2] = -out[:, 2]
    return out
