# CYD Clock — Session Status

## What's Working

- Firmware flashes and boots successfully via WSL + usbipd (`/dev/ttyUSB0`)
- WiFiManager captive portal (`CYD-Clock` AP) connects to home WiFi on boot
- Portal now includes custom fields for **Latitude**, **Longitude**, and **UTC Offset** — saved to LittleFS `/config.json`
- NTP sync working — correct time and date displayed
- Portrait layout (`rotation=0`, 240×320, USB at top) is correct orientation
- Clock screen: `HH:MM` (font 7, large), seconds (font 4), date, weather row
- Timer screen: tap anywhere on clock to switch, tap to pause/resume, resets back to clock when done
- Flicker fixed — text draws with `TFT_BLACK` background (no `fillScreen` in draw loop)
- Full GRAM clear on boot: fills landscape first (`rotation=1`) then switches to portrait — this was the fix for leftover factory firmware pixels

## Known Issues

### Static on Right Side of Display
The right-hand strip of the physical display still shows static/noise pixels. Root cause is not fully confirmed — current theory is the physical panel is 320px wide but `rotation=0` only addresses 240 columns, leaving the rightmost ~80px uncleared. The two-step init (landscape fillScreen → portrait fillScreen) improved but did not fully resolve it.

**Things to try next session:**
- Try writing directly to GRAM outside of TFT_eSPI rotation (raw `writecommand` / `setAddrWindow` to cover full 320×240)
- Try `rotation=2` (portrait inverted) to see if it maps differently
- Check if `TFT_WIDTH`/`TFT_HEIGHT` build flags need swapping for this specific CYD variant
- Try `tft.fillRect(0, 0, 320, 320, TFT_BLACK)` before `setRotation` to blast all possible GRAM addresses

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

# Build
pnpm nx run cyd-clock:build

# Flash firmware
pnpm nx run cyd-clock:flash

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
