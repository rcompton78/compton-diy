# DIY-44: espframe Freenove ESP32-S3 support — handoff

This file is scratch notes for continuing this work across sessions/machines
and can be deleted once the work merges.

## Where things live

- **Repo**: `compton-diy`, branch `diy-44-espframe-nx-submodule` (pushed to
  `origin`, tracks Jira card DIY-44).
- `apps/espframe` is a **fully vendored fork** of `rcompton78/espframe`
  (formerly a git submodule — that was dropped; there is no more upstream
  tracking or sync script). Its source lives directly in this repo's own
  git history from here on.
- This fork now supports **two devices**: the original
  `guition-esp32-p4-jc8012p4a1` (P4, 800x1280 portrait, MIPI-DSI display) and
  the new `freenove-s3` (ESP32-S3 CYD, 240x320, SPI ILI9341 display).

## Getting set up on a new machine

```bash
git clone git@github.com:rcompton78/compton-diy.git
cd compton-diy
git checkout diy-44-espframe-nx-submodule
pnpm i
pnpm run setup
```

`pnpm i` triggers root `postinstall` (`nx run-many -t install`), which runs
the NX `install` target on every project that defines one — today that's
only `espframe` (`pnpm install --ignore-workspace` inside `apps/espframe`,
isolated from the root pnpm workspace via `pnpm-workspace.yaml`).

`pnpm run setup` (`scripts/setup.sh`) sets up **both** build systems in this
repo:
- PlatformIO for `cyd-clock`/`bambu-status-bar` (root `.venv`).
- ESPHome for `espframe` (`apps/espframe/.venv`) — **no Docker required**.
  Requires Python **3.12+** (ESPHome `2026.6.4` needs `>=3.11`; 3.12 is what
  CI pins via `actions/setup-python`, so we match it exactly rather than
  allowing 3.11 as a fallback). On Ubuntu 22.04 (jammy) `python3.12` isn't in
  the default repos — `setup.sh` auto-installs it from the deadsnakes PPA
  (**local dev only**; CI provisions Python directly via
  `actions/setup-python`, see the CI section below).

