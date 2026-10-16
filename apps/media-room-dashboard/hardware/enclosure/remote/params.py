"""Every dimension of the media-room remote's handheld enclosure, in millimetres.

Each value is tagged with where it comes from:

    MEASURED  measured on the real parts by the user
    SOURCED   from a vendor drawing, board file or listing
    DESIGN    a design choice made for this enclosure
    EST       an estimate that still needs checking on real parts

Sources:
  * Freenove FNK0104 (2.8" ESP32-S3 CYD) = LCDwiki ES3C28P. Outline drawing on p13 of
    Datasheet/ES3C28P_ES2N28P_Specification_V1.0.pdf in github.com/Freenove/Freenove_ESP32_S3_Display.
  * Adafruit 6092 (BQ25185 + 3.3V buck): the Eagle board file in
    github.com/adafruit/Adafruit-bq25185-with-3.3V-Buck-PCB, and the product page (height).
  * 504060 LiPo: measured by the user, 61 × 40 × 5.

Coordinate frame (every part is built in it, assembled):
  x = across the remote, left to right seen from the front, 0 on the centreline;
  y = along the remote, up from the outside of the bottom (charge-port) end;
  z = -depth, so the bezel front face is z = 0 and the back cover's outer face is z = -D_BACK_OUTER.
Depths below (D_*) are distances behind the front face, positive.

Variants: VARIANTS maps a variant number to the boards it carries. Only variant 1
(BQ25185 charger, no fuel gauge) exists; a later variant adds an entry here and the
parts it needs, without changing variant 1's numbers.
"""

from dataclasses import dataclass, field

# --- Printer / material -------------------------------------------------------
MATERIAL = "PETG"                 # DESIGN (user choice), both parts
BED = 256.0                       # SOURCED: Bambu P1S build volume (cube)
M3_CLEAR_D = 3.4                  # DESIGN: M3 clearance hole

# --- Heat-set inserts (user's LUKAISEN kit, as on the espframe stand) -----------
INSERT_OD = 4.2                   # SOURCED: kit spec
INSERT_HOLE_D = INSERT_OD - 0.2   # DESIGN, confirmed on the espframe stand's coupons (PETG)
INSERT_L = 4.0                    # SOURCED: M3 × 4 insert from the kit

# --- Freenove FNK0104 ----------------------------------------------------------
PCB_W = 50.0                      # SOURCED: drawing
PCB_L = 86.0                      # SOURCED: drawing
PCB_T = 1.6                       # SOURCED: drawing
PCB_R = 3.5                       # SOURCED: drawing corner radius
HOLE_D = 3.2                      # SOURCED: drawing, Ø5.6 pads
HOLE_PAD_D = 5.6                  # SOURCED
HOLE_INSET = 4.0                  # SOURCED: 42 × 78 pattern, 4 mm in from every edge
# Front-side stack, glass front to PCB front: TP 1.0 + glue 0.5 + LCD 2.3 + glue 0.5.
STACK_T = 4.3                     # SOURCED: drawing side view
BACK_PARTS_H = 4.7                # SOURCED: "4.70 SMD (MAX)" behind the PCB
GLASS_W = 50.0                    # SOURCED: touch panel 50.00 × 69.20
GLASS_L = 69.2                    # SOURCED
GLASS_FROM_BOTTOM = 8.22          # SOURCED: antenna end of the PCB to the glass
VA_W, VA_L = 43.60, 58.05         # SOURCED: visible window (inside the black print)
VA_FROM_BOTTOM = 10.96            # SOURCED
AA_W, AA_L = 43.20, 57.60         # SOURCED: active area (lit pixels)
AA_FROM_BOTTOM = 11.12            # SOURCED
ANTENNA_KEEPOUT = 15.0            # DESIGN: the PCB antenna sits in the last ~10 mm of the bottom end
                                  #   (drawing); no LiPo within 15 mm of that end.
