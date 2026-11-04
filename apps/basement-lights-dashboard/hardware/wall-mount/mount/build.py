"""Build every printed part and coupon, export STL / 3MF / STEP, run the fit checks and render previews.

    python -m mount.build [--out OUT] [--docs DOCS] [--no-render]

Exits non-zero if any check fails.
"""

import argparse
import sys
from pathlib import Path

import numpy as np
from build123d import Mesher, export_step, export_stl

from . import checks
from . import params as p
from . import reference as ref
from .coupons import dovetail_coupon
from .cradle import bottom_slice, build_cradle, cradle_print_pose, fit_frame
from .geom import box
from .plate import build_plate, plate_print_pose

HERE = Path(__file__).resolve().parent.parent
PREFIX = "basement-lights-wall-mount"

CRADLE = (0.34, 0.34, 0.37)
PLATE = (0.55, 0.55, 0.60)
ACRYLIC = (0.72, 0.82, 0.92)
GLASS = (0.08, 0.10, 0.16)
PLUG = (0.12, 0.12, 0.12)
METAL = (0.78, 0.62, 0.30)
WALL = (0.92, 0.89, 0.84)
KEEPOUT = (0.95, 0.40, 0.30)


def export(name, solid, out: Path):
    """Write one part as STL, STEP and 3MF into out/."""
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

    print("building…", flush=True)
    cradle, plate = build_cradle(), build_plate()
    printed = {
        "cradle": cradle_print_pose(cradle),
        "plate": plate_print_pose(plate),
        "coupon-1-fit-frame": fit_frame(cradle),
        "coupon-2-bottom-slice": bottom_slice(cradle),
        "coupon-3-dovetail": dovetail_coupon(),
    }
    for name, solid in printed.items():
        if not solid.is_valid:
            print(f"FAIL: {name} is not a valid solid", file=sys.stderr)
            return 1
        export(f"{PREFIX}-{name}", solid, out)
        grams = solid.volume / 1000 * 1.24
        print(f"  exported {name} ({solid.volume / 1000:.1f} cm³, ~{grams:.0f} g PLA solid)")

    print("running checks…", flush=True)
    results = checks.run(cradle, plate, printed)
    width = max(len(n) for n, _, _ in results)
    report = ["Basement lights wall mount (COM-534)"]
    for name, ok, detail in results:
        line = f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}"
        print("  " + line)
        report.append(line)
    (out / "checks.txt").write_text("\n".join(report) + "\n", encoding="utf-8")

    if not args.no_render:
        print("rendering previews…", flush=True)
        _render_all(cradle, plate, printed, docs)

    failed = [n for n, ok, _ in results if not ok]
    if failed:
        print(f"{len(failed)} check(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all checks passed")
    return 0


def _cable():
    """A polyline for the cable: down out of the plug, bending back to the wall below it."""
    x, d = ref.usb_axis()
    z0 = p.Z_CASE0 - p.PLUG_L
    r = min(15.0, d - 1.0)
    t = np.linspace(0, np.pi / 2, 20)
    arc = np.c_[np.full_like(t, x), -(d - r + r * np.cos(t)), z0 - r * np.sin(t)]
    tail = np.array([[x, -(d - r), z0 - r - 60]])
    return np.vstack([[[x, -d, z0]], arc, tail])


def _detent_screw():
    from .cradle import detent_x
    from .geom import zcyl
    from .plate import ear_z
    z0 = ear_z()[0]
    return zcyl(detent_x(), p.DETENT_D, z0, z0 + p.DETENT_SCREW_L, 3.0) + zcyl(detent_x(), p.DETENT_D, z0 - 2.0, z0, 5.5)


def _render_all(cradle, plate, printed, docs: Path):
    from .render import render

    case = ref.case_outline() + ref.heads()
    glass = ref.glass()
    plug = ref.plug()
    screw = _detent_screw()
    wall = box(-75, 75, -3, 0, -110, 175)
    asm = [(wall, WALL, 1.0), (plate, PLATE, 1.0), (cradle, CRADLE, 1.0), (case, ACRYLIC, 0.55),
           (glass, GLASS, 1.0), (plug, PLUG, 1.0), (screw, METAL, 1.0)]
    cable = [(_cable(), "black", 2.5)]

    render(asm, docs / "front.png", view=(0, -90), size=(7, 9),
           title=f"On the wall, from the front ({2 * p.X_OUT:.0f} × {p.Z_TOP:.0f} mm, {p.D_FRONT:.1f} mm deep)")
    render(asm + [(ref.plug_keepout(), KEEPOUT, 0.18)], docs / "three-quarter.png", view=(15, -50), size=(9, 9),
           lines=cable, title=f"Three-quarter: USB-C plug and the {p.PLUG_BEND_CLEAR:.0f} mm kept clear below (red)")
    keep = box(-30, 30, -3, 40, 70, 80)
    render([(s & keep, c, a) for s, c, a in [(wall, WALL, 1.0), (plate, PLATE, 1.0), (cradle, CRADLE, 1.0),
                                            (case, ACRYLIC, 0.8)]],
           docs / "section-dovetail.png", view=(90, -90), size=(10, 5),
           title=f"Section through the dovetail: {p.SLIDE_CLEAR} mm per flank, {p.PLATE_TOP - p.RAIL_BOTTOM:.0f} mm engaged")
    from .cradle import detent_x
    side = box(detent_x() - 0.01, 200, -10, 60, -400, 400)
    low = box(-200, 200, -10, 60, -15, 40)
    render([(s - side & low, c, a) for s, c, a in [(wall, WALL, 1.0), (plate, PLATE, 1.0), (cradle, CRADLE, 1.0),
                                                  (case, ACRYLIC, 0.6)]] + [(screw, METAL, 1.0)],
           docs / "section-detent.png", view=(0, 0), size=(6, 7),
           title="Detent: M3 × 8 up through the ear\ninto an insert in the cradle")
    render([(cradle, CRADLE, 1.0)], docs / "cradle-back.png", view=(20, 120), size=(8, 9),
           title="Cradle, wall side: groove (open at the bottom), vents, detent insert")
    render([(cradle, CRADLE, 1.0)], docs / "cradle-front.png", view=(25, -55), size=(8, 9),
           title="Cradle, case out: lips, head channels, floor plug opening, TF window")
    render([(plate, PLATE, 1.0)], docs / "plate.png", view=(25, -55), size=(6, 9),
           title=f"Wall plate {p.PLATE_W:.0f} × {p.PLATE_TOP - (-p.DETENT_GAP - p.EAR_T):.0f} mm")
    render([(printed["coupon-1-fit-frame"], CRADLE, 1.0)], docs / "coupon-1.png", view=(45, -60), size=(6, 6),
           title="Coupon 1: fit frame (front 4 mm, as printed)")
    render([(printed["coupon-2-bottom-slice"], CRADLE, 1.0)], docs / "coupon-2.png", view=(40, -60), size=(7, 6),
           title="Coupon 2: bottom slice (as printed)")
    render([(printed["coupon-3-dovetail"], CRADLE, 1.0)], docs / "coupon-3.png", view=(45, -70), size=(9, 5),
           title="Coupon 3: groove block + rails at 0.10 / 0.15 / 0.20")


if __name__ == "__main__":
    sys.exit(main())
