"""Every dimension of the wall mount, tagged by where it comes from.

SOURCED  from Elecrow's STEP of the CrowPanel 5.0" HMI (Basic, board V3.0) in its acrylic case,
         3D_File_CrowPanel_5.0-ESP32_HMI-3.0.zip on elecrow.com (measured in OCP), or its outline
         drawing 5.0inch_HMI_Outline_Dimensions.pdf. The STEP isn't committed (unclear licence).
DESIGN   a choice made here.
MEASURED checked on the real parts (none yet: the fit-test coupons settle the DESIGN clearances).

World frame (mm): X to the right as seen from the front, Z up, Y into the wall. The wall face is
Y = 0, so a part at `d` mm off the wall sits at Y = -d; helpers take d and do the sign.
X = 0 is the case's centre line. Z = 0 is the cradle's underside.

The panel hangs portrait with its USB-C edge at the bottom (`screen_rotation: "90"`, unchanged).
STEP coordinates (landscape, from the front, LCD flex at the bottom): xs to the right, ys up,
zs out of the screen. Turned 90° counter-clockwise, the STEP's left edge (USB-C) is the bottom,
its bottom edge (TF slot) the right side, its top edge (HY2.0 headers) the left side and its right
edge (the ESP32-S3 module's antenna) the top.
"""

# --- Case (SOURCED, STEP coordinates) ----------------------------------------------------------
STEP_CASE_X = (-70.10, 70.10)       # landscape width 140.2
STEP_CASE_Y = (-44.14, 39.76)       # landscape height 83.9
STEP_CASE_BACK_Z = -10.62           # back plate's outer face
STEP_CASE_FRONT_Z = 1.93            # front frame's face
CASE_R = 4.5                        # SOURCED: plan corner radius
STEP_GLASS_FRONT_Z = 0.0            # LCD/touch glass front, 1.93 behind the case face
STEP_WINDOW_X = (-60.55, 60.55)     # front frame window, 121.1
STEP_WINDOW_Y = (-40.56, 36.14)     # 76.7
ACTIVE_W, ACTIVE_H = 108.0, 64.8    # SOURCED: LCD active area (product page / LI050M331Y spec), centred on the window
# Four M2.5 pan heads on the back, Ø4.74, standing 1.14 proud of the back plate.
STEP_HEADS = [(65.10, 34.26), (-65.10, 34.26), (-65.10, -38.64), (65.10, -38.64)]
HEAD_D = 4.74
HEAD_H = 1.14
# The LCD flex tab pokes 0.35 mm out of the case's top edge (STEP compound in the front frame).
STEP_FLEX_TAB = ((34.70, 51.30), (39.06, 40.11), (-5.42, -4.37))      # xs, ys, zs ranges
# USB-C receptacle (left edge, inside the case): its mouth is 1.82 mm in from the case edge.
STEP_USB = ((-68.28, -60.98), (-7.04, 1.90), (-8.28, -4.08))
# Case side openings, in the 3.7 mm spacer layer (zs -8.77..-5.07).
STEP_SPACER_Z = (-8.77, -5.07)
STEP_USB_SLOT_Y = (-7.5, 2.0)        # left edge
STEP_TF_SLOT_X = (-55.5, -39.0)      # bottom edge
STEP_TF_CARD = ((-51.50, -40.50), (-44.22, -38.48), (-7.22, -5.77))   # sticks out 0.08 past the case
# BOOT / RESET: two tactile switches on the PCB back, pressing backwards. The acrylic back plate
# has no holes over them (its only holes are the 4 screws and 4 Ø4 at (±59, 30.3/-34.7)), so they
# can't be pressed in the stock case; flashing uses the CH340's auto-reset instead.
STEP_BUTTONS = [((-64.74, -61.54), (7.55, 12.75)), ((-64.74, -61.54), (16.17, 21.37))]
# ESP32-S3-WROOM-1 module on the back, at the right edge; its PCB antenna is the outer ~7 mm.
STEP_MODULE = ((42.72, 68.52), (-11.68, 6.32), (-8.27, -5.05))
ANTENNA_L = 7.0                      # SOURCED (WROOM-1 datasheet): antenna end of the 25.5 mm module

