# Tamagotchi+

A virtual-pet firmware for the **Waveshare ESP32-S3-Touch-LCD-1.69** (1.69" 240×280
touch display with onboard accelerometer/gyroscope). It brings up the display and
touch, handles Wi-Fi setup and self-updates, and shows an animated pixel-art pet. Real
pet features (stats, care, evolution) are coming in later updates.

## Hardware

| Part | Details |
|---|---|
| MCU | ESP32-S3R8: 16MB flash, 8MB OPI PSRAM |
| Display | ST7789V2, 240×280 (SPI: MOSI 7, SCLK 6, CS 5, DC 4, RST 8, BL 15) |
| Touch | CST816T capacitive, I2C 0x15 (SDA 11, SCL 10, RST 13, INT 14) |
| IMU | QMI8658 6-axis, I2C 0x6B on the same bus (INT1 38). Not used yet |
| RTC | PCF85063, I2C 0x51 (INT 39). Not used yet |
| Power | SYS_EN (GPIO41) power-hold latch, driven HIGH at boot so the board stays on when running from battery |
| Battery sense | BAT_ADC (GPIO1, ADC1_CH0) reads B+ through a 200K/100K divider (B+ = 3 × pin voltage) |
| Charger | ETA6098 (U9). Its STAT charge-status pin (pin 9) is **not routed**: no net, no pull-up. So firmware can't tell whether the battery is charging |
| USB | Native ESP32-S3 USB (no USB-UART bridge). No GPIO senses VBUS |

Pin source: [Waveshare docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69).

## Install (no cables, no toolchain)

