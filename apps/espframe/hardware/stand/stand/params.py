"""Every dimension of the 7" espframe portrait stand, in millimetres.

Each value is tagged with where it comes from:

    MEASURED  measured on the real device by the user
    SOURCED   from a vendor drawing or listing
    DESIGN    a design choice made for this stand
    EST       an estimate that still needs checking on real parts

Sources: the desk-stand audit report (plan rev D), the Waveshare
ESP32-S3-Touch-LCD-7 mechanical drawing, and the Poyiccot panel-mount
USB-C extension listing.

Coordinate frames used by the model:

* Screen frame (plate is built in it): x = across the glass, left to right as
  seen from the front; y = up the glass from its bottom edge; z = -depth, so the
  glass front is z = 0 and the panel back is z = -PANEL_T.
* World frame (foot is built in it): X = across, Y = horizontal depth from the
  glass bottom-front corner towards the rear, Z = up from the desk.
"""

import math

# --- Printer / material -------------------------------------------------------
MATERIAL = "PETG"                 # DESIGN (user choice): plate, risers, foot, lid
PETG_DENSITY = 1.27e-3            # g/mm³, SOURCED (typical PETG)
PRINT_FILL = 0.55                 # EST: walls + 25% infill, fraction of solid mass
SLIDE_CLEAR = 0.02                # MEASURED: dovetail flank clearance per side; coupon round 3 on the P1S
                                  #   (0.02 'pretty good'; 0.05 was a touch loose; round 1 rails were printed flat)
POST_FACE_CLEAR = 0.2             # DESIGN: flat gap between the plate back and the foot post face
PRESS_CLEAR = 0.1                 # DESIGN: per side, light press fits
M3_CLEAR_D = 3.4                  # DESIGN: M3 clearance hole
BED = 256.0                       # SOURCED: Bambu P1S build volume (cube)

# --- Heat-set inserts (user's LUKAISEN kit) -------------------------------------
INSERT_OD = 4.2                   # SOURCED: kit spec
INSERT_HOLE_D = INSERT_OD - 0.2   # DESIGN, confirmed on coupon-jack (PETG): inserts went in snug and flush
INSERT_M3x8_L = 8.0               # SOURCED: kit
INSERT_M3x6_L = 6.0               # SOURCED: kit
INSERT_M3x4_L = 4.0               # SOURCED: kit

# --- 7" panel (Waveshare ESP32-S3-Touch-LCD-7, rev 1.2) ---------------------------
GLASS_W = 110.76                  # SOURCED: drawing (portrait width)
GLASS_H = 192.96                  # SOURCED: drawing (portrait height)
GLASS_R = 7.88                    # SOURCED: drawing corner radius
PANEL_T = 5.0                     # SOURCED: glass + LCD stack ("TP+LCD=5.00")
PCB_BACK_T = 8.5                  # SOURCED: glass front to PCB rear face
TAB_H = 2.0                       # MEASURED (derived: 10 − 8)
BRASS_L = 8.0                     # MEASURED: stock Waveshare F-F brass spacer
BRASS_D = 5.5                     # EST: hex spacer across-corners, for previews
BRASS_THREAD_DEPTH = 6.0          # MEASURED: 6–7, use the low end
JST_H = 12.0                      # MEASURED: UART2 JST above panel back
JST_DESIGN_H = 12.5               # DESIGN: user's safety margin
PLATE_CLEAR = 2.0                 # DESIGN: plate underside above JST_DESIGN_H

# Mount holes, screen frame (x from the left seen from the front, y up from the
# glass bottom edge). From the drawing: 65.65 × 126.20 pattern, rear-view offsets
# 23.53 / 21.58 from the sides and 31.85 / 34.91 from top / bottom.
HOLES = [(21.58, 34.91), (87.23, 34.91), (21.58, 161.11), (87.23, 161.11)]  # SOURCED (rotated)

# Main PCB footprint (screen frame) for the keep-out check.
PCB_X = (7.70, 80.70)             # SOURCED (rotated from the drawing)
PCB_Y = (41.96, 147.96)           # SOURCED

