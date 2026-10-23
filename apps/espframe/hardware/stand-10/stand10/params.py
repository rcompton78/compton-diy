"""Every dimension of the 10" espframe portrait desk stand, in millimetres.

Each value is tagged with where it comes from:

    MEASURED  measured on real parts by the user (carried over from the 7" stand where noted)
    SOURCED   from a vendor drawing or listing
    DESIGN    a design choice made for this stand
    EST       an estimate that still needs checking on the real board

Sources: the Guition JC8012P4A1 structure drawing [G-STRUCT] and spec [G-SPEC] (see
docs/background.md), the user's measurements of the board (COM-396), and the 7" stand's printed and measured fits (apps/espframe/hardware/stand).

Coordinate frames used by the model:

* Screen frame (plate and frame are built in it): x = across the shell, left to right as
  seen from the front, from the shell's left edge; y = up from the shell's bottom edge;
  z = -depth, so the glass front is z = 0 and the flat metal back is z = -BODY_T.
* World frame (foot is built in it): X = across, Y = horizontal depth from the frame's
  bottom-front corner towards the rear, Z = up from the desk.

The 10" stand is on-desk only (COM-396): the frame stands on felt on the desk and the foot
holds the lean from behind through the dovetail, as the 7"'s COM-442 desk variant does.
Orientation: the LCD-FPC end goes up, so the USB-C bank at the other end of the rear box is
nearest the foot.
"""

import math

# --- Printer / material -------------------------------------------------------
PETG_DENSITY = 1.27e-3            # g/mm³, SOURCED (typical PETG)
PRINT_FILL = 0.55                 # EST: walls + 25% infill, fraction of solid mass (7")
SLIDE_CLEAR = 0.02                # MEASURED (7" coupon round 3): dovetail flank clearance per side, rails printed standing up
POST_FACE_CLEAR = 0.2             # DESIGN: flat gap between the plate back and the foot post face
M3_CLEAR_D = 3.4                  # DESIGN: M3 clearance hole
M25_CLEAR_D = 2.9                 # DESIGN: M2.5 clearance hole
BED = 256.0                       # SOURCED: Bambu P1S build volume (cube)

# --- Heat-set inserts (user's LUKAISEN kit) -------------------------------------
INSERT_OD = 4.2                   # SOURCED: kit spec
INSERT_HOLE_D = INSERT_OD - 0.2   # MEASURED (7"): snug and flush in PETG
INSERT_M3x4_L = 4.0               # SOURCED: kit

# --- 10" board (Guition ESP32-P4 JC8012P4A1), front view, portrait ---------------------
SHELL_W = 158.7                   # SOURCED [G-STRUCT] ± 0.05
SHELL_H = 242.8                   # SOURCED
SHELL_R = 3.0                     # EST: shell corner radius (previews only)
GLASS_INSET = 1.75                # SOURCED: touch glass 155.2 × 239.3 sits inside the shell
GLASS_STEP = 0.0                  # MEASURED (user): the glass is flush with the shell's front rim
BODY_T = 8.2                      # MEASURED (user; drawing 7.0): glass front to the flat metal back, face down on the desk
BOSS_D = 7.5                      # SOURCED: mounting boss diameter
BOSS_H = 4.5                      # SOURCED: boss height proud of the back
BOSS_THREAD = 4.4                 # MEASURED (user): M2.5 brass insert depth in each short black standoff

# Mount holes, screen frame: 15 in from each edge, middle row 105 down from the FPC end, so off-centre.
HOLES = [(x, y) for y in (15.0, SHELL_H - 105.0, SHELL_H - 15.0) for x in (15.0, SHELL_W - 15.0)]  # MEASURED (user), = drawing
NYLON_HOLE = (15.0, SHELL_H - 15.0)    # DESIGN: the hole nearest the antenna (top-left seen from the front) takes a nylon screw

