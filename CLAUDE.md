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

> **PlatformIO wrapper:** Always invoke PlatformIO via `scripts/pio.sh` (not `pio` directly). The wrapper uses the repo's `.venv/bin/python3` when available (local dev) and falls back to the system `python3` (CI). Use `cwd: "."` (workspace root) with the `-d <project-dir>` flag so the wrapper can locate itself reliably. The wrapper also pre-creates its `PLATFORMIO_CORE_DIR` (`.platformio-core`) before exec'ing PlatformIO: PlatformIO's first-run init calls `os.makedirs` without `exist_ok`, so two PlatformIO projects built in parallel on a cold runner (e.g. `pr-build.yml`'s `affected -t build` once more than one PlatformIO app is affected) would otherwise race and one would crash with `FileExistsError`.
>
> **Build-time secrets/config via env vars + local `.env` override:** Follow the `RELEASE_VERSION`/`CAT_BUDDY_API_TOKEN` pattern (see `apps/cyd-clock`) for any value that needs to be baked into firmware at build time via a `-D NAME=\"${sysenv.NAME}\"` flag in `platformio.ini`. `scripts/pio.sh` exports a sensible default for each such var (e.g. `"${NAME:-default}"`) so CI and local builds work without extra setup, then auto-sources a gitignored `<project-dir>/.env` (plain `VAR=value` lines) if one exists, letting a developer override any of them locally without touching tracked files — useful for pointing a build at a local/dev backend instead of the real prod one. `.env` values win over exported shell env vars. Because PlatformIO's own incremental build (SCons) tracks build flags as part of its dependency signature, prefer `.env` over exporting the var in your shell when doing more than a single build+flash in one command — env vars exported in one shell/tool call don't persist to a separate one, and a later incremental rebuild (e.g. from a `flash` target that reruns `pio run -t upload`) can silently pick up an empty/default value and re-bake it in.
>
> **ESPHome projects (`apps/espframe`, `apps/media-room-dashboard`) thread `RELEASE_VERSION` the same way, via `scripts/espframe-esphome.sh`** instead of a `platformio.ini` `-D` flag: the wrapper injects ESPHome's own top-level `-s firmware_version "$RELEASE_VERSION"` substitution override (must precede the subcommand, e.g. `esphome -s firmware_version 1.2.3 compile ...`) whenever `RELEASE_VERSION` is set, so it lands in each project's `firmware_version` substitution (`esphome.project.version`, compared against the published manifest by the `update:`/`ota:` OTA-check components). Unset (local dev), it falls back to each yaml's own hardcoded `firmware_version: "dev"` default. Each affected `build-<board>` nx target must list `{ "env": "RELEASE_VERSION" }` in its `inputs` so a `RELEASE_VERSION` change actually invalidates the Nx cache — the same staleness footgun as the SCons one above, just at the Nx layer: without it, a build cached under one `RELEASE_VERSION` (e.g. a prior release, on the self-hosted runner's persistent cache) could be silently reused for a later run with a different one. Any `on_update_available` automation that calls `update.perform` must also skip when `firmware_version == "dev"` (`strcmp("${firmware_version}", "dev") != 0`) — an unversioned build always mismatches the published manifest and would otherwise auto-install (and, since the newly-flashed build is also unversioned, loop) forever.

> **ESPHome `ili9xxx` `model: CUSTOM` `init_sequence` must end with `- [0x00]`.** ESPHome packs the sequence into a vector of `[cmd, len, args...]` without the trailing 0 that `ILI9XXXDisplay::init_lcd_()` loops until, so the driver reads past the end and sends whatever heap bytes follow to the panel as extra commands. The heap layout shifts with any config change, so a board that worked can come up black (backlight, LVGL and the main loop all still running) after an unrelated edit — COM-213 hit this on media-room-dashboard and first misread it as a hang caused by `captive_portal`. Turning on `logger: logs: ili9xxx: VERBOSE` shows every init command sent; anything after the final `0x29` DISPON means the terminator is missing.

### Multi-board PlatformIO projects

When a single app supports multiple boards via separate `[env:...]` sections in `platformio.ini` (e.g. `cyd-clock` supports `cyd` and `freenove-s3`), follow this target layout:

- `build` — CI-facing, wraps `scripts/build-boards.sh <project-dir>`, which discovers every `[env:...]` section in the project's `platformio.ini` and builds each in turn. Adding a new board only requires a new `[env:...]` section — no script or CI changes needed.
- `build-<board>` — dev shortcut for a single board, wraps `scripts/pio.sh run -d <project-dir> -e <board>` directly.
- `flash-<board>` — dev shortcut for uploading to a single board. Always prefix with the board name, even for the primary/original board (e.g. `flash-cyd`), so all boards are named consistently.

