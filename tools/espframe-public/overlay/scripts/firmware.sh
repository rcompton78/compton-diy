#!/usr/bin/env bash
# Build, flash and monitor the 10" frame firmware with the pinned ESPHome.
#
#   scripts/firmware.sh install                 install the pinned ESPHome into .venv
#   scripts/firmware.sh compile                 build the firmware
#   scripts/firmware.sh flash [--device PORT]   build and flash over USB (or an IP, for OTA)
#   scripts/firmware.sh logs  [--device PORT]   show the frame's logs (USB port or IP)
#   scripts/firmware.sh esphome <args...>       run the pinned ESPHome with any other arguments
#
# The ESPHome version is pinned in product/contract/project.json (esphome_version);
# `install` (and any other command, on first use) puts exactly that version in this
# repo's .venv. Needs Python 3.11-3.13.
#
# Settings come from the environment or a .env file in the repo root (plain
# VAR=value lines; .env wins):
#   FIRMWARE_MANIFEST_URL  manifest the frame checks for updates. Unset, a local
#                          build never checks. CI sets it to the fork's Pages URL.
#   RELEASE_VERSION        version baked into the firmware (default "dev"; the
#                          frame never auto-installs over a "dev" build's version).
#   DEVICE                 default --device for flash/logs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONFIG="$ROOT/builds/guition-esp32-p4-jc8012p4a1.factory.yaml"
VENV="$ROOT/.venv"
CONTRACT="$ROOT/product/contract/project.json"

die() { echo "firmware.sh: $*" >&2; exit 1; }

if [ -f "$ROOT/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$ROOT/.env"
    set +a
fi

python_bin() {
    local candidate
    for candidate in python3.13 python3.12 python3.11 python3; do
        if command -v "$candidate" >/dev/null 2>&1 &&
            "$candidate" -c 'import sys; sys.exit(0 if (3, 11) <= sys.version_info[:2] <= (3, 13) else 1)' 2>/dev/null; then
            echo "$candidate"
            return
        fi
    done
    die "ESPHome needs Python 3.11-3.13; install one and try again"
}

pinned_version() {
    "$(python_bin)" -c 'import json, sys; print(json.load(open(sys.argv[1]))["esphome_version"])' "$CONTRACT"
}

installed_version() {
    "$1" version 2>/dev/null | awk '/^Version:/ {print $2}'
}

# Prints the esphome to run: a CI-installed one on PATH if it matches the pin,
# else this repo's .venv, installed on first use.
esphome_bin() {
    local pin on_path
    pin="$(pinned_version)"
    on_path="$(command -v esphome 2>/dev/null || true)"
    if [ -n "$on_path" ] && [ "$on_path" != "$VENV/bin/esphome" ] && [ "$(installed_version "$on_path")" = "$pin" ]; then
        echo "$on_path"
        return
    fi
    if [ ! -x "$VENV/bin/esphome" ] || [ "$(installed_version "$VENV/bin/esphome")" != "$pin" ]; then
        echo "firmware.sh: installing ESPHome $pin into .venv (one-time, takes a few minutes)..." >&2
        rm -rf "$VENV"
        "$(python_bin)" -m venv "$VENV" >&2
        "$VENV/bin/pip" install --quiet --upgrade pip >&2
        "$VENV/bin/pip" install --quiet "esphome==$pin" >&2 || die "pip install esphome==$pin failed"
        [ "$(installed_version "$VENV/bin/esphome")" = "$pin" ] || die "ESPHome in .venv does not report $pin"
    fi
    echo "$VENV/bin/esphome"
}

# -s flags must come before the ESPHome subcommand.
substitutions() {
    SUBS=(-s firmware_version "${RELEASE_VERSION:-dev}")
    if [ -n "${FIRMWARE_MANIFEST_URL:-}" ]; then
        SUBS+=(-s firmware_manifest_url "$FIRMWARE_MANIFEST_URL")
    fi
}

device_args() {
    DEV_ARGS=()
    local dev="${DEVICE:-}"
    while [ $# -gt 0 ]; do
        case "$1" in
            --device) dev="${2:?--device needs a serial port or IP}"; shift 2 ;;
            --device=*) dev="${1#--device=}"; shift ;;
            *) die "unknown option: $1" ;;
        esac
    done
    if [ -n "$dev" ]; then DEV_ARGS=(--device "$dev"); fi
}

cmd="${1:-}"
[ $# -gt 0 ] && shift
case "$cmd" in
    install)
        bin="$(esphome_bin)"
        echo "ESPHome $(installed_version "$bin") ready at $bin"
        ;;
    compile)
        [ $# -eq 0 ] || die "compile takes no options"
        bin="$(esphome_bin)"; substitutions
        exec "$bin" "${SUBS[@]}" compile "$CONFIG"
        ;;
    flash)
        bin="$(esphome_bin)"; substitutions; device_args "$@"
        exec "$bin" "${SUBS[@]}" run --no-logs "$CONFIG" ${DEV_ARGS[@]+"${DEV_ARGS[@]}"}
        ;;
    logs)
        bin="$(esphome_bin)"; substitutions; device_args "$@"
        exec "$bin" "${SUBS[@]}" logs "$CONFIG" ${DEV_ARGS[@]+"${DEV_ARGS[@]}"}
        ;;
    esphome)
        bin="$(esphome_bin)"; substitutions
        exec "$bin" "${SUBS[@]}" "$@"
        ;;
    *)
        sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//' >&2
        exit 2
        ;;
esac
