"""Build every printed part, export STL / 3MF / STEP, render previews and run the fit checks.

    python -m stand.build [--out OUT] [--docs DOCS] [--no-render] [--variant ledge|desk]

With no --variant, builds both stand variants, each in its own subprocess (params are fixed
at import, see params.VARIANT), and writes their checks to one out/checks.txt in sections.
The ledge pass exports the shared parts (plate, lid, coupons) and the ledge frame and foot;
the desk pass exports only its own frame and foot, with a "-desk" suffix (renders too).

Exits non-zero if any check fails.
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

import numpy as np
from build123d import Mesher, Pos, export_step, export_stl

from . import checks
from . import params as p
from . import reference as ref
from .cable import build_path
from .coupons import dovetail_coupon, receptacle_coupon
from .foot import build_foot, build_lid, layout, lid_print_pose
from .frame import build_frame, frame_print_pose
from .frames import SCREEN_TO_WORLD, box
from .plate import build_plate, plate_print_pose

HERE = Path(__file__).resolve().parent.parent
SHARED = ("plate", "lid", "coupon-receptacle", "coupon-dovetail", "coupon-dovetail-rails")

PETG = (0.20, 0.45, 0.62)
FOOT = (0.30, 0.50, 0.66)
GLASS = (0.15, 0.16, 0.18)
BOARD = (0.75, 0.42, 0.30)
BRASS = (0.78, 0.62, 0.25)
METAL = (0.55, 0.57, 0.60)
WOOD = (0.66, 0.48, 0.30)
FELT = (0.25, 0.25, 0.27)
CABLE = "#d2691e"


def export(name, solid, out: Path):
    export_stl(solid, str(out / f"{name}.stl"))
    export_step(solid, str(out / f"{name}.step"))
    mesher = Mesher()
    mesher.add_shape(solid)
    mesher.write(str(out / f"{name}.3mf"))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "out"))
    ap.add_argument("--docs", default=str(HERE / "docs"))
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--variant", choices=p.VARIANTS)
    args = ap.parse_args(argv)
    out, docs = Path(args.out).resolve(), Path(args.docs).resolve()
    out.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)
    if args.variant is None:
        return _build_all(args, out, docs)
    if args.variant != p.VARIANT:
        print(f"--variant {args.variant} needs STAND_VARIANT={args.variant} (params were loaded as {p.VARIANT})",
              file=sys.stderr)
        return 2
    return _build_variant(args, out, docs)


def _build_all(args, out: Path, docs: Path) -> int:
    """Build each variant in its own process, then merge their checks into out/checks.txt.

    Also checks the two passes built the same lid (the ledge pass exports it, the desk foot reuses it):
    each pass writes its lid's volume and bounding box, and they must match."""
    failed = []
    sections = []
    lids = {}
    for v in p.VARIANTS:
        print(f"=== variant: {v} ===", flush=True)
        # Absolute paths: the child runs from HERE, which may not be the caller's cwd.
        cmd = [sys.executable, "-m", "stand.build", "--variant", v, "--out", str(out), "--docs", str(docs)]
        if args.no_render:
            cmd.append("--no-render")
        rc = subprocess.run(cmd, cwd=HERE, env={**os.environ, "STAND_VARIANT": v}).returncode
        part = out / f"checks-{v}.txt"
        if part.exists():
            sections.append(f"# {v}\n" + part.read_text(encoding="utf-8"))
            part.unlink()
        else:
            sections.append(f"# {v}\nFAIL  {v} variant build did not complete (exit {rc}), so it has no checks\n")
        lid = out / f"lid-{v}.json"
        if lid.exists():
            lids[v] = json.loads(lid.read_text(encoding="utf-8"))
            lid.unlink()
        if rc != 0:
            failed.append(v)
    if len(lids) == len(p.VARIANTS):
        ref_lid = lids[p.VARIANTS[0]]
        same = all(abs(a - b) < 1e-3 for l in lids.values() for a, b in zip(l, ref_lid))
        line = (f"{'PASS' if same else 'FAIL'}  Shared lid: one lid fits every foot  "
                + "; ".join(f"{v} lid volume {l[0]:.1f} mm³, box " + " / ".join(f"{x:.2f}" for x in l[1:])
                            for v, l in lids.items()))
        sections.append(f"# shared\n{line}\n")
        print("  " + line)
        if not same:
            failed.append("shared lid")
    (out / "checks.txt").write_text("\n".join(sections), encoding="utf-8")
    if failed:
        print(f"variant(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all variants passed")
    return 0


