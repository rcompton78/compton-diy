"""Fit and stability checks. Each returns (name, ok, detail); build.py fails if any is not ok."""

import math

from build123d import Vertex

from . import params as p
from . import reference as ref
from .cable import build_path
from .coupons import template_on_back
from .foot import build_rail, layout, lid_detents, lift_slot_x, lock_floor_depth
from .frames import SCREEN_TO_WORLD, box, screen_box, screen_point
from .plate import frame_tab_spans, plate_outline


def _rect_dist(x, y, rx, ry):
    """Distance from point (x, y) to the axis-aligned rectangle rx × ry (0 inside)."""
    dx = max(rx[0] - x, 0.0, x - rx[1])
    dy = max(ry[0] - y, 0.0, y - ry[1])
    return math.hypot(dx, dy)


def tip_over(foot, lid, plate_w, frame_w):
    """Horizontal push at the screen centre that tips the stand back about the foot's rear edge."""
    g = 9.81
    masses = []   # (grams, Y, Z, name)
    for name, solid in (("foot", foot), ("lid", lid), ("plate", plate_w)):
        c = solid.center()
        masses.append((solid.volume * p.PETG_DENSITY * p.PRINT_FILL, c.Y, c.Z, name))
    c = frame_w.center()
    masses.append((frame_w.volume * p.FRAME_DENSITY * p.FRAME_FILL, c.Y, c.Z, "frame"))
    sc = screen_point(p.SHELL_W / 2, p.SHELL_H / 2, p.SCREEN_CG_DEPTH)
    masses.append((p.SCREEN_MASS, sc.Y, sc.Z, "board"))
    m_tot = sum(m for m, *_ in masses)
    cg_y = sum(m * y for m, y, *_ in masses) / m_tot
    cg_z = sum(m * z for m, _, z, _ in masses) / m_tot
    push_z = screen_point(p.SHELL_W / 2, p.SHELL_H / 2, 0).Z
    force = m_tot / 1000 * g * (p.FOOT_D - cg_y) / push_z
    side = math.degrees(math.atan(((p.FRAME_X[1] - p.FRAME_X[0]) / 2) / cg_z))
    return force, masses, m_tot, cg_y, cg_z, side


