# 10" stand: what's known so far

Everything gathered for a portrait desk stand for the 10" espframe, so the work isn't lost before the 10" model is built. The 7" stand in this folder is the template: same approach, new numbers. Values marked *est.* or *inferred* need checking on the real board before anything is printed.

## Board

- **Guition ESP32-P4 JC8012P4A1** (also sold as `JC8012P4A1C_I_W_Y`): ESP32-P4 with an ESP32-C6 co-processor for Wi-Fi.
- 10.1" MIPI-DSI JD9365 panel, **800 × 1280 native (portrait-native)**, used as 1280 × 800 by the UI. GSL3680 capacitive touch.
- Weight **about 550 g** ([G-SPEC] p.4).
- Repo: `devices/guition-esp32-p4-jc8012p4a1/` in espframe.

## Mechanical: official structure drawing

The vendor structure drawing **[G-STRUCT]** (`3-Structure_Diagram/JC8012P4A1.pdf`, plus `1.jpg` and `2.jpg`) is in Guition's doc package. Mirrors:
- https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6/tree/HEAD/3-Structure_Diagram
- `DRubioG/JC8012P4A1C-I-W-Y`
- `profi-max/JC8012P4A1_BSP_ESP32P4` (`Demos_and_Docs/3-Structure_Diagram/`)

The drawing is portrait. Stated values (high confidence):

