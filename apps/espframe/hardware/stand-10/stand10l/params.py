"""Every dimension of the 10" espframe landscape desk stand (COM-684), in millimetres.

The board, the fits and the hardware are the portrait stand's (COM-396): every MEASURED and SOURCED value comes from
stand10.params unchanged, and only the board's pose and this stand's DESIGN choices are set here. Tags as there:
MEASURED, SOURCED, DESIGN, EST.

Pose: the portrait board turned 90° clockwise seen from the front. The USB-C end of the rear box is on the left, the
LCD-FPC end on the right, and the C6 antenna window in the top-right corner, away from the desk and the foot. A portrait
point (px, py) lands at (py, portrait SHELL_W − px).

Coordinate frames, as the portrait stand's:

* Screen frame (plate and frame): x = across the shell, left to right seen from the front, from its left edge; y = up
  from its bottom edge; z = -depth, so the glass front is z = 0.
* World frame (foot): X = across, Y = horizontal depth from the glass's bottom-front edge towards the rear, Z = up.

What changes from the portrait stand: the straight plug now points sideways (−x) out of the port, so the cable takes one
R18 bend rearwards, through a notch in the plate's left edge, then a second R18 bend down behind the plate, straight
into the open tray through a notch in the lid's front edge, and on to the coil and the rear jack. The foot sits wholly
behind the plate (no shelf, deck slot or tunnel), and the dovetail moves to the right of the rear box, below the antenna
corner.
"""

# ruff: noqa: F401,F403,F405
from stand10 import params as P
from stand10.params import *  # every MEASURED / SOURCED value and the shared DESIGN fits

# Portrait-only names that would be wrong here: removed so nothing picks them up by accident.
del PORT_X, USBC_X, PORT_MOUTH_Y, PLATE_NOTCH, VENT_PITCH_X, VENT_COLS, TAB_Y, SHELF_FACE_GAP, SHELF_PLATE_GAP
del SHELF_WALL_GAP, SHELF_TUNNEL_SKIN, SLOT_W, TUNNEL_W, SLOT_T, COIL_Y


def rot(pt):
    """Portrait screen-frame point to the landscape pose (90° clockwise seen from the front)."""
    return (pt[1], P.SHELL_W - pt[0])


# --- 10" board, landscape ------------------------------------------------------------------------
SHELL_W, SHELL_H = P.SHELL_H, P.SHELL_W           # 242.8 across, 158.7 up
HOLES = [rot(h) for h in P.HOLES]                 # x 15 / 137.8 / 227.8, y 15 / 143.7
NYLON_HOLE = rot(P.NYLON_HOLE)                    # (227.8, 143.7): in the antenna window, as in portrait
BOX_W, BOX_H = P.BOX_H, P.BOX_W                   # 138 across, 90 up
BOX_X = P.BOX_Y                                   # 54–192: the USB-C end face (port mouths) 54 in from the left
BOX_Y = (SHELL_H - P.BOX_X[1], SHELL_H - P.BOX_X[0])
FPC_W, FPC_H = P.FPC_H, P.FPC_W                   # preview only: now at the right end
AA_W, AA_H = P.AA_H, P.AA_W
AA_X = ((SHELL_W - AA_W) / 2, (SHELL_W + AA_W) / 2)
AA_Y = ((SHELL_H - AA_H) / 2, (SHELL_H + AA_H) / 2)
ANTENNA_C = rot(P.ANTENNA_C)                      # (219.8, 135.7)
ANTENNA_SIZE = (P.ANTENNA_SIZE[1], P.ANTENNA_SIZE[0])
ANTENNA_X = (ANTENNA_C[0] - ANTENNA_SIZE[0] / 2, ANTENNA_C[0] + ANTENNA_SIZE[0] / 2)
ANTENNA_Y = (ANTENNA_C[1] - ANTENNA_SIZE[1] / 2, ANTENNA_C[1] + ANTENNA_SIZE[1] / 2)

# USB_UART port: mouth in the box's left end face, facing −x; the straight plug sticks out leftwards.
PORT_MOUTH_X = BOX_X[0]                           # 54
PORT_Y = SHELL_H - P.PORT_X                       # 58.7 up from the shell bottom
# PORT_DEPTH (11.8) is the portrait's.