def run(parts: dict) -> list:
    plate, foot, lid, frame = parts["plate"], parts["foot"], parts["lid"], parts["frame"]
    plate_w = SCREEN_TO_WORLD * plate
    frame_w = SCREEN_TO_WORLD * frame
    board = ref.board()
    results = []

    def add(name, ok, detail):
        results.append((name, bool(ok), detail))

    # 1. Plate clears the rear box, and touches the board only where the risers seat on the bosses.
    box_gap = plate.distance_to(ref.rear_box())
    add("Plate clears the rear box", abs(box_gap - p.PLATE_CLEAR) < 0.01,
        f"{box_gap:.2f} mm behind its {p.BOX_W:g} × {p.BOX_H:g} × {p.BOX_D:g} back face (design {p.PLATE_CLEAR:g})")
    b_over = (plate & board).volume
    seat_gap = plate.distance_to(ref.bosses())
    back_gap = plate.distance_to(ref.shell())
    add("Risers seat on the bosses", b_over < 1e-3 and seat_gap < 1e-3 and back_gap >= 1.0
        and abs(back_gap - (p.BOSS_H - p.RISER_SEAT[1])) < 0.01,
        f"recess floor on the boss tops ({seat_gap:.3f} mm), overlap with the board {b_over:.4f} mm³; "
        f"skirts sleeve {p.RISER_SEAT[1]:g} mm of each boss and stop {back_gap:.2f} mm off the flat back; recess Ø{p.RISER_SEAT[0]:g} × {p.RISER_SEAT[1]:g} over Ø{p.BOSS_D:g} bosses")

    # 2. M2.5 screws: through the plate and riser into the board's inserts, never bottoming.
    # The printed risers are RISER_FIT_ADD longer than the nominal model's (fit test), and the counterbore with them.
    stack = p.PLATE_T + p.RISER_H + p.RISER_FIT_ADD - p.SCREW_HEAD_CB[1]
    engage = p.SCREW_L - p.WASHER_T - stack
    add(f"M2.5 × {p.SCREW_L:g} engagement in the inserts",
        p.MIN_SCREW_ENGAGE <= engage <= p.BOSS_THREAD - p.MIN_SCREW_BOTTOM,
        f"button head on the counterbore floor, {stack:.1f} mm above the boss top: {engage:.1f} mm into the "
        f"{p.BOSS_THREAD:g} mm insert (need {p.MIN_SCREW_ENGAGE:g}–{p.BOSS_THREAD - p.MIN_SCREW_BOTTOM:.1f}); "
        "no washer, which would lift the head into the cover plug")

    fit_gap = p.FIT_FACE_GAP_MEASURED - p.RISER_FIT_ADD
    add("As-built face gap after the riser fit", 0.1 <= fit_gap <= 0.5,
        f"fit test: {p.FIT_FACE_GAP_MEASURED:g} mm from the frame face's rear to the glass with nominal risers; printed "
        f"{p.RISER_FIT_ADD:g} mm longer, {fit_gap:.2f} mm of air, no face felt (need 0.1–0.5)")

    head_gap = p.SCREW_HEAD_CB[1] - p.WASHER_T - p.BUTTON_HEAD_K - p.PLUG_BODY
    add("Hole plugs clear the screw heads",
        head_gap >= p.MIN_PLUG_HEAD_GAP and max(p.PLUG_TRIAL_D + [p.PLUG_D]) < p.SCREW_HEAD_CB[0],
        f"{p.PLUG_BODY:g} mm plug body in the {p.SCREW_HEAD_CB[1]:g} mm counterbore stops {head_gap:.1f} mm above a "
        f"{p.BUTTON_HEAD_K:g} mm button head (need ≥ {p.MIN_PLUG_HEAD_GAP:g}); plugs Ø{min(p.PLUG_TRIAL_D):g}–"
        f"{max(p.PLUG_TRIAL_D):g} (six at Ø{p.PLUG_D:g}) in a Ø{p.SCREW_HEAD_CB[0]:g} bore")

    # 3. Antenna keep-out: no metal (plate screws other than the nylon one, frame inserts and screws, foot lock) near it.
    metal = {f"M2.5 at ({x:g}, {y:g})": (x, y) for x, y in p.HOLES if (x, y) != p.NYLON_HOLE}
    for _, _, xc in frame_tab_spans():
        for ty in p.TAB_Y:
            metal[f"frame insert at ({xc:.1f}, {ty:g})"] = (xc, ty)
    metal["foot-lock insert"] = (p.DOVE_X, p.LOCK_Y)
    near = min(metal, key=lambda k: _rect_dist(*metal[k], p.ANTENNA_X, p.ANTENNA_Y))
    near_d = _rect_dist(*metal[near], p.ANTENNA_X, p.ANTENNA_Y)
    nylon_d = _rect_dist(*p.NYLON_HOLE, p.ANTENNA_X, p.ANTENNA_Y)
    add("No metal near the antenna window", near_d >= p.ANTENNA_KEEP,
        f"window x {p.ANTENNA_X[0]:.1f}–{p.ANTENNA_X[1]:.1f}, y {p.ANTENNA_Y[0]:.1f}–{p.ANTENNA_Y[1]:.1f}; nearest metal "
        f"{near} {near_d:.1f} mm away (need ≥ {p.ANTENNA_KEEP:g}); nylon screw at {p.NYLON_HOLE} ({nylon_d:.1f} mm)")

    # 4. Dovetail: sliding fit, groove ends inside the spine, engagement.
    rail = build_rail()
    dgap = plate.distance_to(rail)
    dover = (plate & rail).volume
    add("Dovetail clearance", abs(dgap - p.SLIDE_CLEAR) < 0.01 and dover < 1e-3,
        f"{dgap:.3f} mm per side (design {p.SLIDE_CLEAR}), interference {dover:.4f} mm³")
    probe = screen_box(p.DOVE_X - p.DOVE_FLOOR_W / 2, p.DOVE_X + p.DOVE_FLOOR_W / 2, p.DOVE_GROOVE_L + 0.05,
                       p.SPINE_TOP_Y - 0.05, p.D_PLATE_FRONT - p.SPINE_T + 0.05, p.D_PLATE_BACK - 0.05)
    closed = abs((plate & probe).volume - probe.volume) < 1e-3
    add("Dovetail groove ends inside the spine", closed and p.DOVE_GROOVE_L <= p.SPINE_TOP_Y - 2.0,
        f"groove stops at y {p.DOVE_GROOVE_L:g}, {p.SPINE_TOP_Y - p.DOVE_GROOVE_L:g} mm of solid spine above it "
        f"({'closed' if closed else 'CUT THROUGH'})")
    y0 = plate_outline()[2]
    engage_d = min(p.DOVE_GROOVE_L, p.DOVE_RAIL_L - p.RISE) - max(y0, -p.RISE)
    add("Dovetail engagement", engage_d >= p.MIN_DOVE_ENGAGE,
        f"{engage_d:.1f} mm of the {p.DOVE_RAIL_L:.0f} mm rail inside the groove (need ≥ {p.MIN_DOVE_ENGAGE:.0f})")

    # 5. Plug hangs clear of the plate, frame, foot and the board.
    plug = ref.plug()
    pv = (plug & plate).volume + (plug & frame).volume + ((SCREEN_TO_WORLD * plug) & foot).volume + (plug & board).volume
    add("Plug clear of plate, frame, foot and board", pv < 1e-3, f"overlap {pv:.4f} mm³")

    # 6. Cable path.
    path = build_path()
    r = p.CABLE_D / 2
    worst, worst_at = math.inf, None
    for v in path.points[:-3]:   # last points sit inside the jack body by design
        vx = Vertex(v.X, v.Y, v.Z)
        d = min(foot.distance_to(vx), plate_w.distance_to(vx), lid.distance_to(vx), frame_w.distance_to(vx))
        if d < worst:
            worst, worst_at = d, (round(v.X, 1), round(v.Y, 1), round(v.Z, 1))
    add("Cable bend radius", path.min_radius >= p.BEND_R - 0.05,
        f"tightest {path.min_radius:.1f} mm (limit R{p.BEND_R:.0f}, est.)")
    add("Cable length", path.length <= p.CABLE_LEN,
        f"path {path.length:.0f} mm of {p.CABLE_LEN:.0f}; {p.CABLE_LEN - path.length:.0f} mm slack to tuck in the tray")
    add("Cable clearance", worst >= r - 0.3,
        f"closest approach {worst:.2f} mm from centreline (cable radius {r:.2f}) at {worst_at}")
    low = min(v.Z for v in path.points[: path.bend_end + 1]) - r
    add("Plug bend fits above the tray floor", path.drop >= 0 and low >= p.FLOOR - 1e-6,
        f"{path.drop:.1f} mm of straight drop to spare below the plug before the R{p.BEND_R:.0f} bend "
        f"(≈{path.drop * p.C:.1f} mm vertical); it sets how low the screen sits (shell {p.SHELL_Z:g} mm off the desk)")

    # 7. Tip-over.
    force, masses, m_tot, cg_y, cg_z, side = tip_over(foot, lid, plate_w, frame_w)
    add("Tip-over push at screen centre", force >= p.MIN_TIP_FORCE_N,
        f"{force:.2f} N (need ≥ {p.MIN_TIP_FORCE_N}); total {m_tot:.0f} g = "
        + ", ".join(f"{n} {m:.0f}" for m, _, _, n in masses)
        + f"; CG {cg_y:.1f} mm behind the glass bottom edge, {cg_z:.1f} mm up; sideways tip at {side:.0f}°")

    # 8. Plug passes the tunnel and deck slot during assembly.
    L = layout()
    t_side = (p.TUNNEL_W - p.PLUG_W) / 2
    s_side = (p.SLOT_W - p.PLUG_W) / 2
    t_head = (L.z_cavity_top - p.FLOOR) - p.PLUG_T
    add("Plug passes the tunnel and deck slot",
        min(t_side, s_side) >= p.MIN_PLUG_PASS and t_head >= 2 * p.MIN_PLUG_PASS,
        f"{p.PLUG_W:g} × {p.PLUG_T:g} plug: {t_side:.2f} mm per side in the {p.TUNNEL_W:g} mm tunnel, "
        f"{s_side:.2f} in the {p.SLOT_W:g} mm slot (need ≥ {p.MIN_PLUG_PASS:g}); {t_head:.1f} mm of tunnel height to spare")

    # 9. Lid detents (as the 7").
    snap = p.LID_DETENT_H - p.LID_CLEAR
    groove = p.LID_DETENT_H + p.LID_DETENT_GROOVE_CLEAR - p.LID_CLEAR
    wall_left = p.WALL - p.LID_RABBET - groove
    sx0, sx1 = lift_slot_x()
    ridges = lid_detents()
    slot_free = all(b <= sx0 or a >= sx1 for a, b, _, out in ridges if out < 0)
    l_over = (lid & foot).volume
    add("Lid detents", l_over < 1e-3 and snap > 0 and wall_left >= p.MIN_DETENT_WALL and slot_free,
        f"{len(ridges)} ridges {p.LID_DETENT_H:g} mm high snap past {snap:.2f} mm of interference into "
        f"{groove:.2f} mm grooves; {wall_left:.2f} mm of rear wall behind them (need ≥ {p.MIN_DETENT_WALL:g}); "
        f"seated overlap {l_over:.4f} mm³; lift slot {'clear' if slot_free else 'BLOCKED'}")

    # 10. Jack (the 7"'s measured thin-wall mount).
    jack = ref.jack()
    j_over = (jack & foot).volume
    seat = foot.distance_to(jack)
    add("Jack fits its recess and window", j_over < 1e-3 and seat < 1e-3,
        f"flange bears on the {p.JACK_WEB_T:g} mm web ({seat:.3f} mm); overlap {j_over:.4f} mm³")
    ow, oh = p.JACK_WINDOW
    recess = p.JACK_WEB_T - p.JACK_RECEPTACLE_PROUD
    add("Receptacle flush with the rear face", abs(recess) <= 0.3 and ow > p.JACK_RECEPTACLE[0] and oh > p.JACK_RECEPTACLE[1],
        f"receptacle {p.JACK_RECEPTACLE[0]:g} × {p.JACK_RECEPTACLE[1]:g} through a {ow:.2f} × {oh:.2f} window, "
        f"{abs(recess):.1f} mm {'behind' if recess > 0 else 'proud of'} the face")
    body_gap = p.JACK_SCREW_PITCH / 2 - p.M3_CLEAR_D / 2 - p.JACK_BODY[0] / 2
    heads_gap = p.JACK_SCREW_PITCH - p.JACK_SCREW_HEAD_D
    add("Jack screws from outside", body_gap > 0,
        f"screws at {p.JACK_SCREW_PITCH:g} mm pitch pass {body_gap:.2f} mm clear of the body; the heads leave "
        f"{heads_gap:.1f} mm between them (test your chargers)")

    # 11. Foot-lock screw (optional, M3 × 12) reaches the M3 × 4 insert in the plate spine.
    to_floor = lock_floor_depth() - (p.D_PLATE_BACK - p.DOVE_DEPTH)   # counterbore floor to the groove floor
    spine_left = (p.D_PLATE_BACK - p.DOVE_DEPTH) - (p.D_PLATE_FRONT - p.SPINE_T)
    lock_engage = 12.0 - to_floor
    add("Foot-lock screw engagement", p.INSERT_M3x4_L <= lock_engage <= spine_left + 3.0,
        f"M3 × 12 from the counterbore: {lock_engage:.1f} mm past the groove floor into a {p.INSERT_M3x4_L:.0f} mm insert")
    # The post's back is buried in the foot body, so the counterbore runs on as an access bore into the tray: its
    # far end must open into the tray cavity, between the floor and the lid, so the screw can go in from there.
    end = screen_point(p.DOVE_X, p.LOCK_Y, lock_floor_depth() + p.LOCK_ACCESS_L)
    rr = p.LOCK_HEAD_D / 2
    opens = end.Y >= L.front_zone_end + rr and end.Z - rr >= p.FLOOR and end.Z + rr <= L.z_cavity_top
    add("Foot-lock access bore opens into the tray", opens,
        f"Ø{p.LOCK_HEAD_D:g} bore from the counterbore ends at Y {end.Y:.1f} (tray front wall {L.front_zone_end:g}), "
        f"Z {end.Z - rr:.1f}–{end.Z + rr:.1f} (floor {p.FLOOR:g}, lid {L.z_cavity_top:.1f})")

    # 12. Picture frame.
    aa = ref.active_area_prism()
    reveal = min(p.AA_X[0] - p.OPEN_X[0], p.OPEN_X[1] - p.AA_X[1], p.AA_Y[0] - p.OPEN_Y[0], p.OPEN_Y[1] - p.AA_Y[1])
    aa_gap = frame.distance_to(aa)
    aa_over = (frame & aa).volume
    add("Frame opening clear of the picture", reveal >= p.MIN_REVEAL and aa_over < 1e-3 and aa_gap >= p.MIN_REVEAL - 0.01,
        f"reveal {reveal:.2f} mm each side (need ≥ {p.MIN_REVEAL}); closest frame-to-active-area {aa_gap:.2f} mm")
    g_over = (frame & board).volume
    g_gap = frame.distance_to(board)
    add("Frame never touches the board", g_over < 1e-3 and g_gap >= p.MIN_EDGE_GAP,
        f"closest frame-to-board {g_gap:.2f} mm (need ≥ {p.MIN_EDGE_GAP}; glass step {p.GLASS_STEP:g}), overlap {g_over:.4f} mm³")
    w = p.FRAME_WALL
    edges = dict(left=0 - (p.FRAME_X[0] + w), right=(p.FRAME_X[1] - w) - p.SHELL_W, top=(p.FRAME_Y[1] - w) - p.SHELL_H)
    add("Shell edge gap", min(edges.values()) >= p.MIN_EDGE_GAP,
        "L / R / T " + " / ".join(f"{v:.2f}" for v in edges.values())
        + f" mm (need ≥ {p.MIN_EDGE_GAP}); bottom open, the frame stands on felt on the desk")
    faces = dict(left=p.OPEN_X[0] - p.FRAME_X[0], right=p.FRAME_X[1] - p.OPEN_X[1], top=p.FRAME_Y[1] - p.OPEN_Y[1])
    z_face = screen_point(0, 0, -(p.FRAME_FACE_GAP + p.FRAME_FACE_T)).Z
    border = p.OPEN_Y[0] + (z_face - p.DESK_FELT_T) / p.C
    add("Picture centred across the frame", abs(faces["left"] - faces["right"]) < 0.01,
        "faces L / R / T " + " / ".join(f"{v:.2f}" for v in faces.values()) + f" mm; bottom border {border:.1f} mm to the desk")
    felt = ref.face_felt()
    f_gap = felt.distance_to(board)
    f_over = (felt & aa).volume
    add("Face felt clears the glass", abs(f_gap - p.FELT_CLEAR) < 0.01 and f_over < 1e-3,
        f"{f_gap:.2f} mm from the glass (no clamping), {f_over:.4f} mm³ over the picture")
    los = ref.opening_line_of_sight()
    seen = {"frame": (frame & los).volume, "felt": (felt & los).volume, "plate": (plate & los).volume,
            "plug": (plug & los).volume}
    add("Only the picture shows through the opening", all(v < 1e-3 for v in seen.values()),
        "head-on line of sight from the face to the glass: " + ", ".join(f"{k} {v:.3f}" for k, v in seen.items()) + " mm³")
    pf_over = (plate & frame).volume
    pf_gap = plate.distance_to(frame)
    add("Plate edges meet the frame bosses", pf_over < 1e-3 and pf_gap < 1e-3,
        f"plate-to-boss contact {pf_gap:.3f} mm, overlap {pf_over:.4f} mm³")
    f_engage = p.FRAME_SCREW_L - p.FRAME_WASHER_T - p.PLATE_T
    add("Frame screw engagement", p.INSERT_M3x4_L <= f_engage <= p.FRAME_INSERT_DEPTH - 0.5,
        f"M3 × {p.FRAME_SCREW_L:.0f} + washer through the {p.PLATE_T:.0f} mm plate: {f_engage:.1f} mm into a "
        f"{p.FRAME_INSERT_DEPTH:.0f} mm hole with an M3 × 4 insert")
    lf = SCREEN_TO_WORLD * ref.desk_felt()
    fbb, lbb = frame_w.bounding_box(), lf.bounding_box()
    bottom = frame_w & box(-50, 250, -100, 300, p.DESK_FELT_T - 0.01, p.DESK_FELT_T + 0.01)
    reach = bottom.bounding_box() if bottom.volume > 0 else None
    level = reach is not None and reach.min.X <= fbb.min.X + 0.01 and reach.max.X >= fbb.max.X - 0.01
    add("Frame bottom on the desk",
        abs(fbb.min.Z - p.DESK_FELT_T) < 1e-3 and abs(lbb.min.Z) < 1e-3 and lf.distance_to(frame_w) < 1e-3 and level,
        f"face and side walls cut level at {fbb.min.Z:.2f} mm on {p.DESK_FELT_T:g} mm felt "
        f"({'both side walls and the face reach it' if level else 'NOT level'}); shell bottom "
        f"{screen_point(0, 0, 0).Z:.1f} mm off the desk")
    ff_over, ff_gap = (frame_w & foot).volume, frame_w.distance_to(foot)
    add("Frame clears the foot", ff_over < 1e-3 and ff_gap >= 0.1,
        f"the frame carries the board on the desk and never bears on the foot: closest {ff_gap:.2f} mm, "
        f"overlap {ff_over:.4f} mm³")
    shelf = foot & box(-50, 250, -100, L.front_zone_end, 0, 300)
    bend_gap = min(shelf.distance_to(Vertex(v.X, v.Y, v.Z)) for v in path.points[: path.bend_end + 1])
    plug_w = SCREEN_TO_WORLD * plug
    add("Plug bend clears the shelf", (plug_w & shelf).volume < 1e-3 and bend_gap >= r - 0.3 and path.drop >= 0,
        f"plug overlap {(plug_w & shelf).volume:.4f} mm³; bend {bend_gap:.2f} mm from centreline to the shelf "
        f"(cable radius {r:.2f})")
    rail_x = (p.DOVE_X - p.DOVE_FLOOR_W / 2 - 1, p.DOVE_X + p.DOVE_FLOOR_W / 2 + 1)
    under = SCREEN_TO_WORLD * (screen_box(-50, 250, -300, y0, -50, p.D_PLATE_BACK + p.POST_FACE_CLEAR - 0.01)
                               - screen_box(*rail_x, -300, 400, -50, 400))
    pl_gap = plate_w.distance_to(foot & under)
    add("Plate clears the shelf", (plate_w & foot).volume < 1e-3 and pl_gap >= p.SHELF_PLATE_GAP - 0.05
        and plate_w.bounding_box().min.Z > 0,
        f"plate bottom edge {pl_gap:.2f} mm above the shelf (design {p.SHELF_PLATE_GAP:g}); its lowest corner "
        f"{plate_w.bounding_box().min.Z:.1f} mm off the desk")
    ob = foot.bounding_box()
    over_l, over_r = ob.min.X - fbb.min.X, fbb.max.X - ob.max.X
    add("Frame and foot share a centre line", abs(over_l - over_r) <= 0.1,
        f"frame overhangs the foot by {over_l:.2f} mm left / {over_r:.2f} mm right (need equal within 0.1)")

    # 13. The hole template lies flat on the board's back: no overlap with the bosses, rear box or FPC bracket.
    tmpl = template_on_back()
    t_over = (tmpl & board).volume
    t_gap = tmpl.distance_to(ref.shell())
    add("Hole template lies flat on the back", t_over < 1e-3 and t_gap < 1e-3,
        f"overlap with the board {t_over:.4f} mm³ (bosses, rear box, FPC bracket); resting on the flat back ({t_gap:.3f} mm)")

    # 13. Every printed part fits the P1S bed in its print pose.
    for name, solid in parts["print"].items():
        bb = solid.bounding_box()
        size = (bb.max - bb.min)
        add(f"Fits P1S bed: {name}", max(size.X, size.Y, size.Z) <= p.BED,
            f"{size.X:.1f} × {size.Y:.1f} × {size.Z:.1f} mm")
    return results
