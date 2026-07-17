# DIY-44: espframe Freenove ESP32-S3 support — handoff

Continuing this on another machine (for board flashing access). This file is
temporary scratch for the handoff and can be deleted once the work merges.

## Where things live

- **Repo**: `compton-diy`, branch `diy-44-espframe-nx-submodule` (pushed to
  `origin`, tracks Jira card DIY-44).
- `apps/espframe` is a **fully vendored fork** of `rcompton78/espframe`
  (formerly a git submodule — that was dropped; there is no more upstream
  tracking or sync script). Its source lives directly in this repo's own
  git history from here on.

## Getting set up on a new machine

```bash
git clone git@github.com:rcompton78/compton-diy.git
cd compton-diy
git checkout diy-44-espframe-nx-submodule
pnpm i
```

`pnpm i` triggers root `postinstall` (`nx run-many -t install`), which runs
the NX `install` target on every project that defines one — today that's
only `espframe` (`pnpm install --ignore-workspace` inside `apps/espframe`,
isolated from the root pnpm workspace via `pnpm-workspace.yaml`).

**ESPHome**: `pnpm run setup` (`scripts/setup.sh`) creates a local venv at
`apps/espframe/.venv` and installs ESPHome into it (via
`scripts/espframe-esphome.sh`, which the NX targets below call). No Docker
required. Board enumerates at `/dev/ttyACM0` (native ESP32-S3 USB-JTAG/
serial), matching what `project.json` assumes. You'll also need your user in
the `dialout` group (`sudo usermod -aG dialout $USER`, then a fresh shell) to
access the serial device.

**Version note**: `product/contract/project.json` pins `esphome_version:
2026.6.4` for the fork's own Docker-based CI, but that version isn't on PyPI
yet (nor is its own pinned `aioesphomeapi==45.3.1` dependency), so
`scripts/setup.sh` installs whatever's latest on PyPI instead (currently
2025.7.5) for local dev. **This is a real version gap, not just a formality**
— compiling against 2025.7.5 fails outright, because `remote_image`'s
`__init__.py` calls `automation.register_action(..., synchronous=True)`, a
kwarg that doesn't exist until a newer ESPHome. If you hit that, either wait
for 2026.6.4 to reach PyPI, or adapt `remote_image/__init__.py` to not
require it.

**Gotcha (cost real time this session): `esphome upload` does NOT
recompile.** It just reflashes whatever binary is already sitting in
`.esphome/build/<name>/.pioenvs/<name>/` from the last `compile`. After any
YAML edit, always run `compile` before `upload` (or use `run`, which chains
both) — otherwise you silently reflash stale firmware and misread the
result. `pnpm nx run espframe:flash-freenove-s3` is safe (it `dependsOn`
`build-freenove-s3`, which recompiles); it's only a footgun when invoking
`esphome upload` directly for quick iteration.

## NX targets available (`apps/espframe/project.json`)

- `pnpm nx run espframe:install` — pnpm install inside espframe's own tree.
- `pnpm nx run espframe:generate` — runs `pnpm run generate` (regenerates
  `packages.yaml` from `product/contract/devices.json`; does NOT touch
  hand-authored files like screens/fonts/icons).
- `pnpm nx run espframe:build-freenove-s3` — native ESPHome compile (via
  `scripts/espframe-esphome.sh`), cached by NX.
- `pnpm nx run espframe:flash-freenove-s3` — native ESPHome upload, assumes
  the board enumerates at `/dev/ttyACM0`.
- `pnpm nx run espframe:monitor-freenove-s3` — native ESPHome serial log
  monitor.

## What's done

- Submodule wiring, NX targets, pnpm workspace exclusion — committed.
- `product/contract/devices.json` freenove-s3 entry, `packages.yaml`
  generated, `product/espframe.json` regenerated.
