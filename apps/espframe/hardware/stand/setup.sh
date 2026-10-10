#!/usr/bin/env bash
# Create or refresh this project's CAD venv (build123d + preview deps).
#
# The venv is rebuilt only when requirements.txt changes: its hash is stored in
# .venv/.requirements.sha256 and compared on every run. build123d's OCP wheels are
# ~150 MB, so this stays a local-only step and is never run by CI or `pnpm install`.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
VENV="$HERE/.venv"
REQ="$HERE/requirements.txt"
STAMP="$VENV/.requirements.sha256"
want="$(sha256sum "$REQ" | cut -d' ' -f1)"

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
rm -rf "$VENV"
"$PY" -m venv "$VENV"
"$VENV/bin/pip" install --quiet --upgrade pip
"$VENV/bin/pip" install --quiet -r "$REQ"
echo "$want" > "$STAMP"
echo "CAD venv ready"
