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
# top-level `-s`/--substitution flag, which must precede the subcommand.
# Always passed (defaulting to "dev", mirroring pio.sh's own
# "${RELEASE_VERSION:-dev}" pattern) rather than only when RELEASE_VERSION is
# set -- some entrypoints (e.g. apps/espframe/builds/*.factory.yaml) hardcode
# their own non-"dev" firmware_version placeholder ("0.0.0") at the top
# level, which would otherwise win over the package's "dev" default for any
# local/unversioned build, making the firmware_version=="dev" guards in
# common/addon/firmware_update.yaml and media-room-dashboard's dashboard.yaml
# treat an unversioned build as real and risk the exact auto-update loop they
# exist to prevent. Explicitly forcing "dev" here keeps that sentinel
# reliable for every entrypoint regardless of its own yaml default. Unlike
# pio.sh's exported-var pattern, there's no .env override here since
# RELEASE_VERSION is only ever meant to carry a real value in CI release
# builds.
SUBSTITUTION_ARGS=(-s firmware_version "${RELEASE_VERSION:-dev}")

if [ -x "$VENV_ESPHOME" ]; then
    exec "$VENV_ESPHOME" "${SUBSTITUTION_ARGS[@]}" "$@"
else
    exec esphome "${SUBSTITUTION_ARGS[@]}" "$@"
fi
