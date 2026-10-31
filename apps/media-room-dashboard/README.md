# media-room-dashboard

ESPHome-based touchscreen remote for the media room. Each button
calls a Home Assistant service directly over the native API, and lights up
to reflect the real state of what it controls (TV power, active HDMI input,
light on/off). Apart from a few diagnostics (IP address, firmware version,
firmware update, battery voltage/level, USB host connected, the Dev Mode
switch, and the wake reason / sleep log / boot count used for battery
testing), the device has no entities of its own
in HA: it's a remote with feedback, not a general dashboard. Its IP is always on screen,
and it serves an admin portal with a firmware upload page at that address
(see "IP address and admin portal").

Tracks Jira DIY-102 (buttons) and DIY-104 / COM-196 (state feedback).

## Screen layout (COM-531)

`main_page` is design B from COM-530's mock-ups
(`docs/com-530-mockups/`: `index.html` shows every design, `png/b-1.png` and
`png/b-2.png` are the one built). From the top: a status bar ("Media Room",
the DEV badge and the battery readout), a **TV card** (a round power button,
a state line such as "TV is on / Showing: Game", and a Game | Watch TV
switcher), a row of three **light tiles**, and the `http://<ip>` footer.
The TV is blue when on, the lights amber; a pressed button turns dark grey
with a white outline.

The mock-ups are generated, not drawn: `python3 generate.py` rewrites
`index.html` and `svg/` (and the gitignored `widgets/`), and
`render.sh <font dir>` renders `svg/` to `png/` with the Montserrat TTFs from
https://github.com/JulietaUla/Montserrat/tree/master/fonts/ttf.
`generate.py`'s `design_b` holds every coordinate and colour `main_page`
uses. The fonts are Montserrat semibold (`gfonts://Montserrat@600`) and
Material Design Icons, both downloaded at build time with only the glyphs
used; the battery readout stays on LVGL's built-in `montserrat_12`, the only
fonts with the `LV_SYMBOL_BATTERY_*`/`LV_SYMBOL_USB` glyphs.

Two styles ESPHome's YAML can't set are applied in `on_boot`'s lambda: the
light tiles carry `LV_OBJ_FLAG_STATE_TRICKLE`, so their icon and name labels
see the tile's pressed/checked state and colour themselves (the icon is grey
and the name white when off, both dark on amber when on), and the switcher
halves get their dimmed text colour for the `user_1` state, which marks the
TV as not on.

## Board

Currently targets the **Freenove ESP32-S3 CYD** (2.8" ILI9341 + FT6336U
capacitive touch) — the same board `apps/espframe` and `apps/cyd-clock` use.

DIY-102 originally targeted a cheap AliExpress 2.8" ESP32 (classic, not S3)
board instead, but board bring-up stalled on an unknown display pinout (see
`docs/diy-102-bringup/HANDOFF.md`) — the Freenove S3 was swapped in so the
dashboard itself could get built and used while that's unresolved.

### Handheld enclosure (COM-445)

A 3D-printable remote-shaped case for the Freenove, the BQ25185 charger and a 504060 LiPo lives in
[`hardware/enclosure/`](hardware/enclosure/README.md) (Nx project `media-room-remote-enclosure`):
a parametric build123d model with fit checks, renders, print settings and assembly order.
The enclosure puts the Freenove's USB-C at the top end, which is upside down for the panel's
native portrait, so `boards/freenove-s3.yaml` turns the display (panel `transform`, i.e. MADCTL
scan flags, no software rotation) and the touchscreen (its own `transform`) 180°.

### Board HAL

