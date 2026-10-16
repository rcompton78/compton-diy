"""Build every printed part, export STL / 3MF / STEP, render previews and run the fit checks.

    python -m remote.build [--variant N] [--out OUT] [--docs DOCS] [--no-render]

Exits non-zero if any check fails.
"""

import argparse
import sys
from pathlib import Path

from build123d import Mesher, Pos, Rot, export_step, export_stl

from . import checks
from . import params as p
from . import reference as ref
from .cover import build_cover, cover_print_pose
from .shell import build_shell, shell_print_pose

HERE = Path(__file__).resolve().parent.parent

SHELL = (0.22, 0.24, 0.27)
COVER = (0.30, 0.33, 0.37)
GLASS = (0.10, 0.11, 0.13)
BOARD = (0.12, 0.40, 0.25)
CHARGER = (0.15, 0.25, 0.55)
CELL = (0.80, 0.78, 0.70)
METAL = (0.70, 0.71, 0.73)
PIGTAIL = "#d62728"
LEAD = "#ff7f0e"


def export(name, solid, out: Path):
    export_stl(solid, str(out / f"{name}.stl"))
    export_step(solid, str(out / f"{name}.step"))
    mesher = Mesher()
    mesher.add_shape(solid)
    mesher.write(str(out / f"{name}.3mf"))


def main(argv=None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", type=int, default=p.DEFAULT_VARIANT, choices=sorted(p.VARIANTS))
    ap.add_argument("--out", default=str(HERE / "out"))
    ap.add_argument("--docs", default=str(HERE / "docs"))
    ap.add_argument("--no-render", action="store_true")
    args = ap.parse_args(argv)
    variant = p.VARIANTS[args.variant]
    ch = variant.charger
    out, docs = Path(args.out), Path(args.docs)
    out.mkdir(parents=True, exist_ok=True)
    docs.mkdir(parents=True, exist_ok=True)
    prefix = f"media-remote-v{variant.number}"

    print(f"building {variant.title}…", flush=True)
    shell, cover = build_shell(ch), build_cover(ch)
    printed = {"shell": shell_print_pose(shell), "cover": cover_print_pose(cover)}
    for name, solid in printed.items():
        if not solid.is_valid:
            print(f"FAIL: {name} is not a valid solid", file=sys.stderr)
            return 1
        export(f"{prefix}-{name}", solid, out)
        print(f"  exported {name}")

    print("running checks…", flush=True)
    results = checks.run({"shell": shell, "cover": cover, "print": printed}, ch)
    width = max(len(n) for n, _, _ in results)
    report = [f"{variant.title}"]
    for name, ok, detail in results:
        line = f"{'PASS' if ok else 'FAIL'}  {name.ljust(width)}  {detail}"
        print("  " + line)
        report.append(line)
    (out / "checks.txt").write_text("\n".join(report) + "\n", encoding="utf-8")

    if not args.no_render:
        print("rendering previews…", flush=True)
        _render_all(shell, cover, printed, ch, docs)

    failed = [n for n, ok, _ in results if not ok]
    if failed:
        print(f"{len(failed)} check(s) failed: {', '.join(failed)}", file=sys.stderr)
        return 1
    print("all checks passed")
    return 0


def _render_all(shell, cover, printed, ch, docs: Path):
    from .render import render

    fn = ref.freenove(ch)
    glass = ref.fn_glass(ch)
    charger = ref.charger(ch)
    cell = ref.cell(ch)
    wires = [(ref.path_xyz(ref.pigtail_path(ch)), PIGTAIL, 2.2), (ref.path_xyz(ref.cell_lead_path(ch)), LEAD, 2.2)]
    inside = [(fn - glass, BOARD, 1.0), (glass, GLASS, 1.0), (charger, CHARGER, 1.0), (cell, CELL, 1.0)]

    render([(shell, SHELL, 1.0), (glass, GLASS, 1.0)], docs / "front.png", view=(25, -115),
           title="Media remote (variant 1): front three-quarter")
    # Back views: turn everything over (x → −x, z → −z) and look from above, so the remote
    # reads the way you hold it face down: top end up, its right side on your left.
    flip = Rot(Y=180)
    flipped_wires = [(pts * [-1, 1, -1], c, w) for pts, c, w in wires]
    render([(flip * shell, SHELL, 1.0), (flip * cover, COVER, 1.0)], docs / "back.png", view=(35, -115),
           title="Back: cover with RESET / BOOT pinholes, CHG window, two screws")
    # Cover off: boards, cell (taped to the cover in reality) and both wire runs.
    render([(flip * s, c, a) for s, c, a in [(shell, SHELL, 1.0)] + inside], docs / "inside.png", view=(90, -90),
           title="Cover off, from behind: cell, charger, pigtail (red) and cell lead (orange)", lines=flipped_wires)
    # Long section down the centreline.
    from .geom import box
    bb = shell.bounding_box()
    half = box(bb.min.X - 1, 0, bb.min.Y - 1, bb.max.Y + 1, bb.min.Z - 1, bb.max.Z + 1)
    render([(shell & half, SHELL, 1.0), (cover & half, COVER, 1.0)] + [(s & half, c, a) for s, c, a in inside],
           docs / "section.png", view=(0, 0), title="Section on the centreline: bezel, Freenove, cell, cover, charger",
           size=(12, 5))
    render([(printed["shell"], SHELL, 1.0)], docs / "shell-print.png", view=(40, -60),
           title="Front shell print pose: bezel face down, bosses and hooks up")
    render([(printed["cover"], COVER, 1.0)], docs / "cover-print.png", view=(40, -60),
           title="Back cover print pose: outer face down, pocket ribs and tongues up")
    # Exploded: shell, then the cover lifted off behind.
    render([(shell, SHELL, 1.0)] + inside + [(Pos(0, 0, -30) * cover, COVER, 1.0)], docs / "exploded.png",
           view=(-25, -55), title="Exploded: cover off for battery service")


if __name__ == "__main__":
    sys.exit(main())