def _build_variant(args, out: Path, docs: Path) -> int:
    suffix = "-desk" if p.DESK else ""

    print("building parts…", flush=True)
    plate, foot, lid, frame = build_plate(), build_foot(), build_lid(), build_frame()
    printed = {
        "frame": frame_print_pose(frame),
        "plate": plate_print_pose(plate),
        "foot": foot,
        "lid": lid_print_pose(lid),
        "coupon-receptacle": receptacle_coupon(),
        "coupon-dovetail": dovetail_coupon(),
        "coupon-dovetail-rails": dovetail_coupon(include_groove=False),
    }
    for name, solid in printed.items():
        if not solid.is_valid:
            print(f"FAIL: {name} is not a valid solid", file=sys.stderr)
            return 1
        if name in SHARED:
            if p.DESK:
                continue          # same part as the ledge pass exports
            export(f"espframe-7in-stand-{name}", solid, out)
        else:
            export(f"espframe-7in-stand-{name}{suffix}", solid, out)
        print(f"  exported {name}{'' if name in SHARED else suffix}")

    print("running checks…", flush=True)
    results = checks.run({"plate": plate, "foot": foot, "lid": lid, "frame": frame, "print": printed})
    width = max(len(n) for n, _, _ in results)
    report = []
    for name, ok, detail in results:
        line = f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}"
        print("  " + line)
        report.append(line)
    (out / f"checks-{p.VARIANT}.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    # The lid as this pass built it (world frame), for _build_all's shared-lid check.
    bb = lid.bounding_box()
    (out / f"lid-{p.VARIANT}.json").write_text(json.dumps(
        [lid.volume, bb.min.X, bb.max.X, bb.min.Y, bb.max.Y, bb.min.Z, bb.max.Z]), encoding="utf-8")

    if not args.no_render:
        print("rendering previews…", flush=True)
        _render_all(plate, foot, lid, frame, printed, docs, suffix)

    failed = [n for n, ok, _ in results if not ok]
    if failed:
        print(f"{len(failed)} check(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all checks passed")
    return 0


def _render_all(plate, foot, lid, frame, printed, docs: Path, suffix: str):
    from .render import render

    plate_w = SCREEN_TO_WORLD * plate
    frame_w = SCREEN_TO_WORLD * frame
    felt_w = SCREEN_TO_WORLD * ref.seat_felt()
    panel_w = SCREEN_TO_WORLD * ref.panel()
    board_w = SCREEN_TO_WORLD * ref.board_keepout(p.JST_H)
    brass_w = SCREEN_TO_WORLD * ref.tabs_and_spacers()
    plug_w = SCREEN_TO_WORLD * ref.plug()
    jack = ref.jack()
    cable = np.array([[v.X, v.Y, v.Z] for v in build_path().points])
    cable_line = [(cable, CABLE, 2.2)]

    tag = " (on-desk)" if p.DESK else ""
    assembly = [(panel_w, GLASS, 1.0), (board_w, BOARD, 1.0), (brass_w, BRASS, 1.0), (plate_w, PETG, 1.0),
                (frame_w, WOOD, 1.0), (felt_w, FELT, 1.0), (foot, FOOT, 1.0), (lid, FOOT, 1.0), (jack, METAL, 1.0)]
    render(assembly, docs / f"assembly-rear{suffix}.png", view=(22, 35), title=f'7" stand{tag}: rear three-quarter')
    render(assembly, docs / f"assembly-front{suffix}.png", view=(15, -125), title=f'7" stand{tag}: front three-quarter')
    render([(panel_w, GLASS, 1.0), (board_w, BOARD, 1.0), (brass_w, BRASS, 1.0), (plate_w, PETG, 1.0),
            (plug_w, METAL, 1.0), (frame_w, WOOD, 0.45), (foot, FOOT, 0.35)], docs / f"side{suffix}.png", view=(0, 0),
           title=(f"Side{tag}: frame on the desk, plug bend over the shelf" if p.DESK else
                  "Side: frame, plate on brass spacers, plug, cable bend into the foot"), lines=cable_line)
    # Square to the frame face (screen frame, looking down -z), as a viewer in front of the stand sees it.
    render([(ref.panel(), GLASS, 1.0), (frame, WOOD, 1.0)], docs / f"frame-front{suffix}.png", view=(90, -90),
           title=f"Picture frame B{tag} (21 mm, wood-look), square to the face")
    render([(printed["frame"], WOOD, 1.0)], docs / f"frame{suffix}.png", view=(35, -60),
           title=f"Frame{tag} print pose: face down, bosses and walls up")

    # Plan section through the tray (lid off), cut at mid-tray height.
    L = layout()
    zcut = (p.FLOOR + L.z_cavity_top) / 2
    cut = foot & box(L.x0 - 1, L.x1 + 1, -20, p.FOOT_D + 5, -1, zcut)
    render([(cut, FOOT, 1.0), (jack, METAL, 1.0)],
           docs / f"foot-plan{suffix}.png", view=(90, -90),
           title=f"Foot{tag} section at Z = {zcut:.0f} mm: tray, cable coil, jack", lines=cable_line, zshade=True)
    render([(foot, FOOT, 1.0)], docs / f"foot{suffix}.png", view=(28, -50), title=f"Foot{tag} (print upright, as shown)")
    if p.DESK:
        # Bottom-front corner: frame on its felt, the shelf behind the face.
        fb = frame_w.bounding_box()
        clip = box(fb.min.X - 1, fb.min.X + 45, -30, 45, -1, 60)
        render([(s & clip, c, a) for s, c, a in ((panel_w, GLASS, 1.0), (frame_w, WOOD, 1.0), (felt_w, FELT, 1.0),
                                                  (foot, FOOT, 1.0))],
               docs / f"detail{suffix}.png", view=(12, -130), title=f"Bottom-front corner{tag}", size=(7, 7))
        return
    render([(printed["plate"], PETG, 1.0)], docs / "plate.png", view=(55, -60),
           title="Back-plate (print pose: back face down, risers and spine up)")
    render([(printed["plate"], PETG, 1.0)], docs / "plate-underside.png", view=(-55, -60),
           title="Back-plate underside: dovetail groove")
    render([(printed["coupon-receptacle"], PETG, 1.0), (Pos(0, 30, 0) * printed["coupon-dovetail"], FOOT, 1.0)],
           docs / "coupons.png", view=(50, -70), title="Test coupons: receptacle web plates, dovetail fit")

if __name__ == "__main__":
    sys.exit(main())
