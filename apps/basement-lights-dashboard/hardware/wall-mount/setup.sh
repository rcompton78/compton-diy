#!/usr/bin/env bash
# Create or refresh this project's CAD venv (build123d + preview deps).
#
# The venv is rebuilt only when requirements.txt changes: its hash is stored in
# .venv/.requirements.sha256 and compared on every run. build123d's OCP wheels are
# ~150 MB, so `pnpm install` never runs this; only the cad/release targets do (CI's
# release workflow runs it on the self-hosted runner, where the venv is cached).
#
# On that runner .venv is a symlink to a persistent dir outside the checkout, so
# resolve it first: `python -m venv` refuses a symlinked target, and `rm -rf` on
# the link would only remove the link.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# BSD readlink may have no -f, so fall back to Python's realpath.
VENV="$(readlink -f "$HERE/.venv" 2>/dev/null)" \
  || VENV="$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$HERE/.venv" 2>/dev/null)" \
  || VENV=""
if [ -z "$VENV" ]; then
  if [ -L "$HERE/.venv" ]; then
    echo "ERROR: can't resolve the .venv symlink at $HERE/.venv" >&2
    exit 1
  fi
  VENV="$HERE/.venv"
fi
REQ="$HERE/requirements.txt"
STAMP="$VENV/.requirements.sha256"
if command -v sha256sum >/dev/null 2>&1; then
  want="$(sha256sum "$REQ" | cut -d' ' -f1)"
elif command -v shasum >/dev/null 2>&1; then
  want="$(shasum -a 256 "$REQ" | cut -d' ' -f1)"   # macOS / BSD
else
  echo "ERROR: need sha256sum or shasum to hash $REQ" >&2
  exit 1
fi

if [ -x "$VENV/bin/python" ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$want" ]; then
  echo "CAD venv up to date ($VENV)"
  exit 0
fi

PY=""
for candidate in python3.13 python3.12 python3.11 python3.10; do
  if command -v "$candidate" &>/dev/null; then PY="$candidate"; break; fi
done
if [ -z "$PY" ]; then
  echo "ERROR: build123d needs Python 3.10–3.13; none found on PATH." >&2
  exit 1
fi

echo "Creating CAD venv with $PY at $VENV"
# A mis-pointed symlink must not take anything else with it: never delete the
# filesystem root, the home dir, or a dir that contains this project.
case "$HERE/" in
  "$VENV"/*) unsafe=1 ;;
  *) unsafe=0 ;;
esac
if [ "$unsafe" = 1 ] || [ "$VENV" = "/" ] || [ "$VENV" = "$HOME" ]; then
  echo "ERROR: refusing to rm -rf '$VENV'; check the .venv symlink." >&2
  exit 1
fi
rm -rf "$VENV"
"$PY" -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet -r "$REQ"
echo "$want" > "$STAMP"
echo "CAD venv ready"
