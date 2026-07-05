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

# Remind about dialout group (non-fatal)
if ! groups | grep -q dialout; then
  echo ""
  echo "NOTE: Your user is not in the 'dialout' group."
  echo "      Run: sudo usermod -a -G dialout \$USER"
  echo "      Then log out and back in to access /dev/ttyUSB0 without sudo."
fi

echo ""
echo "Setup complete. Run 'pnpm nx run cyd-clock:build' to verify."