Select the board-specific implementation (e.g. touch/display driver backend) at compile time via a per-env build flag macro (e.g. `-D BOARD_CYD=1` / `-D BOARD_FREENOVE_S3=1`), and guard each backend's source files with `#if defined(...)` so PlatformIO's library dependency finder doesn't try to compile backends against libraries that aren't in that env's `lib_deps`. Shared libs are used across apps (`libs/touch-driver` serves cyd-clock's `BOARD_CYD`/`BOARD_FREENOVE_S3` and tamagotchi-plus's `BOARD_WAVESHARE_S3_169`), so guard each backend on its own board macro (`#if defined(BOARD_X)`), never a negation like `#if !defined(BOARD_Y)`, which silently turns the backend on for every new board.

### Wi-Fi provisioning (tamagotchi-plus)

`apps/tamagotchi-plus` runs a WiFiManager captive portal in **non-blocking** mode (`setConfigPortalBlocking(false)` + `wm.process()` in `loop()`) so Improv serial (`improvSerial.handleSerial()`) can be serviced at the same time. That's what lets the ESP Web Tools flasher's post-install "Connect to Wi-Fi" step work, since ESP Web Tools auto-detects Improv and needs no manifest/template change. One gotcha: on ESP32, WiFiManager's portal-save path leaves `WiFi.persistent(false)` set afterwards, so Improv's connect is wrapped with `setCustomConnectWiFi()` to force `WiFi.persistent(true)` around `WiFi.begin`. Without that, credentials provisioned via Improv after a portal save wouldn't survive a reboot.

### Sprite assets (tamagotchi-plus)

`apps/tamagotchi-plus/assets/<name>.aseprite` is the editable source, but the firmware is built from the committed Aseprite exports next to it (`<name>.png` sheet + `<name>.json` json-array data). The `gen-assets` target (`tools/sprites/convert.py`, stdlib-only Python) turns them into the gitignored `include/generated/sprite_assets.h`, and `build`/`build-<board>`/`flash-<board>` all `dependsOn` it. Aseprite must never run in CI or in any build-path target: its license forbids redistributing the binary. Only the local-only `export-sprites` target calls it. So whenever a `.aseprite` changes, re-export and commit the sheet + JSON. The converter runs `--strict` in the target, so any pixel outside the locked `assets/palette.hex` fails the build. The build targets' `inputs` list `assets/*` and `convert.py` directly, and exclude `include/generated/**`, so an export change invalidates the Nx cache for the firmware too.

The egg (`egg.aseprite` + `eggshell.aseprite`) is **generated** by `tools/sprites/gen-egg.py`, not drawn by hand or by PixelLab. Its six crack stages must share a *pixel-identical* shell — if the shell shifts even slightly between stages the egg appears to breathe as it cracks — and per-stage AI generation cannot hold that. The script also carries a second `SHELL_SPECIAL` preset (`--special`) for a variant egg that reuses every crack stage unchanged. After re-running it, re-import via `png-to-aseprite.lua` and re-export, as with any other sprite. Two conventions the egg art has to keep: the outline is 100% navy (slot 1) like every other sprite, and no sprite pixel may use lavender `83769c` — that is the sky, so it makes the silhouette dissolve into the background.

Whole-sprite motion (the egg rocking on tap, its idle rock, the hatch shake) is a **draw-position offset in `PetScene`, not extra sprite frames**: six crack stages times a few wobble frames each would have cost more flash than the crack stages themselves, and an offset makes every stage rock for free. Only motion that changes pixels — the blink, the tumbling lid — is authored as frames.

**TFT_eSPI DMA on the ESP32-S3 needs a workaround** (see `finishDma()` in `apps/tamagotchi-plus/src/FrameRenderer.cpp`). TFT_eSPI 2.5.43's end-of-transfer callback clears `SPI_DMA_CONF_REG(spi_host)`, but this Arduino core's own `REG_SPI_BASE` wins over TFT_eSPI's `#ifndef`-guarded copy and maps `SPI3_HOST` (== 2) to GPSPI2 instead of GPSPI3. So after the first `pushImageDMA()` the display bus stays in DMA-TX mode, and every later direct register write (`setAddrWindow`, any plain `tft.draw*()`) goes out as garbage. The panel silently keeps the last pre-DMA image while serial shows frames being "pushed" normally. Any code that uses TFT_eSPI DMA on an S3 must `dmaWait()` and then clear `SPI_DMA_TX_ENA | SPI_DMA_RX_ENA` on `SPI_DMA_CONF_REG(SPI_PORT)` before the next direct write.

