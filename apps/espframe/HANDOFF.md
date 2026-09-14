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

## Screen tearing — RESOLVED

User reported **intermittent screen tearing** on real hardware in the full
UI. Root cause and fix, found in a later session:

- `mipi_rgb`'s `num_fbs = 1` (hardcoded, no YAML knob — see
  `mipi_rgb.cpp`/`display.py`) has been ESPHome's design for RGB-parallel
  displays since its predecessor `rpi_dpi_rgb`; no upstream fix exists and
  no other ESPHome component exposes double-buffering for this bus type.
  This turned out to be a red herring, though — the actual symptom matches
  a *different*, well-documented ESP32-S3 issue.
- Espressif's own FAQ documents "screen drift" on ESP32-S3 RGB-parallel LCDs:
  PSRAM/flash bus contention starves the RGB DMA engine's bandwidth, which
  reads as tearing/shearing even though it's not a classic write-during-scan
  race. See
  <https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html>.
  Waveshare's own official Arduino reference board config for this exact
  panel (`esp-arduino-libs/ESP32_Display_Panel`,
  `BOARD_WAVESHARE_ESP32_S3_TOUCH_LCD_7.h`) uses the identical bounce-buffer
  setup, confirming this is a generic platform issue, not an ESPHome/this-repo
  bug.
- Espressif's documented fix bundle: `SPIRAM_FETCH_INSTRUCTIONS` +
  `SPIRAM_RODATA` (exposed in ESPHome as `esp32: framework: advanced:
  execute_from_psram: true`) plus `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`.
  (`CONFIG_FREERTOS_HZ=1000`, the other item in the bundle, is already
  ESPHome's default regardless.)
- **`execute_from_psram: true` alone fixed the visible tearing** but
  introduced a new problem: it eats enough PSRAM headroom that the device
  now hits an **out-of-memory abort** (`abort()` inside
  `HttpRequestSendAction::play()` → `std::string` allocation, per the
  decoded serial backtrace) roughly 40–70 seconds into slideshow operation
  — a hard crash-loop, confirmed via `esptool`/serial monitor with a
  MAC-address cross-check to rule out a stale-ARP/wrong-device mixup during
  debugging. **Do not re-enable `execute_from_psram` on this device without
  also addressing the heap-pressure issue** (e.g. a heap sensor + real
  investigation into what's consuming/fragmenting PSRAM, likely the
  image-fetch/JSON buffers in the shared `immich_api`/`http_request` code
  paths — that's shared across all devices, so any real fix there needs
  care).
- **`CONFIG_ESP32S3_DATA_CACHE_LINE_64B` tried alone** (no
  `execute_from_psram`) — user confirms **no tearing observed**, and the
  device survived multiple soak tests (several minutes of continuous
  slideshow cycling, well past the 40–70s crash window) with no crash. This
  is the fix that's actually in the device.yaml now:

  ```yaml
  esp32:
    framework:
      sdkconfig_options:
        CONFIG_ESP32S3_DATA_CACHE_LINE_64B: "y"
  ```

- Caveat: this has had a few minutes of soak testing, not extended
  real-world use. If tearing or instability resurfaces, this is the exact
  fork point to revisit — either try other pieces of Espressif's bundle in
  isolation, or dig into the PSRAM-pressure issue properly so
  `execute_from_psram` can be re-added safely.

## Other features added this round

- **Tap-to-show-IP overlay** on the slideshow screen
  (`device/screen_slideshow.yaml`): a single (non-double) tap briefly shows
  the device's IP address in a small top-center overlay for 5 seconds, then
  auto-hides. Ported from `freenove-s3`'s existing `ip_address_overlay`
  widget + `show_ip_overlay` script (same mechanism, just using this
  device's `noto_400_18_font` instead of freenove's smaller 12px font,
  since this panel is much larger). Wired into the same tap handler as the
  existing double-tap-to-advance gesture, so it doesn't add a new gesture.
  Confirmed working on real hardware (tap reveals correct IP, touch not
  affected).
- **Portrait-only/landscape-only photo filtering** — turned out to already
  exist as a shared, fully-wired feature (`common/addon/immich_filter.yaml`'s
  "Photos: Orientation" select), not something added this round. No action
  needed; documented here only because it came up as a question.
- **Screen rotation (physically turning the panel 90°/270°)** — also
  already exists as a shared feature
  (`common/addon/screen_rotation.yaml`, gated behind a "Developer Features"
  switch for 90°/270°) and is wired into this device's build already. **Not
  fully validated**: testing hit a false alarm (the "Developer Features"
  switch reverted to OFF after a reflash, causing the rotation script's
  safety gate to silently snap back to 0° — not an LVGL bug) and then the
  investigation was cut short by the tearing/crash work above. If revisited:
  turn "Developer Features" ON, set "Screen: Rotation" to 90, and check both
  visual orientation *and* touch alignment (GT911 touch-rotation coupling
  was never actually confirmed working on this device — P4's precedent
  uses a different touch chip with a static, non-rotation-reactive
  transform, so it doesn't prove GT911 will behave the same way).

## What's NOT done yet

1. Delete the throwaway `builds/waveshare-esp32-s3-touch-lcd-7-bringup.yaml`
   once the real device is fully signed off (it's not part of the
   contract/packages system and was only for isolating the display/touch
   bring-up from the full UI).
2. This is a first-pass proportional UI port (scaled from the P4's screens)
   — user should be shown the actual layout/spacing live and asked whether
   anything needs visual polish beyond what plain scaling produced.
3. Decide on PR: this repo has the `wf` plugin configured
   (`.claude/workflow.config.md` present), so opening the PR should go
   through `wf:create-pr`, not a bare `git push` + `gh pr create` — not yet
   done. If a Jira card is involved and this ends up merged, `wf:post-merge-cleanup`
   must also run.
4. README/docs were **not** updated to mention the new board — checked, and
   `freenove-s3` didn't update them either when it was added (repo
   precedent), so this wasn't treated as a gap, but flag it if the user
   wants it done differently this time.
5. Haven't run the full `npm run check:pr` gate — only ran the individual
   `check:*` scripts relevant to devices (see "What's done" #6 above).
6. Screen rotation (90°/270°) touch-alignment validation — see above.
7. Only a few minutes of soak testing on the tearing fix — worth watching
   for recurrence over longer real-world use.

## How to continue

1. Read this file, then `git log --oneline -10` to see exactly what's
   committed so far.
2. Physical board should still be connected (check `/dev/ttyACM0`,
   `ls -la /dev/ttyACM0`) — if this is a fresh machine/session, may need to
   ask the user to reconnect it.
3. Tearing is believed resolved (see above) — the next real open items are
   the 90°/270° rotation touch-alignment validation, and general
   PR/cleanup process items. Everything else is done.
4. `scripts/espframe-esphome.sh compile apps/espframe/builds/waveshare-esp32-s3-touch-lcd-7.factory.yaml`
   then `... upload ... --device /dev/ttyACM0` to test changes.
5. **Caution for future debugging sessions**: don't leave a debug-log-level
   or other diagnostic-only firmware flashed and walk away — one session
   this round temporarily bumped `log_level` to DEBUG to trace an issue,
   which briefly meant the physically-connected device was running a
   throwaway diagnostic build instead of the last known-good one. Revert
   diagnostic-only changes and reflash the real config promptly.