# Rear electronics box (carries the C6 antenna under Guition's rear cover; kept on).
BOX_W, BOX_H = 90.0, 138.0        # MEASURED (user; drawing 93.5 × 138.35)
BOX_D = 15.2                      # MEASURED (user; drawing 15.51): protrusion past the flat back
BOX_X = ((SHELL_W - BOX_W) / 2, (SHELL_W + BOX_W) / 2)   # SOURCED: centred across
BOX_Y = (54.0, 54.0 + BOX_H)      # MEASURED (user): box end face (port mouth plane) 54 above the shell bottom
# LCD-FPC bracket: raised block top-centre between the top holes. Nothing outside the box stands taller
# than the bosses (user), so it's only a preview shape.
FPC_W, FPC_H = 52.0, 22.0         # EST (scaled), height MEASURED within the boss plane

# Active area: the stated size, centred, since the user measured even bezels (about 12 left and right, 13 top and
# bottom). Centred, they come out at 11.67 and 13.11, within the "about"; the drawing's scaled 12.9/10.6/11.7/14.6
# were uneven and are dropped.
AA_W, AA_H = 135.36, 216.58       # SOURCED
AA_X = ((SHELL_W - AA_W) / 2, (SHELL_W + AA_W) / 2)   # MEASURED (user, bezels ≈ 12): 11.67 each side
AA_Y = ((SHELL_H - AA_H) / 2, (SHELL_H + AA_H) / 2)   # MEASURED (user, bezels ≈ 13): 13.11 each end

# C6 antenna window: centre 23 from the top and 23 from the right edge of the back (user), taken as the device's back,
# so 23 from the left seen from the front. If "back case" meant the rear box, the window sits on the box under the
# plate, with no metal near it either way. Size still the earlier 18 × 25 estimate. No metal within ANTENNA_KEEP of it.
ANTENNA_C = (23.0, SHELL_H - 23.0)                # MEASURED (user): window centre
ANTENNA_SIZE = (18.0, 25.0)                       # EST
ANTENNA_X = (ANTENNA_C[0] - ANTENNA_SIZE[0] / 2, ANTENNA_C[0] + ANTENNA_SIZE[0] / 2)
ANTENNA_Y = (ANTENNA_C[1] - ANTENNA_SIZE[1] / 2, ANTENNA_C[1] + ANTENNA_SIZE[1] / 2)
ANTENNA_KEEP = 15.0               # DESIGN

# Board USB-C used for power and data: the UART (USB1, CH340) port at the bottom end of the box,
# mouth facing down.
# One slot low on the box's bottom end face, against the back: microSD then three USB-C (photo, COM-396). Seen from
# the front, the USB-C centres are at about x 103 / 89 / 77 (photo-scaled ± 2); the microSD is right of them at ~114.
# USB_UART is the one beside the TX/RX/GND header (silkscreen photo), the white part at the slot's left end in the
# rear view, so the outer port seen from the front. Measured from the back: its near edge is 54.2 from the back's
# left edge, so its centre is 54.2 + 8.94 / 2 = 58.67 from there, which is the front's right edge.
PORT_X = SHELL_W - (54.2 + 8.94 / 2)   # MEASURED (user): 100.0 from the shell's left edge seen from the front
USBC_X = (PORT_X, PORT_X - 13.6, PORT_X - 25.3)   # USB_UART MEASURED; USB1 / USB2 EST (photo pitch)
PORT_MOUTH_Y = BOX_Y[0]           # MEASURED (user, COM-396): the mouth is the box's end face, 54 above the shell bottom
PORT_DEPTH = BODY_T + 3.6         # MEASURED (user, approx.): port centre about 3.6 off the flat back, so 11.8

SCREEN_MASS = 550.0               # g, SOURCED [G-SPEC]
SCREEN_CG_DEPTH = 10.0            # EST: board centre of mass behind the glass front

