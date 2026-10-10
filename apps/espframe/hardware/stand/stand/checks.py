"""Fit and stability checks. Each returns (name, ok, detail); build.py fails if any is not ok."""

import math

from build123d import Vertex

from . import params as p
from . import reference as ref
from .cable import build_path
from .foot import build_rail
from .frames import SCREEN_TO_WORLD, screen_point


def run(parts: dict) -> list:
    plate, foot, lid, frame = parts["plate"], parts["foot"], parts["lid"], parts["frame"]
    plate_w = SCREEN_TO_WORLD * plate
    frame_w = SCREEN_TO_WORLD * frame
    results = []

    def add(name, ok, detail):
        results.append((name, bool(ok), detail))

    # 1. Plate clears the board's tallest part (UART2 JST, design height) by the required gap.
    gap = plate.distance_to(ref.board_keepout())
    add("Plate clears UART2 JST keep-out", gap >= p.PLATE_CLEAR - 1e-6,
        f"{gap:.2f} mm (need ≥ {p.PLATE_CLEAR}; keep-out = PCB footprint to {p.JST_DESIGN_H} mm)")

    # 2. Risers sit exactly on the brass spacer tops, and nothing else touches the spacers.
    spacers = ref.tabs_and_spacers()
    overlap = (plate & spacers).volume
    riser_face = p.D_PLATE_FRONT - p.RISER_H
    add("Risers seat on brass spacers", abs(riser_face - p.D_BRASS_TOP) < 1e-6 and overlap < 1e-3,
        f"riser face at {riser_face:.2f} mm depth = spacer top {p.D_BRASS_TOP:.2f}; overlap {overlap:.4f} mm³")

    # 3. Plate M3 screws engage the spacer thread without bottoming out.
    engage = p.SCREW_L - p.PLATE_T - p.RISER_H
    add("M3 × 12 engagement in spacer", 4.0 <= engage <= p.BRASS_THREAD_DEPTH,
        f"{engage:.1f} mm of {p.BRASS_THREAD_DEPTH:.0f}–7 mm thread (M3 × 14 would give {engage + 2:.1f}, borderline)")

    # 4. Dovetail sliding fit: perpendicular gap equals the design clearance, no interference.
    rail = build_rail()
    dgap = plate.distance_to(rail)
    dover = (plate & rail).volume
    add("Dovetail clearance", abs(dgap - p.SLIDE_CLEAR) < 0.01 and dover < 1e-3,
        f"{dgap:.3f} mm per side (design {p.SLIDE_CLEAR}), interference {dover:.4f} mm³")

    # 5. Plug hangs clear of the plate and the foot.
    plug = ref.plug()
    pv = (plug & plate).volume + (plug & frame).volume + ((SCREEN_TO_WORLD * plug) & foot).volume
    add("Plug clear of plate, frame and foot", pv < 1e-3, f"overlap {pv:.4f} mm³")

    # 6. Cable path: bend radius, length, and clearance to the foot/plate along its length.
    path = build_path()
    r = p.CABLE_D / 2
    worst, worst_at = math.inf, None
    for i, v in enumerate(path.points[:-3]):   # last points sit inside the jack body by design
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
        f"(≈{path.drop * p.C:.1f} mm vertical; rev D had 1.9)")

    # 7. Tip-over: horizontal push at the screen centre needed to tip the stand backwards.
    g = 9.81
    masses = []   # (grams, Y, Z)
    for name, solid in (("foot", foot), ("lid", lid), ("plate", plate_w)):
        c = solid.center()
        masses.append((solid.volume * p.PETG_DENSITY * p.PRINT_FILL, c.Y, c.Z, name))
    c = frame_w.center()
    masses.append((frame_w.volume * p.FRAME_DENSITY * p.FRAME_FILL, c.Y, c.Z, "frame"))
    sc = screen_point(p.GLASS_W / 2, p.GLASS_H / 2, p.PANEL_T)
    masses.append((p.SCREEN_MASS, sc.Y, sc.Z, "screen"))
    m_tot = sum(m for m, *_ in masses)
    cg_y = sum(m * y for m, y, *_ in masses) / m_tot
    cg_z = sum(m * z for m, _, z, _ in masses) / m_tot
    push_z = screen_point(p.GLASS_W / 2, p.GLASS_H / 2, 0).Z
    force = m_tot / 1000 * g * (p.FOOT_D - cg_y) / push_z
    side = math.degrees(math.atan((p.FOOT_W / 2) / cg_z))
    detail = (f"{force:.1f} N (need ≥ {p.MIN_TIP_FORCE_N}); total {m_tot:.0f} g = "
              + ", ".join(f"{n} {m:.0f}" for m, _, _, n in masses)
              + f"; CG {cg_y:.1f} mm behind the glass corner, {cg_z:.1f} mm up; sideways tip at {side:.0f}°")
    add("Tip-over push at screen centre", force >= p.MIN_TIP_FORCE_N, detail)

    # 8. Jack: flange against the web, receptacle through the window and flush outside,
    # its own screws from outside clear of the body.
    jack = ref.jack()
    j_over = (jack & foot).volume
    seat = foot.distance_to(jack)
    add("Jack fits its recess and window", j_over < 1e-3 and seat < 1e-3,
        f"measured flange {p.JACK_FLANGE_W:g} × {p.JACK_FLANGE_H:g} × {p.JACK_FLANGE_T:g}, body {p.JACK_BODY[0]:g} × {p.JACK_BODY[1]:g}; "
        f"flange bears on the {p.JACK_WEB_T:g} mm web ({seat:.3f} mm); overlap {j_over:.4f} mm³")
    ow, oh = p.JACK_WINDOW
    recess = p.JACK_WEB_T - p.JACK_RECEPTACLE_PROUD
    add("Receptacle flush with the rear face", abs(recess) <= 0.3 and ow > p.JACK_RECEPTACLE[0] and oh > p.JACK_RECEPTACLE[1],
        f"receptacle {p.JACK_RECEPTACLE[0]:g} × {p.JACK_RECEPTACLE[1]:g} pokes through a {ow:.2f} × {oh:.2f} window and ends "
        f"{abs(recess):.1f} mm {'behind' if recess > 0 else 'proud of'} the face")
    body_gap = p.JACK_SCREW_PITCH / 2 - p.M3_CLEAR_D / 2 - p.JACK_BODY[0] / 2
    heads_gap = p.JACK_SCREW_PITCH - p.JACK_SCREW_HEAD_D
    add("Jack screws from outside", body_gap > 0,
        f"screws at {p.JACK_SCREW_PITCH:g} mm pitch pass {body_gap:.2f} mm clear of the body; the heads leave "
        f"{heads_gap:.1f} mm between them, so a plug moulding wider than that won't seat fully (test your chargers)")

    # 9. Foot-lock screw (optional, M3 × 12) reaches the M3 × 4 insert in the plate spine.
    to_floor = p.LOCK_POST_LEFT + p.DOVE_DEPTH + p.POST_FACE_CLEAR
    spine_left = (p.D_PLATE_BACK - p.DOVE_DEPTH) - (p.D_PLATE_FRONT - p.SPINE_T)
    lock_engage = 12.0 - to_floor
    add("Foot-lock screw engagement", p.INSERT_M3x4_L <= lock_engage <= spine_left + 3.0,
        f"M3 × 12 from the counterbore: {lock_engage:.1f} mm past the groove floor into a {p.INSERT_M3x4_L:.0f} mm insert "
        f"(tip {lock_engage - spine_left:.1f} mm proud of the spine, clear of the board)")

    # 10. Picture frame.
    panel = ref.panel()
    aa = ref.active_area_prism()
    reveal = min(p.AA_X[0] - p.OPEN_X[0], p.OPEN_X[1] - p.AA_X[1], p.AA_Y[0] - p.OPEN_Y[0], p.OPEN_Y[1] - p.AA_Y[1])
    aa_gap = frame.distance_to(aa)
    aa_over = (frame & aa).volume
    add("Frame opening clear of the picture", reveal >= p.MIN_REVEAL and aa_over < 1e-3 and aa_gap >= p.MIN_REVEAL - 0.01,
        f"reveal {reveal:.2f} mm each side (need ≥ {p.MIN_REVEAL}); closest frame-to-active-area {aa_gap:.2f} mm")
    g_over = (frame & panel).volume
    g_gap = frame.distance_to(panel)
    add("Frame never touches the glass", g_over < 1e-3 and g_gap >= p.MIN_EDGE_GAP,
        f"closest frame-to-glass {g_gap:.2f} mm (need ≥ {p.MIN_EDGE_GAP}), overlap {g_over:.4f} mm³")
    w = p.FRAME_WALL
    edges = dict(left=0 - (p.FRAME_X[0] + w), right=(p.FRAME_X[1] - w) - p.GLASS_W,
                 top=(p.FRAME_Y[1] - w) - p.GLASS_H, bottom=0 - (p.FRAME_Y[0] + w))
    add("Glass edge gap", min(edges.values()) >= p.MIN_EDGE_GAP,
        "L / R / T / B " + " / ".join(f"{v:.2f}" for v in edges.values()) + f" mm (need ≥ {p.MIN_EDGE_GAP})")
    felt = ref.face_felt()
    f_gap = felt.distance_to(panel)
    f_over = (felt & aa).volume
    add("Face felt clears the glass", abs(f_gap - p.FELT_CLEAR) < 0.01 and f_over < 1e-3,
        f"{f_gap:.2f} mm from the glass (no clamping), {f_over:.4f} mm³ over the picture")
    los = ref.opening_line_of_sight()
    seen = {"frame": (frame & los).volume, "felt": (felt & los).volume, "plate": (plate & los).volume,
            "spacers": (ref.tabs_and_spacers() & los).volume, "plug": (ref.plug() & los).volume}
    add("Only the picture shows through the opening", all(v < 1e-3 for v in seen.values()),
        "head-on line of sight from the face to the glass: "
        + ", ".join(f"{k} {v:.3f}" for k, v in seen.items()) + " mm³ (bevel, bosses, tabs, screws and felt all hidden)")
    pf_over = (plate & frame).volume
    pf_gap = plate.distance_to(frame)
    add("Plate tabs meet the frame bosses", pf_over < 1e-3 and pf_gap < 1e-3,
        f"tab-to-boss contact {pf_gap:.3f} mm, overlap {pf_over:.4f} mm³")
    f_engage = p.FRAME_SCREW_L - p.PLATE_T
    add("Frame screw engagement", p.INSERT_M3x4_L <= f_engage <= p.FRAME_INSERT_DEPTH - 0.5,
        f"M3 × {p.FRAME_SCREW_L:.0f} through the {p.PLATE_T:.0f} mm tab: {f_engage:.1f} mm into a {p.FRAME_INSERT_DEPTH:.0f} mm hole "
        f"with an M3 × 4 insert")
    lf = SCREEN_TO_WORLD * ref.ledge_felt()
    seat_ok = lf.distance_to(foot) < 1e-3 and lf.distance_to(frame_w) < 1e-3 and (frame_w & foot).volume < 1e-3
    add("Frame rail seats on the ledge", seat_ok,
        f"bottom rail sits on {p.FELT_T:.0f} mm felt on the ledge; screen raised {p.RISE:.2f} mm by the rail; "
        f"frame-to-foot overlap {(frame_w & foot).volume:.4f} mm³, closest {frame_w.distance_to(foot):.2f} mm")
    engage = min(p.DOVE_GROOVE_L, p.DOVE_RAIL_L - p.SEAT) - max(0.0, -p.SEAT)
    add("Dovetail engagement", engage >= p.MIN_DOVE_ENGAGE,
        f"{engage:.1f} mm of the {p.DOVE_RAIL_L:.0f} mm rail inside the groove (need ≥ {p.MIN_DOVE_ENGAGE:.0f})")

    # 11. Every printed part fits the P1S bed in its print pose.
    for name, solid in parts["print"].items():
        bb = solid.bounding_box()
        size = (bb.max - bb.min)
        add(f"Fits P1S bed: {name}", max(size.X, size.Y, size.Z) <= p.BED,
            f"{size.X:.0f} × {size.Y:.0f} × {size.Z:.0f} mm")
    return results
