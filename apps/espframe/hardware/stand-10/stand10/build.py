"""Build every printed part, export STL / 3MF / STEP, render previews and run the fit checks.

    python -m stand10.build [--out OUT] [--docs DOCS] [--no-render]

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
from .coupons import dovetail_coupon, plugs_coupon, receptacle_coupon, riser_coupon, template_coupon
from .foot import build_foot, build_lid, layout, lid_print_pose
from .frame import build_frame, frame_print_pose
from .frames import SCREEN_TO_WORLD, box, screen_box
from .plate import build_plate, plate_print_pose

HERE = Path(__file__).resolve().parent.parent

PETG = (0.20, 0.45, 0.62)
FOOT = (0.30, 0.50, 0.66)
GLASS = (0.10, 0.11, 0.13)
PICTURE = (0.35, 0.55, 0.75)
BOX = (0.30, 0.31, 0.33)
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
    out, docs = Path(args.out).resolve(), Path(args.docs).resolve()
    out.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)

    print("building parts…", flush=True)
    plate, foot, lid, frame = build_plate(), build_foot(), build_lid(), build_frame()
    printed = {
        "frame": frame_print_pose(frame),
        "plate": plate_print_pose(build_plate(fit=True)),   # printed risers carry RISER_FIT_ADD
        "foot": foot,
        "lid": lid_print_pose(lid),
        "coupon-template": template_coupon(),
        "coupon-riser": riser_coupon(),
        "coupon-receptacle": receptacle_coupon(),
        "coupon-dovetail": dovetail_coupon(),
        "plugs": plugs_coupon(),
    }
    for name, solid in printed.items():
        if not solid.is_valid:
            print(f"FAIL: {name} is not a valid solid", file=sys.stderr)
            return 1
        export(f"espframe-10in-stand-{name}", solid, out)
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

    W = lambda s: SCREEN_TO_WORLD * s  # noqa: E731
    plate_w, frame_w = W(plate), W(frame)
    # The picture sits in a shallow pocket in the shell face, so no two render faces are coplanar.
    aa = screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], -1, 1.0)
    shell_w = W(ref.shell() - aa)
    pic_w = W(screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], 0.6, 1.0))
    box_w, boss_w, fpc_w = W(ref.rear_box()), W(ref.bosses()), W(ref.fpc_bracket())
    felt_w, plug_w, jack = W(ref.desk_felt()), W(ref.plug()), ref.jack()
    cable = [(np.array([[v.X, v.Y, v.Z] for v in build_path().points]), CABLE, 2.2)]

    board = [(shell_w, GLASS, 1.0), (pic_w, PICTURE, 1.0), (box_w, BOX, 1.0), (boss_w, METAL, 1.0), (fpc_w, BOX, 1.0)]
    stand = [(plate_w, PETG, 1.0), (frame_w, WOOD, 1.0), (felt_w, FELT, 1.0), (foot, FOOT, 1.0),
             (lid, FOOT, 1.0), (jack, METAL, 1.0)]

    render(board + stand, docs / "assembly-front.png", view=(5, -90), title='10" stand: front')
    render(board + stand, docs / "assembly-front-34.png", view=(15, -125), title='10" stand: front three-quarter')
    render(board + [(plate_w, PETG, 1.0), (plug_w, METAL, 1.0), (frame_w, WOOD, 0.45), (foot, FOOT, 0.4),
                    (felt_w, FELT, 1.0)],
           docs / "side.png", view=(0, 0), title="Side: 15° lean, frame on the desk, foot behind, plug bend into the shelf",
           lines=cable)
    render(board + stand, docs / "assembly-rear.png", view=(22, 35), title="Rear three-quarter: plate, jack in the rear wall")
    # Plate sheet hidden, lid off: risers on the bosses, spine, rear box, plug, cable into the coil and the jack.
    sheet = screen_box(-10, 200, -10, 300, p.D_PLATE_FRONT, p.D_PLATE_BACK + 0.01)
    render(board + [(W(plate - sheet), PETG, 1.0), (frame_w, WOOD, 1.0), (foot, FOOT, 1.0),
                    (plug_w, METAL, 1.0), (jack, METAL, 1.0)],
           docs / "assembly-rear-open.png", view=(28, 50),
           title="Rear, plate sheet and lid hidden: risers, spine, rear box, plug, cable, jack", lines=cable)
    clip = box(p.FRAME_X[0] - 1, p.FRAME_X[1] + 1, -30, 80, -1, 70)
    render([(s & clip, c, a) for s, c, a in ((shell_w, GLASS, 1.0), (box_w, BOX, 1.0), (plate_w, PETG, 1.0),
                                              (frame_w, WOOD, 1.0), (foot, FOOT, 1.0), (plug_w, METAL, 1.0))],
           docs / "rear-notch.png", view=(40, 70), title="Plate bottom from behind: cable notch over the shelf (lid off)",
           lines=cable, size=(8, 6))
    render([(ref.shell(), GLASS, 1.0), (frame, WOOD, 1.0)], docs / "frame-front.png", view=(90, -90),
           title="Picture frame (wood-look), square to the face")
    render([(printed["frame"], WOOD, 1.0)], docs / "frame.png", view=(35, -60),
           title="Frame print pose: face down, bosses and walls up")
    L = layout()
    zcut = (p.FLOOR + L.z_cavity_top) / 2
    cut = foot & box(L.x0 - 1, L.x1 + 1, -20, p.FOOT_D + 5, -1, zcut)
    render([(cut, FOOT, 1.0), (jack, METAL, 1.0)], docs / "foot-plan.png", view=(90, -90),
           title=f"Foot section at Z = {zcut:.0f} mm: tray, cable coil, jack", lines=cable, zshade=True)
    render([(foot, FOOT, 1.0)], docs / "foot.png", view=(28, -50), title="Foot (print upright, as shown)")
    render([(printed["plate"], PETG, 1.0)], docs / "plate.png", view=(55, -60),
           title="Back-plate (print pose: back face down, risers and spine up)")
    render([(printed["plate"], PETG, 1.0)], docs / "plate-underside.png", view=(-55, -60),
           title="Back-plate underside: dovetail groove, cable notch")
    render([(printed["coupon-template"], PETG, 1.0), (Pos(-40, 0, 0) * printed["coupon-riser"], FOOT, 1.0),
            (Pos(-75, -30, 0) * printed["plugs"], FOOT, 1.0)],
           docs / "coupon-template.png", view=(60, -70),
           title="Hole template (mirrored, lies on the back), riser coupon and hole plugs")
    render([(printed["coupon-receptacle"], PETG, 1.0), (Pos(0, 30, 0) * printed["coupon-dovetail"], FOOT, 1.0)],
           docs / "coupons.png", view=(50, -70), title="Test coupons: receptacle web plates, dovetail fit")


if __name__ == "__main__":
    sys.exit(main())
