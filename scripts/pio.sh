#!/usr/bin/env bash
# Runs PlatformIO using the repo's .venv if present, otherwise falls back to system python3.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_PYTHON="$REPO_ROOT/.venv/bin/python3"

# Defaulted here (not just in CI) so FIRMWARE_VERSION build flags always resolve,
# including for local/dev builds run outside the release workflow.
export RELEASE_VERSION="${RELEASE_VERSION:-dev}"

# Same idea for cat-buddy-api's endpoint/token (DIY-79) — real prod URL by default, empty
# token for local/dev builds since there's no real deployment to authenticate against by
# default. A project-local .env (below) can override either, e.g. for local testing against
# a LAN dev cat-buddy-api instance.
export CAT_BUDDY_API_URL="${CAT_BUDDY_API_URL:-https://cat-buddy.richcompton.com/api/flash-sale/current}"
export CAT_BUDDY_API_TOKEN="${CAT_BUDDY_API_TOKEN:-}"

# Auto-load a project-local, gitignored .env (dotenv-style: plain VAR=value lines) if the
# invocation targets one via `-d <dir>` / `--project-dir <dir>` — the same flag PlatformIO
# itself accepts, so this works for any project run through this wrapper. Sourced after the
# defaults above so a .env's values win; used for build-time overrides that shouldn't be
# committed (e.g. a local cat-buddy-api dev URL/token, DIY-79).
project_dir=""
prev_arg=""
for arg in "$@"; do
    if [ "$prev_arg" = "-d" ] || [ "$prev_arg" = "--project-dir" ]; then
        project_dir="$arg"
    fi
    prev_arg="$arg"
done
if [ -n "$project_dir" ] && [ -f "$REPO_ROOT/$project_dir/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$REPO_ROOT/$project_dir/.env"
    set +a
fi

# Isolated from the default ~/.platformio: espframe's ESPHome build (a
# separate PlatformIO consumer, via scripts/espframe-esphome.sh) shares that
# global package cache by default, and an unpinned `platform = espressif32`
# resolving differently between the two build systems silently upgraded
# shared framework/tool packages out from under this one. A dedicated core
# dir means the two can never clobber each other's package versions again.
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$REPO_ROOT/.platformio-core}"
# Pre-create it: PlatformIO's own first-run init uses os.makedirs without exist_ok, so two
# builds started in parallel on a cold core dir (nx running several PlatformIO projects at
# once, e.g. in pr-build.yml) race to create it and one crashes with FileExistsError.
# Resolved via readlink -f so a dangling symlink (the self-hosted runner's
# .platformio-core -> /hdd/... link) gets its target created rather than tripping mkdir
# on the link itself; falls back to the raw path wherever readlink -f is unsupported
# (older macOS/BSD) or can't resolve it. Package installs inside the dir are already
# serialized by PlatformIO's own lockfile.
core_dir="$(readlink -f "$PLATFORMIO_CORE_DIR" 2>/dev/null)" || core_dir=""
[ -n "$core_dir" ] || core_dir="$PLATFORMIO_CORE_DIR"
[ -d "$core_dir" ] || mkdir -p "$core_dir"
if [ -x "$VENV_PYTHON" ]; then
    exec "$VENV_PYTHON" -m platformio "$@"
else
    exec python3 -m platformio "$@"
fi
