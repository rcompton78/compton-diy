#!/usr/bin/env bash
# Runs PlatformIO using the repo's .venv if present, otherwise falls back to system python3.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_PYTHON="$REPO_ROOT/.venv/bin/python3"

# Defaulted here (not just in CI) so FIRMWARE_VERSION build flags always resolve,
# including for local/dev builds run outside the release workflow.
export RELEASE_VERSION="${RELEASE_VERSION:-dev}"

# Isolated from the default ~/.platformio: espframe's ESPHome build (a
# separate PlatformIO consumer, via scripts/espframe-esphome.sh) shares that
# global package cache by default, and an unpinned `platform = espressif32`
# resolving differently between the two build systems silently upgraded
# shared framework/tool packages out from under this one. A dedicated core
# dir means the two can never clobber each other's package versions again.
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$REPO_ROOT/.platformio-core}"
if [ -x "$VENV_PYTHON" ]; then
    exec "$VENV_PYTHON" -m platformio "$@"
else
    exec python3 -m platformio "$@"
fi
