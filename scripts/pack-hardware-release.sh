#!/usr/bin/env bash
# Pack a hardware (3D-print) project's build outputs into a release tarball.
#
#   scripts/pack-hardware-release.sh <project-dir> <name> [version]
#
# Expects the project's CAD build to have already run (the `release` target
# dependsOn `cad`), leaving out/*.{3mf,stl,step} and out/checks.txt. Writes
# dist/<name>/<name>-<version>.tar.gz with one top-level <name>-<version>/ dir
# holding the print files, checks.txt, README.md, docs/*.png and a VERSION file.
# release.yml attaches dist/<project>/*.tar.gz to the GitHub release.
#
# Refuses to pack if checks.txt is missing or reports any FAIL, so a model that
# fails its fit / tip-over checks can never be published (the CAD build already
# exits non-zero on a failed check; this is the second guard).
set -euo pipefail

PROJECT_DIR="${1:?usage: $0 <project-dir> <name> [version]}"
NAME="${2:?usage: $0 <project-dir> <name> [version]}"
VERSION="${3:-${RELEASE_VERSION:-dev}}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/$PROJECT_DIR"
OUT="$SRC/out"
CHECKS="$OUT/checks.txt"

if [ ! -f "$CHECKS" ]; then
  echo "ERROR: $CHECKS not found; run the project's cad target first." >&2
  exit 1
fi
if grep -q '^FAIL' "$CHECKS"; then
  echo "ERROR: model checks failed, refusing to pack:" >&2
  grep '^FAIL' "$CHECKS" >&2
  exit 1
fi

shopt -s nullglob
prints=("$OUT"/*.3mf "$OUT"/*.stl "$OUT"/*.step)
renders=("$SRC"/docs/*.png)
if [ ${#prints[@]} -eq 0 ]; then
  echo "ERROR: no 3MF/STL/STEP files in $OUT." >&2
  exit 1
fi

DEST="$ROOT/dist/$NAME"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/$NAME-$VERSION"
mkdir -p "$PKG/docs" "$DEST"

cp "${prints[@]}" "$CHECKS" "$PKG/"
cp "$SRC/README.md" "$PKG/"
if [ ${#renders[@]} -gt 0 ]; then cp "${renders[@]}" "$PKG/docs/"; fi
{
  echo "$NAME $VERSION"
  echo "commit $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
} > "$PKG/VERSION"

rm -f "$DEST/$NAME"-*.tar.gz
tar --sort=name -czf "$DEST/$NAME-$VERSION.tar.gz" -C "$STAGE" "$NAME-$VERSION"
echo "==> $DEST/$NAME-$VERSION.tar.gz ($(du -h "$DEST/$NAME-$VERSION.tar.gz" | cut -f1))"