| Item | Value |
|---|---|
| Shell (outer metal/plastic frame) | **158.7 × 242.8 mm** (± 0.05) |
| Touch glass | **155.2 × 239.3 mm**. It sits *inside* the shell, about 1.75 mm in on each side, so the shell edge is the safe contact surface (unlike the 7", whose glass overhangs). |
| Active area | **135.36 × 216.58 mm** |
| Mounting holes | **6 × Ø2.5, M2.5 thread** (per [MW-WALL]), each on a **Ø7.5 boss standing 4.5 mm proud** of the back |
| Hole pitch | **128.7 mm** across; **90 + 122.8 = 212.8 mm** top to bottom |
| Body (glass front to flat metal back) | **7 mm** |
| Rear electronics box | **93.5 × 138.35 mm**, protrudes **15.51 mm** past the back |
| Total depth (glass to rear of box) | **22.51 mm** |

Positions in the **rear view, portrait**. The origin is the shell's top-left corner, and the top is the LCD-FPC bracket end. The rear view mirrors left and right relative to the front.
- **Hole columns:** x = 15.0 and 143.7 mm (symmetric).
- **Hole rows:** y = 15.0, 105.0 and 227.8 mm. **The middle row is not centred:** it is 105 mm from the FPC end and 137.8 mm from the other end.
- **Rear box:** centred across (32.6 mm each side). It sits about 51 mm from the top and about 53 mm from the bottom (scaled, ± 1).
- **LCD-FPC bracket:** a raised block, about 52 × 22 mm, top-centre between the top holes (scaled). It stays within the 4.5 mm boss plane.
- **Front bezels, shell edge to active area (scaled, ± 0.5):** left 12.9, right 10.6, top 11.7, bottom 14.6 mm. They are uneven, like the 7", so a frame should centre on the active area.
- **Right-edge feature:** a small flex-and-pad about 122 mm from the top, on the right edge of the rear view. It's *inferred* to be the C6 antenna lead or the touch FPC. Keep that edge clear.

Other sources:
- **[G-SPEC]** vendor spec, `jc8012p4a1c_i_w_specifications-en-v1-1.pdf`: https://www.laskakit.cz/user/related_files/jc8012p4a1c_i_w_specifications-en-v1-1.pdf (product page: https://www.laskakit.cz/en/guition-jc8012p4a1c-esp32-p4-c6-m5-vyvojovy-modul-s-displejem-10-1--800x1280/).
- **[MW-WALL]** MakerWorld 2027800, a CC BY-SA wall mount with portrait and landscape STEPs that screw into the 6 holes: https://makerworld.com/en/models/2027800-guition-10-1-esp32p4-jc8012p4a1c-wall-mount. The STEP needs a MakerWorld login. It's a useful cross-check of the hole pattern (expected 128.7 × 90 + 122.8).
- **[MW-STAND]** MakerWorld 2490049, "Guition P4 10inch Screen Stand": https://makerworld.com/en/models/2490049-guition-p4-10inch-screen-stand. This is the stand the user likes the look of. It is slot-in, under a standard licence, so it **can't be remixed**: imitate the look, don't copy the file.

## Connectors, power and cable

- **Connectors (inferred, medium confidence):** the 3 × USB-C bank and the 4-pin UART/INPUT (CN2) are on the short edge of the PCB opposite the LCD-FPC. So they are at the **bottom end of the rear box**, facing down along the back. TF, camera and battery connectors are on one long side; speaker and mics are near the FPC end. Check on the real board.
- **Power and data:** use the same **Poyiccot USB-C panel-mount extension** (full pass-through, 30 cm) as the 7".
  - Plug it into the **USB-to-UART port (USB1, CH340)**: that powers the board and lets esptool flash and read logs through the stand.
  - The board's 5.1 kΩ CC resistors (R67/R68, R69/R70, R72/R74) make any USB-C charger supply 5 V.
- **Port height:** the user measured the USB-C mouth at **about 45 mm** above the shell bottom. The photo estimate had been 53.4, so the measurement is about 8 mm lower than the drawing-derived position. Confirm it.
- **Plug drop:** the plug end sits about 17.5 mm above the glass bottom edge. An R18 bend reaches about 8.7 mm below the glass corner vertically, so an 18 mm ledge leaves about 6.3 mm of margin (est.).
- **Cable pull:** the USB-C ports face sideways or down at one end of the box, so the cable adds a **sideways tipping moment**. Route it straight down into the foot, as on the 7".
- **Spare cable:** about 130 mm to coil in the tray (est.).
- **C6 antenna:**
  - **Keep Guition's rear cover on**, since it carries the C6 antenna. Removing it would save 15.5 mm of depth, but the base depth is set by tipping, not panel depth.
  - Keep metal (screws, inserts, ballast) about 15 mm away from the antenna region: around r 138–156, v 110–135 in the rear view (est.).
  - The nearest plate screw (r 143.7, v 105) should be nylon.

## Firmware rotation (COM-362)

- Rotation is a runtime LVGL setting; picture and touch rotate together (`common/addon/screen_rotation.yaml`).
- **Portrait (90 / 270) is gated behind Developer Features today.** COM-362 (https://linear.app/compton-apps/issue/COM-362) makes portrait a normal user option. The stands assume both 90° and 270° work.
- On the 10", the panel is portrait-native but driven with a 90° offset (`device.yaml:7,179`). User option "90" maps to LVGL 180 (`packages.yaml:11-14`), and touch has its own per-rotation remapping (`device.yaml:219-239`).
- Since power comes through the stand's jack, the board's port edge doesn't force a direction. Pick the rotation that puts the USB-C bank at the bottom, nearest the foot.

## Tip-over

- About **550 g at 242.8 mm tall**: the centre of mass sits roughly 120 mm up, against about 192 mm and 354 g for the 7". The 10" is much more tip-prone, and the side-facing cable adds sideways pull.
- The earlier plan (rev C, est.) was a 127 × 180 mm tray, giving about **8.2 N** to tip back. Two optional slim rear legs, about 160 mm deep, gave about **11.2 N**.
  - That plan included steel ballast. The 7" has since dropped steel (see the lessons below), so re-run the numbers without ballast first.
  - About 210 mm of depth was the no-ballast estimate for a 2× margin. With the 7"'s 4.5 N hands-on target, the 10" will likely need less.

## User decisions so far

- **Screw-on, not slot-in:** the stand screws to the 6 M2.5 holes like the 7".
- **The look:** they like **MakerWorld 2490049**. Imitate it in a screw-mount design.
- **Portrait is a user option** (COM-362), so both rotations are available.
- **Power:** the Poyiccot pass-through jack, same as the 7".

## Open questions

1. Confirm the 6-hole positions with calipers or the [MW-WALL] portrait STEP (expected 128.7 across, 90 + 122.8 down, middle row off-centre).
2. Confirm which end the USB-C bank and CN2 face. Measure the USB-C mouth height (about 45 mm) and identify the UART port (USB1 / CH340).
3. Confirm the C6 antenna position on the rear cover.
4. Measure the rear box height on the real board (drawing: 15.51 mm) and anything standing proud of the 4.5 mm boss plane, such as the FPC bracket.
5. Should it have a picture frame like the 7"? If so, which face width, given the bezels of 10.6–14.6 mm?
6. Tip-over target: is 4.5 N (the 7"'s hands-on figure) enough at 242.8 mm tall? Test the printed foot by hand before adding ballast.

## Lessons from the 7" build

The 7" was printed and tested piece by piece; its README has the details. These are the results that carry over.

**Measured fits: reuse as-is (same printer, same PETG).**
- **Dovetail sliding fit:**
  - `SLIDE_CLEAR = 0.02` mm per side, measured perpendicular to the flank, with rails printed **standing up**. Flat-printed rails come out undersized and read loose.
  - Keep `DOVE_TIP_CLEAR` 0.4 and `POST_FACE_CLEAR` 0.2 so the angled flanks do the gripping.
- **Heat-set inserts:** a hole of `INSERT_HOLE_D = 4.0` (OD − 0.2) went in snug and flush in PETG. The wood-fill PLA frame is still untested.
- **Jack mount:**
  - The Poyiccot jack is measured: flange 25 × 8 × 6.5 with threaded M3 holes 16.5 apart, body 12.5 × 8, 34.5 long, receptacle 1.2 mm proud.
  - The **thin-wall mount** works: the flange sits in a recess inside a 3 mm wall, leaving a 1.2 mm web with the receptacle flush through a window. The jack's own screws go in from outside.
  - It replaced a raised housing with a bezel and two inserts. Reuse it unchanged.
  - The screw heads leave 11 mm between them, so test chargers with wide plug mouldings.

**Design choices that proved out.**
- **The back-plate fills the frame** out to its inner walls (0.3 mm clear). The back is closed with no gap round the plate, and the frame screws go through the plate's edges into bosses on the frame walls instead of through tabs.
- **Vent slots:** with a closed back, there are two rows low (air in) and two high (air out), kept clear of the risers, spine and screws. On the 10", also keep them clear of the antenna region.
- **No steel ballast:** the printed 7" was sturdy without it, so the pockets were dropped. The tip-over check now uses a **4.5 N** target (horizontal push at the screen centre) taken from that hands-on test.
  - Method: model each part's volume × density × fill, find the centre of mass, and check the push needed to tip it about the foot's rear edge. Then **hand-test the printed stand** and set the target from that.
  - The model reads 4.6 N for the final 7".
- **Shorter foot:** the 7" went from 128 to 118 mm deep. The spool post was replaced by a loose cable coil in the tray.
- **Single dovetail:** two parallel sliding fits at 0.02 mm would bind. One locates itself, and the frame's bottom rail resting on the felted ledge takes the weight and stops it rocking.

**Bugs to avoid.**
- **The dovetail groove must end inside the spine.**
  - On the 7" the groove (4 deep) ran 2 mm past the spine top and cut through the 3 mm plate. The slicer showed it, not the checks.
  - Now `DOVE_GROOVE_L = SPINE_TOP_Y − 2`. Add a check for this in the 10" model.
- **Look at a slicer cross-section of every part before printing**; the fit checks don't catch everything.
- **Coupons:**
  - Test plates were the cheap way to settle the receptacle protrusion and the screw pitch.
  - Measure the actual part in hand. The first jack coupon was sized for the wrong jack (a DWEII).

**`stand/params.py`: what to reuse vs re-measure.**
- **Reusable:**
  - fits: `SLIDE_CLEAR`, `DOVE_*` profile, `DOVE_TIP_CLEAR`, `POST_FACE_CLEAR`, `INSERT_*`, `M3_CLEAR_D`
  - jack: all `JACK_*` except `JACK_X`
  - plug and cable: `PLUG_*`, `CABLE_*`, `BEND_R`
  - foot and lid: `WALL`, `FLOOR`, `LID_*`, `TILT_DEG` (10°)
  - plate: `PLATE_T`, `PLATE_EDGE_CLEAR`, `PLATE_R`
  - frame profile: `FRAME_FACE_T`, `FRAME_BEVEL`, `FRAME_OUTER_CH`, `FRAME_WALL`, `FELT_*`, `REVEAL`
  - vents: `VENT_SLOT`, `VENT_PITCH_X`
- **7"-specific, replace from the 10" drawing or measurements:**
  - glass and display: `GLASS_*`, `AA_*`, `SCREEN_MASS`
  - mounting: `HOLES` (6 × M2.5, not 4 × M3), the brass and riser stack (`TAB_H`, `BRASS_L`, `RISER_H`, `JST_*`, `D_PLATE_*`). The 10" has 4.5 mm bosses and a 15.5 mm rear box, so the plate needs a pocket or a spacer stack over the box.
  - port: `PORT_X`, `PORT_MOUTH_Y`, `PORT_DEPTH`
  - spine and dovetail position: `SPINE_X`, `SPINE_TOP_Y`, `DOVE_X`, `LOCK_Y`
  - frame: `FRAME_FACE_W`, `TAB_Y`
  - foot and cable: `FOOT_W`, `FOOT_D`, `COIL_*`, `JACK_X`, `LEDGE_H` (re-check the plug drop), `VENT_COLS`, `VENT_ROWS_Y`
- **Re-test on the 10":**
  - a receptacle and jack plate (same jack, so it's only a sanity check)
  - an M2.5 hole and screw coupon
  - the tip-over by hand with the real 550 g panel
  - the frame border look
  - the cable route with the side-facing ports