# --- Back-plate: closed, on risers behind the rear box (COM-396 Q1 a) ---------------------------
PLATE_T = 3.0                     # DESIGN (7")
PLATE_CLEAR = 2.0                 # DESIGN: plate front behind the rear box
RISER_D = 10.0                    # DESIGN
RISER_SEAT = (BOSS_D + 0.4, 1.0)  # DESIGN: locating recess in the riser face over each boss (dia, depth)
SCREW_L = 20.0                    # DESIGN: M2.5 × 20 pan head + washer; through the plate and riser (15.7 mm to the boss top),
WASHER_T = 0.5                    #   so 3.8 mm into the 4.4 mm insert. Always fit the washer: without it, 4.3 leaves 0.1 to the bottom
MIN_SCREW_ENGAGE = 3.0            # DESIGN: check limit, M2.5 thread engaged in the insert (1.2 × D)
MIN_SCREW_BOTTOM = 0.3            # DESIGN: check limit, screw tip short of the insert's bottom
# Engagement = SCREW_L − WASHER_T − (PLATE_T + RISER_H); the riser's locating recess sits over the boss, not under the screw.
D_BOSS_TOP = BODY_T + BOSS_H                       # 12.7
D_PLATE_FRONT = BODY_T + BOX_D + PLATE_CLEAR       # 25.4
D_PLATE_BACK = D_PLATE_FRONT + PLATE_T             # 28.4
RISER_H = D_PLATE_FRONT - D_BOSS_TOP               # 12.7
PLATE_EDGE_CLEAR = 0.3            # DESIGN (7"): plate edge to the frame's inner walls
PLATE_Y0 = 4.5                    # DESIGN: plate bottom edge up the shell; at the 15° lean its rear edge must stay above the shelf
PLATE_R = 0.5                     # DESIGN (7")
VENT_SLOT = (20.0, 3.0)           # DESIGN (7")
VENT_PITCH_X = 25.0               # DESIGN (7")
VENT_COLS = 4                     # DESIGN
VENT_ROWS_Y = (48.0, 55.0, 203.0, 210.0)   # DESIGN: above the spine, and between the box top and the top risers

# Dovetail: groove in a thickened spine on the plate below the rear box, rail on the foot post.
DOVE_X = 45.0                     # DESIGN: left of the USB-C bank (x ≥ 70) and clear of the bottom-left riser (x ≤ 19.5)
SPINE_W = 24.0                    # DESIGN (7")
SPINE_X = (DOVE_X - SPINE_W / 2, DOVE_X + SPINE_W / 2)
SPINE_TOP_Y = 42.0                # DESIGN: below the rear box (y ≥ 53) and the low vents
SPINE_T = 5.0                     # DESIGN (7")
DOVE_MOUTH_W = 10.0               # DESIGN (7")
DOVE_FLOOR_W = 14.0               # DESIGN (7")
DOVE_DEPTH = 4.0                  # DESIGN (7")
DOVE_TIP_CLEAR = 0.4              # DESIGN (7")
DOVE_GROOVE_L = SPINE_TOP_Y - 2.0  # DESIGN: closed by 2 mm of spine at the top (the 7" bug)
DOVE_RAIL_L = DOVE_GROOVE_L - 2.0  # DESIGN
LOCK_Y = 18.0                     # DESIGN: optional foot-lock screw height
PLATE_NOTCH = (16.0, 6.0)         # DESIGN (7"): cable notch in the plate bottom edge

# --- Foot (world frame) -------------------------------------------------------------
TILT_DEG = 15.0                   # DESIGN (7", COM-417)
S = math.sin(math.radians(TILT_DEG))
C = math.cos(math.radians(TILT_DEG))
SHELL_Z = 7.0                     # DESIGN: shell bottom edge above the desk; the plug's R18 bend sets the floor
                                  #   (about 5.9 for no spare drop), and it sets the frame's face-down height
