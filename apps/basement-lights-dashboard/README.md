# basement-lights-dashboard

ESPHome touchscreen for the basement lights, on two boards: an Elecrow
CrowPanel 5.0" HMI on the wall, in portrait (COM-533), and a Freenove ESP32-S3
2.8" CYD in a desk stand, in landscape (COM-629). Three toggles, **Office**,
**Board Games** and **All**, each call Home Assistant directly over the native
API and light up amber to show the real state. Like `media-room-dashboard`,
it's a remote with feedback, not a general HA dashboard.

Each toggle is bound to an HA light group, not to individual lights, so
adding or removing a light is a group edit in HA and never needs a reflash.
A group reads "on" while any of its lights is on.

| Toggle | HA group | Members (in HA) |
|---|---|---|
| Office | `light.dad_s_office_lights` | Dad Lamp Bulb, Dad Desk WLED, dad-socket outlet |
| Board Games | `light.basement_board_games` | Boardgame Lightbar, Board game socket, Server room socket |
| All | `light.basement_lights` | the two groups above |

The member lines on the CrowPanel's rows are static text in
`layouts/crowpanel-5.yaml`, so update them there if a group's members change.
The Freenove's tiles have no member lines.

## Files

Each build (`builds/<board>.yaml`) combines three packages:

| Package | What's in it |
|---|---|
| `boards/<board>.yaml` | Hardware only: chip, PSRAM, `tft_display`, `tft_touch`, `backlight` (a light). |
| `common/dashboard.yaml` | Everything shared: wifi, API, OTA and update checks, the portal, the HA group sensors and the scripts that repaint the toggles. |
| `layouts/<board>.yaml` | The screen: fonts, the LVGL pages, and when the screen goes idle. |