# USB-C (flashing port) on the back face, centred on the top end. The drawing puts its
# centre 25.00 from either long edge; its overhang and body height are read off the
# drawing's scale, so they're EST.
FN_USB_X = 0.0                    # SOURCED: 11.57 + 13.43 = 25.00 from the long edge
FN_USB_OVERHANG = 1.5             # EST: receptacle mouth past the PCB's top end
FN_USB_GAP = 0.2                  # DESIGN: receptacle mouth to the top wall, so the board drops straight in
FN_USB_W, FN_USB_H = 8.94, 3.26   # SOURCED (typical mid-size USB-C receptacle body), EST on this board
FN_USB_L = 7.3                    # EST: receptacle depth into the PCB
# BAT socket (MX1.25 2-pin) on the back, 23.18 from the top end and 5.18 in from the right
# long edge (seen from the front). RESET and BOOT buttons on the back next to the USB-C.
BAT_FROM_TOP = 23.18              # SOURCED: 86 − (11.82 + 15.97 + 35.03)
BAT_FROM_EDGE = 5.18              # SOURCED
BAT_SOCKET = (6.0, 4.5, 3.5)      # EST: MX1.25 2P right-angle socket (along x, along y, height)
RESET_X = -11.56                  # SOURCED: 25 − 36.56
BOOT_X = 11.57                    # SOURCED: 25 − 13.43
BUTTON_FROM_TOP = 2.1             # EST: read off the drawing's scale
BUTTON_H = 2.0                    # EST: tactile switch height above the PCB back

# --- 504060 LiPo (1200 mAh) ------------------------------------------------------
CELL_W = 40.0                     # MEASURED
CELL_L = 61.0                     # MEASURED: including the folded protection PCB
CELL_T = 5.0                      # MEASURED
CELL_SWELL = 0.10                 # DESIGN: room for 10% swelling in thickness
CELL_TAPE_T = 0.4                 # DESIGN: thin double-sided tape holding the cell on the cover
CELL_LEAD_L = 80.0                # MEASURED: JST-PH lead length from the cell end
CELL_LEAD_FROM_EDGE = 19.5        # MEASURED: lead exit from the nearer long edge, on the protection-PCB end (≈ centred).
                                  #   The wires may be taped along that end to either corner, so the run is checked
                                  #   from the centre and from both corners.
CELL_CLEAR = 1.0                  # DESIGN: pocket clearance per side, x and y
CELL_TO_PARTS = 1.0               # DESIGN: air gap from the cell face to the Freenove's back parts

# --- Wiring --------------------------------------------------------------------
PIGTAIL_STRIP = 6.0               # DESIGN: bare ends stripped into the screw terminal
PIGTAIL_SLACK = 10.0              # DESIGN: service slack on top of the modelled run
                                  #   The pigtail isn't bought yet: the build reports the minimum length to buy
                                  #   (run + strip + slack, rounded up to 5 mm). Splice one if it's shorter.
WIRE_D = 1.1                      # EST: 28 AWG with insulation
MIN_CHANNEL = 3.0                 # DESIGN: smallest wire-channel side for the 2-wire pigtail
JST_PLUG = (6.0, 4.5, 6.0)        # SOURCED (JST PHR-2 housing: 6.0 wide, 4.5 tall, 6.0 long)
JST_BEND = 8.0                    # DESIGN: free length behind the plug for the lead to turn

# --- USB-C plug overmold -----------------------------------------------------------
PLUG_OVERMOLD = (12.0, 6.5)       # EST: typical USB-C cable overmold, width × height
PLUG_CLEAR = 0.5                  # DESIGN: per side round the overmold

# --- Shell -----------------------------------------------------------------------
WALL = 2.2                        # DESIGN: side and bottom walls
TOP_WALL = 3.0                    # DESIGN: top wall, thicker to take the cover's tongue slots
FRONT_T = 1.5                     # DESIGN: bezel / front wall thickness
BEZEL_CHAMFER = 1.0               # DESIGN: 45° chamfer round the screen opening, on the front
OUTER_R = 6.0                     # DESIGN: plan-view corner radius
EDGE_FILLET = 1.6                 # DESIGN: round-over on the front and back outer edges
SIDE_CLEAR = 0.3                  # DESIGN: Freenove board / glass to the side walls
GLASS_FLOAT = 0.1                 # DESIGN: boss face stops the PCB this far short of clamping the glass
BOSS_D = 6.4                      # DESIGN: Freenove mount bosses (M3 × 4 insert)
FN_SCREW_L = 6.0                  # DESIGN: M3 × 6 from behind the Freenove into the boss inserts
MIN_SKIN = 1.0                    # DESIGN: front skin left under an insert hole

