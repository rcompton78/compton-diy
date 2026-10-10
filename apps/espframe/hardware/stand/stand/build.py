"""Build every printed part, export STL / 3MF / STEP, render previews and run the fit checks.

    python -m stand.build [--out OUT] [--docs DOCS] [--no-render]

Exits non-zero if any check fails.
"""

import argparse
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
    args = ap.parse_args(argv)
    out, docs = Path(args.out), Path(args.docs)
    out.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)

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
        export(f"espframe-7in-stand-{name}", solid, out)
        print(f"  exported {name}")

    print("running checks…", flush=True)
    results = checks.run({"plate": plate, "foot": foot, "lid": lid, "frame": frame, "print": printed})
    width = max(len(n) for n, _, _ in results)
    report = []
    for name, ok, detail in results:
        line = f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}"
        print("  " + line)
        report.append(line)
    (out / "checks.txt").write_text("\n".join(report) + "\n", encoding="utf-8")

    if not args.no_render:
        print("rendering previews…", flush=True)
        _render_all(plate, foot, lid, frame, printed, docs)

    failed = [n for n, ok, _ in results if not ok]
    if failed:
        print(f"{len(failed)} check(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all checks passed")
    return 0


def _render_all(plate, foot, lid, frame, printed, docs: Path):
    from .render import render

    plate_w = SCREEN_TO_WORLD * plate
    frame_w = SCREEN_TO_WORLD * frame
    felt_w = SCREEN_TO_WORLD * ref.ledge_felt()
    panel_w = SCREEN_TO_WORLD * ref.panel()
    board_w = SCREEN_TO_WORLD * ref.board_keepout(p.JST_H)
    brass_w = SCREEN_TO_WORLD * ref.tabs_and_spacers()
    plug_w = SCREEN_TO_WORLD * ref.plug()
    jack = ref.jack()
    cable = np.array([[v.X, v.Y, v.Z] for v in build_path().points])
    cable_line = [(cable, CABLE, 2.2)]

    assembly = [(panel_w, GLASS, 1.0), (board_w, BOARD, 1.0), (brass_w, BRASS, 1.0), (plate_w, PETG, 1.0),
                (frame_w, WOOD, 1.0), (felt_w, FELT, 1.0), (foot, FOOT, 1.0), (lid, FOOT, 1.0), (jack, METAL, 1.0)]
    render(assembly, docs / "assembly-rear.png", view=(22, 35), title='7" stand: rear three-quarter')
    render(assembly, docs / "assembly-front.png", view=(15, -125), title='7" stand: front three-quarter')
    render([(panel_w, GLASS, 1.0), (board_w, BOARD, 1.0), (brass_w, BRASS, 1.0), (plate_w, PETG, 1.0),
            (plug_w, METAL, 1.0), (frame_w, WOOD, 0.45), (foot, FOOT, 0.35)], docs / "side.png", view=(0, 0),
           title="Side: frame, plate on brass spacers, plug, cable bend into the foot", lines=cable_line)
    # Square to the frame face (screen frame, looking down -z), as a viewer in front of the stand sees it.
    render([(ref.panel(), GLASS, 1.0), (frame, WOOD, 1.0)], docs / "frame-front.png", view=(90, -90),
           title="Picture frame B (21 mm, wood-look), square to the face")
    render([(printed["frame"], WOOD, 1.0)], docs / "frame.png", view=(35, -60),
           title="Frame print pose: face down, bosses and walls up")

    # Plan section through the tray (lid off), cut at mid-tray height.
    L = layout()
    zcut = (p.FLOOR + L.z_cavity_top) / 2
    cut = foot & box(L.x0 - 1, L.x1 + 1, -20, p.FOOT_D + 5, -1, zcut)
    render([(cut, FOOT, 1.0), (jack, METAL, 1.0)],
           docs / "foot-plan.png", view=(90, -90), title=f"Foot section at Z = {zcut:.0f} mm: tray, cable coil, jack",
           lines=cable_line, zshade=True)
    render([(foot, FOOT, 1.0)], docs / "foot.png", view=(28, -50), title="Foot (print upright, as shown)")
    render([(printed["plate"], PETG, 1.0)], docs / "plate.png", view=(55, -60),
           title="Back-plate (print pose: back face down, risers and spine up)")
    render([(printed["plate"], PETG, 1.0)], docs / "plate-underside.png", view=(-55, -60),
           title="Back-plate underside: dovetail groove")
    render([(printed["coupon-receptacle"], PETG, 1.0), (Pos(0, 30, 0) * printed["coupon-dovetail"], FOOT, 1.0)],
           docs / "coupons.png", view=(50, -70), title="Test coupons: receptacle web plates, dovetail fit")


if __name__ == "__main__":
    sys.exit(main())
