#!/usr/bin/env bash
# Generates builds/secrets.yaml from builds/secrets.yaml.example, substituting
# each key with a same-named (upper-cased) environment variable when set --
# e.g. `wifi_ssid` picks up $WIFI_SSID -- and falling back to the placeholder
# value from the .example file otherwise.
#
# In CI (release.yml), the real secrets are exported as env vars from GitHub
# Actions repo secrets before this runs, so the published/OTA binary gets
# real credentials baked in. In PR builds (pr-build.yml) and any other
# context where those env vars aren't set, every key falls back to its
# placeholder and the config still compiles cleanly.
#
# If builds/secrets.yaml already exists (a developer's own local file, never
# committed), it's left untouched -- this script only ever generates one from
# scratch, never overwrites a real local secrets file.
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../builds" && pwd)"
EXAMPLE="$DIR/secrets.yaml.example"
OUT="$DIR/secrets.yaml"

if [ -f "$OUT" ]; then
    echo "==> $OUT already exists, leaving it as-is"
    exit 0
fi

echo "==> Generating $OUT from environment (falling back to example placeholders)"
: > "$OUT"
while IFS= read -r line; do
    if [[ "$line" =~ ^([A-Za-z0-9_]+):.*$ ]]; then
        key="${BASH_REMATCH[1]}"
        env_name="$(echo "$key" | tr '[:lower:]' '[:upper:]')"
        env_value="${!env_name:-}"
        if [ -n "$env_value" ]; then
            printf '%s: "%s"\n' "$key" "$env_value" >> "$OUT"
            continue
        fi
    fi
    echo "$line" >> "$OUT"
done < "$EXAMPLE"
