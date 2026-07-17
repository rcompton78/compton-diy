#!/usr/bin/env bash
set -euo pipefail

# Install Python 3 (+ venv support) if missing
if command -v apt-get &>/dev/null; then
  PKGS=()
  command -v python3 &>/dev/null || PKGS+=(python3 python3-pip)
  dpkg -s python3-venv &>/dev/null 2>&1 || PKGS+=(python3-venv)
  if [ ${#PKGS[@]} -gt 0 ]; then
    echo "Installing: ${PKGS[*]}"
    sudo apt-get update && sudo apt-get install -y "${PKGS[@]}"
  fi
elif ! command -v python3 &>/dev/null; then
  if command -v brew &>/dev/null; then
    echo "Installing Python 3..."
    brew install python3
  else
    echo "ERROR: Cannot install Python 3 — no supported package manager found (apt/brew)." >&2
    exit 1
  fi
fi
echo "Python 3: $(python3 --version)"

# Install PlatformIO into a local venv if missing
VENV_DIR="$(cd "$(dirname "$0")/.." && pwd)/.venv"
VENV_PYTHON="$VENV_DIR/bin/python"

if ! "$VENV_PYTHON" -m platformio --version &>/dev/null 2>&1; then
  echo "Creating virtualenv at .venv and installing PlatformIO..."
  python3 -m venv "$VENV_DIR"
  "$VENV_DIR/bin/pip" install --quiet platformio
  echo "PlatformIO installed."
else
  echo "PlatformIO already installed: $("$VENV_PYTHON" -m platformio --version)"
fi

# Install ESPHome into its own local venv (separate from the PlatformIO one
# above, since ESPHome pins its own compatible PlatformIO version internally)
# if missing.
#
# NOTE: apps/espframe/product/contract/project.json pins esphome_version
# 2026.6.4 for the fork's own Docker-based CI, but that version (and one of
# its own pinned deps, aioesphomeapi==45.3.1) isn't published on PyPI yet, so
# it can't be pip-installed. Installing latest instead for local dev — this
# may drift from what CI actually builds until PyPI catches up, so re-verify
# on hardware after flashing (this is what fixed the display-init bug).
ESPFRAME_DIR="$(cd "$(dirname "$0")/.." && pwd)/apps/espframe"
if [ -d "$ESPFRAME_DIR/product/contract" ]; then
  ESPFRAME_VENV_DIR="$ESPFRAME_DIR/.venv"
  ESPFRAME_VENV_PYTHON="$ESPFRAME_VENV_DIR/bin/python"

  if ! "$ESPFRAME_VENV_PYTHON" -m esphome version &>/dev/null 2>&1; then
    echo "Creating virtualenv at apps/espframe/.venv and installing latest ESPHome..."
    python3 -m venv "$ESPFRAME_VENV_DIR"
    "$ESPFRAME_VENV_DIR/bin/pip" install --quiet esphome
    echo "ESPHome installed: $("$ESPFRAME_VENV_PYTHON" -m esphome version)"
  else
    echo "ESPHome already installed: $("$ESPFRAME_VENV_PYTHON" -m esphome version)"
  fi
else
  echo "Skipping ESPHome setup: apps/espframe submodule not initialized yet (run 'git submodule update --init')."
fi

# Remind about dialout group (non-fatal)
if ! groups | grep -q dialout; then
  echo ""
  echo "NOTE: Your user is not in the 'dialout' group."
  echo "      Run: sudo usermod -a -G dialout \$USER"
  echo "      Then log out and back in to access /dev/ttyUSB0 without sudo."
fi

echo ""
echo "Setup complete. Run 'pnpm nx run cyd-clock:build' to verify."
