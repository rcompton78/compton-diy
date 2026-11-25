#!/usr/bin/env bash
# Build, flash and monitor the 10" frame firmware with the pinned ESPHome.
#
#   scripts/firmware.sh install                         install the pinned ESPHome into .venv
#   scripts/firmware.sh compile [--chip C]              build the firmware
#   scripts/firmware.sh flash [--chip C] [--device P]   build and flash over USB (or an IP, for OTA)
#   scripts/firmware.sh logs  [--chip C] [--device P]   show the frame's logs (USB port or IP)
#   scripts/firmware.sh esphome <args...>               run the pinned ESPHome with any other arguments
#
# --chip picks the build for the board's ESP32-P4 revision: pre-v3 (the
# default, boards from before late 2026) or v3 (a v3.1 or later chip). Neither
# image boots on the other chip; see the README.
#
# The ESPHome version is pinned in product/contract/project.json (esphome_version);
# `install` (and any other command, on first use) puts exactly that version in this
# repo's .venv. Needs Python 3.11-3.13, on Linux, macOS or WSL (not Git Bash).
#
# Settings come from the environment or a .env file in the repo root (plain
# VAR=value lines, read as text, not run; .env wins):
#   FIRMWARE_MANIFEST_URL  manifest the build checks for updates. Unset, it has
#                          none (a manifest URL the frame already saved, under
#                          Firmware -> Advanced, still applies). CI sets it to
#                          the fork's Pages URL.
#   RELEASE_VERSION        version baked into the firmware (default "dev"). A
#                          "dev" build finds updates but never auto-installs
#                          them; press Install on the frame's web page.
#   SIBLING_FIRMWARE_MANIFEST_URL
#                          the other chip build's manifest. A frame whose saved
#                          manifest URL names it is moved onto its own at boot.
#                          CI sets it; leave it unset locally.
#   CHIP                   default --chip (pre-v3 or v3).
#   DEVICE                 default --device for flash/logs.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VENV="$ROOT/.venv"
CONTRACT="$ROOT/product/contract/project.json"

die() { echo "firmware.sh: $*" >&2; exit 1; }

if [ -f "$ROOT/.env" ]; then
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        case "$line" in '' | '#'*) continue ;; esac
        key="${line%%=*}"
        value="${line#*=}"
        if [ "$key" = "$line" ] || ! [[ "$key" =~ ^[A-Za-z_][A-Za-z0-9_]*$ ]]; then
            die ".env: not a VAR=value line: $line"
        fi
        # Strip one pair of matching surrounding quotes.
        if [[ "$value" =~ ^\"(.*)\"$ ]] || [[ "$value" =~ ^\'(.*)\'$ ]]; then value="${BASH_REMATCH[1]}"; fi
        # Only the documented settings: anything else could overwrite this
        # script's own variables (VENV is rm -rf'd on a reinstall).
        case "$key" in
            FIRMWARE_MANIFEST_URL | SIBLING_FIRMWARE_MANIFEST_URL | RELEASE_VERSION | CHIP | DEVICE) export "$key=$value" ;;
            *) die ".env: unsupported setting '$key' (only FIRMWARE_MANIFEST_URL, SIBLING_FIRMWARE_MANIFEST_URL, RELEASE_VERSION, CHIP, DEVICE)" ;;
        esac
    done < "$ROOT/.env"
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
    local py pin
    [ -f "$CONTRACT" ] || die "missing $CONTRACT"
    py="$(python_bin)" || exit 1
    pin="$("$py" -c 'import json, sys; print(json.load(open(sys.argv[1]))["esphome_version"])' "$CONTRACT" 2>/dev/null)" \
        || die "could not read esphome_version from $CONTRACT"
    [ -n "$pin" ] || die "no esphome_version in $CONTRACT"
    echo "$pin"
}

installed_version() {
    "$1" version 2>/dev/null | awk '/^Version:/ {print $2}'
}

# Prints the esphome to run: a CI-installed one on PATH if it matches the pin,
# else this repo's .venv, installed on first use.
esphome_bin() {
    local pin on_path py
    pin="$(pinned_version)" || exit 1
    on_path="$(command -v esphome 2>/dev/null || true)"
    if [ -n "$on_path" ] && [ "$on_path" != "$VENV/bin/esphome" ] && [ "$(installed_version "$on_path")" = "$pin" ]; then
        echo "$on_path"
        return
    fi
    if [ ! -x "$VENV/bin/esphome" ] || [ "$(installed_version "$VENV/bin/esphome")" != "$pin" ]; then
        echo "firmware.sh: installing ESPHome $pin into .venv (one-time, takes a few minutes)..." >&2
        rm -rf "$VENV"
        py="$(python_bin)" || exit 1
        "$py" -m venv "$VENV" >&2
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
    if [ -n "${SIBLING_FIRMWARE_MANIFEST_URL:-}" ]; then
        SUBS+=(-s sibling_firmware_manifest_url "$SIBLING_FIRMWARE_MANIFEST_URL")
    fi
}

# Sets CONFIG from --chip (or CHIP) and DEV_ARGS from --device (or DEVICE).
# allow_device=0 refuses --device (compile).
parse_args() {
    local allow_device="$1"; shift
    local chip="${CHIP:-pre-v3}" dev="${DEVICE:-}"
    DEV_ARGS=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --chip) chip="${2:?--chip needs pre-v3 or v3}"; shift 2 ;;
            --chip=*) chip="${1#--chip=}"; shift ;;
            --device | --device=*)
                [ "$allow_device" = 1 ] || die "$cmd takes no --device"
                if [ "$1" = --device ]; then dev="${2:?--device needs a serial port or IP}"; shift 2; else dev="${1#--device=}"; shift; fi
                ;;
            *) die "unknown option: $1" ;;
        esac
    done
    case "$chip" in
        pre-v3) CONFIG="$ROOT/builds/guition-esp32-p4-jc8012p4a1.factory.yaml" ;;
        v3) CONFIG="$ROOT/builds/guition-esp32-p4-jc8012p4a1-v3.factory.yaml" ;;
        *) die "unknown chip '$chip' (pre-v3 or v3)" ;;
    esac
    if [ -n "$dev" ]; then DEV_ARGS=(--device "$dev"); fi
}

cmd="${1:-}"
[ $# -gt 0 ] && shift
case "$cmd" in
    install)
        bin="$(esphome_bin)" || exit 1
        echo "ESPHome $(installed_version "$bin") ready at $bin"
        ;;
    compile)
        parse_args 0 "$@"; bin="$(esphome_bin)" || exit 1; substitutions
        exec "$bin" "${SUBS[@]}" compile "$CONFIG"
        ;;
    flash)
        parse_args 1 "$@"; bin="$(esphome_bin)" || exit 1; substitutions
        exec "$bin" "${SUBS[@]}" run --no-logs "$CONFIG" ${DEV_ARGS[@]+"${DEV_ARGS[@]}"}
        ;;
    logs)
        parse_args 1 "$@"; bin="$(esphome_bin)" || exit 1; substitutions
        exec "$bin" "${SUBS[@]}" logs "$CONFIG" ${DEV_ARGS[@]+"${DEV_ARGS[@]}"}
        ;;
    esphome)
        bin="$(esphome_bin)" || exit 1; substitutions
        exec "$bin" "${SUBS[@]}" "$@"
        ;;
    *)
        sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//' >&2
        exit 2
        ;;
esac
