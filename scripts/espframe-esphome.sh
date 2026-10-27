#!/usr/bin/env bash
# Runs ESPHome at the version pinned in apps/espframe/product/contract/project.json
# (see the version-pin note below), installing it on first use if needed.
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
# set -- an entrypoint can hardcode its own top-level firmware_version, which
# would otherwise win over the package's "dev" default for any
# local/unversioned build, making the firmware_version=="dev" guards in
# common/addon/firmware_update.yaml and media-room-dashboard's dashboard.yaml
# treat an unversioned build as real and risk the exact auto-update loop they
# exist to prevent. apps/espframe/builds/*.factory.yaml used to ship "0.0.0"
# there (COM-427 changed it to "dev"); explicitly forcing "dev" here keeps
# that sentinel reliable for every entrypoint regardless of its own yaml
# default. Unlike
# pio.sh's exported-var pattern, there's no .env override here since
# RELEASE_VERSION is only ever meant to carry a real value in CI release
# builds.
SUBSTITUTION_ARGS=(-s firmware_version "${RELEASE_VERSION:-dev}")

# ESPHome version pin (COM-492). Fresh clones and worktrees have no
# apps/espframe/.venv (it's gitignored), and this wrapper used to fall back
# silently to whatever `esphome` was on PATH: a system 2025.2.2 then died deep
# in config loading with `ModuleNotFoundError: esphome.components.esp32_hosted`,
# which says nothing about the version. So the binary is resolved against the
# pin in apps/espframe/product/contract/project.json, first match wins:
#   1. this checkout's apps/espframe/.venv (setup.sh's; release.yml's symlink)
#   2. a shared, version-keyed venv outside the repo, reused by every clone and
#      worktree on the machine (~/.cache/compton-diy/esphome-<version>)
#   3. `esphome` on PATH (pr-build.yml pip-installs the pin there)
# With no match, the shared venv is created on the spot (a few minutes, once
# per machine per pin), so a fresh checkout builds and flashes with no setup
# step. Never in CI (CI is set): there the pin is always installed up front,
# and a mismatch is a workflow bug that should fail loudly. Set
# ESPHOME_SKIP_VERSION_CHECK=1 to run an unpinned ESPHome (e.g. to try a new
# release before bumping the pin).
CONTRACT_JSON="$REPO_ROOT/apps/espframe/product/contract/project.json"
SHARED_VENV_ROOT="${ESPHOME_VENV_CACHE:-${XDG_CACHE_HOME:-$HOME/.cache}/compton-diy}"

die() {
    echo "espframe-esphome.sh: $*" >&2
    exit 1
}

esphome_version_of() {
    "$1" version 2>/dev/null | awk '/^Version:/ {print $2}'
}

if [ "${ESPHOME_SKIP_VERSION_CHECK:-}" = "1" ]; then
    if [ -x "$VENV_ESPHOME" ]; then
        exec "$VENV_ESPHOME" "${SUBSTITUTION_ARGS[@]}" "$@"
    fi
    exec esphome "${SUBSTITUTION_ARGS[@]}" "$@"
fi

[ -f "$CONTRACT_JSON" ] || die "missing $CONTRACT_JSON"
# Parsed as JSON like every other reader of the pin (setup.sh, pr-build.yml,
# release.yml), so they can never disagree on it.
ESPHOME_PIN="$(python3 -c '
import json, sys
v = json.load(open(sys.argv[1])).get("esphome_version")
print(v if isinstance(v, str) else "")
' "$CONTRACT_JSON")" || die "could not parse $CONTRACT_JSON (needs python3 and valid JSON)"
[ -n "$ESPHOME_PIN" ] || die "no esphome_version string in $CONTRACT_JSON"
SHARED_VENV="$SHARED_VENV_ROOT/esphome-$ESPHOME_PIN"

FOUND=()
for candidate in "$VENV_ESPHOME" "$SHARED_VENV/bin/esphome" "$(command -v esphome 2>/dev/null)"; do
    [ -n "$candidate" ] && [ -x "$candidate" ] || continue
    # An unfinished shared venv (no .complete) goes to the locked install
    # below to be rebuilt, never run, even if its esphome already reports the pin.
    if [ "$candidate" = "$SHARED_VENV/bin/esphome" ] && [ ! -f "$SHARED_VENV/.complete" ]; then
        continue
    fi
    version="$(esphome_version_of "$candidate")"
    if [ "$version" = "$ESPHOME_PIN" ]; then
        exec "$candidate" "${SUBSTITUTION_ARGS[@]}" "$@"
    fi
    FOUND+=("${version:-unknown version} at $candidate")