### Release workflow

`.github/workflows/release.yml` runs on every push to `master`. A project's `release` target (see `apps/cyd-clock/project.json`) wraps `tools/esp-flasher/generate_release.py`, producing a merged `.bin`, an OTA `.bin`, and an ESP Web Tools manifest entry per board under `dist/<project>/`. Two modes, one per board flag each:
- `--build <env>:<chip>` — PlatformIO boards (e.g. cyd-clock). The script itself merges bootloader+partitions+app via `esptool merge_bin`.
- `--esphome <env>:<chip>:<esphome_name>` — ESPHome boards (e.g. espframe). ESPHome's own build already produces a merged `firmware.factory.bin` and an app-only `firmware.bin`, so the script just copies them out of `builds/.esphome/build/<esphome_name>/.pioenvs/<esphome_name>/` instead of re-merging.

Both modes write the same output shape, so a project can mix them if it ever needs to. Filename suffixing differs between the two: `--build` only appends `-<env>` when there's more than one variant (a lone `--build` is a real, permanent single-board app). `--esphome` *always* appends `-<env>`, even with only one device wired in — ESPHome apps are expected to grow more devices over time (e.g. espframe went from Freenove-only to also shipping Waveshare and the Guition P4), and an unsuffixed name would silently start meaning something different the moment a second device is added.

