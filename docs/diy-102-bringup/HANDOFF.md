# Handoff: DIY-102 media room dashboard — board bring-up

**Created**: 2026-08-02, evening
**Project**: compton-diy
**Branch**: feature/diy-102-media-room-ha-dashboard
**Jira**: DIY-102 (In Progress)

## What to do

Building a 6-button ESPHome dashboard for the media room on a cheap AliExpress
2.8" ESP32 + ILI9341 + capacitive-touch board. Right now we're still in the
board bring-up phase: figuring out the real pinout by flashing small
diagnostic ESPHome configs directly to the board over USB and reading the
serial logs, since the AliExpress listing gives no schematic. Display and
touch are two separate unsolved problems; touch is done, display is not.

## Current state

**Confirmed working (via live flash + serial log tests):**
- Chip: plain classic ESP32-D0WD-V3 (NOT S3) — use `board: esp32dev` in ESPHome
- Flash: 4MB (auto-detected by esptool) — plenty of headroom for dual-OTA
  partitions; a full build with WiFi/API/OTA/display/touch/6 entities should
  be well under 1MB
- Touch: FocalTech FT6336-family chip (I2C address `0x38`), same chip family
  (`ft63x6` ESPHome platform) already used for the freenove-s3 board in
  `apps/espframe`
  - I2C pins: `sda: GPIO33`, `scl: GPIO32` — bus recovers cleanly here
  - **Do not use GPIO21/22 for I2C** — GPIO21 is actually the backlight pin
    (see below); trying to use it as I2C SDA fails bus recovery ("SDA held
    low"), which is what led us to find the backlight pin
  - Touch works fine in **polling mode with no `interrupt_pin` set at all** —
    we tried `interrupt_pin: GPIO21` first (wrong guess, no touch events
    logged), removed it, and got clean `Touch at x=.. y=..` log lines on tap
  - Confirmed by physically tapping the screen and watching serial logs
- Backlight: `GPIO21` (PWM via `output: platform: ledc`) — confirmed lit
  physically. This matches the original CYD (ESP32-2432S028R) board's known
  backlight pin, which was the tell that led us to it.
- Display SPI pins (per the known CYD reference design, which this board's
  wiring matches so far): CLK=GPIO14, MOSI=GPIO13, MISO=GPIO12, CS=GPIO15,
  DC=GPIO2

**Not yet working — the open problem:**
- The ILI9341 display fill (`it.fill(Color(255, 0, 0))`, solid red) renders
  as **uniform light grey** instead. Backlight is confirmed lit (not the
  "totally dark" failure mode), so this isn't a backlight issue.
- Tried and ruled out:
  - `invert_colors: true` vs `false` — **identical grey both ways**, which
    strongly suggests the panel isn't executing our commands at all (a
    color-config bug would change the color, not leave it unchanged)
  - Adding `reset_pin: GPIO4` (a guess) — still grey, not confirmed to have
    helped or hurt
- Leading theory: either the hardware reset line is on some other GPIO and
  is holding the panel in reset (a floating/pulled-low RST would produce
  exactly this "backlit but blank/grey" symptom — this is very likely NOT
  simply tied to EN like the original CYD, since that theory hasn't
  produced a picture yet), or CS/DC are subtly wrong despite matching the
  known CYD wiring (this specific AliExpress seller's PCB could differ),
  or the SPI data rate (40MHz) is too fast for this board's wiring/ribbon
  and needs to be lowered.
- A red herring encountered along the way: branch
  `feature/diy-93-blank-screen-new-board` sounded relevant to a "blank
  screen" bug but turned out to be an empty branch (no commits past
  9e38ea9) — not related to this work, don't bother checking it again.

## Relevant files

- `docs/diy-102-bringup/i2c-scan.yaml` — first diagnostic: bare `i2c: scan:
  true` config that found the touch controller at 0x38 on GPIO33/32.
  Superseded by display-touch-test.yaml but kept for reference.
- `docs/diy-102-bringup/display-touch-test.yaml` — current working test
  config: backlight + SPI display fill + I2C touch with on_touch logging.
  This is the file to keep iterating on for the display fix. Currently has
  `reset_pin: GPIO4` (last untested guess) and `invert_colors: true` (proven
  not to matter either way).
