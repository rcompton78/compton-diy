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
- The display fill (`it.fill(...)`) never visibly renders on the panel,
  under any configuration tried so far. Backlight is confirmed lit (not the
  "totally dark" failure mode), so this isn't a backlight issue.
- **Definitively ruled out this session: it is not a driver/color/RAM
  software issue.** The panel simply isn't receiving/executing our SPI
  writes. Evidence, in order:
  1. `platform: ili9xxx, model: ILI9341` — uniform light grey.
     `invert_colors: true` vs `false` made no difference (identical grey
     both ways).
  2. Swapped to `platform: st7789v` (ESPHome's separate ST7789 component —
     distinct from `ili9xxx`, matching a real historical bug in this repo:
     `apps/cyd-clock/STATUS.md` documents the *original* CYD board also
     being mislabeled ILI9341 when its actual silicon was ST7789, with the
     identical "grey strip" symptom). With `model: CUSTOM`, this **crashed
     in a boot loop** — `EXCVADDR: 0x00000000`, `StoreProhibitedCause`, in
     `ST7789V::setup()` at `st7789v.cpp:123`
     (`memset(this->buffer_, ...)` on a null `buffer_`). Root cause: the
     component's `RAMAllocator` failed to allocate a full 16-bit RGB565
     240×320 framebuffer (153,600 bytes) — this board is a plain classic
     ESP32 with **no PSRAM**, and the build's available DRAM (~157KB per
     the linker map) left almost no margin. The component doesn't check
     the allocation result before writing to it — a genuine upstream bug,
     not a config mistake. **Fix: `eightbitcolor: true`** halves the buffer
     to ~76.8KB, which allocates fine and stops the crash loop cleanly
     (`setup() finished successfully!`, no more reboots).
  3. Note: `st7789v`'s `reset_pin` is a **required** option — passing `-1`
     or omitting it entirely both fail config validation
     (`Invalid pin number: -1`). Unlike some ESPHome display components,
     there's no "no physical reset" sentinel here.
  4. With the crash fixed (still `cs_pin: GPIO15, dc_pin: GPIO2,
     reset_pin: GPIO4`), the panel booted clean but showed a **static,
     unchanging black** screen.
  5. Swapped `cs_pin`/`dc_pin` (CS=GPIO2, DC=GPIO15) on the theory this
     seller's PCB might wire them opposite to the reference CYD design —
     the resting color changed (black → grey/white), which looked like a
     promising signal at first.
  6. **The decisive test**: changed the lambda to alternate
     `Color(255,255,255)` / `Color(0,0,0)` every second
     (`if ((millis()/1000) % 2 == 0) ... else ...`) — a real display would
     visibly blink. **It stayed static.** This proves the grey/white shift
     from the CS/DC swap in step 5 was incidental (some other idle/power
     artifact), not evidence of real communication — the panel is not
     executing our writes at all, under any pin/driver combination tried
     so far.
- **Conclusion**: at least one of the four core SPI signals (CLK=GPIO14,
  MOSI=GPIO13, and whichever CS/DC assignment) is wrong for this specific
  board. We've exhausted the "matches the known CYD reference design"
  hypothesis in both CS/DC orientations across two driver ICs with no
  signal ever reaching the panel. Blind further pin-guessing has low odds
  from here.
