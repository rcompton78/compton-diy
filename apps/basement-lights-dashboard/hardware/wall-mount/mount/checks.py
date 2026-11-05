"""Fit checks. Each returns (name, ok, detail); build.py exits non-zero, and the release refuses to
pack, if any fails."""

from build123d import Pos

from . import params as p
from . import reference as ref
from .cradle import detent_x, head_channel_x, lip_edges, window_x
from .geom import box, groove_half, rail_half, step_d, step_x, step_z
from .plate import csk_depth, ear_z

EPS = 1e-3


def run(cradle, plate, printed: dict) -> list:
    results = []

    def add(name, ok, detail):
        results.append((name, bool(ok), detail))

    case = ref.case()

    # 1. The case fits the cradle with no overlap; report the smallest gap.
    over = (cradle & case).volume
    # The case stands on the floor (contact by design), so measure the gaps above the floor.
    gap = (cradle & box(-100, 100, -10, 60, p.Z_CASE0 + 0.01, 400)).distance_to(case)
    add("Case fits the cradle", over < EPS and gap >= min(p.CLEAR_SIDE, p.CLEAR_BACK, p.LIP_GAP) - EPS,
        f"case + screw heads + flex tab + TF card: overlap {over:.4f} mm³, nearest {gap:.2f} mm "
        f"(sides {p.CLEAR_SIDE}, back {p.CLEAR_BACK}, lip {p.LIP_GAP}); stands on the {p.FLOOR_T:.0f} mm floor")

    # 2. The case slides in from the top: the volume it sweeps from well above the cradle down into its
    #    seat (every feature extruded straight up) never meets the cradle.
    s_over = (cradle & ref.case_swept(p.Z_TOP + 50)).volume
    add("Case slides in from the open top", s_over < EPS,
        f"the case's swept path from above the cradle to its seat never touches it (overlap {s_over:.4f} mm³): "
        f"heads ride in {len(head_channel_x())} channels, the flex tab in a {p.FLEX_RELIEF_DEPTH} mm relief")

    # 3. Lips stay outside the window; the active area is fully clear.
    w_over = (cradle & ref.window_column()).volume
    w_gap = cradle.distance_to(ref.window_column())
    a_over = (cradle & ref.active_column()).volume
    lx0, lx1, lz = lip_edges()
    wx0, _ = window_x()
    add("Lips clear the case window", w_over < EPS and w_gap >= p.LIP_WINDOW_MARGIN - EPS and a_over < EPS,
        f"nearest lip {w_gap:.2f} mm outside the window (need ≥ {p.LIP_WINDOW_MARGIN}); nothing over the "
        f"{p.ACTIVE_W:.0f} × {p.ACTIVE_H:.1f} active area; lips cover {wx0 - (-p.CASE_W / 2) - p.LIP_WINDOW_MARGIN:.1f} mm "
        f"of each {p.BORDER_X:.1f} mm side border and {lz - p.Z_CASE0:.2f} of the {p.BORDER_Z:.2f} bottom border")

    # 4. Lips actually hold the case: they overlap its face.
    lip_hold = min(lx0 + p.CASE_W / 2, p.CASE_W / 2 - lx1, lz - p.Z_CASE0) - p.CLEAR_SIDE
    add("Lips retain the case", lip_hold >= 2.0,
        f"each lip overlaps the case face by ≥ {lip_hold:.2f} mm even pushed to one side (need ≥ 2); printed "
        f"unsupported: side lips overhang {max(lx0 + p.X_IN, p.X_IN - lx1):.1f} mm, the bottom lip spans "
        f"{2 * p.X_IN:.0f} × {lz - p.FLOOR_T:.0f} mm over the pocket")

    # 5. Plug and cable.
    k_over = (cradle & ref.plug_keepout()).volume + (plate & ref.plug_keepout()).volume
    ux, ud = ref.usb_axis()
    add("USB-C plug and cable clear", k_over < EPS,
        f"reserved {p.PLUG_W} × {p.PLUG_T} × {p.PLUG_L} mm overmold through a "
        f"{p.PLUG_W + 2 * p.PLUG_CLEAR:.1f} × {p.PLUG_T + 2 * p.PLUG_CLEAR:.1f} floor opening; "
        f"{p.PLUG_BEND_CLEAR:.0f} mm below the case clear for the plug and its bend (overlap {k_over:.4f} mm³); "
        f"axis {ud:.1f} mm off the wall")

    # 6. TF slot reachable through the right wall.
    t_over = (cradle & ref.tf_slot_column()).volume
    add("TF slot open through the right wall", t_over < EPS,
        f"nothing of the cradle in front of the case's {p.STEP_TF_SLOT_X[1] - p.STEP_TF_SLOT_X[0]:.1f} mm TF slot "
        f"(overlap {t_over:.4f} mm³), {p.TF_MARGIN_Z} mm spare along and {p.TF_MARGIN_D} mm across")

    # 7. Antenna end: open top, and no metal near it.
    az = ref.antenna_zone().bounding_box()
    metal_z = [z for z in p.WOOD_SCREW_Z] + [ear_z()[0]]
    near = min(az.min.Z - z for z in metal_z)
    top_open = (cradle & box(az.min.X, az.max.X, p.D_BACK1, p.D_FRONT + 1, p.Z_CASE1 - 0.01, 400)).volume
    add("Antenna end kept clear", near >= p.ANTENNA_METAL_KEEPOUT and top_open < EPS,
        f"nearest screw or insert {near:.1f} mm below the antenna (need ≥ {p.ANTENNA_METAL_KEEPOUT:.0f}); "
        f"the cradle stops at the case's top edge: nothing over it, only the PLA back and walls beside it")

    # 8. Dovetail clearance, measured on the solids: the groove's walls against the rail alone.
    rail = plate & box(-50, 50, p.D_PLATE_FACE + 0.01, p.D_RAIL_TIP + 1, 10, p.PLATE_TOP - 10)
    groove_walls = cradle & box(-15, 15, p.D_BACK0 - 1, p.D_GROOVE_FLOOR + 1, 10, p.PLATE_TOP - 10)
    flank_gap = groove_walls.distance_to(rail)
    d_over = (cradle & plate).volume
    tip_gap = p.D_GROOVE_FLOOR - p.D_RAIL_TIP
    back_gap = p.D_BACK0 - p.D_PLATE_FACE
    add("Dovetail clearance", d_over < EPS and abs(flank_gap - p.SLIDE_CLEAR) < 0.01,
        f"measured {flank_gap:.3f} mm from the groove to the rail (square to the flank; design {p.SLIDE_CLEAR}), "
        f"{tip_gap:.2f} mm at the tip, {back_gap:.2f} mm back to plate; cradle and plate never overlap "
        f"({d_over:.4f} mm³)")

    # 9. Engagement, the weight path, and the lift needed to take the cradle off: lift it L, pull it
    #    1 mm off the wall, and see whether it still meets the plate.
    engage = p.PLATE_TOP - max(p.RAIL_BOTTOM, 0.0)
    seat = (cradle & box(-50, 50, p.D_PLATE_FACE, p.D_RAIL_TIP, p.PLATE_TOP, p.PLATE_TOP + 0.01)).volume

    def captive(lift):
        return (Pos(0, -1.0, lift) * cradle & plate).volume > EPS

    lo, hi = 0.0, p.PLATE_TOP + 20
    ok_bounds = captive(lo) and not captive(hi)
    for _ in range(12):
        mid = (lo + hi) / 2
        lo, hi = (mid, hi) if captive(mid) else (lo, mid)
    headroom = hi
    add("Dovetail engagement and lift-off", engage >= p.MIN_ENGAGE and seat > 0 and ok_bounds
        and headroom <= p.MAX_HEADROOM,
        f"{engage:.0f} mm of rail in the groove (need ≥ {p.MIN_ENGAGE:.0f}); the groove's closed top sits on the "
        f"rail's top end, so the weight goes down the rail, not through a cantilever; measured lift to free it "
        f"{headroom:.1f} mm (limit {p.MAX_HEADROOM:.0f}), so leave {headroom + 5:.0f} mm free above the cradle")

    # 10. Detent: the ear clears the cradle, and the screw reaches the insert.
    _, ez1 = ear_z()
    ear_gap = 0.0 - ez1
    reach = p.DETENT_SCREW_L - p.EAR_T - p.DETENT_GAP
    # Wall left round the insert itself (Ø INSERT_OD), not just its hole.
    walls = min(p.DETENT_D - p.INSERT_OD / 2 - p.D_BACK0, p.D_BACK1 - (p.DETENT_D + p.INSERT_OD / 2))
    groove_web = detent_x() - p.INSERT_OD / 2 - groove_half(p.D_GROOVE_FLOOR)
    plug_web = p.EAR_X[0] - (ux + p.PLUG_W / 2 + p.PLUG_CLEAR + p.PLUG_BEND_MARGIN)
    add("Detent screw", ear_gap > 0 and p.INSERT_L - 1.0 <= reach <= p.INSERT_HOLE_DEPTH - 0.3
        and walls >= p.MIN_INSERT_WALL and groove_web >= p.MIN_INSERT_WALL and plug_web > 0,
        f"M3 × {p.DETENT_SCREW_L:.0f} up through the {p.EAR_T:.0f} mm ear: {reach:.1f} mm into an M3 × "
        f"{p.INSERT_L:.0f} insert ({p.INSERT_HOLE_DEPTH} mm hole); ear {ear_gap:.1f} mm under the cradle "
        f"(never carries it); {walls:.2f} mm of wall round the Ø{p.INSERT_OD} insert, {groove_web:.1f} mm to the groove; ear "
        f"{plug_web:.1f} mm clear of the plug zone")

    # 11. Wall screws.
    head_top = p.D_RAIL_TIP - p.WOOD_HEAD_SINK
    skin = head_top - csk_depth()
    add("Wall screws", p.WOOD_HEAD_SINK >= 0.5 - EPS and skin >= 1.0 and p.WOOD_HEAD_D < 2 * rail_half(head_top),
        f"2 × #6 countersunk, {p.WOOD_SCREW_Z[1] - p.WOOD_SCREW_Z[0]:.0f} mm apart on the centre line: heads "
        f"{p.WOOD_HEAD_SINK} mm below the rail face (the groove slides over them), Ø{p.WOOD_HEAD_D} countersink "
        f"in the {2 * rail_half(head_top):.1f} mm rail, {skin:.1f} mm under the cone")

    # 12. Wall thicknesses.
    skin_back = p.D_BACK1 - p.D_GROOVE_FLOOR
    head_back = (p.D_CASE_BACK - p.HEAD_H - p.HEAD_POCKET_EXTRA) - p.D_BACK0
    relief_wall = p.WALL - p.FLEX_RELIEF_DEPTH
    ok = (p.WALL >= p.MIN_WALL and p.FLOOR_T >= p.MIN_WALL and skin_back >= p.BACK_SKIN_MIN
          and p.PLATE_T >= p.MIN_WALL and p.EAR_T >= p.MIN_WALL and relief_wall >= p.MIN_LOCAL_WALL
          and head_back >= p.MIN_WALL and p.LIP_T >= 1.5)
    add("Wall thickness", ok,
        f"walls {p.WALL}, floor {p.FLOOR_T}, back {skin_back:.2f} in front of the groove, {head_back:.2f} behind "
        f"the head channels, plate {p.PLATE_T}, ear {p.EAR_T}, lips {p.LIP_T} (need ≥ {p.MIN_WALL}, lips 1.5); "
        f"{relief_wall:.1f} at the flex relief (need ≥ {p.MIN_LOCAL_WALL})")

    # 13. Every part fits the P1S bed.
    sizes = []
    fits = True
    for name, solid in printed.items():
        bb = solid.bounding_box()
        size = (bb.max.X - bb.min.X, bb.max.Y - bb.min.Y, bb.max.Z - bb.min.Z)
        fits &= all(s <= b for s, b in zip(size, p.BED)) and abs(bb.min.Z) < 1e-3
        sizes.append(f"{name} {size[0]:.0f} × {size[1]:.0f} × {size[2]:.1f}")
    add("Every part fits the P1S bed, flat", fits, "; ".join(sizes))

    # 14. Overall depth off the wall.
    add("Depth off the wall", p.D_FRONT <= 27.0,
        f"{p.D_FRONT:.2f} mm to the lip face (limit 27); cradle {2 * p.X_OUT:.1f} × {p.Z_TOP:.1f} mm")
    return results


def summary_numbers():
    """A few derived numbers for the README and the coupon notes."""
    ux, ud = ref.usb_axis()
    return {"usb_x": ux, "usb_d": ud, "tf_z": (step_z(p.STEP_TF_SLOT_X[0]), step_z(p.STEP_TF_SLOT_X[1])),
            "tf_d": (step_d(p.STEP_SPACER_Z[0]), step_d(p.STEP_SPACER_Z[1])), "heads_x": [step_x(y) for _, y in p.STEP_HEADS]}