- `apps/espframe/devices/freenove-s3/device/device.yaml` — reference for
  how this repo wires up an `ft63x6` touchscreen + display + OTA in ESPHome
  (different pins, but same chip family and same overall structure to copy
  once the display works)
- `apps/espframe/common/addon/firmware_update.yaml` — reference for the
  "auto OTA like espframe" mechanism the user wants replicated: it's just
  standard ESPHome native `ota: platform: esphome` plus an
  `update: platform: http_request` component polling a GitHub Pages
  `manifest.json` — no custom protocol
- Jira DIY-102 — has the finalized 6-button → HA entity mapping already
  locked in (Roku TCL on/off, Roku TCL play game, Roku TCL watch tv, window
  media room lamp, window lights, lamp), plus the board listing link and
  capacitive-touch note

## Context and decisions

- The board was bought as the capacitive-touch variant of an AliExpress
  listing whose default option is a resistive XPT2046 touch (same
  ILI9341+ESP32 PCB family sold in both variants). It turned out to be a
  near-exact hardware clone of the original CYD (ESP32-2432S028R): same
  MCU, same display driver, same SPI pins, same backlight pin — just with
  the resistive XPT2046 touch chip swapped for a capacitive FT6336 chip on
  the same connector header, repurposing GPIO32/33 for I2C instead of the
  XPT2046's shared-SPI touch wiring.
- All hardware findings above came from directly flashing the board over
  USB (`/dev/ttyUSB0`, confirmed group-accessible via `dialout`, no sudo
  needed) using `scripts/espframe-esphome.sh compile/run/logs <path>
  --device /dev/ttyUSB0`. This wrapper uses the repo's own ESPHome venv/core
  dir under `apps/espframe/`, so it works from this repo without any global
  ESPHome install.
- Working iteration loop that worked well: edit the yaml, `scripts/
  espframe-esphome.sh run <path> --device /dev/ttyUSB0` (compiles, flashes,
  and immediately tails logs in one command — don't run `logs` as a
  separate command right after `run`, you'll miss the boot output since the
  device reboots and logs immediately after flashing completes).
- Final device config (once display works) will be a real ESPHome project
  following the `apps/espframe` package/device.yaml structure, not left
  living in `docs/diy-102-bringup/`. That directory is scratch/diagnostic
  only.

## How to continue

1. Get the board + PC set up again with `scripts/espframe-esphome.sh run
   docs/diy-102-bringup/display-touch-test.yaml --device /dev/ttyUSB0`
   (adjust the device path if it enumerates differently on the other
   machine).
2. Next things to try for the grey-screen problem, roughly in order of
   effort:
   - Remove `miso_pin` from the `spi:` block entirely — GPIO12 is an ESP32
     strapping pin (flash voltage select) and produces a boot warning; the
     display doesn't need MISO for writes, so eliminating it removes a
     possible source of interference
   - Try lowering `data_rate` on the display (e.g. add `data_rate: 20MHz` or
     `10MHz`) in case 40MHz is too fast for this particular board's wiring
   - Try `reset_pin: -1` / omitting reset_pin entirely again now that CS/DC
     have been double-checked, to isolate whether GPIO4 as reset_pin was a
     bad guess actively breaking things vs. neutral
   - Try alternate `model:` values ESPHome's ili9xxx platform supports
     (e.g. `ILI9341` variants, or explicitly check if this panel actually
     needs `TFT 2.8`/different init sequence) in case the seller shipped a
     different controller than advertised
   - If still stuck, physically photograph the board's silkscreen/back
     side for pin labels, or look for a wiki/schematic link in the
     AliExpress listing's images (only sometimes present)
3. Once display renders correctly, confirm rotation/mirroring look right
   (current config has `mirror_x: YES` — unverified, was ESPHome's
   default inference, may need adjustting once you can actually see the
   screen).
4. After display + touch are both solid, build the real device config
   under a new `apps/` project (or wherever DIY-102 ends up living)
   following the espframe device.yaml pattern: the 6 HA entity actions from
   the Jira card, wired as touch buttons on the display, plus the
   standard ESPHome OTA + http_request update-manifest mechanism.
