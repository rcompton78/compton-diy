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
# ESPHome 2026.6.4 (the version pinned in
# apps/espframe/product/contract/project.json) requires Python >=3.11, so it
# won't resolve under a 3.10 interpreter — pip silently excludes it from the
# candidate list rather than erroring, which looks identical to "this version
# doesn't exist" unless you read pip's own "ignored versions" output closely.
ESPFRAME_DIR="$(cd "$(dirname "$0")/.." && pwd)/apps/espframe"
if [ -d "$ESPFRAME_DIR/product/contract" ]; then
  ESPHOME_VERSION="$(python3 -c "import json; print(json.load(open('$ESPFRAME_DIR/product/contract/project.json'))['esphome_version'])")"
  ESPFRAME_VENV_DIR="$ESPFRAME_DIR/.venv"
  ESPFRAME_VENV_PYTHON="$ESPFRAME_VENV_DIR/bin/python"

  ESPFRAME_PYTHON_BIN=""
  for candidate in python3.13 python3.12 python3.11; do
    if command -v "$candidate" &>/dev/null; then
      ESPFRAME_PYTHON_BIN="$candidate"
      break
    fi
  done

  if [ -z "$ESPFRAME_PYTHON_BIN" ]; then
    echo "No python3.11/3.12/3.13 found; ESPHome $ESPHOME_VERSION requires Python >=3.11."
    if command -v apt-get &>/dev/null; then
      # Ubuntu's default repos only carry this far back as an rc build
      # (jammy/22.04) or not at all on older releases, so pull from deadsnakes.
      # Local dev convenience only -- CI should use actions/setup-python
      # instead, not this script (PPA + apt-get update is slow/network-flaky,
      # and CI already provisions Python explicitly per workflow).
      echo "Installing python3.12 from the deadsnakes PPA..."
      sudo apt-get install -y software-properties-common
      sudo add-apt-repository -y ppa:deadsnakes/ppa
      sudo apt-get update
      sudo apt-get install -y python3.12 python3.12-venv
      ESPFRAME_PYTHON_BIN="python3.12"
    elif command -v brew &>/dev/null; then
      echo "Installing python@3.12 via Homebrew..."
      brew install python@3.12
      ESPFRAME_PYTHON_BIN="python3.12"
    else
      echo "ERROR: No supported package manager found (apt/brew) to install Python 3.12." >&2
      echo "       Install python3.11+ manually, then re-run this script." >&2
      exit 1
    fi
  fi

  if ! "$ESPFRAME_VENV_PYTHON" -m esphome version &>/dev/null 2>&1; then
    echo "Creating virtualenv at apps/espframe/.venv ($ESPFRAME_PYTHON_BIN) and installing ESPHome $ESPHOME_VERSION..."
    "$ESPFRAME_PYTHON_BIN" -m venv "$ESPFRAME_VENV_DIR"
    "$ESPFRAME_VENV_DIR/bin/pip" install --quiet "esphome==$ESPHOME_VERSION"
    echo "ESPHome installed: $("$ESPFRAME_VENV_PYTHON" -m esphome version)"
  else
    echo "ESPHome already installed: $("$ESPFRAME_VENV_PYTHON" -m esphome version)"
  fi
else
  echo "Skipping ESPHome setup: apps/espframe not present."
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
