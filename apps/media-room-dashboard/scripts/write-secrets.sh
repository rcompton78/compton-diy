#!/usr/bin/env bash
# Generates builds/secrets.yaml from builds/secrets.yaml.example, substituting
# each key with an environment variable when set and falling back to the
# placeholder value from the .example file otherwise. Each key is looked up
# as HA_<UPPERCASED_KEY> first, then <UPPERCASED_KEY> -- e.g.
# `api_encryption_key` picks up $HA_API_ENCRYPTION_KEY, else
# $API_ENCRYPTION_KEY.
#
# The HA_ prefix is the local-dev name: it matches how the real key is kept
# on dev machines (bws `shared/HA_API_ENCRYPTION_KEY`), so a host that
# exports it builds firmware HA can actually connect to. Without it, a local
# build silently bakes in the example's public placeholder key, HA's native
# API handshake fails, and every button's service call is dropped.
#
# In CI (release.yml), the real secrets are exported as the unprefixed env
# vars from GitHub Actions repo secrets before this runs, so the published/OTA binary gets
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

if [ ! -f "$EXAMPLE" ]; then
    echo "==> $EXAMPLE not found" >&2
    exit 1
fi

echo "==> Generating $OUT from environment (falling back to example placeholders)"
TMP="$OUT.tmp"
trap 'rm -f "$TMP"' EXIT
umask 077
: > "$TMP"
while IFS= read -r line; do
    if [[ "$line" =~ ^([A-Za-z0-9_]+):.*$ ]]; then
        key="${BASH_REMATCH[1]}"
        env_name="$(echo "$key" | tr '[:lower:]' '[:upper:]')"
        ha_env_name="HA_$env_name"
        env_value="${!ha_env_name:-${!env_name:-}}"
        if [ -n "$env_value" ]; then
            escaped="$(printf '%s' "$env_value" | sed 's/\\/\\\\/g; s/"/\\"/g; s/\t/\\t/g' | sed ':a;N;$!ba;s/\n/\\n/g')"
            printf '%s: "%s"\n' "$key" "$escaped" >> "$TMP"
            continue
        fi
    fi
    echo "$line" >> "$TMP"
done < "$EXAMPLE"
mv "$TMP" "$OUT"
trap - EXIT