CASE_W = STEP_CASE_Y[1] - STEP_CASE_Y[0]      # 83.9 (portrait width)
CASE_H = STEP_CASE_X[1] - STEP_CASE_X[0]      # 140.2 (portrait height)
CASE_T = STEP_CASE_FRONT_Z - STEP_CASE_BACK_Z  # 12.55
STEP_YC = (STEP_CASE_Y[0] + STEP_CASE_Y[1]) / 2   # -2.19

# --- USB-C plug (DESIGN: reserved until fit-test coupon 2 tries Richard's cable) ----------------
PLUG_W, PLUG_T = 12.5, 7.0           # DESIGN: overmold envelope, across × front-to-back
PLUG_L = 35.0                        # DESIGN: overmold plus strain relief below the case
PLUG_CLEAR = 0.5                     # DESIGN: floor opening per side round the overmold
PLUG_BEND_CLEAR = 50.0               # DESIGN: kept clear below the case's bottom edge (plug + bend)
PLUG_BEND_MARGIN = 2.0               # DESIGN: extra width each side of that clear zone

# --- Cradle (DESIGN, black PLA) ------------------------------------------------------------------
CLEAR_SIDE = 0.4                     # DESIGN: case to side walls, per side (fit-test coupon 1)
CLEAR_BACK = 0.3                     # DESIGN: case back to the cradle back
LIP_GAP = 0.15                       # DESIGN: lip's back face to the case face
LIP_T = 1.5                          # DESIGN: lip thickness
LIP_WINDOW_MARGIN = 0.5              # DESIGN: lips stop this far outside the case window
WALL = 2.4                           # DESIGN: side wall thickness
FLOOR_T = 4.0                        # DESIGN: floor under the case (carries it in compression)
BACK_T = 7.0                         # DESIGN: flat back, wall side down on the bed; holds the dovetail groove
BACK_SKIN_MIN = 2.4                  # DESIGN: check limit for the back left in front of the groove
HEAD_POCKET_D = 6.2                  # DESIGN: Ø4.74 heads + 0.73 per side
HEAD_POCKET_EXTRA = 0.3              # DESIGN: pocket floor past the head
FLEX_RELIEF_DEPTH = 0.6              # DESIGN: relief in the left wall for the 0.35 mm flex tab
FLEX_RELIEF_MARGIN = 1.0             # DESIGN: round the tab, in Z and depth
TF_MARGIN_Z, TF_MARGIN_D = 2.0, 1.0  # DESIGN: TF window past the case's slot, along and across
VENT_W, VENT_H, VENT_PITCH = 18.0, 2.5, 8.0   # DESIGN: vent slots each side of the spine
VENT_Z = (55.0, 110.0)               # DESIGN: their span (bottom of the lowest, top of the highest)
EDGE_CHAMFER = 0.8                   # DESIGN: front edge chamfer round the lips

# --- Dovetail (DESIGN; the espframe stand's groove/rail approach, sized for PLA) ------------------
RAIL_ROOT_W = 14.0                   # DESIGN: rail width at the plate face
RAIL_TIP_W = 18.0                    # DESIGN: rail width at its tip (undercut)
RAIL_D = 4.0                         # DESIGN: rail height off the plate
SLIDE_CLEAR = 0.15                   # DESIGN: flank clearance per side (fit-test coupon 3: 0.10/0.15/0.20)
DOVE_TRIAL_CLEARS = (0.10, 0.15, 0.20)
DOVE_TIP_CLEAR = 0.4                 # DESIGN: rail tip to groove floor (the groove floor is an 18 mm bridge)
BACK_GAP = SLIDE_CLEAR               # DESIGN: cradle back face to plate face, the same as the flanks
MIN_ENGAGE = 80.0                    # DESIGN: check limit (the PETG stand needed 30; PLA creeps)