# Board USB-C used for power and data: the UART (CH343) port, mouth facing down.
PORT_X = 44.71                    # SOURCED (rotated); which port is UART is still to confirm
PORT_MOUTH_Y = 40.0               # MEASURED (28 to active area + 12 bezel)
PORT_DEPTH = 11.7                 # EST: port centre behind the glass front

SCREEN_MASS = 354.0               # g, SOURCED (Waveshare listing, touch SKU)

# --- Back-plate ------------------------------------------------------------------
PLATE_T = 3.0                     # DESIGN
RISER_D = 9.0                     # DESIGN: wide footprint, spreads screw load
RISER_H = (PANEL_T + JST_DESIGN_H + PLATE_CLEAR) - (PANEL_T + TAB_H + BRASS_L)  # = 4.5, DESIGN
PLATE_EDGE_CLEAR = 0.3           # DESIGN: plate edge to the frame's inner walls; the plate closes the whole back
PLATE_R = 0.5                     # DESIGN: corner radius (the frame's inner corners are square)
# Vent slots through the plate (the back is otherwise closed): rows low down draw air in, rows
# near the top let warm air out. Kept clear of the risers, spine, cable notch and frame screws.
VENT_SLOT = (20.0, 3.0)           # DESIGN: slot length (across) × width
VENT_PITCH_X = 25.0               # DESIGN: slot centres across, centred on the glass
VENT_COLS = 4                     # DESIGN
VENT_ROWS_Y = (50.0, 57.0, 176.0, 183.0)   # DESIGN: row centres up the glass (above the lower risers, above the upper ones)
SCREW_L = 12.0                    # DESIGN: M3 × 12 (confirmed against thread depth)

# Depths behind the glass front (positive numbers).
D_TAB_TOP = PANEL_T + TAB_H                    # 7.0
D_BRASS_TOP = D_TAB_TOP + BRASS_L              # 15.0
D_PLATE_FRONT = D_BRASS_TOP + RISER_H          # 19.5
D_PLATE_BACK = D_PLATE_FRONT + PLATE_T         # 22.5

# Dovetail: groove in a thickened spine on the plate, rail on the foot post.
SPINE_X = (58.0, 82.0)            # DESIGN: clear of the plug (x ≤ 51) and FPC
SPINE_TOP_Y = 39.5                # DESIGN: below the PCB edge and JST (y ≥ 42); 2 mm above the groove end so it stays closed
SPINE_T = 5.0                     # DESIGN: extra thickness towards the panel
DOVE_X = sum(SPINE_X) / 2         # 70.0
DOVE_MOUTH_W = 10.0               # DESIGN: groove width at the plate back face
DOVE_FLOOR_W = 14.0               # DESIGN: groove width at the floor (undercut)
DOVE_DEPTH = 4.0                  # DESIGN
DOVE_TIP_CLEAR = 0.4              # DESIGN: rail tip to groove floor; the groove floor is a 14 mm bridge on the plate and may sag
DOVE_GROOVE_L = SPINE_TOP_Y - 2.0  # DESIGN: open at the bottom edge, closed by 2 mm of spine at the top (a groove past the spine cut through the 3 mm plate)
DOVE_RAIL_L = DOVE_GROOVE_L - 2.0   # DESIGN: 2 mm short of the groove end
LOCK_Y = 18.0                     # DESIGN: optional foot-lock screw height
PLATE_NOTCH = (14.0, 6.0)         # DESIGN: cable notch in the plate bottom edge above the plug bend (w, h)

# --- Foot (world frame) -------------------------------------------------------------
TILT_DEG = 10.0                   # DESIGN
S = math.sin(math.radians(TILT_DEG))
C = math.cos(math.radians(TILT_DEG))
LEDGE_H = 18.0                    # DESIGN: ledge seat height at the frame's front face (world Z)
FOOT_W = 130.0                    # DESIGN
FOOT_D = 118.0                    # DESIGN: glass corner to rear wall; shortest that still meets MIN_TIP_FORCE_N without extra ballast
WALL = 3.0                        # DESIGN
FLOOR = 3.0                       # DESIGN
DECK_SKIN = 1.5                   # DESIGN: material over the cavity behind the plate
LEDGE_OVER = 1.5                  # DESIGN: ledge runs this far in front of the frame's front face
POST_W = 30.0                     # DESIGN
POST_T = 9.0                      # DESIGN
POST_H = 45.0                     # DESIGN: up the plate, along the tilt
LOCK_HEAD_D = 6.5                 # DESIGN: counterbore for the lock screw head
LOCK_POST_LEFT = 2.0              # DESIGN: post material under the counterbore

