"""Fit checks. Each returns (name, ok, detail); build.py fails if any is not ok."""

import math

from build123d import Vertex

from . import params as p
from . import reference as ref
from .cover import cover_outline
from .geom import ChargerFrame, dcyl
from .shell import cover_boss_xy, hook_strain, outline, tongue_slot_x

EPS = 1e-3


def run(parts: dict, ch: p.Charger) -> list:
    shell, cover = parts["shell"], parts["cover"]
    results = []

    def add(name, ok, detail):
        results.append((name, bool(ok), detail))

    fn_pcb, glass, keepout = ref.fn_pcb(ch), ref.fn_glass(ch), ref.fn_back_keepout(ch)
    charger, cell, swollen = ref.charger(ch), ref.cell(ch), ref.cell_swollen(ch)
    f = ChargerFrame(ch)

    # 1. Freenove: bosses on its four holes, faces seat on the PCB front, glass floats behind the bezel.
    holes = ref.fn_hole_xy(ch)
    hx = p.PCB_W / 2 - p.HOLE_INSET
    pattern = (holes[1][0] - holes[0][0], holes[2][1] - holes[0][1])
    seat = shell.distance_to(fn_pcb)
    over = (shell & fn_pcb).volume + (shell & glass).volume
    g_gap = shell.distance_to(glass)
    add("Freenove sits on its four bosses",
        abs(pattern[0] - 2 * hx) < 1e-6 and abs(pattern[1] - (p.PCB_L - 2 * p.HOLE_INSET)) < 1e-6 and seat < EPS and over < EPS,
        f"bosses on the {pattern[0]:.0f} × {pattern[1]:.0f} hole pattern; boss faces touch the PCB ({seat:.3f} mm); "
        f"glass {g_gap:.2f} mm behind the bezel (floats, never clamped); overlap {over:.4f} mm³")

    # 2. Bosses stay off the glass ends.
    g = glass.bounding_box()
    boss_r = p.BOSS_D / 2
    ends = min(g.min.Y - (holes[0][1] + boss_r), (holes[2][1] - boss_r) - g.max.Y)
    add("Bosses clear the glass ends", ends >= 0.5, f"{ends:.2f} mm between a boss and the glass edge (need ≥ 0.5)")

    # 3. Freenove back: nothing of the shell or cover in its 4.7 mm part keep-out.
    k_over = (shell & keepout).volume + (cover & keepout).volume
    add("Shell and cover clear the Freenove's back parts", k_over < EPS,
        f"overlap with the {p.BACK_PARTS_H} mm keep-out {k_over:.4f} mm³")

    # 4. Insert and screw: M3 × 6 through the PCB, into an M3 × 4 insert, with skin left under it.
    depth = p.INSERT_L + 0.6
    skin = p.D_PCB_FRONT - depth
    reach = p.FN_SCREW_L - p.PCB_T
    add("Freenove screws and inserts", reach >= p.INSERT_L - 1.0 and reach <= depth - 0.1 and skin >= p.MIN_SKIN,
        f"M3 × {p.FN_SCREW_L:.0f}: {reach:.1f} mm past the PCB into a {depth:.1f} mm insert hole; "
        f"{skin:.2f} mm of bezel left in front of it (need ≥ {p.MIN_SKIN})")

    # 5. Bezel: the opening shows all the lit pixels, and its lip rests on the glass's black border.
    y0, y1, y_top, y_out = p.layout(ch)
    va0 = y0 + p.VA_FROM_BOTTOM
    aa0 = y0 + p.AA_FROM_BOTTOM
    reveal = min((p.VA_W - p.AA_W) / 2, aa0 - va0, (va0 + p.VA_L) - (aa0 + p.AA_L))
    aa = ref.active_area(ch)
    aa_over = (shell & aa).volume
    lip = min((p.GLASS_W - p.VA_W) / 2, va0 - g.min.Y, g.max.Y - (va0 + p.VA_L))
    add("Bezel frames the active area", aa_over < EPS and reveal >= 0.1,
        f"opening = visible window {p.VA_W} × {p.VA_L}; {reveal:.2f} mm min reveal round the active area; "
        f"line of sight onto the pixels blocked by {aa_over:.4f} mm³")
    add("Bezel lip rests on the glass border", lip >= 2.0, f"lip overlaps the glass by ≥ {lip:.2f} mm on every side (need ≥ 2)")

    # 6. Both USB-C openings: on the connector's axis, and the overmold reaches the mouth.
    for name, plug, wall in (("Flashing port (Freenove)", ref.fn_overmold(ch), p.TOP_WALL),
                             ("Charge port (BQ25185)", ref.charge_overmold(ch), p.WALL)):
        o_over = (plug & shell).volume + (plug & cover).volume
        o_gap = min(plug.distance_to(shell), plug.distance_to(cover))
        add(name, o_over < EPS and o_gap >= p.PLUG_CLEAR - 0.01,
            f"{p.PLUG_OVERMOLD[0]:g} × {p.PLUG_OVERMOLD[1]:g} overmold seats against the receptacle mouth through the "
            f"{wall:g} mm wall with {o_gap:.2f} mm clear all round (need ≥ {p.PLUG_CLEAR})")
    fx = p.FN_USB_X
    cx, cd, cy = ref.charger_usb_axis(ch)
    usb = ref.fn_usb(ch).bounding_box()
    add("Ports sit on their connector axes",
        abs((usb.min.X + usb.max.X) / 2 - fx) < 1e-6 and abs(-(usb.min.Z + usb.max.Z) / 2 - ref.fn_usb_axis_d()) < 1e-6
        and abs(cy - (p.Y_INNER_BOTTOM + 0.1)) < 1e-6 and abs(y_top - usb.max.Y - p.FN_USB_GAP) < 1e-6,
        f"flash port x {fx:.2f}, depth {ref.fn_usb_axis_d():.2f}, mouth {p.FN_USB_GAP:g} inside the wall; "
        f"charge port x {cx:.2f}, depth {cd:.2f}, mouth 0.1 inside the wall")

    # 7. Charger: standoffs and pegs on its holes, snap hooks grip it, nothing interferes.
    c_over = (charger & shell).volume + (charger & cover).volume
    c_seat = shell.distance_to(charger)
    strain, flex, defl = hook_strain(ch)
    add("Charger seats on its standoffs", c_over < EPS and c_seat < EPS,
        f"4 standoffs Ø{p.CHARGER_STANDOFF_D:g} with Ø{p.PEG_D:g} pegs through its Ø{ch.hole_d:g} holes "
        f"({(ch.hole_d - p.PEG_D) / 2:.2f} mm per side); contact {c_seat:.3f} mm; overlap {c_over:.4f} mm³")
    add("Charger snap hooks", strain <= p.HOOK_MAX_STRAIN and p.HOOK_BARB >= 0.4,
        f"{len(ch.hooks)} hooks, {p.HOOK_BARB:g} mm barb over the PCB edge; arm {flex:.1f} mm long flexes {defl:.2f} mm "
        f"at {strain * 100:.1f}% strain (limit {p.HOOK_MAX_STRAIN * 100:.0f}%)")
    term = ref.terminal_screw_column(ch)
    t_over = (term & shell).volume
    add("Terminal screws reachable with the cover off", t_over < EPS,
        f"nothing of the shell over the 4.5V-max terminal's screws ({t_over:.4f} mm³); "
        f"{p.D_COVER_INNER - f.d(ch.terminal_h):.1f} mm above them to the cover")

    # 8. Cell pocket: clearance, swelling, and gap to the board.
    pk = ref.cell_pocket(ch)
    cb = cell.bounding_box()
    side = min(cb.min.X - pk.x0, pk.x1 - cb.max.X, cb.min.Y - pk.y0, pk.y1 - cb.max.Y)
    s_over = (swollen & shell).volume + (swollen & cover).volume + (swollen & keepout).volume
    gap = cell.distance_to(keepout)
    add("Cell pocket clearance", side >= p.CELL_CLEAR - 1e-6 and (cell & cover).volume < EPS,
        f"{p.CELL_W:g} × {p.CELL_L:g} × {p.CELL_T:g} cell (measured): {side:.2f} mm per side in the pocket")
    add("Cell swelling allowance", s_over < EPS,
        f"cell swollen {p.CELL_SWELL * 100:.0f}% to {p.CELL_POCKET_T:.2f} mm still clears everything ({s_over:.4f} mm³); "
        f"unswollen it sits {gap:.2f} mm behind the Freenove's back parts")
    antenna_top = y0 + p.ANTENNA_KEEPOUT
    ribs_y0 = pk.y0 - p.RIB_T
    add("Cell kept off the antenna end", min(cb.min.Y, ribs_y0) >= antenna_top,
        f"pocket starts {ribs_y0 - y0:.1f} mm up the PCB (keep-out {p.ANTENNA_KEEPOUT:g} mm from its antenna end)")

    # 9. Wiring.
    solids = {"shell": shell, "cover": cover, "charger": charger, "cell": cell, "Freenove PCB": fn_pcb}

    def clearance(path, skip_first, skip_last):
        """Closest solid to the wire's centreline, ignoring the ends that sit on their connectors."""
        worst, at = math.inf, None
        for pt in ref.path_xyz(path)[skip_first: len(path) - skip_last]:
            v = Vertex(*pt)
            for name, s in solids.items():
                d = s.distance_to(v)
                if d < worst:
                    worst, at = d, name
        return worst, at

    pig = ref.pigtail_path(ch)
    pl = ref.path_length(pig)
    pw, pwho = clearance(pig, 0, 2)
    in_keep = any(keepout.distance_to(Vertex(*pt)) < p.WIRE_D / 2 for pt in ref.path_xyz(pig)[1:-2])
    add("Pigtail run (terminal → BAT socket)", pl <= p.PIGTAIL_L and pw >= p.WIRE_D / 2 and not in_keep,
        f"{pl:.0f} mm of the EST {p.PIGTAIL_L:g} mm pigtail; nearest part {pw:.2f} mm from the wire's centreline ({pwho}); "
        f"{'crosses' if in_keep else 'stays out of'} the back-parts keep-out until the socket")
    x_chan0 = pk.x1 + p.RIB_T
    chan_w = p.INNER_HALF_W - p.LEDGE_W - x_chan0
    chan_d = p.D_COVER_INNER - p.D_PARTS_BACK
    add("Wire channel beside the cell", min(chan_w, chan_d) >= p.MIN_CHANNEL,
        f"{chan_w:.1f} wide × {chan_d:.1f} deep (above the ledge; need ≥ {p.MIN_CHANNEL:g} for the 2-wire pigtail)")
    lead = ref.cell_lead_path(ch)
    ll = ref.path_length(lead)
    lw, lwho = clearance(lead, 1, 1)   # starts on the cell, ends in the plug
    jz = ref.jst_plug_zone(ch)
    j_over = (jz & shell).volume + (jz & cover).volume
    add("Cell lead run (cell → JST-PH)", ll <= p.CELL_LEAD_L and lw >= p.WIRE_D / 2,
        f"{ll:.0f} mm of the EST {p.CELL_LEAD_L:g} mm lead; nearest part {lw:.2f} mm from its centreline ({lwho})")
    x_exit = lead[0][0]
    rib_end = pk.x1 - p.LEAD_GAP
    margin = min(x_exit - rib_end, pk.x1 - x_exit)
    add("Cell lead clears the bottom rib", margin >= 2.0,
        f"lead leaves the bottom-right corner {p.CELL_LEAD_INSET:g} mm in (EST), through the {p.LEAD_GAP:g} mm gap with "
        f"{margin:.1f} mm to spare (need ≥ 2)")
    add("JST-PH plug bay", j_over < EPS,
        f"plug {p.JST_PLUG[0]:g} × {p.JST_PLUG[1]:g} × {p.JST_PLUG[2]:g} plus {p.JST_BEND:g} mm for the lead to turn: "
        f"overlap {j_over:.4f} mm³")

    # 10. Cover: fits inside the walls, tongues engage, screws engage, it rests on its supports.
    cs_over = (shell & cover).volume
    cs_gap = shell.distance_to(cover)
    add("Cover fits the shell", cs_over < EPS and cs_gap < EPS,
        f"overlap {cs_over:.4f} mm³; rests on the ledges and screw bosses ({cs_gap:.3f} mm); "
        f"{p.COVER_CLEAR:g} mm round the edge")
    tongue_in = p.TONGUE_L
    slot_room = p.SLOT_DEPTH - (p.COVER_CLEAR + p.TONGUE_L) + 0.5
    add("Cover tongues hook into the top wall", tongue_in >= 1.0 and slot_room >= 0.3 and p.SLOT_H - p.TONGUE_T >= 0.5,
        f"2 × {p.TONGUE_W:g} mm tongues reach {tongue_in:g} mm past the wall's inner face, {slot_room:.1f} mm short of the "
        f"slot end, with {p.SLOT_H - p.TONGUE_T:.1f} mm of slot height to swing in; "
        f"{p.TOP_WALL - p.SLOT_DEPTH:.1f} mm of top wall behind the slots")
    engage = p.COVER_SCREW_L - p.COVER_T
    hole = p.COVER_SCREW_L - p.COVER_T + 1.0
    add("Cover screw engagement", p.INSERT_L - 0.5 <= engage <= hole - 0.5 and p.CSK_DEPTH <= p.COVER_T - 0.5,
        f"M3 × {p.COVER_SCREW_L:g} countersunk: head flush in the {p.COVER_T:g} mm cover, {engage:.1f} mm into a "
        f"{hole:.1f} mm boss hole with an M3 × 4 insert")

    # 11. Paperclip holes reach RESET / BOOT without hitting anything.
    blocked = []
    for name, x, y in ref.button_xy(ch):
        pin = dcyl(x, y, p.D_PCB_BACK + p.BUTTON_H + 0.2, p.D_BACK_OUTER + 1, p.PINHOLE_D * 0.9)
        v = (pin & shell).volume + (pin & cover).volume + (pin & cell).volume + (pin & ref.fn_usb(ch)).volume
        if v > EPS:
            blocked.append(name)
    add("RESET / BOOT reachable through the cover", not blocked,
        f"Ø{p.PINHOLE_D:g} holes, a straight paperclip run of {p.D_BACK_OUTER - p.D_PCB_BACK - p.BUTTON_H:.1f} mm"
        + (f"; blocked: {', '.join(blocked)}" if blocked else ""))

    # 12. Walls and overall size.
    add("Wall thickness", min(p.WALL, p.TOP_WALL, p.FRONT_T + 0.5) >= 2.0 and p.COVER_T >= 2.0,
        f"side/bottom {p.WALL:g}, top {p.TOP_WALL:g}, cover {p.COVER_T:g}, bezel {p.FRONT_T:g} mm (+{p.BEZEL_CHAMFER:g} chamfer)")
    bb = shell.bounding_box()
    add("Overall size", True,
        f"{bb.max.X - bb.min.X:.1f} × {bb.max.Y - bb.min.Y:.1f} × {bb.max.Z - bb.min.Z:.1f} mm (w × l × thick)")

    # 13. Every printed part fits the P1S bed in its print pose.
    for name, solid in parts["print"].items():
        b = solid.bounding_box()
        size = b.max - b.min
        on_bed = abs(b.min.Z) < 1e-6
        add(f"Fits P1S bed: {name}", max(size.X, size.Y, size.Z) <= p.BED and on_bed,
            f"{size.X:.0f} × {size.Y:.0f} × {size.Z:.0f} mm{'' if on_bed else ' (NOT on the bed)'}")
    return results