- **All previously-missing hand-authored device files now exist** (scaled
  down from the P4 reference for a 240x320 screen — 20px titles, 13px body
  text, 54px clock, etc.): `screen_loading.yaml`, `screen_wifi_setup.yaml`,
  `screen_immich_setup.yaml`, `screen_slideshow.yaml`, `assets/fonts.yaml`,
  `assets/icons.yaml`, `dev.yaml`, `.gitignore`, `builds/freenove-s3.yaml`,
  `builds/freenove-s3.factory.yaml`.
  - `screen_slideshow.yaml` still declares the `portrait_pair_container` /
    `portrait_left_img` / `portrait_right_img` widgets (common/addon code
    references their IDs at compile time via `id()`, so they must exist even
    if unused) but forces `portrait_pairing_enabled` off via a device-local
    `on_boot` hook (priority -160, after `screen_rotation.yaml`'s -150 hook)
    — this device's screen is only 240px wide, too narrow for the P4's
    dual-portrait side-by-side layout, and the shared rotation script
    otherwise turns pairing ON by default (rotation "0"/"180", which is this
    device's locked default since non-dev users can't pick 90/270).
- **Full firmware compiles clean** (`build-freenove-s3`): RAM 24.5%
  (80,360/327,680 B), Flash 22.8% (1,853,575/8,126,464 B). Plenty of
  headroom.
- **Display hardware bug found and fixed.** ESPHome's built-in
  `ili9xxx: model: ILI9341` sends TFT_eSPI's *standard* ILI9341 init
  sequence, but this physical panel needs the *alternate* init table (same
  one selected by `ILI9341_2_DRIVER` in cyd-clock's freenove-s3
  `platformio.ini` env — different VCOM/power-control/gamma values, see
  https://github.com/Bodmer/TFT_eSPI/issues/1172). Using the standard model
  left the panel showing a **blank white screen** with no error — confirmed
  via `model: CUSTOM` + a hand-written `init_sequence:` replicating the
  alternate table (`devices/freenove-s3/device/device.yaml`).
  - Also needed `transform: { mirror_x: true }` in the display config.
    ESPHome's `ILI9XXXDisplay::setup()` calls `this->set_madctl()`
    **unconditionally right after** the init sequence, rebuilding the
    MADCTL byte purely from `color_order:`/`transform:` config flags — it
    ignores whatever raw MADCTL byte your own `init_sequence:` sent. Without
    `mirror_x: true`, text rendered mirrored/wrong-corner.
  - Color: `color_order: BGR` is correct (confirmed empirically — a pure
    green fill rendered as green; RGB order swapped red into blue). A pure
    red fill looks slightly orange in photos, but that's very plausibly just
    indoor lighting/camera white balance on a cheap TN panel, not a firmware
    bug (user confirmed it looks close to red in person).
  - All of the above verified via a temporary standalone diagnostic
    ESPHome config (no LVGL/touch/immich — just `display:` + a `lambda:`
    fill+text draw), iterated directly against the physical board. That
    scratch file has been deleted; the confirmed settings are now in the
    real `devices/freenove-s3/device/device.yaml`.
- Touchscreen (`ft63x6`) was suspected as the cause of a boot-time hang (see
  below) and temporarily disabled to test — **the hang persisted with touch
  fully disabled**, ruling it out. Touch is re-enabled in `device.yaml` as
  normal.

## What's NOT done yet — an unresolved intermittent boot freeze

The full firmware (and even a **minimal** standalone test — just
`display:` + `lvgl:` with one page/label, no touch, no immich, no
slideshow) exhibits an intermittent freeze on real hardware:

- Boot proceeds normally through component setup and logging (WiFi AP
  setup starts, `[C][wifi:962]: Setting up AP:` prints).
- Then a long, unpredictable gap (tens of seconds, not consistent between
  runs) before anything else logs.
- It sometimes continues after the gap (into a `safe_mode: Boot seems
  successful` message, then an NVS `preferences: Writing 1 items` log line)
  and then goes silent again for good; sometimes it doesn't continue at
  all within the observation window.
- **The screen never shows newly-drawn content** — it stays on whatever was
  last successfully drawn by a previous (unrelated) firmware, i.e. LVGL's
  first flush never appears to complete.

Ruled out so far:
- Not the display init sequence (confirmed working standalone, repeatedly,
  with a non-LVGL lambda-draw test — solid fills + text render fine, no
  freeze, `update_interval: 1s` kept redrawing without issue).
- Not the touchscreen (disabling it didn't change the freeze).

Leading hypotheses, untested:
- An ESP-IDF task-watchdog-triggered reset loop (each reboot attempt makes
  a little more progress than the last before resetting again, which would
  explain the "long gap then continues, from roughly the same point" and
  the earlier-observed native-USB-CDC `Serial port closed!` interruptions —
  a WDT reset would re-enumerate `/dev/ttyACM0` and could look like this).
- Contention between the LVGL/display SPI activity and an NVS flash
  (`preferences:`) write — the freeze in the minimal LVGL test happened
  right around a `preferences: Writing 1 items` log line.

Next steps to try:
1. Test the minimal LVGL diagnostic config **without WiFi/captive_portal**
   at all, to see if it renders "LVGL OK" cleanly — would isolate whether
   WiFi-AP-mode + LVGL together (memory/timing pressure) is the trigger.
2. If that's clean, add WiFi back but drop `psram: octal` mode temporarily,
   or try a longer `CONFIG_ESP_TASK_WDT_TIMEOUT_S` in `sdkconfig_options`,
   to test the watchdog-loop theory directly.
3. Consider capturing a real backtrace (ESPHome's docker image can decode
   addresses from a crash — worth checking if the watchdog actually panics
   with a backtrace rather than silently resetting).
4. Once display+LVGL is confirmed stable standalone, re-test the full
   `builds/freenove-s3.factory.yaml` end to end: WiFi setup, Immich setup,
   slideshow, touch, backlight.
5. Verify actual runtime PSRAM/heap usage on-device once it boots cleanly.

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
both fully data-driven off `product/contract/devices.json` (confirmed: once
the freenove-s3 device entry + `builds/freenove-s3*.yaml` existed, no
workflow changes were needed for the fork's own CI to pick it up). It also
has its own GitHub Pages web flasher (`rcompton78.github.io/espframe`,
separate from this repo's `dist/index.html`).

Two options were on the table, **not yet decided**:
1. **Keep it a separate product** (leaning towards this) — espframe's
   builds/releases stay entirely in the fork's own CI + Pages site; this
   repo's `release.yml` just pins the submodule commit like a dependency
   bump, maybe with a lightweight PR compile-sanity-check.
2. **Fully integrate** — add `build`/`release` NX targets so espframe's
   binaries also land in this repo's own `dist/`/GitHub Releases alongside
   cyd-clock/bambu-status-bar. This needs a new release script — `tools/
   esp-flasher/generate_release.py` is deeply PlatformIO-specific (esptool
   `merge_bin` from `.pio/build/<env>/*.bin`, `platformio.ini` partition
   parsing) and doesn't apply to ESPHome's Docker-compile-then-copy output.

Come back to this once the boot-freeze issue is resolved and the device
actually works end to end on real hardware.

## Jira

Card: **DIY-44** (project key `DIY` on `rcompton78.atlassian.net`). DIY-43
was consolidated into DIY-44 and closed — DIY-44 is the single source of
truth for this whole effort.
