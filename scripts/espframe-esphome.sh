#!/usr/bin/env bash
# Runs ESPHome using apps/espframe's own .venv if present, otherwise falls back to system esphome.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_ESPHOME="$REPO_ROOT/apps/espframe/.venv/bin/esphome"

if [ -x "$VENV_ESPHOME" ]; then
    exec "$VENV_ESPHOME" "$@"
else
    exec esphome "$@"
fi
