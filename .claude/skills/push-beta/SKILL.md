---
name: push-beta
description: Build a firmware project's OTA binaries and publish them as a prerelease GitHub release tagged "-beta", so the user can flash-test a branch remotely without a build machine or USB access. Use when the user asks to "push to beta", "build a beta", "cut a beta release", or wants to test the current branch on-device without flashing over USB themselves.
allowed-tools: [Bash]
---

# Push Beta

Build one firmware app's OTA binaries from the current branch and publish them as a
prerelease GitHub release, so the user can install them from a device's config-portal
Update page without needing USB access or waiting for CI (CI's `release.yml` only builds
on push to `master`).

The argument is the project name (e.g. `cyd-clock`). If omitted and only one project in
the repo has a `release` target, use that one; if more than one does, ask which.

## Steps

1. **Confirm there's something to build.** Run `git status --short`. If there are
   uncommitted changes, show them to the user and ask whether to commit first (a beta
   build should generally come from committed, pushed code so the release notes can
   reference a real commit). If they want it committed, stage and commit with a
   conventional message, then push. **If they decline and uncommitted changes remain,
   stop here** — do not build or publish a release that doesn't match any real commit.

2. **Push the branch**, checking the upstream state first:
   ```bash
   git fetch origin
   git status -sb   # shows ahead/behind counts against the upstream, if any
   ```
   - No upstream yet: `git push -u origin $(git branch --show-current)`.
   - Upstream exists and is behind/even: `git push origin $(git branch --show-current)`.
   - Upstream exists and has diverged (local is behind, or both ahead and behind): stop
     and ask the user whether to rebase or merge before proceeding — don't push through
     a diverged branch or guess which resolution they want.

3. **Build the release artifacts** for the target project:
   ```bash
   RELEASE_VERSION="beta-$(git rev-parse --short HEAD)-$(date -u +%Y%m%d%H%M)" \
     pnpm nx run <project>:release
   ```
   This produces `dist/<project>/<project>-<env>-ota.bin` for every board the project's
   `project.json` `release` target builds (check `--build <env>:<chip>` flags there for
   the list of envs).

4. **Publish the prerelease.** Prefer a unique tag per run, e.g.
   `<branch-name>-beta-<short-sha>`, so each build gets its own release with no cleanup
   needed. If the user instead wants to reuse one tag per branch (overwriting the previous
   beta rather than accumulating history), delete both the release *and* its git tag first
   — `gh release delete <tag> --yes --cleanup-tag` — since `gh release delete` alone only
   removes the GitHub release and leaves the tag behind, which would make the next
   `gh release create <tag>` fail as a duplicate.
   ```bash
   gh release create <tag> \
     dist/<project>/<project>-*-ota.bin \
     --title "<Project display name> beta: <short description>" \
     --notes "Beta build from branch <branch> ($(git rev-parse --short HEAD)). <what changed>. Upload the *-ota.bin matching your board via the device's config portal Update page." \
     --prerelease \
     --target <branch-name>
   ```
   `--target` must be a ref that exists on the remote (i.e. after step 2's push), or
   `gh` will reject it with a 422.

5. **Report the release URL** and remind the user which `-ota.bin` matches which board.
