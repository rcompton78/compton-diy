# media-room-dashboard

ESPHome-based 6-button touchscreen remote for the media room. Each button
calls a Home Assistant service directly over the native API — this device
has no local entities of its own, it's purely a remote control.

Tracks Jira DIY-102.

## Board

Currently targets the **Freenove ESP32-S3 CYD** (2.8" ILI9341 + FT6336U
capacitive touch) — the same board `apps/espframe` and `apps/cyd-clock` use.

DIY-102 originally targeted a cheap AliExpress 2.8" ESP32 (classic, not S3)
board instead, but board bring-up stalled on an unknown display pinout (see
`docs/diy-102-bringup/HANDOFF.md`) — the Freenove S3 was swapped in so the
dashboard itself could get built and used while that's unresolved.

### Board HAL

Hardware config is isolated from app logic so a future board swap (e.g. once
the AliExpress board's pinout is known) doesn't require touching the button
UI or HA wiring:

- `boards/<name>.yaml` — hardware only: chip/PSRAM, backlight, SPI display
  + touch controller + pins. Must define `tft_display`, `tft_touch`,
  `backlight_output`, and `deep_sleep_1` (with its wake source configured)
  component ids — nothing else in the app depends on board-specific detail
  beyond those four ids.
- `common/dashboard.yaml` — everything else: wifi/api/ota, the 6-button LVGL
  UI, and the HA service calls. Board-agnostic.
- `builds/<name>.yaml` — thin entrypoint combining the two as ESPHome
  packages. This is the file you point `esphome`/nx targets at.

To add a board: write `boards/<new-name>.yaml`, copy `builds/freenove-s3.yaml`
to `builds/<new-name>.yaml` swapping the `board:` package include, then add
matching `build-<new-name>`/`flash-<new-name>`/`monitor-<new-name>` targets
to `project.json` (see the `freenove-s3` ones for the pattern).

## Battery power (COM-214)

Two of COM-214's three options are active, layered together:

- **Automatic light sleep** (option 3, `components/light_sleep/`): ESP-IDF
  power management drops the CPU/WiFi into light sleep during idle gaps.
  No reboot and no dropped HA connection, so wake is instant. Requires the
  backlight to be a plain `gpio` output (light sleep stops LEDC PWM, which
  flickered the screen) and `CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION` (keeps the
  USB serial console alive while a host is attached).
- **Backlight off on idle** (option 2): after `idle_timeout` (20s) with no
  touches the backlight turns off; the next touch turns it straight back on.

**Deep sleep (option 1) is disabled.** On its own it worked, but a
120-minute deep-sleep tier on top of light sleep self-wakes within seconds of
sleeping, with no touch. Three GPIO17 fixes didn't help; details are in the
commented-out tier in `common/dashboard.yaml` and in
`components/light_sleep/light_sleep_component.h`. Untested theory: every test
ran with USB attached, so try again on battery with USB unplugged.

Battery voltage and a rough LiPo percentage come from the board's GPIO9
divider (`boards/freenove-s3.yaml`). The percentage is only an estimate from
voltage; a MAX17048 fuel gauge would be more accurate.

## Setup

```bash
cp apps/media-room-dashboard/builds/secrets.yaml.example apps/media-room-dashboard/builds/secrets.yaml
# edit secrets.yaml: api_encryption_key (generate with `openssl rand -base64 32`)
```

No station wifi credentials go in `secrets.yaml` on purpose. The device has
no compile-time ssid/password at all — on first boot (or whenever it can't
join a network) it raises its own open fallback AP (`<friendly_name> Setup`,
no password — a short, physically-supervised setup window, so skipping a
password is one less thing to type on a phone) with a captive portal;
connect to it from a phone once and submit your real wifi credentials
there. The device's own screen shows the device name plus "if setup is
needed, join '...' wifi" (with the 6 dashboard buttons hidden) whenever
it isn't connected, so there's an on-device hint this step is needed.
ESPHome persists the credentials you submit to the ESP32's NVS flash
partition, separate from the OTA app partition, so they survive firmware
updates without ever needing to be baked into a build. Same mechanism as
espframe's factory image.

This screen intentionally shows one combined message rather than a
distinct "still connecting" vs. "join the setup AP" page — every attempt
to split those into two separate LVGL pages (switched once the fallback
AP is confirmed active) reliably hung the device solid the moment the
switch happened, across four different trigger mechanisms. See DIY-105
before attempting that split again.

### Secrets in CI

`scripts/write-secrets.sh` generates `builds/secrets.yaml` if it doesn't
already exist (so it never clobbers a real local file), substituting each
key in `secrets.yaml.example` with a same-named env var when set. It's run
automatically as a dependency of the `build-freenove-s3` nx target.

In `release.yml` (push-to-master builds), `API_ENCRYPTION_KEY` is exported
from a GitHub Actions repo secret before the build, so the published/OTA
binary gets the real value. `pr-build.yml` doesn't set it, so PR builds
fall back to the placeholder in `secrets.yaml.example` and still compile.
Adding a future project's own secrets works the same way — its own
`secrets.yaml.example` + a matching script + matching repo secrets, no
workflow file changes needed.

## Adding to Home Assistant

The device isn't auto-registered — after flashing, HA's built-in ESPHome
integration discovers it over mDNS and shows it under Settings > Devices &
Services > Discovered. Click Configure and enter the same
`api_encryption_key` from `secrets.yaml`. The 6 buttons' `homeassistant.
service` calls only work after this pairing exists — they ride the native
API connection HA establishes with the device, not a generic REST call.

**Also required:** on the device's entry under Settings > Devices &
Services > ESPHome, open the ⋮ menu > Reconfigure and check "Allow the
device to perform Home Assistant actions". This defaults to off for newly
paired ESPHome devices; with it off, `homeassistant.service` calls are
silently dropped — no error on the device or in HA, the buttons just do
nothing. This is easy to mistake for a touch/firmware bug (it was, during
this app's initial bring-up) since nothing in the logs points to it.

## Build / flash / monitor

```bash
pnpm nx run media-room-dashboard:build-freenove-s3
pnpm nx run media-room-dashboard:flash-freenove-s3   # --device /dev/ttyACM0 by default
pnpm nx run media-room-dashboard:monitor-freenove-s3
```

Uses `scripts/espframe-esphome.sh` under the hood (apps/espframe's own
ESPHome venv — no separate install needed).

## Button mapping

| Button | Action |
|---|---|
| Roku Power | `remote.toggle` on `remote.living_room_tv` |
| Play Game | `remote.send_command` (HDMI1) on `remote.living_room_tv` |
| Watch TV | `remote.send_command` (HDMI3) on `remote.living_room_tv` |
| Window Lamp | `light.toggle` on `light.media_room_window_lamp_2` |
| Window Lights | `light.toggle` on `light.window_lights` |
| Lamp | `switch.toggle` on `switch.media_room_plug` |