done

if [ ${#FOUND[@]} -gt 0 ]; then
    found_msg="found $(IFS=';'; echo "${FOUND[*]}" | sed 's/;/, /g')"
else
    found_msg="no esphome found"
fi
mismatch_msg="ESPHome $ESPHOME_PIN required (apps/espframe/product/contract/project.json), $found_msg."

# GitHub Actions sets CI=true; CI=false/0 (a common local opt-out) isn't CI.
if [ -n "${CI:-}" ] && [ "${CI}" != "false" ] && [ "${CI}" != "0" ]; then
    die "$mismatch_msg Install the pinned version before building."
fi

PYTHON_BIN=""
for candidate in python3.13 python3.12; do
    if command -v "$candidate" &>/dev/null; then
        PYTHON_BIN="$candidate"
        break
    fi
done
[ -n "$PYTHON_BIN" ] || die "$mismatch_msg No python3.12/3.13 to install it with: run scripts/setup.sh."

# Serialized so two builds on a cold machine don't install into the same venv
# at once. A venv's scripts hardcode its path, so it can't be built elsewhere
# and moved in; instead .complete marks a finished install, and a venv
# without one (an interrupted install) is rebuilt. The lock is taken by
# Python's fcntl.flock on fd 9, inherited from this shell: a flock belongs to
# the open file, so it stays held after Python exits, until fd 9 closes. That
# works on macOS too, which has no flock(1). The wait is bounded so a stuck
# install elsewhere fails with a message rather than hanging every build.
mkdir -p "$SHARED_VENV_ROOT"
exec 9>"$SHARED_VENV.lock"
# Exit 3 means a real timeout; anything else (a bad ESPHOME_LOCK_TIMEOUT, a
# filesystem without flock support) is reported as a lock failure instead.
lock_rc=0
"$PYTHON_BIN" -c '
import fcntl, sys, time
try:
    deadline = time.monotonic() + float(sys.argv[1])
    while True:
        try:
            fcntl.flock(9, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            if time.monotonic() > deadline:
                sys.exit(3)
            time.sleep(1)
except (ValueError, OSError) as e:
    print(f"espframe-esphome.sh: {e}", file=sys.stderr)
    sys.exit(2)
' "${ESPHOME_LOCK_TIMEOUT:-1800}" || lock_rc=$?
case "$lock_rc" in
    0) ;;
    3) die "timed out after ${ESPHOME_LOCK_TIMEOUT:-1800}s waiting for another ESPHome install ($SHARED_VENV.lock); retry once it finishes" ;;
    *) die "could not lock $SHARED_VENV.lock (check ESPHOME_LOCK_TIMEOUT is a number of seconds and that $SHARED_VENV_ROOT supports flock)" ;;
esac
if [ ! -f "$SHARED_VENV/.complete" ] || [ "$(esphome_version_of "$SHARED_VENV/bin/esphome")" != "$ESPHOME_PIN" ]; then
    echo "$mismatch_msg" >&2
    echo "espframe-esphome.sh: installing ESPHome $ESPHOME_PIN into $SHARED_VENV (one-time, takes a few minutes)..." >&2
    rm -rf "$SHARED_VENV"
    "$PYTHON_BIN" -m venv "$SHARED_VENV" || die "failed to create $SHARED_VENV"
    "$SHARED_VENV/bin/pip" install --quiet "esphome==$ESPHOME_PIN" >&2 \
        || die "pip install esphome==$ESPHOME_PIN failed; see output above"
    [ "$(esphome_version_of "$SHARED_VENV/bin/esphome")" = "$ESPHOME_PIN" ] \
        || die "installed ESPHome in $SHARED_VENV does not report $ESPHOME_PIN"
    touch "$SHARED_VENV/.complete"
fi
exec 9>&-

exec "$SHARED_VENV/bin/esphome" "${SUBSTITUTION_ARGS[@]}" "$@"
