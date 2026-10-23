"""Reference (non-printed) parts: the board, its rear box and bosses, the plug and the jack.

Used for preview renders and fit checks only.
"""

from build123d import Align, Cylinder, Part, Pos, RectangleRounded, extrude

from . import params as p
from .frames import SCREEN_TO_WORLD, box, screen_box


def shell() -> Part:
    """Shell body from the glass front to the flat metal back (screen frame)."""
    return extrude(Pos(p.SHELL_W / 2, p.SHELL_H / 2, -p.BODY_T) * RectangleRounded(p.SHELL_W, p.SHELL_H, p.SHELL_R),
                   p.BODY_T)


def rear_box() -> Part:
    """Rear electronics box behind the flat back (screen frame)."""
    return screen_box(p.BOX_X[0], p.BOX_X[1], p.BOX_Y[0], p.BOX_Y[1], p.BODY_T, p.BODY_T + p.BOX_D)


def fpc_bracket() -> Part:
    """LCD-FPC bracket: top-centre between the top holes, within the boss plane."""
    x0 = (p.SHELL_W - p.FPC_W) / 2
    return screen_box(x0, x0 + p.FPC_W, p.SHELL_H - p.FPC_H - 2, p.SHELL_H - 2, p.BODY_T, p.D_BOSS_TOP)


def bosses() -> Part:
    """The six Ø7.5 × 4.5 mounting bosses on the flat back."""
    out = None
    for x, y in p.HOLES:
        b = Pos(x, y, -p.D_BOSS_TOP) * Cylinder(p.BOSS_D / 2, p.BOSS_H, align=(Align.CENTER, Align.CENTER, Align.MIN))
        out = b if out is None else out + b
    return out


def board() -> Part:
    """The whole board as one keep-out: shell, bosses, rear box and FPC bracket."""
    return shell() + bosses() + rear_box() + fpc_bracket()


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
    """1 mm felt on the back of the frame face, over the black glass border only (screen frame)."""
    g = p.GLASS_INSET + 0.5
    ring = screen_box(g, p.SHELL_W - g, g, p.SHELL_H - g, -p.FRAME_FACE_GAP, -(p.FRAME_FACE_GAP - p.FELT_T))
    k = p.FELT_INSET
    hole = screen_box(p.OPEN_X[0] - k, p.OPEN_X[1] + k, p.OPEN_Y[0] - k, p.OPEN_Y[1] + k,
                      -p.FRAME_FACE_GAP - 1, 0)
    return ring - hole


def desk_felt() -> Part:
    """1 mm felt under the frame's face and side walls, on the desk (screen frame)."""
    w = p.FRAME_WALL
    footprint = (screen_box(p.FRAME_X[0], p.FRAME_X[1], -80, 0, -(p.FRAME_FACE_GAP + p.FRAME_FACE_T), -p.FRAME_FACE_GAP)
                 + screen_box(p.FRAME_X[0], p.FRAME_X[0] + w, -80, 0, -p.FRAME_FACE_GAP, p.D_PLATE_BACK)
                 + screen_box(p.FRAME_X[1] - w, p.FRAME_X[1], -80, 0, -p.FRAME_FACE_GAP, p.D_PLATE_BACK))
    return footprint & (SCREEN_TO_WORLD.inverse() * box(-50, 250, -100, 300, 0, p.DESK_FELT_T))


def active_area_prism() -> Part:
    """The visible picture area, extruded forward of the glass: nothing may enter it."""
    return screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], -20, 0)


def opening_line_of_sight() -> Part:
    """Head-on view through the frame opening, from the face front back to the glass front."""
    return screen_box(p.OPEN_X[0], p.OPEN_X[1], p.OPEN_Y[0], p.OPEN_Y[1],
                      -(p.FRAME_FACE_GAP + p.FRAME_FACE_T) - 1, 0)