1. Open **[https://rcompton78.github.io/compton-diy/](https://rcompton78.github.io/compton-diy/)**
   in Chrome or Edge (needs Web Serial) with the board plugged in over USB-C.
2. Pick **Tamagotchi+**, click **Connect**, choose the serial port and install.
3. When the install finishes, the flasher offers **Connect to Wi-Fi** (Improv serial).
   Pick your network and enter the password. The device joins right away, and the
   credentials are saved for later boots.

## First boot / Wi-Fi setup

If Wi-Fi wasn't set up from the flasher, or the saved network can't be reached, the
device opens a setup hotspot and the screen shows **Wi-Fi setup: join "Tamagotchi+ Setup"**.

- **Phone/laptop:** join the `Tamagotchi+ Setup` network. The captive portal should
  open on its own; if it doesn't, browse to `192.168.4.1`. Then pick your home network
  and enter its password.
- **Web flasher:** Improv serial keeps listening while the hotspot is up, so you can
  also reconnect the board to the flasher page and use **Connect to Wi-Fi**.

Either way, the credentials are saved to flash and the device reconnects on every boot.

## Main screen

An animated pixel-art pet on a small scene. Along the top are the name, the firmware
version with the power status next to it (e.g. `v1.2.3   USB  87%`), and live Wi-Fi status
(network name + IP, connecting, or setup mode).

- **Tap** the screen: the pet plays its happy animation and a heart floats up, then it
  goes back to idling.
- **Hold** for about a second: swaps the scene to the "sick" colour palette (hold again to
  swap back). This is a demo of palette swapping for now, not a real pet state.

### Battery and USB status

- **Battery %** is read from the battery voltage every 2 seconds and smoothed. It is
  mapped along a LiPo discharge curve, not a straight line from 3.0V to 4.2V, because a
  LiPo holds around 3.7–3.9V for most of its charge and then drops quickly. The number on
  screen moves at most one point every 15 seconds, so it eases up or down rather than
  jumping when you plug in or unplug.
- **`USB`** appears when a USB *host* (computer or hub) is connected, within about half
  a second of plugging in or unplugging.

Limits of the unmodified board (see the hardware table):

- **No "charging" indicator.** The charger's STAT pin isn't connected, so there's no
  way to tell "charging" from "plugged in, battery already full". Adding one would need
  a bodge wire from STAT to a spare GPIO plus a pull-up.
- **Wall chargers and power banks don't show `USB`.** The ESP32-S3 has no VBUS sense
  here. `USB` means the chip is seeing USB start-of-frame packets from a host
  (`HWCDC::isPlugged()`), and a charge-only supply never sends any. The board still
  charges from one; only the indicator doesn't know about it.
- **The % is an estimate from voltage alone.** The curve is for a battery at rest, but
  the battery is always either powering the board or charging:
  - On battery, the screen and Wi-Fi pull it down about 0.15V (measured: a nearly full
    cell reads 4.02V running vs. ~4.17V at rest). On the steep part of the curve that's
    worth ~20 points, so the firmware adds it back (`BAT_LOAD_SAG_V` in `Config.h`).
  - While charging, the charger pushes the battery up towards its 4.2V charge voltage,
    so the target reads high (100% once it reaches 4.2V) however full the battery really
    is. The on-screen % climbs towards it one point at a time, so you see a rising
    number, but it isn't a measure of the actual charge going in.
- Each reading is also logged to serial every 5 seconds
  (`power: 3.912 V, target 50.4%, shown 51%, usb host no`), which is handy for checking
  the curve against a multimeter.

## Staying up to date

Once connected, the device checks
`https://rcompton78.github.io/compton-diy/tamagotchi-plus/manifest.json` right away and
then every hour. If a newer release is published, it downloads it, shows a progress
screen, and reboots into the new version. Builds made outside the release workflow
report version `dev` and never auto-update, so a locally flashed build won't be
replaced by the published one.

## Development

```bash
pnpm nx run tamagotchi-plus:build                    # all boards (CI)
pnpm nx run tamagotchi-plus:build-waveshare-s3-169   # just this board
pnpm nx run tamagotchi-plus:flash-waveshare-s3-169   # build + upload over USB
pnpm nx run tamagotchi-plus:monitor                  # serial monitor
```

The serial port also carries Improv, so you may see Improv frames in the monitor.

Sprite assets are generated from the committed exports in `assets/` before every build
(the `gen-assets` target, which `build*`/`flash*` depend on), so there's no extra step.

## Graphics

### How a frame is drawn

The scene is pixel art at a low **logical resolution of 60×70**, and every logical pixel
is drawn as a crisp 4×4 block to fill the 240×280 panel. Rendering happens in
`src/FrameRenderer.cpp`:

1. **Scene layers, in palette indices.** Background → pet → effects (the heart) are drawn
   into a 60×70 canvas of 4-bit palette indices (4KB, internal RAM). Nothing is RGB yet.
2. **UI layer, full resolution.** Text is drawn with the normal TFT_eSPI calls onto a
   full-screen 4bpp `TFT_eSprite` (33KB, PSRAM), using the same locked palette. Index 0 is
   see-through, so text sits on top of the scene at full sharpness rather than 4× blocky.
3. **Compose + DMA in strips.** `present()` builds the frame 20 rows at a time: palette
   lookup, 4× horizontal/vertical expand, UI overlay (skipped entirely for rows with no
   UI pixels), and hands each strip to SPI DMA. It double-buffers: the next strip is
   composed while the previous one is still being sent.

The whole frame is always composed off-screen, so nothing is ever cleared and redrawn
where you can see it: no flicker. The pet only redraws when something changed (a new
animation frame, a heart moving by a logical pixel, a status update), capped at ~60fps.

**Why strips instead of one full-screen framebuffer in PSRAM:** a 240×280 RGB565 frame is
134KB, too big to keep in internal RAM next to Wi-Fi. On this Arduino core (ESP-IDF 4.4),
SPI DMA can't read PSRAM directly: the driver would bounce-copy the whole thing through
internal RAM anyway. Two 240×20 strips are 19KB of DMA-capable internal RAM and give the
same on-screen result. The full-screen state that does exist (the canvas and the UI
layer) stays in compact index form.

### Palettes and palette swaps

Every colour on the main screen comes from a **locked 16-entry palette**:
`assets/palette.hex`, 15 colours (the PICO-8 palette minus black) plus index 0 =
transparent. Sprites store 4-bit indices, not colours. `assets/palette-<name>.hex` files
recolour the same 15 slots. The "sick" palette (`palette-sick.hex`) turns the whole scene
olive and washed-out with no extra pixel data, just a different 16-entry lookup table.
`include/PaletteIndex.h` names the slots for code (background, UI text).

The sprites' outline colour is slot 1 (navy), so keep backgrounds off navy or outlines vanish.

### Library choice: TFT_eSPI (kept) vs LovyanGFX

Both drive this ST7789 over SPI with DMA on the ESP32-S3. We kept **TFT_eSPI**:

- **DMA works for what we need.** `initDMA()` + `pushImageDMA()` stream a strip while
  the CPU composes the next one, which is all the renderer asks of the library.
- **Scaling isn't needed from the library.** LovyanGFX's big win is sprite
  zoom/rotate (`pushRotateZoom`) plus palette sprites with nicer ergonomics. But integer
  4× scaling of a 60×70 index canvas is a trivial inner loop (4 stores per logical
  pixel), and doing it ourselves during compose avoids an intermediate full-size
  buffer. A generic zoom would be slower and would need that buffer.
- **Already set up and verified.** The panel's BGR order, 20px CGRAM offset and pins were
  verified on hardware in COM-295, and the OTA/Wi-Fi screens, touch driver and
  `libs/*` code all use TFT_eSPI (as does cyd-clock). Switching would mean re-deriving the
  panel config in LovyanGFX's format and porting all of that for no rendering gain.
- **What would change the decision:** if we later want rotation/scaling of large
  sprites at runtime, parallel-bus panels, or tearing-free output via the panel's TE
  signal (this board doesn't expose TE, so neither library can sync to it here),
  LovyanGFX is the stronger library. The renderer only touches the library in
  `present()`, so swapping later is contained.

### Performance

The firmware logs rough numbers to serial every 5 seconds while the scene is animating:

```
gfx: <fps> fps (redraw-on-change), frame <avg> ms avg / <max> ms max, compose <ms> ms avg | heap <n> free, psram <n> free | assets <n> B
```

Measured on the Waveshare board (40 MHz SPI):

| Metric | Value |
|---|---|
| Full frame (compose + DMA push, 240×280) | 27.7 ms avg, 27.8 ms max → ~36 fps ceiling |
| CPU compose time per frame | 1.5 ms: the rest is the SPI transfer itself (26.9 ms minimum at 40 MHz) |
| Redraw rate while idling | 5 fps: 5 idle frames per 1s loop, and it only redraws when the frame changes |
| Sprite pixel data (flash) | 8,860 B for 12 frames (4bpp: 11 pet frames at 40×40 + a 12×10 heart) |
| Canvas + DMA strips (internal RAM) | 4.2KB + 19.2KB |
| UI layer (PSRAM) | 33.6KB |
| Free heap / PSRAM at runtime | ~255KB / ~8.3MB |

The frame time is bound by the SPI clock, not the CPU. If faster motion is ever needed, raising
`SPI_FREQUENCY` to 80 MHz (the ST7789 usually tolerates it) would roughly halve it.

## Sprite workflow

Art is authored as Aseprite files, and the firmware is built from committed exports of them:

```
PixelLab MCP  →  PNG frames + manifest  →  png-to-aseprite.lua  →  <name>.aseprite
                                                                      │ (hand touch-ups)
                                                   export-sprites     ▼
firmware  ←  gen-assets (tools/sprites/convert.py)  ←  <name>.png + <name>.json (committed)
```

Layout of `assets/`:

| Path | What it is |
|---|---|
| `palette.hex`, `palette-*.hex` | Locked palette + alternates (one `RRGGBB` per line) |
| `<name>.aseprite` | Editable source of truth |
| `<name>.png` + `<name>.json` | Aseprite sheet export (`json-array`, with tags). **These are what the build reads.** |
| `src/<name>/` | Raw PNG frames + `manifest.json` that the `.aseprite` was imported from |

**Aseprite never runs in CI or the firmware build.** Its license doesn't allow
redistributing the binary, so it's a local-only tool. `gen-assets` reads only the
committed `.png` + `.json`, with a small standard-library-only Python script (no Pillow).
Always re-export and commit the sheet + JSON after editing a `.aseprite`.

The converter enforces the **palette lock**: in the Nx target (`--strict`), any
off-palette or partially transparent pixel fails the build. Run it by hand without
`--strict` to see every offending colour, snapped to the nearest palette colour:

```bash
python3 tools/sprites/convert.py --assets apps/tamagotchi-plus/assets --out /tmp/sprites.h
```

### 1. Generate frames with PixelLab

The repo's `.mcp.json` registers the PixelLab MCP for Claude Code sessions in this repo. It
reads the API key from the `PIXELLAB_SECRET` environment variable (stored in Bitwarden as
`shared/PIXELLAB_SECRET`), so export that before starting Claude Code.

What worked for the current pet (4 generations total):

1. `create_image_pixflux`: a 40×40 base frame, `no_background`, with the locked palette
   passed as `color_image_base64` (a 15×1 PNG of `palette.hex`) so it's already close to
   on-palette.
2. `animate_image` on that frame, once per tag (`idle` 4 frames, `happy` 6 frames).
3. `create_image_pixen` (16×16) for the heart, cropped to its content.

Download finished images with the API key (`?index=N` picks an animation frame; index 0
is the input frame):

```bash
curl -f -H "Authorization: Bearer $PIXELLAB_SECRET" -o frame.png \
  "https://api.pixellab.ai/mcp/images/<job_id>/download?index=1"
```

Save the frames as `assets/src/<name>/<tag>/NN.png` and write the `manifest.json` (below).

### 2. Import into Aseprite

`tools/sprites/png-to-aseprite.lua` builds a `.aseprite` from a folder of frames and a
`manifest.json` (tags, per-frame durations). It maps every frame onto the locked palette
(which also cleans up stray colours in AI output) and saves a normal, hand-editable file.
From the repo root:

```bash
aseprite -b \
  --script-param manifest=apps/tamagotchi-plus/assets/src/pet/manifest.json \
  --script-param palette=apps/tamagotchi-plus/assets/palette.hex \
  --script-param out=apps/tamagotchi-plus/assets/pet.aseprite \
  --script tools/sprites/png-to-aseprite.lua
```

`manifest.json` lists the tags in play order, each with its frames (paths relative to the
manifest) and per-frame durations. `direction` is optional (`forward`, `reverse` or
`pingpong`):

```json
{
  "tags": [
    { "name": "idle", "direction": "forward",
      "frames": [ { "file": "idle/00.png", "ms": 400 }, { "file": "idle/01.png", "ms": 200 } ] },
    { "name": "happy", "frames": [ { "file": "happy/00.png", "ms": 80 } ] }
  ]
}
```

Re-importing replaces the target `.aseprite` and loses any hand touch-ups made there since
the last import. So the script refuses to overwrite an existing file unless you add
`--script-param force=true`, and import is a one-off command per sprite rather than an Nx
target. For example, `heart.aseprite` has a hand recolour that a forced re-import would undo.

### 3. Export sheet + JSON

```bash
pnpm nx run tamagotchi-plus:export-sprites   # every assets/*.aseprite → <name>.png + <name>.json
```

### 4. Preview, build, flash

```bash
python3 tools/sprites/preview.py --assets apps/tamagotchi-plus/assets --out build/sprite-preview
pnpm nx run tamagotchi-plus:flash-waveshare-s3-169
```

`preview.py` (needs Pillow) writes an 8× PNG of every frame (one row per palette) and a
GIF per tag at the authored timings, rendered through the converter so it shows exactly
what the device will draw. It's handy for reviewing art in a chat before flashing.

### Installing Aseprite (for steps 2–3 only)

Aseprite is paid: buy it on Steam or itch.io (both include a Linux build), or compile it
from source ([instructions](https://github.com/aseprite/aseprite/blob/main/INSTALL.md)).
Put the `aseprite` binary on your `PATH`, or set `ASEPRITE=/path/to/aseprite` for
`export-sprites`. Tested with Aseprite 1.3.18.

**Headless / WSL:** both scripts run Aseprite with `-b` (batch mode), which never opens a
window, so they work in WSL, over SSH, or with `DISPLAY` unset. Use the **Linux** build
inside WSL, not the Windows `.exe` through interop: the scripts pass Linux paths.
Opening the editor UI for hand touch-ups does need a display (WSLg provides one on Windows
11), or edit the `.aseprite` in Aseprite on Windows directly.
