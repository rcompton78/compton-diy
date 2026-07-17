#!/usr/bin/env bash
# Builds every espframe device with a builds/<id>.factory.yaml, skipping any
# ids listed in apps/espframe/devices.json's "skip" array. Devices are
# discovered from the builds/ directory so adding a new device only requires
# a new factory.yaml and matching build-<id> nx target, not a change here.
#
# Usage: scripts/espframe-build-devices.sh
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="apps/espframe"
BUILDS_DIR="$REPO_ROOT/$PROJECT_DIR/builds"
DEVICES_JSON="$REPO_ROOT/$PROJECT_DIR/devices.json"

DEVICES=()
while IFS= read -r id; do
    DEVICES+=("$id")
done < <(cd "$BUILDS_DIR" && ls -1 *.factory.yaml | sed 's/\.factory\.yaml$//' | sort)

if [ "${#DEVICES[@]}" -eq 0 ]; then
    echo "No *.factory.yaml files found in $PROJECT_DIR/builds" >&2
    exit 1
fi

for id in "${DEVICES[@]}"; do
    reason=$(python3 -c "
import json, sys
data = json.load(open('$DEVICES_JSON'))
for d in data.get('skip', []):
    if d.get('id') == '$id':
        print(d.get('reason', 'skipped'))
        sys.exit()
")
    if [ -n "$reason" ]; then
        echo "==> Skipping espframe ($id): $reason"
        continue
    fi
    echo "==> Building espframe ($id)"
    pnpm nx run "espframe:build-$id"
done