`common/dashboard.yaml`'s header lists what a layout must provide (page and
widget ids, a `wake_screen` script). A layout adds to the shared scripts with
`!extend` (the CrowPanel's All-row summary and IP footer) and to shared
triggers by repeating the key, since ESPHome appends lists from packages.

## Behaviour on both boards

A tap never flips a toggle by itself: the toggles aren't LVGL-`checkable`,
and the amber only changes once HA reports the new state (usually well under
a second). A pressed toggle shows a grey fill with a white outline. While HA
isn't connected, every toggle drops to neutral with a `--` pill.

A published firmware update installs on its own while the screen is idle (off
on the CrowPanel, the screen saver on the Freenove), so it never interrupts
someone using it. The manifest is checked every 6 hours and the idle state
every 15 minutes, so a release can take up to about 6¼ hours to install. Local
`dev` builds never auto-install; see [Admin portal](#admin-portal). The
backlight isn't exposed to HA: switched from outside, it would fall out of
step with the screen logic.

## CrowPanel 5": screen layout

COM-533 design P2, a "switch plate", picked from the mock-ups in
[`docs/com-533-mockups/`](docs/com-533-mockups/) (`python3 generate.py`
redraws them; `render.sh <montserrat-ttf-dir>` renders the PNGs). Landscape
designs A-C came first; the panel's USB-C exits the side, so it's mounted
portrait instead. The page is 480×800:

- **Header:** "Basement" on the left, the time (from HA) on the right.
- **Three equal full-width rows**, Office, Board Games and All lights: an
  icon, the name, a subtitle and an ON/OFF pill. Amber while on. The room
  rows list what's in the group; the All row says what's on ("Office is
  on", "Board Games is on", "Both rooms on", "Everything is off"). Tapping
  All toggles `light.basement_lights`: with either room on it turns
  everything off, with both off it turns everything on.
- **Footer:** the admin portal address, `http://<ip>`.

The panel is 800×480 natively; LVGL's `rotation:` (`screen_rotation`, 90 by
default) turns it and the touch together, in software. If the picture is
upside down for how it's mounted, set `screen_rotation` to 270.

## CrowPanel 5": screen on/off and burn-in

The backlight is either full on or off, never dimmed:

- **Motion turns it on.** The panel subscribes to the basement PIR,
  `binary_sensor.0xa4c138abf0cc23b5_occupancy` (`motion_sensor`), over the
  same HA connection. Every motion report turns the screen on and restarts
  the off timer.
- **Off an hour after the last motion** (`screen_on_time`) with no touch,
  counted from when the sensor clears, and never while it still reads
  motion.
  The PIR reports "on" for only ~35-70 s per movement and can't see someone
  sitting still, hence the long hold.
- **Touch wakes it without pressing anything.** While the screen is off LVGL
  is paused, so the waking tap never reaches a toggle; the screen comes on
  when the finger lifts, and the next tap acts. Every touch also restarts
  the off timer.
- **While HA is disconnected** it stays on: with no motion data, only a touch
  could wake it.
- **Pixel shift:** every 10 minutes (`pixel_shift_interval`) the whole main
  page moves to the next of nine offsets within ±4 px (`pixel_shift_px`).
  It's an LCD, so it doesn't burn in like an OLED, but a fixed layout can
  leave faint image retention, and the backlight off for most of the day is
  what saves its LEDs.

## Freenove 2.8": screen layout and screen saver

COM-629's mock-ups L1 and S1, picked from
[`docs/com-629-mockups/`](docs/com-629-mockups/) (same tooling as COM-533's).
The page is 320×240, landscape:

- **Header:** "Basement" on the left, the time (from HA) on the right. The
  portal address isn't on the main page; it's on the connecting page and in
  HA.
- **Three side-by-side tiles**, Office, Board Games and All lights, 100×200
  each: an icon, the name and an ON/OFF pill, amber while on. All toggles
  `light.basement_lights`, as on the CrowPanel.

There's no motion sensor, so a **screen saver** stands in for the CrowPanel's
backlight-off:

- **After 2 minutes with no touch** (`saver_after_s`, in seconds) the tiles
  give way to just the time, on black, with the backlight dimmed to 30%
  (`saver_brightness`, the PWM duty). The screen never goes fully dark.
- **The time jumps to a random spot every minute.** That's also the burn-in
  guard; the tiles are only up for a couple of minutes after a touch, so
  they need no pixel shift.
- **Touch wakes it without pressing anything.** The screen comes back when
  the finger lifts; the waking tap lands on the saver, which has nothing to
  press, so it never toggles a light. Every touch restarts the countdown.
- It only starts from the main page, once HA has sent the time (so it never
  shows a blank screen). HA going away doesn't stop it.

The panel is 240×320 natively; LVGL's `rotation:` (`screen_rotation`, 90 by
default) turns it and the touch to landscape. If the picture is upside down
in the stand, set `screen_rotation` to 270.

## Board: Elecrow CrowPanel 5"

**Elecrow CrowPanel 5.0" HMI, Basic edition**: ESP32-S3-WROOM-1-N4R8 (4 MB
flash, 8 MB octal PSRAM), 800×480 RGB parallel panel, GT911 capacitive
touch, in the stock acrylic case. Some listings call it a "32-bit LX6"
(original ESP32) board; it's an S3 (an RGB panel this size needs the S3's LCD
peripheral). It hangs on the wall in that case: a printed cradle on a
dovetail wall plate, [`hardware/wall-mount/`](hardware/wall-mount/README.md)
(COM-534), portrait with the USB-C at the bottom, which is what the default
`screen_rotation: "90"` draws upright.

| Function | Pins |
|---|---|
| RGB data, red (R1-R5) | 45, 48, 47, 21, 14 |
| RGB data, green (G0-G5) | 5, 6, 7, 15, 16, 4 |
| RGB data, blue (B1-B5) | 8, 3, 46, 9, 1 |
| DE / HSYNC / VSYNC / PCLK | 40 / 39 / 41 / 0 |
| Backlight (PWM) | 2 |
| Touch I2C (GT911) SDA / SCL | 19 / 20 (no reset or interrupt pin) |

Config notes (`boards/crowpanel-5.yaml`):

