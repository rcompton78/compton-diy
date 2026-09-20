#!/usr/bin/env bash
# Runs ESPHome using apps/espframe's own .venv if present, otherwise falls back to system esphome.
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV_ESPHOME="$REPO_ROOT/apps/espframe/.venv/bin/esphome"

# Isolated from the default ~/.platformio: ESPHome bundles its own PlatformIO
# dependency, which shares that global package cache with scripts/pio.sh
# (cyd-clock, bambu-status-bar) by default. An unpinned `platform =
# espressif32` resolving differently between the two build systems silently
# upgraded shared framework/tool packages out from under the other one. A
# dedicated core dir means the two can never clobber each other's package
# versions again.
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$REPO_ROOT/apps/espframe/.platformio-core}"

# ESPHome equivalent of scripts/pio.sh's `-D NAME="${sysenv.NAME}"` build-flag
# pattern: threads RELEASE_VERSION into the compiled firmware's own
# `firmware_version` substitution (esphome.project.version) via ESPHome's
# top-level `-s`/--substitution flag, which must precede the subcommand. When
# unset (local dev builds), each project's yaml keeps defaulting
# firmware_version to "dev" -- unlike pio.sh's exported-var pattern, there's
# no .env override here since RELEASE_VERSION is only ever meant to carry a
# real value in CI release builds.
SUBSTITUTION_ARGS=()
if [ -n "${RELEASE_VERSION:-}" ]; then
    SUBSTITUTION_ARGS=(-s firmware_version "$RELEASE_VERSION")
fi

if [ -x "$VENV_ESPHOME" ]; then
    exec "$VENV_ESPHOME" "${SUBSTITUTION_ARGS[@]}" "$@"
else
    exec esphome "${SUBSTITUTION_ARGS[@]}" "$@"
fi