# Tray lid: the cable tray is open-topped so it prints without a roof, and a flat
# lid drops into a rabbet flush with the deck.
LID_T = 1.6                       # DESIGN
LID_RABBET = 1.5                  # DESIGN: step in the tray wall the lid sits on
LID_CLEAR = 0.15                  # DESIGN: per side

# Solid front under the ledge (no ballast: the user's printed stand is sturdy without it);
# the cable tunnel runs through it to the tray.
FRONT_ZONE_D = 40.6               # DESIGN: glass corner to the tray's front wall

# Cable and plug (Poyiccot USB 3.1 straight panel-mount extension, 30 cm).
PLUG_LEN = 35.0                   # SOURCED: listing, overall incl. tip
PLUG_TIP = 7.5                    # SOURCED: listing
PLUG_OUT = PLUG_LEN - PLUG_TIP    # 27.5 EST: assumes 35 includes the tip
PLUG_W = 12.5                     # SOURCED
PLUG_T = 6.5                      # EST
CABLE_LEN = 300.0                 # SOURCED
CABLE_D = 4.5                     # EST
BEND_R = 4 * CABLE_D              # 18, EST: static minimum (4 × D)
SLOT_W = 14.0                     # DESIGN: plug must pass through when assembling
SLOT_T = (PORT_DEPTH - 4.0, PORT_DEPTH + 4 * CABLE_D + CABLE_D / 2 + 1.5)   # DESIGN: covers the whole plug bend through the deck
COIL_W = 32.0                     # DESIGN: free cable coil in the tray, stadium (centreline R 18.25 ≥ BEND_R); no post
COIL_Y = 62.0                     # DESIGN: coil centre (horizontal depth)
COIL_LEN = 63.0                   # DESIGN: coil overall length, sized to the spare cable

# Panel jack (Poyiccot female end) behind the rear wall: the flange presses against the inside
# of a 1.2 mm web, the metal receptacle pokes through it flush with the outside face, and the
# jack's own screws go in from outside into the flange's threaded holes.
JACK_FLANGE_W = 25.0              # MEASURED (user, Poyiccot in hand)
JACK_FLANGE_H = 8.0               # MEASURED
JACK_FLANGE_T = 6.5               # MEASURED: flange depth, front to back
JACK_BODY = (12.5, 8.0)           # MEASURED: body width × height behind the flange
JACK_BODY_L = 34.5                # MEASURED: flange 6.5 + body and strain relief 28
JACK_RECEPTACLE_PROUD = 1.2       # MEASURED (receptacle test plate 2 flush): metal USB-C receptacle stands proud of the flange face
JACK_RECEPTACLE = (8.94, 3.26)    # SOURCED: standard USB-C receptacle shell (width × height)
JACK_SCREW_PITCH = 16.5           # MEASURED (confirmed on receptacle test plate): flange holes, centre to centre; threaded M3
JACK_SCREW_HEAD_D = 5.5           # EST: the jack's own screws (pan head); a plug moulding wider than the gap between heads can't seat
JACK_WEB_T = JACK_RECEPTACLE_PROUD   # DESIGN: rear wall thinned to this under the flange, so the receptacle ends flush outside
JACK_POCKET = (JACK_FLANGE_W + 0.4, JACK_FLANGE_H + 0.4) # DESIGN, confirmed on coupon-jack: flange recess in the inside face of the rear wall
JACK_WINDOW = (JACK_RECEPTACLE[0] + 0.4, JACK_RECEPTACLE[1] + 0.4)   # DESIGN, confirmed on receptacle test plate: just the receptacle pokes through
JACK_X = PORT_X                   # DESIGN: in line with the coil's exit, so the cable runs straight into the jack
JACK_Z = FLOOR + 1.0 + JACK_FLANGE_H / 2   # DESIGN: jack centre height (keeps the cable under the lid)

