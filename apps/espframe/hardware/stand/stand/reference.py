"""Reference (non-printed) parts: the panel, its keep-outs, brass spacers, plug and jack.

Used for preview renders and fit checks only.
"""

from build123d import Align, Cylinder, Part, Pos, RectangleRounded, extrude

from . import params as p
from .foot import layout
from .frames import box, screen_box


def panel() -> Part:
    """Glass + LCD stack (screen frame)."""
    return extrude(Pos(p.GLASS_W / 2, p.GLASS_H / 2, -p.PANEL_T) * RectangleRounded(p.GLASS_W, p.GLASS_H, p.GLASS_R), p.PANEL_T)


def board_keepout(height: float = p.JST_DESIGN_H) -> Part:
    """Main PCB footprint up to the tallest part's design height (UART2 JST), screen frame."""
    return screen_box(p.PCB_X[0], p.PCB_X[1], p.PCB_Y[0], p.PCB_Y[1], p.PANEL_T, p.PANEL_T + height)


def tabs_and_spacers() -> Part:
    """Stamped tabs (as small blocks) and the stock 8 mm brass spacers, screen frame."""
    out = None
    for x, y in p.HOLES:
        tab = screen_box(x - 4, x + 4, y - 4, y + 4, p.PANEL_T, p.D_TAB_TOP)
        brass = Pos(x, y, -p.D_BRASS_TOP) * Cylinder(p.BRASS_D / 2, p.BRASS_L, align=(Align.CENTER, Align.CENTER, Align.MIN))
        part = tab + brass
        out = part if out is None else out + part
    return out


def plug() -> Part:
    """The straight USB-C plug body below the port mouth, screen frame."""
    return screen_box(p.PORT_X - p.PLUG_W / 2, p.PORT_X + p.PLUG_W / 2,
                      p.PORT_MOUTH_Y - p.PLUG_OUT, p.PORT_MOUTH_Y,
                      p.PORT_DEPTH - p.PLUG_T / 2, p.PORT_DEPTH + p.PLUG_T / 2)


def jack() -> Part:
    """Poyiccot female panel end (measured): flange against the rear-wall web, receptacle through it (world)."""
    cx = p.JACK_X
    front = p.FOOT_D - p.JACK_WEB_T          # flange face against the inside of the web
    flange = box(cx - p.JACK_FLANGE_W / 2, cx + p.JACK_FLANGE_W / 2, front - p.JACK_FLANGE_T, front,
                 p.JACK_Z - p.JACK_FLANGE_H / 2, p.JACK_Z + p.JACK_FLANGE_H / 2)
    bw, bh = p.JACK_BODY
    body = box(cx - bw / 2, cx + bw / 2, front - p.JACK_BODY_L, front - p.JACK_FLANGE_T, p.JACK_Z - bh / 2, p.JACK_Z + bh / 2)
    rw, rh = p.JACK_RECEPTACLE
    receptacle = box(cx - rw / 2, cx + rw / 2, front, front + p.JACK_RECEPTACLE_PROUD,
                     p.JACK_Z - rh / 2, p.JACK_Z + rh / 2)
    return flange + body + receptacle


def face_felt() -> Part:
    """1 mm felt on the back of the frame face, over the black bezel only (screen frame)."""
    ring = screen_box(0.5, p.GLASS_W - 0.5, 0.5, p.GLASS_H - 0.5, -p.FRAME_FACE_GAP, -p.FELT_CLEAR)
    k = p.FELT_INSET
    hole = screen_box(p.OPEN_X[0] - k, p.OPEN_X[1] + k, p.OPEN_Y[0] - k, p.OPEN_Y[1] + k,
                      -p.FRAME_FACE_GAP - 1, 0)
    return ring - hole


def ledge_felt() -> Part:
    """1 mm felt between the ledge and the frame's bottom rail (screen frame)."""
    return screen_box(p.FRAME_X[0], p.FRAME_X[1], -p.SEAT, -p.RISE,
                      -(p.FRAME_FACE_GAP + p.FRAME_FACE_T), p.D_PLATE_BACK)


def active_area_prism() -> Part:
    """The visible picture area, extruded forward of the glass: nothing may enter it."""
    return screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], -20, 0)


def opening_line_of_sight() -> Part:
    """Head-on view through the frame opening, from the face front back to the glass front."""
    return screen_box(p.OPEN_X[0], p.OPEN_X[1], p.OPEN_Y[0], p.OPEN_Y[1],
                      -(p.FRAME_FACE_GAP + p.FRAME_FACE_T) - 1, 0)