# --- Cable path behind the board: plug -> R bend rearwards -> plate notch -> R bend down -> lid notch -> tray -------
PLUG_END_X = PORT_MOUTH_X - PLUG_OUT              # 26.5: the plug's cable end
CABLE_X = PLUG_END_X - BEND_R                     # 8.5: the cable's line after the first bend
CABLE_DEPTH = PORT_DEPTH + 2 * BEND_R             # 47.8: its depth behind the glass after the second bend
CABLE_DROP_Y = PORT_Y - BEND_R                    # 40.7: where it starts straight down the screen
CABLE_NOTCH = (CABLE_X + CABLE_D / 2 + 2.25, 8.0)  # DESIGN: notch open to the plate's left edge (reach in x, height):
                                                  #   the cable slides in sideways before the frame closes it
# The drop lands over the tray itself (it's 47.8 behind the glass, against the portrait's 11.8), so there's no deck slot
# or tunnel: the cable drops through a notch in the lid's front edge, and the plug passes the open tray with the lid off.
LID_NOTCH_CLEAR = 1.5                             # DESIGN: notch edge off the cable
COIL_DIR = 1                                      # DESIGN: coil right of the cable (the left wall is beside it)
COIL_Y = 73.0                                     # DESIGN: coil centre (world Y); its entry is the end of the floor bend
COIL_LEN = 48.0                                   # DESIGN: shorter than portrait's 63: the path behind the plate is longer
JACK_X = 14.0                                     # DESIGN: rear jack just right of the cable's line, its flange inside
                                                  #   the side wall; the coil's first arc passes in front of its body
JACK_Z = FLOOR + 1.0 + JACK_FLANGE_H / 2

# --- Back-plate --------------------------------------------------------------------------------------------------------
PLATE_Y0 = 2.5                                    # DESIGN: lower than portrait (no shelf under it); lowest corner ~2 mm
                                                  #   off the desk
VENT_XS = tuple(SHELL_W / 2 + (i - 2) * 25.0 for i in range(5))   # DESIGN: five slots, centred, clear of the risers
VENT_ROWS_Y = (30.5, 36.0, 129.0, 136.0)          # DESIGN: two rows low (air in) and two above the rear box (heat rises
                                                  #   out); the low rows stay above the foot's deck, which would seal
                                                  #   them, and the air behind a slot runs down the 15° lean
MIN_VENT_CLEAR = 5.0                              # DESIGN: check limit, free air behind every vent slot before the foot

# Dovetail: right of the rear box (x ≥ 197, box ≤ 192) and left of the bottom-right riser (x ≤ 222.3); the box spans
# the middle, so the spine can't sit centred below it.
DOVE_X = 209.0                                    # DESIGN
SPINE_X = (DOVE_X - SPINE_W / 2, DOVE_X + SPINE_W / 2)

# --- Foot ----------------------------------------------------------------------------------------------------------------
FOOT_D = 132.0                                    # DESIGN: glass bottom-front edge to the rear wall; set by the coil and jack
FRONT_ZONE_D = 47.0                               # DESIGN (portrait): tray front wall, just behind the dovetail post
LID_DETENT_N = 3                                  # DESIGN: a longer lid

# --- Picture frame ---------------------------------------------------------------------------------------------------
OPEN_X = (AA_X[0] - REVEAL, AA_X[1] + REVEAL)
OPEN_Y = (AA_Y[0] - REVEAL, AA_Y[1] + REVEAL)
FACE_L = OPEN_X[0] + FRAME_OUT
FRAME_X = (-FRAME_OUT, OPEN_X[1] + FACE_L)
FRAME_Y = (-FRAME_OUT, SHELL_H + FRAME_OUT)
SCREEN_Z0 = SHELL_Z
# Frame-to-plate M3s in the side walls: the right wall's upper one stays ANTENNA_KEEP off the window (y ≤ 111.7), and
# both stay off the cable notch at y 54.7–62.7 on the left.
TAB_Y = (30.0, 105.0)                             # DESIGN
