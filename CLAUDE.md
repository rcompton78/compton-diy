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
- `flash-<board>` — dev shortcut for uploading to a single board (the default board, if there's a primary one, can keep the unprefixed `flash` name).

Select the board-specific implementation (e.g. touch/display driver backend) at compile time via a per-env build flag macro (e.g. `-D BOARD_CYD=1` / `-D BOARD_FREENOVE_S3=1`), and guard each backend's source files with `#if defined(...)` so PlatformIO's library dependency finder doesn't try to compile backends against libraries that aren't in that env's `lib_deps`.

### Release workflow

`.github/workflows/release.yml` runs on every push to `master`. A project's `release` target (see `apps/cyd-clock/project.json`) wraps `tools/esp-flasher/generate_release.py` with one `--build <env>:<chip>` flag per supported board, producing a merged `.bin`, an OTA `.bin`, and an ESP Web Tools manifest entry per board under `dist/<project>/`.

- **Pages deploy always rebuilds every project** (`nx run-many -t build,build-fs,release`) so the web flasher index at `dist/index.html` stays complete regardless of what changed in a given push — Pages replaces the whole site on each deploy, so an affected-only build would drop unaffected projects' flashers from the live site.
- **GitHub Release creation is gated on `nx affected`** — only projects with a `release` target that actually changed since the prior commit on `master` (via `nx show projects --affected --withTarget release`) get their `.bin`s attached to the new release. A push that doesn't touch any firmware project's sources skips creating a release entirely.
- When adding a new board to an existing project, add its `--build <env>:<chip>` flag and matching `dependsOn` entry to that project's `release` target.

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
