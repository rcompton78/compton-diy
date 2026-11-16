#!/usr/bin/env bash
# Pack the 10" stand's print files into a release tarball.
#
#   scripts/release/pack_stand.sh [version]
#
# Run the CAD build first (hardware/stand-10/setup.sh, then
# `.venv/bin/python -m stand10.build` in hardware/stand-10), which leaves
# out/*.{3mf,stl,step} and out/checks.txt. Writes
# dist/stand/espframe-stand-10-<version>.tar.gz holding the print files,
# checks.txt, the stand README, its renders, LICENSE, NOTICE and a VERSION file.
#
# Refuses to pack if checks.txt is missing or reports any FAIL, so a model that
# fails its fit or tip-over checks is never published.
set -euo pipefail

VERSION="${1:-${RELEASE_VERSION:-dev}}"
NAME="espframe-stand-10"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SRC="$ROOT/hardware/stand-10"
OUT="$SRC/out"
CHECKS="$OUT/checks.txt"

[ -f "$CHECKS" ] || { echo "ERROR: $CHECKS not found; build the stand first." >&2; exit 1; }
if grep -q '^FAIL' "$CHECKS"; then
  echo "ERROR: stand checks failed, refusing to pack:" >&2
  grep '^FAIL' "$CHECKS" >&2
  exit 1
fi

shopt -s nullglob
prints=("$OUT"/*.3mf "$OUT"/*.stl "$OUT"/*.step)
renders=("$SRC"/docs/*.png)
[ ${#prints[@]} -gt 0 ] || { echo "ERROR: no 3MF/STL/STEP files in $OUT." >&2; exit 1; }

DEST="$ROOT/dist/stand"
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
PKG="$STAGE/$NAME-$VERSION"
mkdir -p "$PKG/docs" "$DEST"

cp "${prints[@]}" "$CHECKS" "$SRC/README.md" "$ROOT/LICENSE" "$ROOT/NOTICE" "$PKG/"
if [ ${#renders[@]} -gt 0 ]; then cp "${renders[@]}" "$PKG/docs/"; fi
{
  echo "$NAME $VERSION"
  echo "commit $(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)"
} > "$PKG/VERSION"

rm -f "$DEST/$NAME"-*.tar.gz
# Sorted entries for a reproducible tarball; bsdtar (macOS) has no --sort.
if tar --version 2>/dev/null | grep -q 'GNU tar'; then
  tar --sort=name -czf "$DEST/$NAME-$VERSION.tar.gz" -C "$STAGE" "$NAME-$VERSION"
else
  (cd "$STAGE" && find "$NAME-$VERSION" | LC_ALL=C sort | tar -czf "$DEST/$NAME-$VERSION.tar.gz" -n -T -)
fi
echo "==> $DEST/$NAME-$VERSION.tar.gz ($(du -h "$DEST/$NAME-$VERSION.tar.gz" | cut -f1))"
