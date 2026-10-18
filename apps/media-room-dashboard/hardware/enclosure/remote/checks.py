"""Fit checks. Each returns (name, ok, detail); build.py fails if any is not ok."""

import math

from build123d import Vertex

from . import params as p
from . import reference as ref
from .cover import cover_outline
from .geom import ChargerFrame, dcyl
from build123d import Pos

from .cover import slide_in
from .shell import cover_boss_xy, hook_strain, outline

EPS = 1e-3


def run(parts: dict, ch: p.Charger) -> list:


    """Run every fit check on the assembled shell and cover (and their print poses); returns [(name, ok, detail)]."""
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

    # 6. Flashing with the cover off, and the charge port.
    cover_off = (shell & ref.fn_overmold(ch)).volume
    off_gap = shell.distance_to(ref.fn_overmold(ch))
    env = ref.fn_usb_envelope(ch)
    env_gap = cover.distance_to(env)
    add("Flashing port reachable with the cover off", cover_off < EPS and off_gap >= p.PLUG_CLEAR - 0.01,
        f"a {p.PLUG_OVERMOLD[0]:g} × {p.PLUG_OVERMOLD[1]:g} overmold plugs into the Freenove's USB-C from the open top end "
        f"with {off_gap:.2f} mm to the shell (the bezel lip)")
    add("End wall closes over the USB-C", (cover & env).volume < EPS and env_gap >= p.END_GAP - 0.01,
        f"{p.FN_USB_ENV_W:g} × {p.FN_USB_ENV_H:g} envelope reaching {p.FN_USB_ENVELOPE:g} mm past the PCB end "
        f"(nominal overhang {p.FN_USB_OVERHANG:g}); {env_gap:.2f} mm to the cover's end wall")
    access = ref.top_access_zone(ch)
    a_over = (shell & access).volume
    add("RESET / BOOT / USB-C open with the cover off", a_over < EPS,
        f"nothing of the shell behind the board's top {p.TOP_ACCESS:g} mm ({a_over:.4f} mm³): press RESET / BOOT by hand")
    plug = ref.charge_overmold(ch)
    o_over = (plug & shell).volume + (plug & cover).volume
    o_gap = min(plug.distance_to(shell), plug.distance_to(cover))
    ow, oh = p.PLUG_OVERMOLD
    add("Charge port (BQ25185)", o_over < EPS and o_gap >= p.PLUG_CLEAR - 0.01,
        f"{ow + 2 * p.PLUG_CLEAR:g} × {oh + 2 * p.PLUG_CLEAR:g} opening: a {ow:g} × {oh:g} overmold seats against the "
        f"receptacle mouth through the {p.WALL:g} mm wall with {o_gap:.2f} mm clear all round")

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
    t_clear = p.D_COVER_INNER - f.d(ch.terminal_h)
    add("Terminal block under the cover", t_over < EPS and t_clear >= p.MIN_TERMINAL_CLEAR,
        f"measured {ch.terminal_h + ch.pcb_t:g} mm block: {t_clear:.1f} mm to the cover (need ≥ {p.MIN_TERMINAL_CLEAR:g}); "
        f"nothing of the shell over its screws")
    pins = ref.terminal_pins(ch)
    pin_gap = shell.distance_to(pins)
    add("Terminal solder pins clear the front wall", (pins & shell).volume < EPS and pin_gap >= 1.0,
        f"{ch.terminal_pins[4]:g} mm pins below the PCB: nearest shell part {pin_gap:.1f} mm away, {p.D_CHARGER_FRONT - ch.terminal_pins[4] - p.FRONT_T:.1f} mm to the front wall; no relief needed")

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
    add("Pigtail run (terminal → BAT socket)", pw >= p.WIRE_D / 2 and not in_keep,
        f"{pl:.0f} mm run, so buy a pigtail of at least {ref.pigtail_min_length(ch)} mm "
        f"(+{p.PIGTAIL_STRIP:g} stripped into the terminal, +{p.PIGTAIL_SLACK:g} slack); "
        f"nearest part {pw:.2f} mm from the wire's centreline ({pwho}); "
        f"{'crosses' if in_keep else 'stays out of'} the back-parts keep-out until the socket")
    x_chan0 = pk.x1 + p.RIB_T
    chan_w = p.INNER_HALF_W - p.LEDGE_W - x_chan0
    chan_d = p.D_COVER_INNER - p.D_PARTS_BACK
    add("Wire channel beside the cell", min(chan_w, chan_d) >= p.MIN_CHANNEL,
        f"{chan_w:.1f} wide × {chan_d:.1f} deep (above the ledge; need ≥ {p.MIN_CHANNEL:g} for the 2-wire pigtail)")
    jz = ref.jst_plug_zone(ch)
    j_over = (jz & shell).volume + (jz & cover).volume
    # Fitted label towards the board, the photo's taped corner is the right-hand one: the centre and
    # right-corner runs must fit. The left-corner run only happens with the cell flipped.
    runs = ref.cell_lead_paths(ch)
    worst_len, worst_clear, notes = 0.0, math.inf, []
    for name in ("centre", "via right corner"):
        ln = ref.path_length(runs[name])
        cl, who = clearance(runs[name], 1, 1)   # starts on the cell, ends in the plug
        worst_len, worst_clear = max(worst_len, ln), min(worst_clear, cl)
        notes.append(f"{name} {ln:.0f} mm ({cl:.2f} from {who})")
    flipped = ref.path_length(runs["via left corner"])
    flipped_note = ("fits too" if flipped <= p.CELL_LEAD_L else
                    "too long, so peel the lead off the end and run it from the centre")
    add("Cell lead run (cell → JST-PH)", worst_len <= p.CELL_LEAD_L and worst_clear >= p.WIRE_D / 2,
        f"longest {worst_len:.0f} mm of the measured {p.CELL_LEAD_L:g} mm lead: " + "; ".join(notes)
        + f". Cell flipped (lead via the left corner): {flipped:.0f} mm, {flipped_note}")
    rib_top = p.D_COVER_INNER - p.BOTTOM_RIB_H
    over_rib = rib_top - (ref.cell_lead_d() + p.WIRE_D / 2)
    catch = p.BOTTOM_RIB_H - p.CELL_TAPE_T
    add("Cell lead clears the bottom rib", over_rib >= 0.3 and catch >= 1.0,
        f"the {p.BOTTOM_RIB_H:g} mm bottom rib passes {over_rib:.2f} mm under the lead wherever it leaves the cell end "
        f"(need ≥ 0.3), and still catches {catch:.1f} mm of the cell's end")
    add("JST-PH plug bay", j_over < EPS,
        f"plug {p.JST_PLUG[0]:g} × {p.JST_PLUG[1]:g} × {p.JST_PLUG[2]:g} plus {p.JST_BEND:g} mm for the lead to turn: "
        f"overlap {j_over:.4f} mm³")

    # 10. Cover: fits inside the walls, tongues engage, screws engage, it rests on its supports.
    cs_over = (shell & cover).volume
    cs_gap = shell.distance_to(cover)
    add("Cover fits the shell", cs_over < EPS and cs_gap < EPS,
        f"overlap {cs_over:.4f} mm³; rests on the ledges and screw bosses ({cs_gap:.3f} mm); "
        f"{p.COVER_CLEAR:g} mm round the edge")
    slide = slide_in()
    hits = [s for s in [i * 0.5 for i in range(int(slide / 0.5) + 1)] if (Pos(0, s, 0) * cover & shell).volume > EPS]
    lowered = [k for k in range(0, 31, 2) if (Pos(0, slide, -k) * cover & shell).volume > EPS]
    skin = p.WALL - p.TOP_TONGUE_IN - p.TOP_SLOT_CLEAR
    add("Cover slides into its tongue slots", not hits and not lowered and skin >= 1.0,
        f"laid in {slide:g} mm high, it drops straight down and slides {slide:g} mm into place without touching the shell"
        + (f" (collides at slide {hits})" if hits else "") + (f" (collides lowering at {lowered} mm)" if lowered else "")
        + f"; 2 tongues {p.TOP_TONGUE_L:g} mm long reach {p.TOP_TONGUE_IN:g} mm into the side walls, leaving {skin:.1f} mm of wall")
    engage = p.COVER_SCREW_L - p.COVER_T
    hole = p.COVER_SCREW_L - p.COVER_T + 1.0
    add("Cover screw engagement", p.INSERT_L - 0.5 <= engage <= hole - 0.5 and p.CSK_DEPTH <= p.COVER_T - 0.5,
        f"M3 × {p.COVER_SCREW_L:g} countersunk: head flush in the {p.COVER_T:g} mm cover, {engage:.1f} mm into a "
        f"{hole:.1f} mm boss hole with an M3 × 4 insert")

    # 11. Room round the BAT socket and its plug.
    be = ref.bat_envelope(ch)
    b_over = (be & shell).volume + (be & cover).volume
    add("BAT socket and plug clear", b_over < EPS,
        f"{p.BAT_ENVELOPE[0]:g} × {p.BAT_ENVELOPE[1]:g} × {p.BAT_ENVELOPE[2]:g} mm round the socket: overlap {b_over:.4f} mm³")

    # 12. Walls and overall size.
    walls = {"side/bottom": p.WALL, "top end (on the cover)": p.TOP_WALL, "cover": p.COVER_T}
    thin = [n for n, t in walls.items() if t < p.MIN_WALL]
    add("Wall thickness", not thin and p.FRONT_T >= p.MIN_BEZEL,
        ", ".join(f"{n} {t:g}" for n, t in walls.items()) + f" mm (need ≥ {p.MIN_WALL:g}); "
        f"bezel {p.FRONT_T:g} mm (need ≥ {p.MIN_BEZEL:g}, +{p.BEZEL_CHAMFER:g} chamfer)"
        + (f"; too thin: {', '.join(thin)}" if thin else ""))
    assembled = shell + cover
    bb = assembled.bounding_box()
    w, ln, t = bb.max.X - bb.min.X, bb.max.Y - bb.min.Y, bb.max.Z - bb.min.Z
    add("Overall size within limits", w <= p.MAX_WIDTH and ln <= p.MAX_LENGTH and t <= p.MAX_THICKNESS,
        f"{w:.1f} × {ln:.1f} × {t:.1f} mm (limits {p.MAX_WIDTH:g} × {p.MAX_LENGTH:g} × {p.MAX_THICKNESS:g})")

    # 13. Every printed part fits the P1S bed in its print pose.
    for name, solid in parts["print"].items():
        b = solid.bounding_box()
        size = b.max - b.min
        on_bed = abs(b.min.Z) < 1e-6
        add(f"Fits P1S bed: {name}", max(size.X, size.Y, size.Z) <= p.BED and on_bed,
            f"{size.X:.0f} × {size.Y:.0f} × {size.Z:.0f} mm{'' if on_bed else ' (NOT on the bed)'}")
    return results