- **Display:** ESPHome's `mipi_rgb` with model `RPI` (a plain RGB panel with
  no init sequence) at a 12 MHz pixel clock, with the porch and pulse widths
  spelled out. They're the defaults of the deprecated `rpi_dpi_rgb`
  component that the community config on esphome-devices uses for this
  board. Raising the clock or changing the porches can make the image drift
  or flicker.
- **PSRAM and cache:** octal PSRAM at 80 MHz, a 64 KB data cache, and code
  and read-only data fetched from PSRAM, from the community config, so flash
  writes (OTA, NVS) don't starve the panel.
- **USB:** GPIO19/20 are the S3's native USB pins, and this board uses them
  for touch, so the native USB-JTAG port can't be used. Flashing and logs go
  through the on-board USB-UART bridge (`/dev/ttyUSB0`), and the logger is on
  UART0. ESPHome warns that GPIO19/20 conflict with USB-Serial-JTAG; that's
  expected.
- **Flash:** 4 MB, so each OTA slot is about 1.8 MB. The firmware uses about
  80% of it, so keep an eye on size before adding big fonts or images.
- **Flashing over USB** works with the board in the case; `esphome logs`
  holds the board in reset through the USB-UART bridge's RTS line, so read
  boot logs with a plain serial terminal at 115200 (or over wifi once it's
  on the network).

## Board: Freenove ESP32-S3 CYD 2.8"

**Freenove ESP32-S3 2.8" (FNK0104)**: ESP32-S3 with 16 MB flash and 8 MB
octal PSRAM, a 240×320 ILI9341 SPI panel and FT6336U capacitive touch. The
same board as `media-room-dashboard` and espframe's `freenove-s3`;
`boards/freenove-s3.yaml` is media-room's board file without its battery
sleep tiers (this one runs on USB power in a stand).

| Function | Pins |
|---|---|
| Display SPI CLK / MOSI / MISO | 12 / 11 / 13 |
| Display CS / DC | 10 / 46 |
| Backlight (PWM) | 45 |
| Touch I2C (FT6336U) SDA / SCL | 16 / 15 (reset and interrupt not used) |

Config notes:

- **Display:** ili9xxx `CUSTOM` with the alternate init sequence from
  espframe (the stock ILI9341 model leaves this panel white), ending in the
  required `[0x00]` terminator (see CLAUDE.md). `mirror_x: true` is the
  board's upright orientation; landscape is LVGL's job.
