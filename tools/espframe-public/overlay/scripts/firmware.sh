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
#                          the fork's Pages URL. Each chip build has its own
#                          manifest (manifest.json for pre-v3,
#                          manifest-guition-esp32-p4-jc8012p4a1-v3.json for v3),
#                          and a URL naming the other chip's is refused: that
#                          image can't boot this chip, so every update would fail.
#   RELEASE_VERSION        version baked into the firmware (default "dev"). A
#                          "dev" build finds updates but never auto-installs
#                          them; press Install on the frame's web page.
#   SIBLING_FIRMWARE_MANIFEST_URL
#                          the other chip build's manifest. A frame whose saved
#                          manifest URL names it is moved onto its own at boot.
#                          Unset, it's derived from FIRMWARE_MANIFEST_URL when
#                          that names this chip's manifest (the other chip's
#                          manifest in the same folder). Set without
#                          FIRMWARE_MANIFEST_URL, it's refused.
#   CHIP                   default --chip (pre-v3 or v3).
#   DEVICE                 default --device for flash/logs.
#   PLATFORMIO_CORE_DIR    where PlatformIO keeps its toolchains and Python env
#                          (several GB). Default: .platformio-core/ in this repo,
#                          so the build never changes the ~/.platformio other
#                          projects share. Point worktrees or clones at one
#                          folder to share a copy. A relative path is relative
#                          to the repo root; a leading ~/ is $HOME.
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
            FIRMWARE_MANIFEST_URL | SIBLING_FIRMWARE_MANIFEST_URL | RELEASE_VERSION | CHIP | DEVICE | PLATFORMIO_CORE_DIR) export "$key=$value" ;;
            *) die ".env: unsupported setting '$key' (only FIRMWARE_MANIFEST_URL, SIBLING_FIRMWARE_MANIFEST_URL, RELEASE_VERSION, CHIP, DEVICE, PLATFORMIO_CORE_DIR)" ;;
        esac
    done < "$ROOT/.env"
fi

# Keep PlatformIO's toolchains and Python env in this repo unless told otherwise:
# its default, ~/.platformio, is shared by every PlatformIO and ESPHome project
# on the machine, and a build here can recreate its Python env. A value from
# .env or the environment is anchored the same way, so the script finds the
# same folder whatever directory it runs from.
case "${PLATFORMIO_CORE_DIR:-}" in
    '') PLATFORMIO_CORE_DIR="$ROOT/.platformio-core" ;;
    '~') PLATFORMIO_CORE_DIR="$HOME" ;;
    '~/'*) PLATFORMIO_CORE_DIR="$HOME/${PLATFORMIO_CORE_DIR#'~/'}" ;;
    '~'*) die "PLATFORMIO_CORE_DIR (from .env or the environment): only a leading ~/ is expanded; use a full path instead of $PLATFORMIO_CORE_DIR" ;;
    /*) ;;
    *) PLATFORMIO_CORE_DIR="$ROOT/$PLATFORMIO_CORE_DIR" ;;
esac
export PLATFORMIO_CORE_DIR

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

# Each chip build's manifest file name. Keep in step with BUILDS in
# scripts/release/make_pages.py and the manifest URLs in .github/workflows/release.yml.
PRE_V3_MANIFEST=manifest.json
V3_MANIFEST=manifest-guition-esp32-p4-jc8012p4a1-v3.json

# -s flags must come before the ESPHome subcommand. With a chosen chip (CHIP_SEL,
# set by parse_args; the esphome passthrough has none), a manifest URL that
# names the other chip's manifest is refused, and one naming this chip's
# manifest gives the sibling URL when that isn't set.
substitutions() {
    local url="${FIRMWARE_MANIFEST_URL:-}" sibling="${SIBLING_FIRMWARE_MANIFEST_URL:-}" own other path
    SUBS=(-s firmware_version "${RELEASE_VERSION:-dev}")
    if [ -n "$url" ] && [ -n "${CHIP_SEL:-}" ]; then
        if [ "$CHIP_SEL" = v3 ]; then own="$V3_MANIFEST" other="$PRE_V3_MANIFEST"; else own="$PRE_V3_MANIFEST" other="$V3_MANIFEST"; fi
        # Compare the path (no query, fragment or trailing slash), but bake in
        # a custom URL as given.
        path="${url%%[?#]*}"
        path="${path%/}"
        case "$path" in
            */"$other") die "FIRMWARE_MANIFEST_URL names the other chip build's manifest ($other), whose image can't boot a $CHIP_SEL chip; for --chip $CHIP_SEL use .../$own" ;;
            */"$own")
                [ "$url" != "$path/" ] || url="$path"
                [ -n "$sibling" ] || sibling="${path%/*}/$other"
                ;;
        esac
    fi
    if [ -n "$url" ]; then
        SUBS+=(-s firmware_manifest_url "$url")
    fi
    if [ -n "$sibling" ]; then
        # The frame would move a saved sibling URL onto its own, an empty one here,
        # which turns update checks off.
        [ -n "$url" ] || die "SIBLING_FIRMWARE_MANIFEST_URL needs FIRMWARE_MANIFEST_URL: without it, a frame with the sibling saved would be moved onto no manifest (update checks off)"
        SUBS+=(-s sibling_firmware_manifest_url "$sibling")
    fi
}

# Sets CHIP_SEL and CONFIG from --chip (or CHIP), and DEV_ARGS from --device
# (or DEVICE).
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
    CHIP_SEL="$chip"
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