DESK_FELT_T = 1.0                 # DESIGN (7")
SHELF_FACE_GAP = 0.5              # DESIGN (7")
SHELF_WALL_GAP = 0.5              # DESIGN (7")
SHELF_PLATE_GAP = 0.6             # DESIGN (7")
SHELF_TUNNEL_SKIN = 3.0           # DESIGN (7")
FOOT_D = 120.0                    # DESIGN: glass bottom-front edge to the rear wall (world Y); sized for MIN_TIP_FORCE_N
WALL = 3.0                        # DESIGN (7")
FLOOR = 3.0                       # DESIGN (7")
POST_W = 30.0                     # DESIGN (7")
POST_T = 9.0                      # DESIGN (7")
POST_H = 45.0                     # DESIGN (7")
LOCK_HEAD_D = 6.5                 # DESIGN (7")
LOCK_POST_LEFT = 2.0              # DESIGN (7")
DECK_Z = 25.0                     # DESIGN: tray deck (lid top) height
LID_T = 1.6                       # DESIGN (7")
LID_RABBET = 1.5                  # DESIGN (7")
LID_CLEAR = 0.15                  # DESIGN (7")
LID_DETENT_H = 0.3                # DESIGN (7", printed)
LID_DETENT_N = 2                  # DESIGN
LID_DETENT_L = 8.0                # DESIGN
LID_DETENT_GROOVE_CLEAR = 0.1     # DESIGN
FRONT_ZONE_D = 47.0               # DESIGN: glass bottom edge to the tray's front wall; the lid stays behind the dovetail post

# Cable and plug (Poyiccot USB 3.1 straight panel-mount extension, 30 cm): the same part as the 7" (user), so the
# 7"'s values, which are still listing values or estimates there too.
PLUG_LEN = 35.0                   # SOURCED
PLUG_TIP = 7.5                    # SOURCED
PLUG_OUT = PLUG_LEN - PLUG_TIP    # 27.5 EST
PLUG_W = 12.5                     # SOURCED
PLUG_T = 6.5                      # EST
CABLE_LEN = 300.0                 # SOURCED
CABLE_D = 4.5                     # EST
BEND_R = 4 * CABLE_D              # 18, EST
SLOT_W = 16.0                     # DESIGN (7", COM-417)
TUNNEL_W = 16.0                   # DESIGN (7", COM-417)
SLOT_T = (PORT_DEPTH - 4.0, PORT_DEPTH + 4 * CABLE_D + CABLE_D / 2 + 1.5)
COIL_W = 32.0                     # DESIGN (7")
COIL_Y = 68.0                     # DESIGN: coil centre (horizontal depth), behind the tray front wall
COIL_LEN = 63.0                   # DESIGN (7")
COIL_DIR = -1                     # DESIGN: the coil runs left of the port (-1) or right (+1), to stay inside the tray

# Panel jack: the 7"'s measured thin-wall mount, unchanged.
JACK_FLANGE_W = 25.0              # MEASURED (7")
JACK_FLANGE_H = 8.0               # MEASURED
JACK_FLANGE_T = 6.5               # MEASURED
JACK_BODY = (12.5, 8.0)           # MEASURED
JACK_BODY_L = 34.5                # MEASURED
JACK_RECEPTACLE_PROUD = 1.2       # MEASURED
JACK_RECEPTACLE = (8.94, 3.26)    # SOURCED
JACK_SCREW_PITCH = 16.5           # MEASURED
JACK_SCREW_HEAD_D = 5.5           # EST (7")
JACK_WEB_T = JACK_RECEPTACLE_PROUD
JACK_POCKET = (JACK_FLANGE_W + 0.4, JACK_FLANGE_H + 0.4)
JACK_WINDOW = (JACK_RECEPTACLE[0] + 0.4, JACK_RECEPTACLE[1] + 0.4)
JACK_X = PORT_X                   # DESIGN: in line with the coil's exit
JACK_Z = FLOOR + 1.0 + JACK_FLANGE_H / 2

