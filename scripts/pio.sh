#!/usr/bin/env bash
# Runs PlatformIO using the repo's .venv if present, otherwise falls back to system python3.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_PYTHON="$REPO_ROOT/.venv/bin/python3"

# Defaulted here (not just in CI) so FIRMWARE_VERSION build flags always resolve,
# including for local/dev builds run outside the release workflow.
export RELEASE_VERSION="${RELEASE_VERSION:-dev}"
if [ -x "$VENV_PYTHON" ]; then
    exec "$VENV_PYTHON" -m platformio "$@"
else
    exec python3 -m platformio "$@"
fi