# --- Wall plate (DESIGN) --------------------------------------------------------------------------
PLATE_T = 4.0
PLATE_W = 52.0
PLATE_TOP = 128.0                    # DESIGN: Z of the plate's (and rail's) top end; the groove's top end sits on it
RAIL_BOTTOM = 24.0                   # DESIGN: Z where the rail starts (engagement = PLATE_TOP - RAIL_BOTTOM)
WOOD_SCREW_Z = (50.0, 110.0)         # DESIGN: two #6 countersunk, 60 mm apart, through the rail
WOOD_CLEAR_D = 3.8                   # DESIGN: #6 (3.5 mm) shank clearance
WOOD_HEAD_D = 7.4                    # SOURCED: #6 flat head is 0.262" (6.65 mm) max; + 0.75
WOOD_CSK_ANGLE = 82.0                # SOURCED: imperial flat-head angle
WOOD_HEAD_SINK = 0.5                 # DESIGN: head top below the rail face

# --- Detent (DESIGN) ------------------------------------------------------------------------------
# A block on the plate's bottom end reaches forward under the cradle. An M3 x 8 goes up through it
# into an insert in the cradle's underside. It only stops the cradle being lifted: the cradle's
# weight sits on the rail's top end, and the ear clears the cradle by DETENT_GAP.
EAR_X = (13.0, 23.0)                 # DESIGN: right of the plug, clear of the bend zone
EAR_T = 4.0                          # DESIGN: thickness (Z); the screw passes through it
EAR_D = 12.0                         # DESIGN: reaches this far off the wall
DETENT_GAP = 0.3                     # DESIGN: ear top to the cradle's underside
DETENT_SCREW_L = 8.0                 # M3 x 8
M3_CLEAR_D = 3.4
INSERT_HOLE_D = 4.0                  # MEASURED in PETG on the espframe stand's coupons; check in PLA
INSERT_L = 4.0
INSERT_HOLE_DEPTH = 5.5
DETENT_D = 7.65                      # DESIGN: screw axis off the wall, centred in the 7 mm back (4.15..11.15)
MIN_INSERT_WALL = 1.2                # DESIGN: check limit round an insert hole

# --- Antenna and print checks ---------------------------------------------------------------------
ANTENNA_METAL_KEEPOUT = 15.0         # DESIGN: no screws or inserts this close to the antenna end
MIN_WALL = 2.4                       # DESIGN: check limit (PLA)
MIN_LOCAL_WALL = 1.5                 # DESIGN: check limit where a relief thins a wall
BED = (256.0, 256.0, 256.0)          # SOURCED: Bambu P1S build volume

# --- Derived depths (off the wall) ----------------------------------------------------------------
D_PLATE_FACE = PLATE_T                         # 4.0
D_RAIL_TIP = PLATE_T + RAIL_D                  # 8.0
D_BACK0 = PLATE_T + BACK_GAP                   # 4.15: cradle back face (wall side)
D_BACK1 = D_BACK0 + BACK_T                     # 11.15: cradle back's inner face
D_CASE_BACK = D_BACK1 + CLEAR_BACK             # 11.45
D_CASE_FRONT = D_CASE_BACK + CASE_T            # 24.0
D_LIP0 = D_CASE_FRONT + LIP_GAP                # 24.15
D_FRONT = D_LIP0 + LIP_T                       # 25.65: depth off the wall
D_GROOVE_FLOOR = D_RAIL_TIP + DOVE_TIP_CLEAR   # 8.4

# --- Derived plan ---------------------------------------------------------------------------------
Z_CASE0 = FLOOR_T                              # the case stands on the floor
Z_CASE1 = Z_CASE0 + CASE_H                     # 144.2
Z_CASE_MID = (Z_CASE0 + Z_CASE1) / 2
X_IN = CASE_W / 2 + CLEAR_SIDE                 # cavity half-width
X_OUT = X_IN + WALL                            # outer half-width
Z_TOP = Z_CASE1                                # walls stop level with the case top (no top retention, option b)
BORDER_X = (CASE_W - (STEP_WINDOW_Y[1] - STEP_WINDOW_Y[0])) / 2      # 3.6
BORDER_Z = (CASE_H - (STEP_WINDOW_X[1] - STEP_WINDOW_X[0])) / 2      # 9.55