Hardware config is isolated from app logic so a future board swap (e.g. once
the AliExpress board's pinout is known) doesn't require touching the button
UI or HA wiring:

- `boards/<name>.yaml` — hardware only: chip/PSRAM, backlight, SPI display
  + touch controller + pins. Must define `tft_display`, `tft_touch`,
  `backlight_output` (an on/off output), `deep_sleep_1` (deep_sleep with
  a touch wake source), `battery_level` (battery % sensor),
  `usb_host_connected` (binary sensor, on while a PC is attached) and a
  `set_light_sleep_allowed` script (parameter `allowed: bool`) — nothing
  else in the app depends on board-specific detail beyond those ids. Battery
  sensing, light sleep and the sleep diagnostics are board-specific too, so
  they live there.
- `common/dashboard.yaml` — everything else: wifi/api/ota, the six-button LVGL
  UI, and the HA service calls. Board-agnostic.
- `builds/<name>.yaml` — thin entrypoint combining the two as ESPHome
  packages. This is the file you point `esphome`/nx targets at.

To add a board: write `boards/<new-name>.yaml`, copy `builds/freenove-s3.yaml`
to `builds/<new-name>.yaml` swapping the `board:` package include, then add
matching `build-<new-name>`/`flash-<new-name>`/`monitor-<new-name>` targets
to `project.json` (see the `freenove-s3` ones for the pattern).

## Battery power (COM-214)

Three power tiers, layered:

1. **Automatic light sleep** (`components/light_sleep/`, always on): ESP-IDF
   power management drops the CPU/WiFi into light sleep during idle gaps.
   No reboot and no dropped HA connection. Requires the backlight to be a
   plain `gpio` output (light sleep stops LEDC PWM, which flickered the
   screen) and `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` (keeps the USB serial
   console alive while a host is attached). The CPU runs at 160MHz.
2. **Screen blank** after `idle_timeout` (30s) untouched, on `main_page`
   only: backlight off and LVGL paused. The connecting and setup pages never
   blank. The tap that wakes the screen only wakes it (LVGL is paused while
   the finger is down, and resumes on release); buttons respond from the
   next tap.
3. **Deep sleep** `deep_sleep_timeout` (30min) after the last touch while
   wifi is connected, or `disconnected_sleep_timeout` (5min) after it while
   wifi is down (router off, out of range), so a lost network can't run the
   battery flat. The connecting and setup pages stay lit until then. Only
   first-time setup (no saved wifi credentials) never sleeps, and a phone
   joined to the fallback AP (re-entering wifi details) holds it awake. The
   api's `reboot_timeout` is off, so a device on wifi that can't reach HA
   still reaches deep sleep instead of rebooting every 15 minutes. The panel goes
   into its own sleep mode (SLPIN) first. Touching the screen wakes it (ext0 on the touch
   controller's INT line, GPIO17), but that's a full reboot and reconnect.

**Firmware updates and deep sleep.** The update check runs on every boot,
which now means every touch wake (the 6-hourly poll never gets a chance:
the device sleeps 30 minutes after the last touch). An available update is
not installed on wake, which would tie the remote up right when it's picked
up. Instead it installs once the device has been idle for
`deep_sleep_timeout`, in place of that deep sleep (versioned builds only,
like before). The portal's **Install Firmware Update** button still installs
immediately.

**Release vs. local builds.** Release builds report their release version
(the `Firmware Version` sensor, e.g. `2026.10.16-143`): CI sets
`RELEASE_VERSION`, and `scripts/espframe-esphome.sh` passes it to the compile
as `-s firmware_version`. Local and PR builds report `dev`. A `dev` build
always differs from the manifest, so it shows "update available" all the time
and the idle auto-install skips it, so it doesn't replace a build you're
testing. As a result, a remote flashed from a local build stays on that build
until you put it back. Press **Install Firmware Update** in the portal once
(or `curl -X POST "http://<device-ip>/button/Install%20Firmware%20Update/press"`).
It then runs the published release, and the idle auto-install keeps it
current from then on.

The `Update check already in progress` warning right after a
wake is harmless: the boot-time check and the first poll land together, and
the second just returns.

For a quick on-device test, build with shorter timeouts from the command
line rather than editing them:
`scripts/espframe-esphome.sh -s idle_timeout 20s -s deep_sleep_timeout 30s -s disconnected_sleep_timeout 1min compile apps/media-room-dashboard/builds/freenove-s3.yaml`.

**Deep sleep on the ESP32-S3: what it took.** The first deep sleep + light
sleep build woke itself within seconds of sleeping, for two reasons:

- On the ESP32-S3, light sleep's GPIO wakeup doesn't work for deep sleep
  (only ext0/ext1 do), and it was left armed going into deep sleep. Light
  sleep now disarms itself in `on_powerdown()` and `deep_sleep_1` uses ext0.
- With ext0 alone it still woke after 3-20s (Sleep Log: `EXT0 ... (pin
  high)`): ext0 moves GPIO17 to the RTC mux, where the digital pull-up no
  longer applies, so the touch INT line floated. `on_powerdown()` now turns
  on the RTC pull-up.
- After a touch wake the screen stayed black: the S3 can't hold one pin
  through deep sleep, so ESP-IDF isolates and holds every digital pad, and
  on wake only releases the wake pin. `setup()` now releases the global
  hold before anything else configures its pins.
- That isolation also floated the touch chip's RST and I2C lines, and it
  still woke on its own every minute or two. They're now held (RST high,
  I2C pulled up) through deep sleep. As a backstop, a wake with no touch
  behind it (checked over I2C before wifi starts) goes straight back to
  sleep, logged as `no-touch` in the Sleep Log.

**Light sleep and the screen.** ESP-IDF's `CONFIG_PM_SLP_DISABLE_GPIO`
disables every pin during each light sleep, which flickered the backlight.
The light_sleep component's `keep_level_pins` (backlight, display CS/DC, touch I2C)
opts those pins out. Light sleep never runs while a USB host (a PC) is
attached (`CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION`), so test light sleep on a
charger or battery, not on PC USB.

Diagnostics for testing without a serial console, all in HA and the web
portal:

- **Wake Reason**: why this boot happened, e.g. `deep sleep, woke on EXT0`
  (a touch), `power-on`, `USB`, `software` (OTA/restart).
- **Sleep Log**: the last 5 boots, newest first, e.g.
  `#7 EXT0 after 42s (pin high)`: what woke it, how long it was down, and
  GPIO17's level as it went down (`LOW` there means it slept with the touch
  line already asserted). `no-touch` marks a wake the guard sent straight
  back to sleep, and `E<n>` that n GPIO/RTC/sleep calls failed while
  going down (Wake Reason likewise reports failed calls in setup). Kept in RTC memory; it starts over at `#1 power-on` after
  a power loss (e.g. moving the board to a charger).
