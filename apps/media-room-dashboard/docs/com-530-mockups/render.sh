#!/usr/bin/env bash
# Renders svg/*.svg to png/*.png at 2x with Montserrat (COM-530).
# Usage: render.sh <dir with Montserrat-{Regular,Medium,SemiBold,Bold}.ttf>
# (https://github.com/JulietaUla/Montserrat/tree/master/fonts/ttf)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
fonts="${1:?font dir}"
mkdir -p "$here/png"
args=(--no-system-font --font-default-family Montserrat --fit-width 480)
for w in Regular Medium SemiBold Bold; do args+=(--font-file "$fonts/Montserrat-$w.ttf"); done
for s in "$here"/svg/*.svg; do
  pnpm dlx @resvg/resvg-js-cli "${args[@]}" "$s" "$here/png/$(basename "$s" .svg).png" >/dev/null
done
