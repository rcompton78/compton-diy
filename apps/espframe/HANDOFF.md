# CJS-216: espframe on Waveshare ESP32-S3-Touch-LCD-7 — handoff

This file is scratch notes for continuing this work across sessions/machines
and can be deleted once the work merges. (Replaces the old DIY-44
freenove-s3 handoff, which was already merged — see git history if that
context is ever needed again.)

## Where things live

- **Repo**: `compton-diy`, branch `CJS-216` (this branch), Jira card CJS-216,
  already transitioned to "In Progress".
- New device added: `devices/waveshare-esp32-s3-touch-lcd-7/` (ESP32-S3,
  800x480 RGB parallel display, GT911 capacitive touch), following the
  existing `freenove-s3` device's conventions exactly (directory layout,
  `product/contract/devices.json` schema, `project.json` target naming).
- Orchestrator context: this session was spawned by a driver session in
  `compton-apps` (peer name `compton-apps-49`, reachable via `SendMessage`
  if resuming this from a fresh session under the orchestrator) — status
  updates were sent to it throughout. If picking this up as a normal
  standalone session instead, that peer relationship doesn't matter.

## Hardware in hand

Real Waveshare ESP32-S3-Touch-LCD-7 (Rev1.2) board, connected via
`/dev/ttyACM0` (a CH340-style USB-UART bridge port — **not** the ESP32-S3's
native USB). ESPHome venv set up at `apps/espframe/.venv` (Python 3.12,
ESPHome 2026.6.4, matches `product/contract/project.json`'s pinned version).

## What's done

1. **Root-caused the original "blank screen" symptom** (reported before this
   session started): NOT a display/hardware fault. A peer session pulled a
   serial boot log showing the factory Arduino `esp_panel`/`esp32_smartdisplay`
   firmware crash-looping (`No default board configuration detected` /
   `assert failed ... board->begin()`) — it just looked like a static
   lit-grey screen because the crash-reboot cycle is fast. Board itself is
   healthy.

2. **Found a correct, cross-validated RGB pinout/timing + GT911 touch
   config** for this exact board from two independent working ESPHome
   community configs (`inytar/waveshare-esp32-s3-touch-lcd-7-esphome`,
   `jimmyeao/waveshare-esp32-s3-7inch-esphome`), cross-checked against the
   official Waveshare wiki pin table. Display uses ESPHome's `mipi_rgb`
   platform (RGB/DPI parallel bus, not real MIPI DSI — S3 has no DSI
   hardware) with model shorthand `ESP32-S3-TOUCH-LCD-7-800X480`. A CH422G
   I2C GPIO expander drives LCD backlight-enable (EXIO2), LCD reset (EXIO3),
   and touch reset (EXIO1) — the RGB bus alone uses all the GPIOs otherwise
   available for those signals.

3. **Built a throwaway hardware bring-up firmware**
   (`builds/waveshare-esp32-s3-touch-lcd-7-bringup.yaml` — standalone, not
   part of the contract/packages system) to validate the display+touch in
   isolation before investing in the full UI port. Found and fixed two real
   bugs on real hardware this way:
   - **PSRAM at octal/120MHz (the community configs' default) crash-loops**
     on this physical board (ESPHome itself flags 120MHz octal as
     experimental). **80MHz is stable.**
   - **This board's actual flash is 16MB** (ESP32-S3-WROOM-1-N16R8), not 8MB
     as the community configs and the original Jira card assumed —
     Waveshare's own wiki spec table confirms 16MB Flash/8MB PSRAM.
   - (Separately, logs needed `logger: hardware_uart: UART0`, not
     `USB_SERIAL_JTAG` like `freenove-s3` uses, since `/dev/ttyACM0` here is
     the UART-bridge port.)
   - Bring-up firmware showed correct-order color bars and registered
     touch taps on the real panel — pinout/timing confirmed correct.
   - **This throwaway file should be deleted** once the real device is fully
     signed off; it's not part of the device/contract system.

4. **Built the real device**, following `freenove-s3`'s pattern:
   - `product/contract/devices.json` — new `waveshare-esp32-s3-touch-lcd-7`
     entry (schema-valid, `node scripts/check_product_schema.mjs` passes).
   - `devices/waveshare-esp32-s3-touch-lcd-7/device/device.yaml` — real
     hardware config (80MHz octal PSRAM, 16MB flash, UART0 logger, CH422G
     expander, `mipi_rgb` display, `gt911` touch).
   - `devices/waveshare-esp32-s3-touch-lcd-7/device/lvgl_base.yaml` — LVGL
     buffer_size 15% (conservative starting point — see open issue below),
     byte_order big_endian, no rotation.
   - `devices/waveshare-esp32-s3-touch-lcd-7/assets/{fonts,icons}.yaml` —
     ported from the P4 device, point sizes scaled ~0.6x (this device's
     480px logical height vs the P4's 800px; both are landscape widescreen
     so height was used as the scaling axis). Font IDs renamed to match new
     sizes.
   - `devices/waveshare-esp32-s3-touch-lcd-7/device/screen_{loading,
     wifi_setup,immich_setup,slideshow}.yaml` — ported from the P4 device
     (closest aspect ratio: P4 is 1280x800 landscape, this is 800x480
     landscape, both use the portrait-photo-pairing feature) via an
     automated proportional geometry rescale script (width/x/etc. ×0.625,
     height/y/etc. ×0.6, font IDs remapped) — see
     `/tmp/.../scratchpad/port_screens.py` if it needs re-running (scratch
     dir is session-local, may not exist in a fresh session; the script is
     short, rewrite if needed — logic is in this file's git history /
     conversation, not worth reproducing here verbatim).
   - `builds/waveshare-esp32-s3-touch-lcd-7{,.factory}.yaml`,
     `devices/waveshare-esp32-s3-touch-lcd-7/esphome.yaml` — CI/local-dev
     wrappers, mirrored from `freenove-s3`'s.
   - `project.json` — `build-`/`flash-`/`monitor-waveshare-esp32-s3-touch-lcd-7`
     targets, plus `release` target's `--esphome` flag and `dependsOn` entry.
   - `espframe_component_ref` set to `"main"` (not a per-device branch like
     freenove-s3's `add-freenove-s3-device`) — no dedicated upstream branch
     exists for this device, and this repo's fork is fully vendored with no
     more upstream sync anyway, so inventing a branch name would be
     dishonest. Local/CI builds always override via a `local` components
     path regardless, so this only matters for someone following the
     device's own `esphome.yaml` for manual local dev.
   - **Naming gotcha**: had to shorten the ESPHome device name from
     `immich-frame-waveshare-s3-touch-lcd-7` (37 chars) to
     `immich-frame-waveshare-s3` (25 chars) — ESPHome caps hostnames at 31.
   - **Real bug found & fixed**: `common/addon/backlight.yaml` (shared
     across all devices) expects a dimmable (`FloatOutput`) backlight via
     `light: platform: monochromatic`. This board's backlight is a plain
     on/off CH422G GPIO-expander pin (`GPIOBinaryOutput`), which ESPHome
     rejected at compile time. Fixed with an `output: platform: template,
     type: float` shim in `device.yaml` that collapses any brightness > 0
     to "on" (there's no real PWM to drive here regardless).

5. **Compiled clean and flashed to real hardware.** User confirmed on the
   physical device: display + touch + the full ported UI all work
   end-to-end — they got into the Immich/photo-source setup screen, not
   just the loading/WiFi screens.

6. **Verified repo tooling accepts the new device**: schema check, asset
   generator (`packages.yaml` is regenerated and in sync,
   `generate_assets.py --check` passes clean), `check_build_budgets.py`,
   `check_source_ownership.py`, and `check_firmware_fields.py` all pass.
   `check_compatibility.py` / `check_backup_config.py` fail, but on a
   pre-existing, unrelated issue (`clock.size`/`clock.position` backup
   fixtures) — confirmed not caused by this work. `check_product_contract.py`
   (`npm run check:product`) is already known-broken repo-wide (see the old
   DIY-44/45 history above) and was **not** used as a gate for this device
   either — it already fails identically for the existing `freenove-s3`
   device on `master`.

## Open issue — screen tearing (NOT yet resolved)

User reports **intermittent screen tearing** on real hardware once in the
full UI (not seen on the earlier bring-up test, though that only showed a
static test pattern with no periodic re-renders, so it may not have been a
fair test either way).

Was mid-investigation when this session paused. Findings so far, from
reading the actual installed ESPHome `mipi_rgb` component source
(`apps/espframe/.venv/lib/python3.12/site-packages/esphome/components/mipi_rgb/mipi_rgb.cpp`):

```cpp
esp_lcd_rgb_panel_config_t config{};
config.flags.fb_in_psram = 1;
config.bounce_buffer_size_px = this->width_ * 10;
config.num_fbs = 1;
```

- **`num_fbs = 1`** — single frame buffer, hardcoded, not exposed as a YAML
  option anywhere in this component's schema (checked `display.py` in the
  same directory — no buffer-count/double-buffer config key at all). LVGL
  draws directly into the one buffer that the RGB peripheral is
  continuously scanning out via DMA — a write landing mid-scanout is a
  textbook tearing cause, and there's no config knob in this ESPHome
  component to add a second buffer.
- **`bounce_buffer_size_px = width * 10`** — a small (10-row) PSRAM→internal-RAM
  bounce buffer ESP-IDF's RGB LCD driver uses internally for DMA bandwidth
  reasons; this is separate from LVGL's own draw buffer (`lvgl_base.yaml`'s
  `buffer_size: 15%`) and not something we control from YAML either.
- A peer (relaying the user) suggested the likely causes are (a) missing
  double/triple buffering vs. panel refresh, or (b) PCLK/porch timing
  slightly off so the panel free-runs out of sync with LVGL's flush timing,
  and flagged that PSRAM bandwidth is tighter now at 80MHz vs. the
  community configs' 120MHz (which crash-looped on this board — see above).
  Given `num_fbs` is hardcoded to 1 with no exposed alternative, (a) as
  literally "add a second full frame buffer" isn't available through this
  component as-is; the more promising angles are:
  - Whether `pclk_frequency` (currently `16MHz`, matching both community
    configs) can be tuned — a mismatch between PCLK and the actual PSRAM
    read bandwidth at 80MHz could plausibly cause the RGB DMA engine to
    occasionally underrun/tear. Worth trying a lower PCLK (e.g. 10-12MHz)
    as a quick experiment, or checking if raising it changes the symptom.
  - Whether ESPHome's `rpi_dpi_rgb` sibling component (imported from in
    `mipi_rgb/display.py` for `CONF_PCLK_FREQUENCY`/`CONF_PCLK_INVERTED`)
    has a materially different/more mature buffering implementation worth
    comparing against.
  - Whether this is a known upstream ESPHome/esp-idf issue for `mipi_rgb`
    on ESP32-S3 (worth a quick search of ESPHome's GitHub issues) rather
    than something fixable purely from this repo's YAML.
  - LVGL's `lvgl_base.yaml` `buffer_size: 15%` was picked as "a conservative
    starting point" without real tuning — worth trying larger/smaller
    values to see if it affects tearing frequency, even though the
    single-`num_fbs` issue above seems like the more likely root cause.

**This is the next thing to pick up.** Not resolved, needs either an ESPHome
component-level workaround, a timing tweak, or accepting it as a known
limitation to document.

## What's NOT done yet

1. Resolve the tearing issue above.
2. Delete the throwaway `builds/waveshare-esp32-s3-touch-lcd-7-bringup.yaml`
   once the real device is fully signed off (it's not part of the
   contract/packages system and was only for isolating the display/touch
   bring-up from the full UI).
3. This is a first-pass proportional UI port (scaled from the P4's screens)
   — user should be shown the actual layout/spacing live and asked whether
   anything needs visual polish beyond what plain scaling produced.
4. Decide on PR: this repo has the `wf` plugin configured
   (`.claude/workflow.config.md` present), so opening the PR should go
   through `wf:create-pr`, not a bare `git push` + `gh pr create` — not yet
   done. If a Jira card is involved and this ends up merged, `wf:post-merge-cleanup`
   must also run.
5. README/docs were **not** updated to mention the new board — checked, and
   `freenove-s3` didn't update them either when it was added (repo
   precedent), so this wasn't treated as a gap, but flag it if the user
   wants it done differently this time.
6. Haven't run the full `npm run check:pr` gate — only ran the individual
   `check:*` scripts relevant to devices (see "What's done" #6 above).

## How to continue

1. Read this file, then `git log --oneline -10` to see exactly what's
   committed so far.
2. Physical board should still be connected (check `/dev/ttyACM0`,
   `ls -la /dev/ttyACM0`) — if this is a fresh machine/session, may need to
   ask the user to reconnect it.
3. Pick up the tearing investigation (see above) — that's the one real
   open technical question. Everything else is either done or is
   process/cleanup (bring-up file deletion, PR workflow).
4. `scripts/espframe-esphome.sh compile apps/espframe/builds/waveshare-esp32-s3-touch-lcd-7.factory.yaml`
   then `... upload ... --device /dev/ttyACM0` to test changes.