- **Boot Count**: total boots, kept in flash, so it survives power loss.
  A device that keeps self-waking shows this climbing with no touches.

Battery voltage and a rough LiPo percentage come from the board's GPIO9
divider (`boards/freenove-s3.yaml`). The percentage is only an estimate from
voltage; a MAX17048 fuel gauge would be more accurate.

### Battery on screen (COM-446)

The right end of `main_page`'s status bar shows a battery glyph (full, 3/4, 1/2,
1/4, empty) and the %, e.g. `87%`. It turns amber below `battery_low_pct`
(20%) and red below `battery_critical_pct` (10%), and shows `--%` until the
first reading. It's driven by the existing `Battery Level` sensor, so it
costs no extra ADC reads or wakeups: the sensor reads once at boot (so every
deep-sleep wake shows a fresh value) and every 60s while awake.

While a PC is attached the glyph is a USB plug and the colour stays grey: the
battery is charging then, so it reads near its 4.2V charge voltage and the %
isn't a real charge level. A wall charger can't be told apart from battery
(the BQ25185 charger exposes no power-good or charge-status pin), so on a
charger it shows the normal readout, which reads high while charging. The
same battery can read several points apart on a PC and on a charger (85%
vs 91% in testing): charge current and load both shift the measured
voltage, and dev mode keeps the screen and CPU busy with no light sleep.

### Dev mode (COM-446)

While developing, blanking and deep sleep drop the USB connection and make
flashing and logging painful. Dev mode turns all three sleep tiers off:

- **On** while a PC is attached to the board's USB-C (the S3's native
  USB-Serial/JTAG), shown as the `USB Host Connected` diagnostic. ESP-IDF's
  `usb_serial_jtag_is_connected()` only sees a USB *host* sending frames,
  so a charger (the BQ25185's USB-C, or a plain charger on the board's
  port) never counts and battery use still sleeps. It turns off 3s after the
  host goes away, so esptool's reset mid-flash doesn't count as an unplug.