# Gap between the charger's terminal end and the Freenove's bottom end: the pigtail leaves
# the terminal along +y and turns rearwards past the Freenove's back parts there.
WIRE_TURN_GAP = 7.0               # DESIGN

# --- Back cover --------------------------------------------------------------------
COVER_T = 2.4                     # DESIGN: takes an M3 countersunk head flush
COVER_CLEAR = 0.2                 # DESIGN: cover edge to the shell walls
RIB_T = 1.2                       # DESIGN: cell-pocket ribs
RIB_H = 5.0                       # DESIGN: rib height above the cover's inner face
BOTTOM_RIB_H = 1.5                # DESIGN: the bottom rib is low, so the lead passes over it wherever it leaves the cell end
LEDGE_W = 1.0                     # DESIGN: cover-support ledge on the side walls (45° underside)
TONGUE_W = 8.0                    # DESIGN: two tongues on the cover's top edge
TONGUE_X = 15.0                   # DESIGN: ± from the centreline, clear of the USB opening
TONGUE_L = 1.2                    # DESIGN: how far a tongue reaches into its slot
TONGUE_T = 1.2                    # DESIGN
SLOT_DEPTH = 1.6                  # DESIGN: slot depth into the top wall
SLOT_H = 2.0                      # DESIGN: slot height (depth axis) for the tongue to swing in
SLOT_CLEAR = 0.2                  # DESIGN: per side across the slot
COVER_SCREW_L = 8.0               # DESIGN: M3 × 8 countersunk (ISO 10642), two at the bottom
CSK_D = 6.0                       # SOURCED: ISO 10642 M3 head Ø (5.54 max actual; 6.0 cut)
CSK_DEPTH = 1.7                   # SOURCED: M3 countersunk head height
COVER_BOSS_D = 7.6                # DESIGN: overlaps the side wall (a boss tangent to it breaks the 3MF mesh)
COVER_BOSS_INSET = 3.5            # DESIGN: boss centre in from the side wall
COVER_BOSS_Y = 9.0                # DESIGN: cover screw bosses, centred this far up from the outside bottom
PINHOLE_D = 2.0                   # DESIGN: RESET / BOOT pinholes in the cover (paperclip)
LED_HOLE_D = 2.5                  # DESIGN: CHG LED window in the cover


# --- Charger boards ----------------------------------------------------------------
@dataclass(frozen=True)
class Charger:
    """A charger board, in its own coordinates.

    bx runs along the board from its USB-C end (0) to the far end; by across it. The
    component side faces the back cover once fitted (see shell.charger_frame()).
    """
    name: str
    length: float
    width: float
    pcb_t: float
    holes: tuple                  # (bx, by) centres
    hole_d: float
    usb: tuple                    # (bx0, bx1, by0, by1) body footprint; bx0 < 0 is the overhang
    usb_h: float
    jst: tuple                    # (bx0, bx1, by0, by1) battery socket body; opening faces +by
    jst_h: float
    terminal: tuple               # (bx0, bx1, by0, by1) screw terminal; wires enter from +bx
    terminal_h: float
    terminal_wire_by: tuple       # by of the screw terminal's + and − wires
    terminal_wire_h: float        # wire entry height above the component face
    chg_led: tuple                # (bx, by) of the charge LED on the component side
    hooks: tuple = field(default=())   # (bx centre, side) snap-hook positions; side −1 = by 0 edge, +1 = far edge


