#!/usr/bin/env bash
# Runs PlatformIO using the repo's .venv if present, otherwise falls back to system python3.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_PYTHON="$REPO_ROOT/.venv/bin/python3"
if [ -x "$VENV_PYTHON" ]; then
    exec "$VENV_PYTHON" -m platformio "$@"
else
    exec python3 -m platformio "$@"
fi