- **Or** while the `Dev Mode` switch (HA and the web portal) is on, for an
  OTA or log session over wifi. It's off at every boot and turns itself off
  after `dev_mode_timeout` (1h).

While it's on, the screen never blanks, neither deep-sleep countdown runs
(so the idle-time firmware auto-install doesn't either), and automatic light
sleep is blocked (an `ESP_PM_NO_LIGHT_SLEEP` lock in the light_sleep
component; a PC on USB already blocks it). An amber `DEV` badge shows in the
status bar. When it turns off, the 30s blank and 30min deep sleep count from that
moment, as if the screen had just been touched.

A remote already in deep sleep doesn't notice being plugged in until it's
touched once; after that it stays awake, so USB flashing works without
holding BOOT.

## Setup

```bash
# the remote's own key (bws media-room-dashboard/MEDIA_ROOM_DASHBOARD_API_ENCRYPTION_KEY,
# or a fresh `openssl rand -base64 32` for a new device)
echo "MEDIA_ROOM_DASHBOARD_API_ENCRYPTION_KEY=<key>" > apps/media-room-dashboard/.env
```

Every build generates `builds/secrets.yaml` from it; see [Secrets](#secrets).

No station wifi credentials go in `secrets.yaml` on purpose. The device has
no compile-time ssid/password at all — on first boot (or whenever it can't
join a network) it raises its own open fallback AP (`<friendly_name> Setup`,
no password — a short, physically-supervised setup window, so skipping a
password is one less thing to type on a phone) with a captive portal;
connect to it from a phone once and submit your real wifi credentials
there. While it isn't connected, the device's own screen (with the 6
dashboard buttons hidden) shows "Connecting to WiFi" while it retries its
saved network, then switches to "Connect to WiFi: '<friendly_name> Setup'"
once it gives up and the fallback AP is actually up, so there's an
on-device hint that setup is needed and which network to join.
ESPHome persists the credentials you submit to the ESP32's NVS flash
partition, separate from the OTA app partition, so they survive firmware
updates without ever needing to be baked into a build. Same mechanism as
espframe's factory image.

### Display init terminator

`boards/freenove-s3.yaml`'s custom `init_sequence` must end with a
`- [0x00]` entry. ESPHome doesn't terminate the sequence itself, so without
it the display driver sends whatever heap bytes follow as extra panel
commands, and an unrelated config change can leave the screen black while
the firmware keeps running normally (COM-213). This was the real cause of
the "device hangs switching to setup_page" failures once blamed on
captive_portal (DIY-105).

### Secrets

The remote has its own API encryption key, used by nothing else (COM-463):
`MEDIA_ROOM_DASHBOARD_API_ENCRYPTION_KEY`. It lives in bws as
`media-room-dashboard/MEDIA_ROOM_DASHBOARD_API_ENCRYPTION_KEY` and as the
same-named GitHub Actions repo secret, and both must hold the key HA was
given when the remote was paired. Release 2026.10.18-148 was built with a
shared `API_ENCRYPTION_KEY` that HA had never seen, so HA rejected the
remote ("Invalid encryption key") and every button did nothing.

`scripts/write-secrets.sh apps/media-room-dashboard` (the `write-secrets` nx
target, a dependency of `build-freenove-s3`) generates `builds/secrets.yaml`
from `secrets.yaml.example`. Each key comes from
`MEDIA_ROOM_DASHBOARD_<KEY>`, read from the shell or from a gitignored
`apps/media-room-dashboard/.env` (`.env` wins). Shared names like
`HA_API_ENCRYPTION_KEY` or `API_ENCRYPTION_KEY` are deliberately ignored.
When the var is missing, the key keeps the value from the last generated
file, else falls back to the example's public placeholder with a warning.
The script prints key and variable names, never values.

For local builds that will actually talk to HA, put the key in `.env` (or
export it):

```bash
echo "MEDIA_ROOM_DASHBOARD_API_ENCRYPTION_KEY=$(bws secret get <id> | jq -r .value)" \
  > apps/media-room-dashboard/.env
```

A build that ends up with the placeholder can't complete HA's native API
handshake, so the device shows as unavailable and every button silently
does nothing. The script regenerates a `secrets.yaml` it wrote itself (first
line `# Generated by scripts/write-secrets.sh ...`), but never touches a
hand-written one. That includes a file generated before COM-463, so delete
an old one once.

In `release.yml`, both build steps pass the GitHub secret and set
`REQUIRE_APP_SECRETS=1`. In that mode a missing per-app var fails the run
instead of shipping a placeholder key, so the secret has to exist before
anything that needs it is merged. `pr-build.yml` sets neither, so PR builds
compile with the placeholder.

**Rotating the key:** generate a new one (`openssl rand -base64 32`), then
update the GitHub secret and the bws entry. A secret-only change touches no
source, so the remote would be carried at its old version and never
offered. Run the CI workflow manually (Actions → CI → Run workflow on
`master`) with `force_bump` set to `media-room-dashboard`, press **Install
Firmware Update** on the portal, then re-enter the new key in HA (the
device's ESPHome entry, ⋮ > Reconfigure).

## Adding to Home Assistant

The device isn't auto-registered — after flashing, HA's built-in ESPHome
integration discovers it over mDNS and shows it under Settings > Devices &
Services > Discovered. Click Configure and enter the same
`api_encryption_key` from `secrets.yaml` (the remote's own key, see
[Secrets](#secrets)). The 6 buttons' `homeassistant.
service` calls only work after this pairing exists — they ride the native
API connection HA establishes with the device, not a generic REST call.

**Also required:** on the device's entry under Settings > Devices &
Services > ESPHome, open the ⋮ menu > Reconfigure and check "Allow the
device to perform Home Assistant actions". This defaults to off for newly
paired ESPHome devices; with it off, `homeassistant.service` calls are
silently dropped — no error on the device or in HA, the buttons just do
nothing. This is easy to mistake for a touch/firmware bug (it was, during
this app's initial bring-up) since nothing in the logs points to it.

## IP address and admin portal

The device's current IP is always on its own screen, so you never need the
router or HA to find it:

- **Main page:** a grey footer under the light tiles shows `http://<ip>`,
  which is also the admin portal's address. It's blanked the moment wifi drops, so
  it never shows a stale address.
- **Connecting page:** "Not connected", plus "Last IP: <ip>" once the device
  has had one this boot.
- **Setup page (fallback AP):** adds "Then open http://192.168.4.1", the
  AP's own address, for phones that don't pop the captive portal up
  themselves.

The same address is also an `IP Address` diagnostic text sensor
(`wifi_info`), visible in HA and in the portal.

### Admin portal

ESPHome's stock `web_server` (v3, port 80) at `http://<device-ip>/`. It
lists the diagnostic entities (IP, firmware version), the `Firmware Update`
entity's state (checked against the GitHub Pages manifest on every boot and
every 6 hours awake), **Check for Firmware Update** / **Install Firmware
Update** buttons (the
stock UI has no install control on the update entity itself; Install is a
no-op unless an update is available, and does run on "dev" builds since it's
a deliberate press), and an **OTA Update** upload form (`ota: platform:
web_server`). To flash a local build without USB, upload
`builds/.esphome/build/media-room-dash-freenove-s3/.pioenvs/media-room-dash-freenove-s3/firmware.ota.bin`
(or a release's `*-ota.bin`); the device flashes and reboots. `ota:
platform: esphome` is enabled too, so `scripts/espframe-esphome.sh upload
apps/media-room-dashboard/builds/freenove-s3.yaml --device <ip>` works over
the network. `local: true` embeds the UI's JS/CSS in the firmware, so the
portal works with no internet (e.g. from a phone on the setup AP).

**The portal is unprotected.** There's no login on the web UI or on either
OTA path, same as cyd-clock's and espframe's portals. Anyone on the LAN, or
on the open setup AP while it's up, can reflash the device. That's a
deliberate choice for a home-LAN remote. A protected design would put
`web_server: auth:` and an `ota:` password behind per-app build secrets
(`MEDIA_ROOM_DASHBOARD_*`) passed through `scripts/write-secrets.sh` and
`release.yml`.

How this compares to the other two apps:

| | cyd-clock | espframe | media-room-dashboard |
|---|---|---|---|
| Portal | WiFiManager (Arduino) custom pages | ESPHome `web_server` v3 + custom generated JS UI | ESPHome `web_server` v3, stock UI |
| Manual upload | WiFiManager's `/update` | none (manifest only) | `ota: platform: web_server` form |
| Manifest update | `libs/ota-update-client` | `update: http_request` | `update: http_request` |
| Auth | none | none | none |

`libs/ota-update-client` is Arduino C++, so it can't be used from ESPHome.
The `update: http_request` entity is the ESPHome equivalent, polling the
same `dist/<project>/manifest.json`.

`captive_portal` works alongside `web_server`. It only takes over requests
while the fallback AP is active, so the wifi setup form still comes up on
the AP, and the full portal is served at the station IP once connected.

## Build / flash / monitor

```bash
pnpm nx run media-room-dashboard:build-freenove-s3
pnpm nx run media-room-dashboard:flash-freenove-s3   # --device /dev/ttyACM0 by default
pnpm nx run media-room-dashboard:monitor-freenove-s3
```

Uses `scripts/espframe-esphome.sh` under the hood, which runs the ESPHome
version pinned in `apps/espframe/product/contract/project.json` and installs
it into a shared `~/.cache/compton-diy/esphome-<version>` venv on first use if
neither apps/espframe's own venv nor PATH has it, so no separate install is
needed.

## Button mapping

| Button | Action | Lit when |
|---|---|---|
| TV power (round button) | `remote.toggle` on `remote.living_room_tv` | TV is on (blue; the state line beside it says "TV is on"/"TV is off") |
| Game | `remote.send_command` (HDMI1) on `remote.living_room_tv` | TV on and `media_player.living_room_tv`'s `app_id` is `tvinput.hdmi1` (blue) |
| Watch TV | `remote.send_command` (HDMI3) on `remote.living_room_tv` | TV on and `app_id` is `tvinput.hdmi3` (blue) |
| Window Lamp | `light.toggle` on `light.media_room_window_lamp_2` | light is on (amber) |
| Window Lights | `light.toggle` on `light.window_lights` | light is on (amber) |
| Standing Lamp | `switch.toggle` on `switch.media_room_plug` | switch is on (amber) |

## State feedback

Button state comes back down the same native API connection the service
calls go out on: `common/dashboard.yaml` declares one `homeassistant` text
sensor per controlled entity (plus the Roku media_player's `app_id`
attribute for the active input), and each one's `on_value` sets the
matching button's LVGL `checked` state. All of them are `internal: true`,
so they don't show up as entities on the device in HA. Unlike the service
calls, these subscriptions don't need the "Allow the device to perform Home
Assistant actions" option: HA pushes subscribed states to any paired
device.

- **No optimistic toggling.** The buttons aren't LVGL-`checkable`, so a tap
  never changes the highlight by itself. It only changes once HA reports the
  new state, usually well under a second later.
- **Input buttons are gated on power.** The Roku can keep reporting its
  last input's `app_id` while in standby, so Game / Watch TV only light up
  while the TV is on, and both are dimmed while it isn't. The state line's
  "Showing: Game"/"Showing: TV" follows the same rule, and stays blank for
  any other app.
- **`app_id`, not `source`.** `source` is the input's user-editable name on
  the TV, while `app_id` stays `tvinput.hdmiN`. The entity id and the two
  app ids are substitutions at the top of `common/dashboard.yaml`, in case
  they ever change.
- **Stale state is cleared.** When the last state-subscribed API client (i.e.
  HA) disconnects, every button drops back to neutral and the state line
  shows just "TV" again. HA re-sends all subscribed states on reconnect.
