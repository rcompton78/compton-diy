# CYD Clock — Session Status

## What's Working

- Firmware flashes and boots successfully via WSL + usbipd (`/dev/ttyUSB0`)
- WiFiManager captive portal (`CYD-Clock` AP) connects to home WiFi on boot
- Portal includes custom fields for **Latitude**, **Longitude**, and **UTC Offset** — saved to LittleFS `/config.json`
- NTP sync working — correct time and date displayed
- Portrait layout (`rotation=0`, 240×320, USB at bottom) confirmed correct orientation
- Clock screen: `HH:MM` (font 7, large), seconds (font 4), date, weather row
- Timer screen: tap anywhere on clock to switch, tap to pause/resume, resets back to clock when done
- Flicker fixed — text draws with `TFT_BLACK` background (no `fillScreen` in draw loop)
- **No grey strip** — display driver was wrong (ILI9341 on an ST7789 panel). Fixed by switching to `ST7789_DRIVER=1` + `TFT_INVERSION_ON=1` + `CGRAM_OFFSET=1`

## Display Configuration (Confirmed)

- **Driver IC:** ST7789 (not ILI9341 as originally assumed — this specific board ships with ST7789)
- **Rotation:** 0 → portrait 240×320, USB at bottom
- **Key flags:** `ST7789_DRIVER=1`, `TFT_INVERSION_ON=1`, `CGRAM_OFFSET=1`
- Note: other CYD boards in this set have ILI9341 — keep separate platformio configs if flashing both

## Layout (Implemented)

Portrait 240×320, USB at bottom:

```
┌─────────────────────┐  y=0
│  12:34  ²³  18C Clear│  ← 40px header (clock + weather, always on)
├─────────────────────┤  y=40
│                     │
│    pixel cat        │  ← 175px animal zone (touch to feed)
│   (animated)        │
│                     │
│   tap to feed       │
├─────────────────────┤  y=215
│  [1m] [5m][10m][30m]│  ← 50px quick-pick buttons
├─────────────────────┤  y=265
│  05:00      [||] [X]│  ← 55px timer row (font 6 digits)
└─────────────────────┘  y=320
        [USB]
```

### Features
- **Header**: HH:MM + seconds + weather temp/description, redraws every second
- **Pixel cat**: white cat with ears, eyes (blink every 4s), whiskers, nose, paws, tail
  - Idle: neutral expression, slow blink
  - Happy (3s): big W-smile + sparkles — triggered by tapping the animal zone
  - Celebrating (6s): bounce + sparkles — triggered automatically when timer finishes
- **Quick-pick buttons**: [1m] [5m] [10m] [30m] — tap to start that timer, active button highlighted
- **Timer row**: MM:SS in font 6 (green=running, yellow=paused, red=finished)
  - [||/▶] button: pause / resume
  - [X] button: reset timer and clear active pick
- Dirty-flag rendering — only redraws zones that changed, no flicker

## Freenove ESP32-S3 CYD Bring-up

- Board attaches as `/dev/ttyACM0` (native USB-Serial/JTAG) rather than `/dev/ttyUSB0`
- Colors were inverted on first flash (white background, black cat) — `tft.invertDisplay(false)` in `setup()` was unconditionally undoing the `TFT_INVERSION_ON=1` build flag needed for this panel; now guarded to `BOARD_CYD` only
- Touch, backlight, and full app (clock/weather/timer/config UI) confirmed working
- Flash: `pnpm nx run cyd-clock:flash-freenove-s3`

## Known Issues

### Weather Not Showing
Location defaults to `0.0, 0.0` until set via the WiFiManager portal. To configure:
1. Reset WiFi credentials: power cycle while holding BOOT, or call `wm.resetSettings()` in code
2. Connect to `CYD-Clock` AP
3. Go to `192.168.4.1` in browser
4. Set WiFi credentials + latitude/longitude/UTC offset

### UTC Offset
Currently hardcoded default in `ConfigManager` — user needs to set via portal on first boot.

## Branch & Files

- Branch: `feature/DIY-1-cyd-clock-weather-timer`
- Jira card: DIY-1
- Main firmware: `apps/cyd-clock/src/main.cpp`
- Config: `apps/cyd-clock/include/Config.h`
- PlatformIO: `apps/cyd-clock/platformio.ini` (upload_port hardcoded to `/dev/ttyUSB0` for WSL)

## Flash Commands (WSL)

```bash
# Attach device (PowerShell, run once per session)
usbipd attach --wsl --busid <busid>

# Install tooling (first time only)
pnpm setup

# Build
pnpm nx run cyd-clock:build

# Flash firmware
pnpm nx run cyd-clock:flash-cyd

# Flash filesystem (only needed if data/ changes)
pnpm nx run cyd-clock:flash-fs

# Serial monitor
pnpm nx run cyd-clock:monitor
```

## CI Pipeline

- Triggers on every push to `master`
- Uses `nx affected` — only rebuilds changed apps
- Versioning via `@compton/nx-version` (creates `cyd-clock@x.y.z` tags)
- Deploys web flasher to GitHub Pages: `https://rcompton78.github.io/compton-diy/`
- `NPM_TOKEN` secret set on repo for Bytesafe `@compton` registry
- **Note:** `@compton/nx-version@0.9.1` depends on `@nx/devkit@20.8.0` but repo uses NX 22 — watch for runtime errors on first CI run