- **Touch:** no `reset_pin`: ESPHome's reset is too quick for the FT6336U, so
  its own power-on reset is used instead (the full story is in
  media-room-dashboard's board file).
- **Backlight:** LEDC PWM with `gamma_correct: 1.0`, so the screen saver's
  brightness is the duty cycle. (media-room uses a plain GPIO only because
  its light sleep stops LEDC.)
- **USB:** native USB-Serial/JTAG (`/dev/ttyACM0`) for flashing and logs.

## Setup

```bash
# the app's key, shared by both boards (bws
# basement-lights-dashboard/BASEMENT_LIGHTS_DASHBOARD_API_ENCRYPTION_KEY)
echo "BASEMENT_LIGHTS_DASHBOARD_API_ENCRYPTION_KEY=<key>" > apps/basement-lights-dashboard/.env
```

Every build generates `builds/secrets.yaml` from it; see [Secrets](#secrets).

There are no compile-time wifi credentials. On first boot (or whenever it
can't join its saved network) the panel raises an open fallback AP,
`Basement Lights Setup`, with a captive portal. The screen says
"Connecting to WiFi" while it retries, then "Connect to WiFi: 'Basement
Lights Setup'" once the AP is up. Join it from a phone and enter the wifi
details; they're kept in NVS and survive firmware updates.

### Secrets

Both boards share one API encryption key, used by nothing else (COM-463):
`BASEMENT_LIGHTS_DASHBOARD_API_ENCRYPTION_KEY`, kept in bws as
`basement-lights-dashboard/BASEMENT_LIGHTS_DASHBOARD_API_ENCRYPTION_KEY` and
as the same-named GitHub Actions repo secret. Both must hold the key HA was
given when the devices were paired; each device's HA entry keeps its own
copy.

`scripts/write-secrets.sh apps/basement-lights-dashboard` (the
`write-secrets` nx target, a dependency of each `build-<board>`) generates
`builds/secrets.yaml` from `secrets.yaml.example`, reading
`BASEMENT_LIGHTS_DASHBOARD_API_ENCRYPTION_KEY` from the shell or the
gitignored `apps/basement-lights-dashboard/.env` (`.env` wins). Without it,
the build uses the example's public placeholder with a warning; that build
can't pair with HA, so every toggle silently does nothing.

`release.yml` passes the GitHub secret in both build steps with
`REQUIRE_APP_SECRETS=1`, so **the repo secret must exist before this app is
merged**, or the release run fails. To rotate the key, update the GitHub
secret and bws, run the CI workflow manually with `force_bump` set to
`basement-lights-dashboard`, then for **each** device press **Install
Firmware Update** on its portal and re-enter the new key in HA (that device's
ESPHome entry, ⋮ > Reconfigure). A device left on the old key in HA stops
toggling anything.

## Adding to Home Assistant

After flashing, HA's ESPHome integration discovers each device under Settings
> Devices & Services > Discovered. Click Configure and enter the API key from
[Secrets](#secrets).

**Also required:** on the device's ESPHome entry, ⋮ > Reconfigure, check
"Allow the device to perform Home Assistant actions". It defaults to off,
and with it off every tap is silently dropped: the toggles still show the
right state (HA pushes subscribed states regardless), they just don't
switch anything.

## Admin portal

ESPHome's stock web UI at `http://<ip>` (the CrowPanel's footer, or either
board's connecting page; v3, embedded, so it works from a phone on the setup
AP too). It shows the firmware version, IP
address and the **Firmware Update** entity with **Check for
Firmware Update** / **Install Firmware Update** buttons, plus an upload form
for flashing a local build without USB. It's deliberately unprotected, like
the other ESPHome apps here: anyone on the LAN can reflash it.

Release builds check their manifest on GitHub Pages every 6 hours: the
CrowPanel `basement-lights-dashboard/manifest.json`, the Freenove
`manifest-freenove-s3.json`. Both boards are ESP32-S3s, so the shared
manifest can only hold one of them; the release keeps the CrowPanel in it
(`--shared-manifest crowpanel-5`) because the one on the wall was deployed
polling that URL, and would otherwise never see another update.

A local build reports version `dev`, always shows "update available", and
never auto-installs; pressing **Install Firmware Update** replaces it with the
latest release.

## Build / flash / monitor

```bash
pnpm nx run basement-lights-dashboard:build-crowpanel-5
pnpm nx run basement-lights-dashboard:flash-crowpanel-5     # --device /dev/ttyUSB0
pnpm nx run basement-lights-dashboard:monitor-crowpanel-5  # see below

pnpm nx run basement-lights-dashboard:build-freenove-s3
pnpm nx run basement-lights-dashboard:flash-freenove-s3     # --device /dev/ttyACM0
pnpm nx run basement-lights-dashboard:monitor-freenove-s3
```

`build` builds both boards.

`monitor-crowpanel-5` runs `esphome logs` over USB, which holds the board in
reset through the USB-UART bridge's RTS line, so it shows nothing. Read boot
logs with a plain serial terminal at 115200 instead (DTR/RTS low), or once
the panel is on wifi, `scripts/espframe-esphome.sh logs
apps/basement-lights-dashboard/builds/crowpanel-5.yaml --device <ip>`.

Uses `scripts/espframe-esphome.sh`, which runs the ESPHome version pinned in
`apps/espframe/product/contract/project.json` (installing it into a shared
`~/.cache/compton-diy/esphome-<version>` venv on first use). After the first
USB flash, `esphome upload --device <ip>` or the portal's upload form work
over wifi.