# Coupons.
RECEPTACLE_TRIAL_T = [1.0, 1.2, 1.4]   # DESIGN: test plates bracketing JACK_RECEPTACLE_PROUD (1–3 notches)
DOVE_TRIAL_CLEARS = [0.02, 0.03, 0.04]                    # DESIGN: round 3, brackets SLIDE_CLEAR (round 2: 0.05 closest, a touch loose)

# Required results.
MIN_TIP_FORCE_N = 4.5             # MEASURED (user): the printed stand felt sturdy without ballast; was 6.5 with steel (rev A design: 3.2)

# --- Picture frame (style B, classic bevel, 21 mm face, wood-look) -------------------
FRAME_DENSITY = 1.18e-3           # g/mm³, EST: wood-fill PLA (1.15–1.2); PolyWood is lighter
FRAME_FILL = 0.85                 # EST: thin walls print near-solid
FELT_T = 1.0                      # DESIGN: adhesive felt on the frame face back and on the ledge seat
FELT_CLEAR = 0.2                  # DESIGN: felt-to-glass clearance (felt never clamps the glass)
FELT_INSET = 1.0                  # DESIGN: felt edge kept this far outside the opening (hidden at oblique views)
REVEAL = 0.75                     # DESIGN: black line left visible around the picture
MIN_REVEAL = 0.5                  # DESIGN: check limit
FRAME_FACE_W = 21.0               # DESIGN: minimum for an even border (top/bottom bezels are 19.04)
FRAME_FACE_T = 4.0                # DESIGN
FRAME_BEVEL = 3.0                 # DESIGN: 45° × 3 into the opening
FRAME_OUTER_CH = 2.0              # DESIGN: 45° outer chamfer
FRAME_WALL = 2.0                  # DESIGN
FRAME_FACE_GAP = FELT_T + FELT_CLEAR   # face rear surface in front of the glass (1.2)
MIN_EDGE_GAP = 0.5                # DESIGN: check limit, glass edge to frame wall

# Active area (screen frame: x from the left seen from the front, y up from the glass bottom).
AA_X = (11.00, 97.72)             # SOURCED: Waveshare drawing (portrait)
AA_Y = (19.04, 173.92)            # SOURCED

# Opening: active area + reveal, so the frame face (equal width all round) hides the
# uneven bezel. Outer edge = opening + face width.
OPEN_X = (AA_X[0] - REVEAL, AA_X[1] + REVEAL)
OPEN_Y = (AA_Y[0] - REVEAL, AA_Y[1] + REVEAL)
FRAME_X = (OPEN_X[0] - FRAME_FACE_W, OPEN_X[1] + FRAME_FACE_W)
FRAME_Y = (OPEN_Y[0] - FRAME_FACE_W, OPEN_Y[1] + FRAME_FACE_W)
RISE = -FRAME_Y[0]                # 2.71: frame bottom below the glass bottom edge
SEAT = RISE + FELT_T              # glass bottom edge above the ledge surface (screen frame)

# Frame-to-plate fixing: the plate's edges over 4 frame bosses, M3 × 8 + washers into M3 × 4 inserts.
TAB_Y = (45.0, 150.0)             # DESIGN: screw centres, up the glass from its bottom edge
FRAME_BOSS_D = 7.0                # DESIGN
FRAME_BOSS_L = 8.0                # DESIGN: boss length towards the glass from the plate front face
FRAME_INSERT_DEPTH = INSERT_M3x4_L + 2.0   # DESIGN: M3 × 8 through a 3 mm tab reaches 5
FRAME_SCREW_L = 8.0               # DESIGN
FRAME_NOTCH_W = 15.0              # DESIGN: cable notch through the frame's bottom rail

MIN_DOVE_ENGAGE = 30.0            # DESIGN: check limit