- **Update, later same session**: physical inspection + a community-board
  match + an automated GPIO sweep were all tried. Still unresolved. Details:
  - The carrier PCB's silkscreen reads **"2.8 ESP32 240*320 V1.1"** — a
    generic, widely-resold reference design. Found a close community match:
    the **Guition JC2432W328C**
    (`esphome.atmyworkshop.online/devices/jc2432w328c/`), which uses the
    *exact* CLK=14/MOSI=13/MISO=12/CS=15/DC=2 we'd already converged on,
    ESPHome's newer **`mipi_spi`** platform (`model: ST7789V`) instead of
    the deprecated `st7789v` component, and — notably — **no `reset_pin`
    for the display at all** (unlike `st7789v`, `mipi_spi` allows omitting
    it). That reference's touch chip (CST816) differs from ours (confirmed
    FT6336U), so it's a close-but-not-identical variant, not a certain
    match.
  - Tried `platform: mipi_spi, model: ST7789V, cs_pin: GPIO15, dc_pin:
    GPIO02, data_rate: 40MHz, rotation: 90, show_test_card: true` (no
    reset_pin). Clean boot, no errors, `mipi_spi` uses a much smaller
    partial-buffer strategy (19,200 bytes vs. the 76.8–153KB full-buffer
    approaches) so no RAM crash risk either. A logged "display took a long
    time for an operation (411ms)" confirms a real, correctly-sized SPI
    write was attempted (240×320×2 bytes' worth of transfer time) — **but
    this only proves the ESP32 attempted transmission, not that the panel
    received/understood it** (write-only SPI, no readback). Screen: still
    full black.
  - User physically opened the board and photographed the ribbon
    connectors. Found the touch panel's own connector fully labeled:
    6-pin, `1.VDD 2.SDA 3.SCL 4.INT 5.RST 6.GND` (part marking
    "RKX-S219F0x") — confirms touch has its own RST/INT lines we aren't
    currently using (polling mode without them already works fine, so
    low-priority). A second, separate, visibly **wider** gold ribbon
    (marked "133") was also found running from the glass panel itself,
    confirming the display *does* have its own dedicated connector (not
    chip-on-board/no-cable as briefly suspected) — but no readable
    pin-function labels on it, so it didn't yield exact GPIO numbers. User
    was asked to stop opening the assembly further to avoid damaging the
    fragile flex connectors.
  - Built an **automated GPIO sweep** (`docs/diy-102-bringup/
    display-touch-test.yaml`'s current content): bypasses ESPHome's
    `spi:`/`display:` components entirely, manually bit-bangs a minimal
    ST7789/ILI9341-compatible init + address-window + white-fill sequence
    (opcodes SWRESET/SLPOUT/COLMOD/DISPON/CASET/RASET/RAMWR are identical
    across both chip families) via raw `pinMode`/`digitalWrite` in an
    `esphome: on_boot:` lambda, holding CLK=14/MOSI=13/DC=2/RST=4 fixed
    while cycling CS through candidates `{15, 5, 4, 27, 26, 25, 17, 16, 22,
    19}`, each filling a 240×40 white strip for a few seconds before moving
    on (`App.feed_wdt()` called periodically in the pixel loop to avoid a
    watchdog reset during the tight bit-bang loop). Note: `touchscreen:`
    components require a `display:` component to be declared even when
    unused, so it had to be removed from this test file entirely (touch is
    already separately confirmed working, add back once display pins are
    known).
  - First run (2.5s/candidate): user reported the screen went "white for
    4/5ths of the screen with a top black bar" at some point — a real,
    qualitatively different response from every prior static grey/black
    result. Could not pin down which candidate caused it (logs showed the
    sweep had run twice — once before log-tailing started, once fully
    captured — so the timing was ambiguous).
  - Re-ran slower (5s/candidate) to nail down which candidate, watching
    logs live. **This time the screen was black almost the entire pass.**
    The earlier "4/5 white" result did not reproduce. Current best guess:
    it was a one-off transient (possibly a reset/brownout glitch from the
    tight unyielding bit-bang loop, or leftover GRAM content from a
    previous test accumulating without an explicit clear — the sweep never
    clears the screen between candidates) rather than a real, repeatable
    CS match. **Not confirmed as a real signal — don't assume any of the
    10 swept candidates is the answer.**
  - **Decision**: stop blind hardware iteration for now. User is messaging
    the AliExpress seller directly, asking for (ranked by value): (1) the
    board's demo/example code (GitHub/Drive/Baidu link — would contain a
    working `User_Setup.h` or similar with real pins), (2) the actual
    display driver IC, (3) exact display SPI pin mapping, (4) whether this
    is the same reference design as Guition JC2432W328, (5) touch
    controller part number confirmation. **Session paused pending seller
    response.**
  - Also worth noting: the AliExpress listing's own description text
    turned out to be unreliable/templated — it claims "XPT2046 touch
    controller for responsive capacitive or resistive touch input," but
    XPT2046 is a resistive-only controller and is never used for capacitive
    touch, meaning the listing conflates multiple SKUs/variants into one
    generic description. Its "ILI9341 driver" claim should likewise be
    treated as unconfirmed for this specific capacitive-touch unit until
    the seller (or a real schematic) confirms it.
- A red herring encountered along the way: branch
  `feature/diy-93-blank-screen-new-board` sounded relevant to a "blank
  screen" bug but turned out to be an empty branch (no commits past
  9e38ea9) — not related to this work, don't bother checking it again.
  (Note: a *different*, unrelated, real DIY-93 card/branch was later
  created in a separate session — `feature/DIY-93-cyd-clock-blank-screen-
  2-8-board` — for an actual cyd-clock blank-screen bug on yet another
  board. Confirmed genuinely unrelated to DIY-102's board/problem, just a
  coincidentally similar symptom name.)