You'll also need your user in the `dialout` group (`sudo usermod -aG
dialout $USER`, then a fresh shell) to access the serial device without
sudo, for both build systems.

**WSL2 users**: if the board is attached via `usbipd` from Windows, use
`usbipd attach --wsl --busid <BUSID> --auto-attach` (not a one-shot
`attach`) — the board's native USB-CDC re-enumerates on every reset, and a
one-shot attach won't survive that, making it look like the board hung when
it's actually just the serial connection that dropped.

**Gotcha: `esphome upload` does NOT recompile.** It just reflashes whatever
binary is already sitting in `.esphome/build/<name>/.pioenvs/<name>/` from
the last `compile`. After any YAML edit, always run `compile` before
`upload` — otherwise you silently reflash stale firmware and misread the
result. The NX `flash-*` targets are safe (they `dependsOn` the matching
`build-*` target, which recompiles); it's only a footgun when invoking
`esphome upload` directly.

## NX targets available (`apps/espframe/project.json`)

- `pnpm nx run espframe:install` — pnpm install inside espframe's own tree.
- `pnpm nx run espframe:generate` — runs `pnpm run generate` (regenerates
  `packages.yaml` from `product/contract/devices.json`; does NOT touch
  hand-authored files like screens/fonts/icons).
- `pnpm nx run espframe:build-freenove-s3` / `flash-freenove-s3` /
  `monitor-freenove-s3` — native ESPHome compile/upload/logs for the
  Freenove S3, cached by NX. Assumes `/dev/ttyACM0`.
- `pnpm nx run espframe:build-guition-esp32-p4-jc8012p4a1` /
  `flash-guition-esp32-p4-jc8012p4a1` /
  `monitor-guition-esp32-p4-jc8012p4a1` — same, for the original P4 device.
  Defaults to `/dev/ttyUSB0` — **unverified against real P4 hardware**,
  adjust the `--device` flag in `project.json` if it enumerates differently.

## What's done

- **espframe fully vendored** — no more git submodule, no upstream sync.
- **Docker replaced with native ESPHome** (`apps/espframe/.venv`, via
  `scripts/espframe-esphome.sh`). NX's `build-freenove-s3` target properly
  caches now (`cache: true` + correct `outputs` path).
- **freenove-s3 device fully built out**: all hand-authored files exist
  (`screen_loading.yaml`, `screen_wifi_setup.yaml`,
  `screen_immich_setup.yaml`, `screen_slideshow.yaml`, `assets/fonts.yaml`,
  `assets/icons.yaml`, `dev.yaml`, `.gitignore`, `builds/freenove-s3.yaml`,
  `builds/freenove-s3.factory.yaml`), scaled down for a 240x320 screen.
  - `screen_slideshow.yaml` still declares the `portrait_pair_container`
    widgets (common/addon code references their IDs at compile time) but
    forces `portrait_pairing_enabled` off via an `on_boot` hook — this
    device's screen is too narrow for the P4's dual-portrait layout.
- **Full firmware compiles and flashes clean.** RAM ~24%, Flash ~23%
  against this board's actual budget. Plenty of headroom.
- **Display init sequence bug (fixed earlier session)**: this physical
  ILI9341 panel needs TFT_eSPI's *alternate* init table (`ILI9341_2_DRIVER`,
  not ESPHome's stock `model: ILI9341`), replicated via `model: CUSTOM` +
  a hand-written `init_sequence:` in `devices/freenove-s3/device/device.yaml`,
  plus `transform: { mirror_x: true }` (ESPHome's `set_madctl()` rebuilds
  MADCTL from config flags regardless of what the init sequence sent).
- **Grey-screen bug: root cause found and fixed.** The panel showed a flat,
  unchanging grey — LVGL logs showed real `flush_cb` calls with correct
  areas, and a firmware `debug:` component confirmed the board was alive
  and running (WiFi scans, safe_mode boot confirmation, NVS writes), so
  this was never actually a boot freeze. Root cause:
  `devices/freenove-s3/device/lvgl_base.yaml` had `byte_order:
  little_endian`, copied verbatim from the P4 device's config — but the P4
  uses `platform: mipi_dsi` (a totally different display driver), so that
  setting was never actually validated for this device's SPI `ili9xxx`
  panel. Switched to `byte_order: big_endian` (ESPHome's documented default
  for SPI TFT displays) and real content renders correctly.
  - What looked like an "intermittent boot freeze" in earlier debugging was
    two separate red herrings: (1) WSL2's `usbipd` one-shot attach not
    surviving the board's native-USB-CDC re-enumeration on reset (fixed by
    `usbipd attach --auto-attach`), and (2) not having waited long enough /
    not having verbose-enough logs on some earlier attempts. Once
    `--auto-attach` was running and `log_level: VERY_VERBOSE` was set, the
    board's logs showed it running completely normally the whole time —
    the only actual bug was the invisible-but-"successful" LVGL flush.
  - `buffer_size` is `100%` (full-frame buffer) — at 240x320 RGB565 that's
    only 150KB, negligible against this board's 8MB PSRAM, so there's no
    reason to chunk it like the P4 does (`6%`, necessary there since its
    screen is much bigger).
- **cyd-clock build fix (collateral damage from today's espframe work)**:
  `apps/cyd-clock/platformio.ini` had `platform = espressif32` unpinned.
  PlatformIO's default `espressif32` platform now resolves to a community
  fork (`pioarduino/platform-espressif32`) whose latest release bundles
  Arduino-ESP32 core 3.3.9, which dropped a global `FS` alias
  `WebServer.h` needs — broke both `cyd` and `freenove-s3` envs with `'FS'
  was not declared in this scope`. Root cause: cyd-clock's PlatformIO and
  espframe's ESPHome-bundled PlatformIO shared the same global
  `~/.platformio` package cache by default; building espframe today
  triggered a fresh resolution that silently overwrote cyd-clock's
  previously-working cached packages.
  - Fixed by pinning `platform = espressif32@6.12.0` (the previously-cached
    known-good version) in both `cyd-clock` envs.
  - Fixed the underlying collision too: `scripts/pio.sh` now uses
    `PLATFORMIO_CORE_DIR=<repo>/.platformio-core`,
    `scripts/espframe-esphome.sh` uses
    `apps/espframe/.platformio-core` — fully isolated caches, can't
    clobber each other again. Verified both cyd-clock envs and
    `espframe:build-freenove-s3` all build clean against their now-isolated
    caches.

## What's NOT done yet

1. **End-to-end verification on real hardware** — WiFi setup, Immich setup,
   slideshow (photo rendering, not just the setup screens), touch,
   backlight, all the way through, now that the grey-screen bug is fixed.
   Only the WiFi setup screen has been confirmed rendering correctly so far.
2. **Verify actual runtime PSRAM/heap usage** once the full flow is
   exercised (not just idle at the WiFi setup screen).
3. **Update the fork's README/docs** to note the new supported board.
4. **CI/release integration decision (still open, not decided)** — see
   below.
5. Eventually: open a PR from `diy-44-espframe-nx-submodule` into
   `compton-diy` master, and mark Jira DIY-44 done.

## Open question — CI/release integration (not decided)

cyd-clock and bambu-status-bar are native PlatformIO projects in this
monorepo: `release.yml` already runs `nx run-many -t build,build-fs,release`
on every push to `master`, gated by `nx show projects --affected
--withTarget release`, publishing into this repo's own `dist/` web flasher
and GitHub Releases. **espframe doesn't participate in that at all** —
`project.json` has no `build`/`release` target, so `nx run-many` silently
skips it.

espframe is architecturally different: it already has its **own**,
fully self-hosted release pipeline in the fork — `compile.yml` (PR
validation via `workflow_dispatch`) and `release.yml` (GitHub Releases),
both fully data-driven off `product/contract/devices.json`, plus its own
GitHub Pages web flasher (`rcompton78.github.io/espframe`, separate from
this repo's `dist/index.html`). Both workflows already provision Python via
`actions/setup-python@v5` pinned to `3.12` directly — confirmed neither
calls `scripts/setup.sh`, and that separation should stay (a PPA + `apt-get
update` is too slow/network-flaky for CI; local dev convenience only).

Two options were on the table, **not yet decided**:
1. **Keep it a separate product** (leaning towards this) — espframe's
   builds/releases stay entirely in the fork's own CI + Pages site; this
   repo's `release.yml` just pins the submodule commit like a dependency
   bump, maybe with a lightweight PR compile-sanity-check.
2. **Fully integrate** — add `build`/`release` NX targets so espframe's
   binaries also land in this repo's own `dist/`/GitHub Releases alongside
   cyd-clock/bambu-status-bar. This needs a new release script — `tools/
   esp-flasher/generate_release.py` is deeply PlatformIO-specific and
   doesn't apply to ESPHome's compile-then-copy output.

Come back to this once end-to-end hardware verification is done.

## Jira

Card: **DIY-44** (project key `DIY` on `rcompton78.atlassian.net`). DIY-43
was consolidated into DIY-44 and closed — DIY-44 is the single source of
truth for this whole effort.
