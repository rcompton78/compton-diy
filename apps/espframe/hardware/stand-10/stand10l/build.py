"""Landscape stand (COM-684): build every printed part, export STL / 3MF / STEP, render previews and run the checks.

    python -m stand10l.build [--out OUT] [--docs DOCS] [--no-render]

Writes out/espframe-10in-landscape-stand-<part>.*, out/checks-landscape.txt and docs/landscape-*.png beside the
portrait stand's files. The coupons (hole template, riser, plugs, receptacle, dovetail) are the portrait stand's: same
board, same fits. Exits non-zero if any check fails.
"""

import argparse
import sys
from pathlib import Path

import numpy as np

from stand10.build import BOX, CABLE, FELT, FOOT, GLASS, METAL, PETG, PICTURE, WOOD, export
from stand10.coupons import riser_coupon

from . import checks
from . import params as p
from . import reference as ref
from .cable import build_path
from .foot import build_foot, build_lid, layout, lid_print_pose
from .frame import build_frame, frame_print_pose
from .frames import SCREEN_TO_WORLD, box, screen_box
from .plate import build_plate, plate_print_pose

HERE = Path(__file__).resolve().parent.parent
PREFIX = "espframe-10in-landscape-stand"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(HERE / "out"))
    ap.add_argument("--docs", default=str(HERE / "docs"))
    ap.add_argument("--no-render", action="store_true")
    args = ap.parse_args(argv)
    out, docs = Path(args.out).resolve(), Path(args.docs).resolve()
    out.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)
    # Clear this build's previous outputs first, so a run that stops partway can't leave an old checks report (which
    # the pack scripts trust) next to new or missing print files.
    for old in [out / "checks-landscape.txt", *out.glob(f"{PREFIX}-*")]:
        old.unlink()

    print("building landscape parts…", flush=True)
    plate, foot, lid, frame = build_plate(), build_foot(), build_lid(), build_frame()
    printed = {
        "frame": frame_print_pose(frame),
        "plate": plate_print_pose(build_plate(fit=True)),   # printed risers carry RISER_FIT_ADD
        "foot": foot,
        "lid": lid_print_pose(lid),
    }
    for name, solid in printed.items():
        if not solid.is_valid:
            print(f"FAIL: {name} is not a valid solid", file=sys.stderr)
            return 1
        export(f"{PREFIX}-{name}", solid, out)
        print(f"  exported {name}")

    print("running landscape checks…", flush=True)
    results = checks.run({"plate": plate, "foot": foot, "lid": lid, "frame": frame, "print": printed,
                          "coupon-riser": riser_coupon()})
    width = max(len(n) for n, _, _ in results)
    report = []
    for name, ok, detail in results:
        line = f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}"
        print("  " + line)
        report.append(line)
    (out / "checks-landscape.txt").write_text("\n".join(report) + "\n", encoding="utf-8")

    if not args.no_render:
        print("rendering landscape previews…", flush=True)
        _render_all(plate, foot, lid, frame, printed, docs)

    failed = [n for n, ok, _ in results if not ok]
    if failed:
        print(f"{len(failed)} check(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all landscape checks passed")
    return 0