## Relevant files

- `docs/diy-102-bringup/i2c-scan.yaml` — first diagnostic: bare `i2c: scan:
  true` config that found the touch controller at 0x38 on GPIO33/32.
  Superseded by display-touch-test.yaml but kept for reference.
- `docs/diy-102-bringup/display-touch-test.yaml` — this is the file to keep
  iterating on for the display fix. **Currently holds the automated GPIO
  sweep** (see "Not yet working" above), not a normal ESPHome
  `spi:`/`display:` config — it bypasses those components entirely and
  bit-bangs raw GPIO from an `on_boot:` lambda. `touchscreen:`/`i2c:` were
  stripped out for this (touchscreen requires a `display:` component to be
  declared even when unused). **Once real pins are known** (from the
  seller or further testing), this file should be rewritten back to a
  normal `spi:` + `display: platform: mipi_spi, model: ST7789V` (or
  `ili9xxx` if it turns out to be genuinely ILI9341) + `i2c:` +
  `touchscreen: platform: ft63x6` config — don't just keep patching the
  sweep lambda.
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

**Session paused pending a reply from the AliExpress seller** (message
sent asking for demo code/schematic, driver IC, exact display pins, and
whether this matches the Guition JC2432W328 reference design — see "Not
yet working" above for the full ask). Check for a reply first.

1. **Get ground-truth pin numbers** — from the seller's reply if it
   arrives with useful info (their demo code's `User_Setup.h`/config would
   be the fastest path to real pins), or fall back to a multimeter
   continuity trace from the ESP32 module's own labeled pins to wherever
   the wider (display) ribbon connects, if no useful seller response comes.
   More blind GPIO-guessing beyond the sweep already tried has low odds —
   see the "Not yet working" section above for the full trail of ruled-out
   combinations and the inconclusive sweep result. Once real CS/CLK/MOSI/DC
   pin numbers are known, rewrite `docs/diy-102-bringup/
   display-touch-test.yaml` back to a normal `spi:`/`display:` config (see
   "Relevant files" above — don't keep patching the sweep lambda).
2. Get the board + PC set up again with `scripts/espframe-esphome.sh run
   docs/diy-102-bringup/display-touch-test.yaml --device /dev/ttyUSB0`
   (adjust the device path if it enumerates differently on the other
   machine). **Caution**: `run` and `logs` both hold the serial port open
   indefinitely (they tail forever) — if you need to `compile`/`upload`
   again afterward, stop the previous log-tailing process first or the
   next upload will fail with "port is busy." Prefer `compile` then
   `upload` as two separate bounded commands while iterating, and only use
   `logs`/`run` when you actually want to watch boot output.
3. With correct pins, the black/white blink lambda already in the test
   file should immediately show real blinking — that's the confirmation
   signal. Swap back to a plain `it.fill(Color(255, 0, 0))` afterward.
4. Decide whether to keep `platform: st7789v` + `eightbitcolor: true` (now
   confirmed to boot cleanly) or revisit `ili9xxx`/`model: ILI9341` once a
   picture is actually visible — with real pins, either driver might turn
   out to be correct; the grey vs. black difference seen this session was
   never confirmed to mean anything since neither attempt had working pins.
   If sticking with `ili9xxx`, note it may have the same RAM-allocation
   fragility on this non-PSRAM board — check for an equivalent 8-bit/
   palette color option there too if a similar crash appears.
5. Once display renders correctly, confirm rotation/mirroring look right
   (current config has no `mirror_x` set — unverified assumption from the
   original CYD reference, may need adjusting once you can actually see
   the screen).
6. After display + touch are both solid, build the real device config
   under a new `apps/` project (or wherever DIY-102 ends up living)
   following the espframe device.yaml pattern: the 6 HA entity actions from
   the Jira card, wired as touch buttons on the display, plus the
   standard ESPHome OTA + http_request update-manifest mechanism.