MIN_TIP_FORCE_N = 4.5             # DESIGN (user, COM-396): horizontal push at the screen centre
MIN_PLUG_PASS = 1.0               # DESIGN (7"): plug clearance per side through the tunnel and deck slot
MIN_DETENT_WALL = 1.0             # DESIGN (7"): rear wall left behind a lid detent groove
MIN_DOVE_ENGAGE = 30.0            # DESIGN (7")
MIN_REVEAL = 0.5                  # DESIGN (7")
MIN_EDGE_GAP = 0.5                # DESIGN (7"): shell edge to frame wall

# Coupons.
RECEPTACLE_TRIAL_T = [1.0, 1.2, 1.4]   # DESIGN (7"): rear-wall web plates, a sanity check of the same jack
DOVE_TRIAL_CLEARS = [0.02, 0.03, 0.04] # DESIGN (7" round 3): brackets SLIDE_CLEAR
TEMPLATE_T = 1.2                  # DESIGN: hole-template coupon thickness (lies on the flat back)
TEMPLATE_BAR = 6.0                # DESIGN: template bar width
TEMPLATE_BOX_CLEAR = 1.0          # DESIGN: template's box-outline band inside edge off the rear box

# --- Picture frame (D-sym: outer tight to the shell, only the picture shows) -------------------
FRAME_DENSITY = 1.18e-3           # g/mm³, EST: wood-fill PLA
FRAME_FILL = 0.85                 # EST
FELT_T = 1.0                      # DESIGN (7")
FELT_CLEAR = 0.2                  # DESIGN (7"): felt never touches the glass
FELT_INSET = 1.0                  # DESIGN (7")
REVEAL = 0.75                     # DESIGN (7")
FRAME_FACE_T = 4.0                # DESIGN (7")
FRAME_BEVEL = 3.0                 # DESIGN (7")
FRAME_OUTER_CH = 2.0              # DESIGN (7"): top and sides
FRAME_WALL = 2.0                  # DESIGN (7"): side and top walls, no bottom wall
WALL_GAP = 0.7                    # DESIGN: shell edge to the frame's inner wall (left and top)
FRAME_FACE_GAP = GLASS_STEP + FELT_T + FELT_CLEAR   # face rear in front of the glass (clears the shell rim too)

OPEN_X = (AA_X[0] - REVEAL, AA_X[1] + REVEAL)
OPEN_Y = (AA_Y[0] - REVEAL, AA_Y[1] + REVEAL)
FRAME_OUT = WALL_GAP + FRAME_WALL                    # 2.7: frame outer edge beyond the shell
# Left and top sit FRAME_OUT outside the shell; the right side matches the left face width so
# the picture is centred across (D-sym). The bottom runs down to the desk felt.
FACE_L = OPEN_X[0] + FRAME_OUT
FRAME_X = (-FRAME_OUT, OPEN_X[1] + FACE_L)
FRAME_Y = (-FRAME_OUT, SHELL_H + FRAME_OUT)          # FRAME_Y[0]: nominal bottom (the plate's datum)
RISE = FRAME_OUT

# World Z of the screen-frame origin (shell bottom-left, glass front).
SCREEN_Z0 = SHELL_Z

# Frame-to-plate fixing: M3 × 8 through the plate's edges into M3 × 4 inserts in side-wall bosses,
# kept out of the antenna keep-out on the left.
TAB_Y = (35.0, 185.0)             # DESIGN: the upper pair stays 15 mm off the antenna window
FRAME_BOSS_D = 7.0                # DESIGN (7")
FRAME_BOSS_L = 8.0                # DESIGN (7")
FRAME_INSERT_DEPTH = INSERT_M3x4_L + 2.0
FRAME_SCREW_L = 8.0               # DESIGN (7"): M3 × 8 + washer into the M3 × 4 inserts
