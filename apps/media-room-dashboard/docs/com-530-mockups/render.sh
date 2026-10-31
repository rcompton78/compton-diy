#!/usr/bin/env bash
# Renders svg/*.svg to png/*.png at 2x with Montserrat (COM-530).
# Usage: render.sh <dir with Montserrat-{Regular,Medium,SemiBold,Bold}.ttf>
# (https://github.com/JulietaUla/Montserrat/tree/master/fonts/ttf)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fonts="${1:?font dir}"
# Pinned so a CLI release can't silently change the committed PNGs.
resvg="@resvg/resvg-js-cli@2.6.2-beta.1"
shopt -s nullglob
svgs=("$here"/svg/*.svg)
if [ ${#svgs[@]} -eq 0 ]; then
  echo "render.sh: no SVGs in $here/svg; run generate.py first" >&2
  exit 1
fi
args=(--no-system-font --font-default-family Montserrat --fit-width 480)
for w in Regular Medium SemiBold Bold; do
  # --no-system-font means a missing font renders blank text, not an error.
  f="$fonts/Montserrat-$w.ttf"
  [ -f "$f" ] || { echo "render.sh: missing $f" >&2; exit 1; }
  args+=(--font-file "$f")
done
mkdir -p "$here/png"
for s in "${svgs[@]}"; do
  pnpm dlx "$resvg" "${args[@]}" "$s" "$here/png/$(basename "$s" .svg).png" >/dev/null
done
