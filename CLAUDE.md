# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repo Overview

NX monorepo for DIY ESP32 and Arduino firmware projects. Projects live under `apps/` (firmware/executables) and `libs/` (shared C/C++ libraries). NX is used for task orchestration only — the actual build tooling per project will typically be PlatformIO or Arduino CLI.

## Package Manager

Always use `pnpm` (for install/run) and `pnpm dlx` (the npx equivalent, for one-off remote packages). Never use `npm` or `npx`.

## Common Commands

```bash
# Install dependencies
pnpm install

# Run a target for all projects
pnpm nx run-many -t build
pnpm nx run-many -t test

# Run a target for a specific project
pnpm nx run <project>:build
pnpm nx run <project>:flash

# Run only affected projects (relative to master)
pnpm nx affected -t build

# Show all registered projects
pnpm nx show projects
```

## Adding a New Project

1. Create the project directory under `apps/<name>/` or `libs/<name>/`.
2. Add a `project.json` to register it with NX and define targets (`build`, `flash`, `test`, etc.).
3. For PlatformIO projects, targets typically wrap `pio run`, `pio run -t upload`, and `pio test` via `scripts/pio.sh` (see wrapper note below).

Example `project.json` for a PlatformIO app:

```json
{
  "name": "my-sensor",
  "targets": {
    "build": { "executor": "nx:run-commands", "options": { "command": "scripts/pio.sh run -d apps/my-sensor", "cwd": "." } },
    "flash": { "executor": "nx:run-commands", "options": { "command": "scripts/pio.sh run -d apps/my-sensor -t upload", "cwd": "." } },
    "test":  { "executor": "nx:run-commands", "options": { "command": "scripts/pio.sh test -d apps/my-sensor", "cwd": "." } }
  }
}
```

> **PlatformIO wrapper:** Always invoke PlatformIO via `scripts/pio.sh` (not `pio` directly). The wrapper uses the repo's `.venv/bin/python3` when available (local dev) and falls back to the system `python3` (CI). Use `cwd: "."` (workspace root) with the `-d <project-dir>` flag so the wrapper can locate itself reliably.

### Multi-board PlatformIO projects

When a single app supports multiple boards via separate `[env:...]` sections in `platformio.ini` (e.g. `cyd-clock` supports `cyd` and `freenove-s3`), follow this target layout:

- `build` — CI-facing, wraps `scripts/build-boards.sh <project-dir>`, which discovers every `[env:...]` section in the project's `platformio.ini` and builds each in turn. Adding a new board only requires a new `[env:...]` section — no script or CI changes needed.
- `build-<board>` — dev shortcut for a single board, wraps `scripts/pio.sh run -d <project-dir> -e <board>` directly.
- `flash-<board>` — dev shortcut for uploading to a single board. Always prefix with the board name, even for the primary/original board (e.g. `flash-cyd`), so all boards are named consistently.

Select the board-specific implementation (e.g. touch/display driver backend) at compile time via a per-env build flag macro (e.g. `-D BOARD_CYD=1` / `-D BOARD_FREENOVE_S3=1`), and guard each backend's source files with `#if defined(...)` so PlatformIO's library dependency finder doesn't try to compile backends against libraries that aren't in that env's `lib_deps`.

### Release workflow

`.github/workflows/release.yml` runs on every push to `master`. A project's `release` target (see `apps/cyd-clock/project.json`) wraps `tools/esp-flasher/generate_release.py`, producing a merged `.bin`, an OTA `.bin`, and an ESP Web Tools manifest entry per board under `dist/<project>/`. Two modes, one per board flag each:
- `--build <env>:<chip>` — PlatformIO boards (e.g. cyd-clock). The script itself merges bootloader+partitions+app via `esptool merge_bin`.
- `--esphome <env>:<chip>:<esphome_name>` — ESPHome boards (e.g. espframe). ESPHome's own build already produces a merged `firmware.factory.bin` and an app-only `firmware.bin`, so the script just copies them out of `builds/.esphome/build/<esphome_name>/.pioenvs/<esphome_name>/` instead of re-merging.

Both modes write the same output shape, so a project can mix them if it ever needs to. Filename suffixing differs between the two: `--build` only appends `-<env>` when there's more than one variant (a lone `--build` is a real, permanent single-board app). `--esphome` *always* appends `-<env>`, even with only one device wired in — ESPHome apps are expected to grow more devices over time (e.g. espframe's skipped P4), and an unsuffixed name would silently start meaning something different the moment a second device is added.

- **Pages deploy always rebuilds every project** (`nx run-many -t build,build-fs,release`) so the web flasher index at `dist/index.html` stays complete regardless of what changed in a given push — Pages replaces the whole site on each deploy, so an affected-only build would drop unaffected projects' flashers from the live site.
- **GitHub Release creation is gated on `nx affected`**, diffed against a `last-release` git tag (not `event.before`/the prior commit) — only projects with a `release` target that changed since the last commit that *actually* produced a release get their `.bin`s attached to the new one. The tag only advances after a successful "Create GitHub release" step, so a CI failure mid-build can't silently narrow the next run's diff window and drop changes from the release. A push that doesn't touch any firmware project's sources since then skips creating a release entirely.
- When adding a new board to an existing project, add its `--build <env>:<chip>` or `--esphome <env>:<chip>:<esphome_name>` flag and matching `dependsOn` entry to that project's `release` target.
- `dist/<project>/manifest.json` isn't just for the browser flasher (ESP Web Tools' `parts`) — each build entry also carries an `ota: {path, md5}` pointing at the app-only OTA binary. `libs/ota-update-client` (used by cyd-clock) polls this file directly on GitHub Pages rather than the GitHub Releases API, since the manifest regenerates fresh for every project on every push regardless of what else changed — a release that only contains another project's binaries can't make a device's update check fail the way it could when checking the Releases API for a matching asset.

## NX Guidelines

<!-- nx configuration start-->
<!-- Leave the start & end comments to automatically receive updates. -->

- For navigating/exploring the workspace, invoke the `nx-workspace` skill first - it has patterns for querying projects, targets, and dependencies
- When running tasks (for example build, lint, test, e2e, etc.), always prefer running the task through `nx` (i.e. `nx run`, `nx run-many`, `nx affected`) instead of using the underlying tooling directly
- Prefix nx commands with `pnpm` (e.g., `pnpm nx build`, `pnpm nx test`) - avoids using globally installed CLI
- You have access to the Nx MCP server and its tools, use them to help the user
- For Nx plugin best practices, check `node_modules/@nx/<plugin>/PLUGIN.md`. Not all plugins have this file - proceed without it if unavailable.
- NEVER guess CLI flags - always check nx_docs or `--help` first when unsure

### Scaffolding & Generators

- For scaffolding tasks (creating apps, libs, project structure, setup), ALWAYS invoke the `nx-generate` skill FIRST before exploring or calling MCP tools

### When to use nx_docs

- USE for: advanced config options, unfamiliar flags, migration guides, plugin configuration, edge cases
- DON'T USE for: basic generator syntax (`nx g @nx/react:app`), standard commands, things you already know
- The `nx-generate` skill handles generator discovery internally - don't call nx_docs just to look up generator syntax

<!-- nx configuration end-->
