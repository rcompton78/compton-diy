#!/usr/bin/env bash
# Builds a PlatformIO project for every environment defined in its platformio.ini.
# Envs are discovered from the ini file itself so adding a new board only
# requires an [env:...] section, not a change here.
#
# Usage: scripts/build-boards.sh <project-dir> [pio-target]
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="$1"
PIO_TARGET="${2:-}"

mapfile -t ENVS < <(grep -oP '^\[env:\K[^\]]+' "$REPO_ROOT/$PROJECT_DIR/platformio.ini")

if [ "${#ENVS[@]}" -eq 0 ]; then
    echo "No [env:...] sections found in $PROJECT_DIR/platformio.ini" >&2
    exit 1
fi

for env in "${ENVS[@]}"; do
    echo "==> Building $PROJECT_DIR ($env)"
    if [ -n "$PIO_TARGET" ]; then
        "$REPO_ROOT/scripts/pio.sh" run -d "$PROJECT_DIR" -e "$env" -t "$PIO_TARGET"
    else
        "$REPO_ROOT/scripts/pio.sh" run -d "$PROJECT_DIR" -e "$env"
    fi
done