def _render_all(plate, foot, lid, frame, printed, docs: Path):
    from stand10.render import render

    W = lambda s: SCREEN_TO_WORLD * s  # noqa: E731
    plate_w, frame_w = W(plate), W(frame)
    aa = screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], -1, 1.0)
    shell_w = W(ref.shell() - aa)
    pic_w = W(screen_box(p.AA_X[0], p.AA_X[1], p.AA_Y[0], p.AA_Y[1], 0.6, 1.0))
    box_w, boss_w, fpc_w = W(ref.rear_box()), W(ref.bosses()), W(ref.fpc_bracket())
    felt_w, plug_w, jack = W(ref.desk_felt()), W(ref.plug()), ref.jack()
    cable = [(np.array([[v.X, v.Y, v.Z] for v in build_path().points]), CABLE, 2.2)]

    board = [(shell_w, GLASS, 1.0), (pic_w, PICTURE, 1.0), (box_w, BOX, 1.0), (boss_w, METAL, 1.0), (fpc_w, BOX, 1.0)]
    stand = [(plate_w, PETG, 1.0), (frame_w, WOOD, 1.0), (felt_w, FELT, 1.0), (foot, FOOT, 1.0),
             (lid, FOOT, 1.0), (jack, METAL, 1.0)]
    t = '10" landscape stand'

    render(board + stand, docs / "landscape-assembly-front.png", view=(5, -90), title=f"{t}: front")
    render(board + stand, docs / "landscape-assembly-front-34.png", view=(15, -125), title=f"{t}: front three-quarter")
    render(board + [(plate_w, PETG, 1.0), (plug_w, METAL, 1.0), (frame_w, WOOD, 0.45), (foot, FOOT, 0.4),
                    (felt_w, FELT, 1.0)],
           docs / "landscape-side.png", view=(0, 0),
           title="Side: 15° lean, frame on the desk, foot behind, cable down behind the plate into the tray",
           lines=cable)
    render(board + stand, docs / "landscape-assembly-rear.png", view=(22, 35),
           title=f"{t}: rear three-quarter, cable through the plate's edge notch into the lid notch", lines=cable)
    sheet = screen_box(-10, 260, -10, 200, p.D_PLATE_FRONT, p.D_PLATE_BACK + 0.01)
    render(board + [(W(plate - sheet), PETG, 1.0), (frame_w, WOOD, 1.0), (foot, FOOT, 1.0),
                    (plug_w, METAL, 1.0), (jack, METAL, 1.0)],
           docs / "landscape-assembly-rear-open.png", view=(28, 50),
           title="Rear, plate sheet and lid hidden: risers, spine, rear box, plug, cable, jack", lines=cable)
    clip = box(p.FRAME_X[0] - 1, 70, -30, p.FOOT_D + 5, -1, 90)
    render([(s & clip, c, a) for s, c, a in ((shell_w, GLASS, 1.0), (box_w, BOX, 1.0), (plate_w, PETG, 1.0),
                                              (frame_w, WOOD, 1.0), (foot, FOOT, 1.0), (lid, FOOT, 1.0),
                                              (plug_w, METAL, 1.0))],
           docs / "landscape-cable.png", view=(30, 120),
           title="Left end from behind: plug, R18 bend through the plate notch, R18 down into the lid notch",
           lines=cable, size=(8, 6))
    render([(ref.shell(), GLASS, 1.0), (frame, WOOD, 1.0)], docs / "landscape-frame-front.png", view=(90, -90),
           title="Landscape picture frame (wood-look), square to the face")
    render([(printed["frame"], WOOD, 1.0)], docs / "landscape-frame.png", view=(35, -60),
           title="Landscape frame print pose: face down, bosses and walls up")
    L = layout()
    zcut = (p.FLOOR + L.z_cavity_top) / 2
    cut = foot & box(L.x0 - 1, L.x1 + 1, -20, p.FOOT_D + 5, -1, zcut)
    render([(cut, FOOT, 1.0), (jack, METAL, 1.0)], docs / "landscape-foot-plan.png", view=(90, -90),
           title=f"Landscape foot section at Z = {zcut:.0f} mm: tray, cable coil, jack", lines=cable, zshade=True)
    render([(foot, FOOT, 1.0)], docs / "landscape-foot.png", view=(28, -50), title="Landscape foot (print upright, as shown)")
    render([(printed["plate"], PETG, 1.0)], docs / "landscape-plate.png", view=(55, -60),
           title="Landscape back-plate (print pose: back face down, risers and spine up)")
    render([(printed["lid"], FOOT, 1.0)], docs / "landscape-lid.png", view=(50, -60),
           title="Landscape tray lid: cable notch front-left, lift slot front-right")


if __name__ == "__main__":
    sys.exit(main())
