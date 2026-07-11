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
   conventional message, then push.

2. **Push the branch** if it has no upstream yet or is behind:
   ```bash
   git push -u origin $(git branch --show-current)
   ```

3. **Build the release artifacts** for the target project:
   ```bash
   RELEASE_VERSION="beta-$(git rev-parse --short HEAD)-$(date -u +%Y%m%d%H%M)" \
     pnpm nx run <project>:release
   ```
   This produces `dist/<project>/<project>-<env>-ota.bin` for every board the project's
   `project.json` `release` target builds (check `--build <env>:<chip>` flags there for
   the list of envs).

4. **Publish the prerelease.** Pick a tag distinct per run, e.g.
   `<branch-name>-beta` (reuse the same tag across pushes on the same branch so
   `gh release create` naturally errors on a duplicate — in that case use
   `gh release delete <tag> --yes` first, or just append a short timestamp to keep tags
   unique; prefer the reuse approach unless the user wants history of every beta build).
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
