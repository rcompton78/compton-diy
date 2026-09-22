#!/usr/bin/env bash
# Exports every <name>.aseprite in an assets dir to the <name>.png sheet + <name>.json
# (json-array, with tags and frame durations) that tools/sprites/convert.py reads (COM-296).
#
# Local-only: needs Aseprite. CI and the firmware build never call this. They only read the
# committed exports, so commit the regenerated .png/.json after running it.
#
# Usage: tools/sprites/export-sprites.sh apps/tamagotchi-plus/assets
# Aseprite is taken from $ASEPRITE, else `aseprite` on PATH.
set -euo pipefail

assets_dir="${1:?usage: export-sprites.sh <assets-dir>}"
if [ ! -d "$assets_dir" ]; then
    echo "export-sprites: $assets_dir is not a directory" >&2
    exit 1
fi
aseprite="${ASEPRITE:-aseprite}"

if ! command -v "$aseprite" >/dev/null 2>&1; then
    echo "export-sprites: Aseprite not found (install it, or set ASEPRITE=/path/to/aseprite)." >&2
    echo "export-sprites: see apps/tamagotchi-plus/README.md > Installing Aseprite." >&2
    exit 1
fi

shopt -s nullglob
sources=("$assets_dir"/*.aseprite)
if [ ${#sources[@]} -eq 0 ]; then
    echo "export-sprites: no .aseprite files in $assets_dir" >&2
    exit 1
fi

for src in "${sources[@]}"; do
    name="${src%.aseprite}"
    # -b: batch mode, no UI (works headless, including WSL without a display).
    "$aseprite" -b "$src" \
        --sheet "$name.png" --sheet-type horizontal \
        --data "$name.json" --format json-array --list-tags \
        >/dev/null
    echo "export-sprites: $src -> $(basename "$name").png + $(basename "$name").json"
done