BQ25185 = Charger(
    name="Adafruit 6092 BQ25185 + 3.3V buck",
    length=29.21, width=19.05, pcb_t=1.6,                                      # SOURCED: board outline (Eagle)
    holes=((2.54, 2.54), (26.67, 2.54), (2.54, 16.51), (26.67, 16.51)),       # SOURCED
    hole_d=2.5,                                                                # SOURCED: MOUNTINGHOLE2.5
    usb=(-1.14, 6.20, 5.055, 13.995), usb_h=3.26,                              # SOURCED footprint; height EST
    jst=(9.97, 17.97, 10.997, 18.497), jst_h=6.0,                              # SOURCED footprint; height SOURCED (PH SMD)
    terminal=(21.71, 27.97, 4.0, 15.05), terminal_h=8.4,                       # SOURCED footprint; 10 mm total − PCB (listing)
    terminal_wire_by=(6.985, 12.065), terminal_wire_h=3.5,                     # SOURCED pins; entry height EST
    chg_led=(6.985, 18.288),                                                   # SOURCED
    hooks=((14.5, -1), (22.5, 1)),                                             # DESIGN: clear of the JST and pads
)
CHARGER_STANDOFF_D = 4.5          # DESIGN: printed standoffs under the charger's four holes
PEG_D = 2.2                       # DESIGN: locating pegs through the 2.5 mm holes
PEG_PROUD = 1.0                   # DESIGN: pegs stand this far past the PCB's top face
D_CHARGER_FRONT = 8.0             # DESIGN: depth of the charger PCB's bottom (non-component) face
CHARGER_EDGE_CLEAR = 0.15         # DESIGN: PCB edge to its snap hooks
HOOK_T = 1.2                      # DESIGN: hook arm thickness
HOOK_W = 3.0                      # DESIGN
HOOK_BARB = 0.5                   # DESIGN: barb overhang onto the PCB's component face
HOOK_MAX_STRAIN = 0.02            # DESIGN: PETG cantilever limit for one snap
PETG_E_STRAIN_NOTE = "PETG yields ~4%; keep a one-off snap under 2%"


@dataclass(frozen=True)
class Variant:
    number: int
    title: str
    charger: Charger
    extra_boards: tuple = ()      # future: fuel gauge etc. Empty for variant 1.


VARIANTS = {
    1: Variant(1, "Variant 1: BQ25185 charger, no gauge", BQ25185),
}
DEFAULT_VARIANT = 1


# --- Derived layout (depends only on the values above) ------------------------------
# Depths (behind the front face).
D_GLASS_FRONT = FRONT_T
D_PCB_FRONT = D_GLASS_FRONT + STACK_T + GLASS_FLOAT
D_PCB_BACK = D_PCB_FRONT + PCB_T
D_PARTS_BACK = D_PCB_BACK + BACK_PARTS_H
D_CELL_FRONT = D_PARTS_BACK + CELL_TO_PARTS
CELL_POCKET_T = CELL_T * (1 + CELL_SWELL)
D_COVER_INNER = D_CELL_FRONT + CELL_POCKET_T + CELL_TAPE_T
D_BACK_OUTER = D_COVER_INNER + COVER_T

# Plan view.
INNER_HALF_W = PCB_W / 2 + SIDE_CLEAR
OUTER_W = 2 * (INNER_HALF_W + WALL)
Y_INNER_BOTTOM = WALL


def charger_y0(ch: Charger) -> float:
    """y of the charger's bx = 0 edge: its USB-C mouth just inside the bottom wall."""
    return Y_INNER_BOTTOM - ch.usb[0] + 0.1


def pcb_y0(ch: Charger) -> float:
    """y of the Freenove PCB's bottom (antenna) end."""
    return charger_y0(ch) + ch.length + WIRE_TURN_GAP


def layout(ch: Charger):
    """Plan-view y positions that depend on the charger: PCB ends, inner top, outer length."""
    y0 = pcb_y0(ch)
    y1 = y0 + PCB_L
    y_inner_top = y1 + FN_USB_OVERHANG + FN_USB_GAP
    return y0, y1, y_inner_top, y_inner_top + TOP_WALL
