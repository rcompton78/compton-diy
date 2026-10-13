#!/usr/bin/env bash
# Runs nx targets for every project, giving each release project its own
# RELEASE_VERSION from tools/esp-flasher/resolve_versions.py's output (COM-428).
#
# Bumped projects (and projects without a release target) run in one run-many
# under the run's new version. Each carried project runs on its own under the
# version of its last real release, so its manifest version (and, via the Nx
# cache's RELEASE_VERSION input, its binaries) stay what devices already have
# and nothing reinstalls. Every project is still built, so the Pages flasher
# index stays complete.
#
# Usage: scripts/release-run.sh <versions.json> <target> [<target>...]
set -euo pipefail
VERSIONS_JSON="$1"
shift
TARGETS="$(IFS=,; echo "$*")"

NEW_VERSION="$(jq -r .version "$VERSIONS_JSON")"
CARRIED="$(jq -r '.carried | keys | join(",")' "$VERSIONS_JSON")"

EXCLUDE=()
[ -n "$CARRIED" ] && EXCLUDE=(--exclude "$CARRIED")
RELEASE_VERSION="$NEW_VERSION" pnpm nx run-many -t "$TARGETS" "${EXCLUDE[@]}"

mapfile -t ENTRIES < <(jq -r '.carried | to_entries[] | "\(.key) \(.value)"' "$VERSIONS_JSON")
for entry in "${ENTRIES[@]}"; do
    project="${entry%% *}"
    version="${entry#* }"
    echo "==> $project: unchanged, keeping release $version"
    RELEASE_VERSION="$version" pnpm nx run-many -t "$TARGETS" -p "$project"
done
