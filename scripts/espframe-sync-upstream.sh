#!/usr/bin/env bash
set -euo pipefail

# Syncs the apps/espframe submodule (rcompton78/espframe fork) with upstream
# jtenniswood/espframe, then records the new commit in compton-diy.

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SUBMODULE_DIR="$ROOT_DIR/apps/espframe"

cd "$SUBMODULE_DIR"

git remote get-url upstream &>/dev/null || \
  git remote add upstream https://github.com/jtenniswood/espframe.git

echo "Fetching upstream/main..."
git fetch upstream main

BRANCH="$(git branch --show-current)"
echo "Merging upstream/main into $BRANCH..."
git merge upstream/main

echo "Pushing $BRANCH to fork (origin)..."
git push origin "$BRANCH"

cd "$ROOT_DIR"
if ! git diff --quiet -- apps/espframe; then
  git add apps/espframe
  git commit -m "chore: sync espframe submodule with upstream"
  echo "Recorded new espframe submodule commit. Review and push compton-diy when ready."
else
  echo "Submodule pointer unchanged (already up to date)."
fi
