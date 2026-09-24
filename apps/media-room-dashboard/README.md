# media-room-dashboard

ESPHome-based 6-button touchscreen remote for the media room. Each button
calls a Home Assistant service directly over the native API, and lights up
to reflect the real state of what it controls (TV power, active HDMI input,
light on/off). The device has no entities of its own in HA: it's a remote
with feedback, not a general dashboard.

Tracks Jira DIY-102 (buttons) and DIY-104 / COM-196 (state feedback).

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
  + touch controller + pins. Must define `tft_display`, `tft_touch`, and
  `backlight_output` component ids — nothing else in the app depends on
  board-specific detail beyond those three ids.
- `common/dashboard.yaml` — everything else: wifi/api/ota, the 6-button LVGL
  UI, and the HA service calls. Board-agnostic.
- `builds/<name>.yaml` — thin entrypoint combining the two as ESPHome
  packages. This is the file you point `esphome`/nx targets at.

To add a board: write `boards/<new-name>.yaml`, copy `builds/freenove-s3.yaml`
to `builds/<new-name>.yaml` swapping the `board:` package include, then add
matching `build-<new-name>`/`flash-<new-name>`/`monitor-<new-name>` targets
to `project.json` (see the `freenove-s3` ones for the pattern).

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

### Secrets in CI

`scripts/write-secrets.sh` generates `builds/secrets.yaml` if it doesn't
already exist (so it never clobbers a real local file), substituting each
key in `secrets.yaml.example` with an env var when set: `HA_<KEY>` first
(e.g. `HA_API_ENCRYPTION_KEY`), then plain `<KEY>` (`API_ENCRYPTION_KEY`),
then the example's placeholder. It's run automatically as a dependency of
the `build-freenove-s3` nx target.

For local builds that will actually talk to HA, export
`HA_API_ENCRYPTION_KEY` (the real key lives in bws as
`shared/HA_API_ENCRYPTION_KEY`). Without it the firmware gets the public
placeholder key, HA can't complete the native API handshake, and the device
shows as unavailable with every button silently doing nothing. If a
placeholder `secrets.yaml` was already generated, delete it first; the
script never overwrites an existing file.

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

| Button | Action | Lit (amber) when |
|---|---|---|
| Power On / Power Off | `remote.toggle` on `remote.living_room_tv` | TV is on (label names the action a tap performs) |
| Play Game | `remote.send_command` (HDMI1) on `remote.living_room_tv` | TV on and `media_player.living_room_tv`'s `app_id` is `tvinput.hdmi1` |
| Watch TV | `remote.send_command` (HDMI3) on `remote.living_room_tv` | TV on and `app_id` is `tvinput.hdmi3` |
| Window Lamp | `light.toggle` on `light.media_room_window_lamp_2` | light is on |
| Window Lights | `light.toggle` on `light.window_lights` | light is on |
| Standing Lamp | `switch.toggle` on `switch.media_room_plug` | switch is on |

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
  last input's `app_id` while in standby, so Play Game / Watch TV only
  light up while the TV is on.
- **`app_id`, not `source`.** `source` is the input's user-editable name on
  the TV, while `app_id` stays `tvinput.hdmiN`. The entity id and the two
  app ids are substitutions at the top of `common/dashboard.yaml`, in case
  they ever change.
- **Stale state is cleared.** When the last state-subscribed API client (i.e.
  HA) disconnects, every button drops back to neutral and the power button
  shows "Roku Power" again. HA re-sends all subscribed states on reconnect.