- **Pages deploy always rebuilds every project** (`nx run-many -t build,build-fs,release`) so the web flasher index at `dist/index.html` stays complete regardless of what changed in a given push — Pages replaces the whole site on each deploy, so an affected-only build would drop unaffected projects' flashers from the live site. `build-and-release` intentionally still uses `run-many`, not `affected`, for this reason — swapping it would reintroduce that bug. Speed instead comes from `NX_CACHE_DIRECTORY` **and** `NX_WORKSPACE_DATA_DIRECTORY` both pointing at persistent, external dirs (see below) — Nx needs both to actually reuse a cache hit: `NX_CACHE_DIRECTORY` holds the task outputs, but `NX_WORKSPACE_DATA_DIRECTORY` holds the project-graph/task-hasher bookkeeping Nx uses to *recognize* those outputs as valid. Setting only the former (an early version of this pipeline's mistake) leaves the latter at its default in-repo location, which `actions/checkout`'s clean step wipes every run — Nx then rebuilds its workspace-data from scratch and reports the persisted cache dir's contents as "Unrecognized Cache Artifacts," unable to use them. With both persisted, untouched projects come back as instant Nx cache hits rather than real recompiles, so `run-many` stays fast without needing to narrow which projects it asks for.
- **`build-and-release` runs on the self-hosted `home-diy` runner** (label `compton-diy`), not `ubuntu-latest` — its `_work` directory persists across runs (unlike GitHub-hosted VMs, which are wiped every job), which is what makes the Nx cache actually pay off. `pr-build.yml` stays on `ubuntu-latest` deliberately: it triggers on plain `pull_request`, and since this repo is public, anyone can open a fork PR — running that on a homelab-hosted runner would mean arbitrary external code execution on the home network. Only `push`-to-`master` (which requires write access) runs self-hosted.
  - `actions/checkout`'s default `clean: true` runs `git clean -ffdx` before checkout, which deletes untracked *and gitignored* files — on a persistent runner that would wipe `.venv`, `apps/espframe/.venv`, `.platformio-core`, etc. on every run and defeat the point of self-hosting. The `Prepare persistent cache directories` step works around this by symlinking those repo-relative paths (which `scripts/pio.sh`/`scripts/espframe-esphome.sh` already default to) onto external targets under `/hdd/compton-diy/` on the runner host, recreated each run — the symlink itself gets cleaned, but the directory it points at doesn't.
  - The PlatformIO and ESPHome `.venv`s and `.platformio-core` dirs are kept as *separate* external targets (`venv-root` vs `venv-espframe`, `platformio-core-root` vs `platformio-core-espframe`) — never point both at the same path. A shared PlatformIO core dir once let cyd-clock's and espframe's builds silently clobber each other's cached toolchain/framework package versions.
  - `.platformio-core` doesn't need any staleness check: PlatformIO's core dir is itself version-keyed, so a `platformio.ini` bump just adds the new package version alongside the old one. `apps/espframe/.venv` does need one — a venv holds exactly one installed `esphome` version, so the "Install ESPHome" step compares the installed version against `project.json`'s pinned `esphome_version` and rebuilds the venv from scratch on a mismatch, rather than silently continuing to run a stale version forever.
  - **`python -m venv` must never be pointed at a symlinked path, even one that resolves fine.** CPython's own `venv` module special-cases this: `ensure_directories`/`create_if_needed` in `venv/__init__.py` raises `Unable to create directory` if the target path already exists as a symlink, regardless of what it resolves to — it's a deliberate refusal, not a bug that a pre-existing/pre-`mkdir`'d target fixes. So `.venv` and `apps/espframe/.venv` are created by pointing `python3.12 -m venv` directly at the real path behind the symlink (`/hdd/compton-diy/venv-root` / `/hdd/compton-diy/venv-espframe`), never at the repo-relative symlinked path — the symlink is only ever used afterward for plain file access (`pip install`, `esphome version`, etc.), which has no such restriction. PlatformIO's own package writes under `.platformio-core` don't have this issue since it isn't CPython's venv module — no equivalent workaround needed there.
  - `actions/setup-python` is dropped entirely on this runner — `python3.12` is provisioned once on the host (via `scripts/setup.sh`'s deadsnakes install) rather than per-run, the same way GitHub-hosted images come pre-provisioned. `actions/checkout`/`setup-node`/`pnpm-action-setup`/the two Pages actions are kept (matching the `compton-apps` precedent of not hand-rolling first-party actions even on self-hosted) but pinned to an exact commit SHA rather than a floating major-version tag, for supply-chain hardening.
- **GitHub Release creation is gated on `nx affected`**, diffed against a `last-release` git tag (not `event.before`/the prior commit) — only projects with a `release` target that changed since the last commit that *actually* produced a release get their `.bin`s attached to the new one. The tag only advances after a successful "Create GitHub release" step, so a CI failure mid-build can't silently narrow the next run's diff window and drop changes from the release. A push that doesn't touch any firmware project's sources since then skips creating a release entirely.
- When adding a new board to an existing project, add its `--build <env>:<chip>` or `--esphome <env>:<chip>:<esphome_name>` flag and matching `dependsOn` entry to that project's `release` target.
- `dist/<project>/manifest.json` isn't just for the browser flasher (ESP Web Tools' `parts`) — each build entry also carries an `ota: {path, md5}` pointing at the app-only OTA binary. `libs/ota-update-client` (used by cyd-clock and tamagotchi-plus) polls this file directly on GitHub Pages rather than the GitHub Releases API, since the manifest regenerates fresh for every project on every push regardless of what else changed — a release that only contains another project's binaries can't make a device's update check fail the way it could when checking the Releases API for a matching asset.
- **Build-time secrets (`RELEASE_VERSION`, `CAT_BUDDY_API_TOKEN`, etc.) must be set in the `env:` of every workflow step that can trigger a rebuild, not just the primary "Build all firmware" step.** A `release` target's `dependsOn` (e.g. `build-cyd`/`build-freenove-s3`) is a *different* nx target from `build` even though both ultimately invoke the same `pio.sh` command — nx doesn't treat them as already-satisfied by each other, so the "Generate releases" step re-triggers a real PlatformIO recompile. If that step's env is missing a secret the first step had, PlatformIO's build-flag signature changes and SCons silently recompiles with the missing var defaulted to empty, clobbering the correctly-built binary right before `generate_release.py` packages it — a release-only bug that's invisible in local/dev builds.
- **espframe's `build-<board>` targets and its `build` wrapper set `"parallelism": false`** (see `apps/espframe/project.json`). All of espframe's ESPHome boards share one ESP-IDF managed-component cache (`apps/espframe/builds/.esphome/.espressif/service_*/`), and when `espframe:release`'s `dependsOn` let Nx run them concurrently they raced on unpacking `lvgl/lvgl` (`OSError: [Errno 39] Directory not empty`, `component "lvgl/lvgl" is corrupted`), failing "Generate releases" — the same class of race as the PlatformIO core-dir race `scripts/pio.sh` guards against. Nx's `parallelism: false` is fully exclusive, not per-target: the scheduler runs no other task alongside it (`canBeScheduled` in `nx/.../tasks-schedule.js`), so espframe builds also no longer overlap cyd-clock/tamagotchi-plus/media-room-dashboard. That's deliberate — it also keeps media-room-dashboard off the `apps/espframe/.platformio-core` it shares with espframe — at the cost of a few extra minutes on a cold release. The `build` wrapper needs the flag too: `scripts/espframe-build-devices.sh` spawns a nested `pnpm nx run` per board, which the outer scheduler can't see or serialize against. Any new espframe `build-<board>` target must carry the flag as well.

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
