#!/usr/bin/env bash
# Exits 0 if this run's commit may be published as the newest release: it is the
# head of the default branch, or the commits after it together only change files that
# release.yml's paths-ignore skips (README.md, Markdown under docs/, .gitignore,
# files under .claude/), which
# start no release of their own.
#
# release.yml runs it first in each job that publishes (preflight, release,
# pages), since "Re-run failed jobs" re-runs those without re-running preflight.
# Otherwise re-running an old run would publish an older build as the newest,
# and every auto-updating frame would install it.
#
# Needs GITHUB_REPOSITORY, GITHUB_SHA, DEFAULT_BRANCH and GH_TOKEN.
set -euo pipefail

repo="${GITHUB_REPOSITORY:?}"
sha="${GITHUB_SHA:?}"
branch="${DEFAULT_BRANCH:?}"

head="$(gh api "repos/$repo/commits/$branch" --jq .sha)"
if [ "$head" = "$sha" ]; then
  exit 0
fi

compare="$(gh api "repos/$repo/compare/$sha...$head")"
if [ "$(jq -r .status <<<"$compare")" = "ahead" ]; then
  # Both names of a rename count. GitHub lists at most 300 files: past that,
  # or with no list at all, refuse rather than guess.
  if ! files="$(jq -r 'if (.files | type) != "array" or (.files | length) >= 300 then error("no usable file list")
                      else .files[] | .filename, (.previous_filename // empty) end' <<<"$compare")"; then
    echo "::error title=Not the newest commit::Could not read the files changed after $sha. Run the Release workflow on $branch instead of re-running this run."
    exit 1
  fi
  # Keep in step with release.yml's on.push.paths-ignore
  # (README.md, docs/**/*.md, .gitignore, .claude/**).
  others="$(grep -Ev '^(README\.md|docs/.*\.md|\.gitignore|\.claude/.*)$' <<<"$files" || true)"
  if [ -n "$files" ] && [ -z "$others" ]; then
    exit 0
  fi
fi

echo "::error title=Not the newest commit::$sha is behind the head of $branch ($head), which changes firmware or stand files. Run the Release workflow on $branch instead of re-running this run."
exit 1
