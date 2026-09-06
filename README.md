# compton-diy

An NX monorepo for DIY ESP32 and Arduino projects — IoT firmware, tooling, and shared libraries.

## Structure

This repo uses [Nx](https://nx.dev) to manage multiple projects in a single workspace. Firmware, libraries, and supporting tools live as separate Nx projects under `apps/` and `libs/`.

## Getting Started

```bash
pnpm install
```

Run a specific project:

```bash
pnpm nx run <project>:<target>
```

## Device Setup

- [Cyd-Clock Installation/Setup](apps/cyd-clock/README.md)

## Projects

### Apps

| Project | Board | Description |
|---|---|---|
| `cyd-clock` | ESP32-2432S028 (Cheap Yellow Display) / Freenove ESP32-S3 | Clock and countdown timer. Shows NTP time, current weather (Open-Meteo), and a touch-operated countdown timer with cat animation, plus a gamified pet-care mode with a store for cosmetic stuffies, toys, blankets, and room themes. Configures WiFi and location/timezone via a built-in web portal. See [`apps/cyd-clock/README.md`](apps/cyd-clock/README.md) for install and first-boot setup instructions. |
| `bambu-status-bar` | ESP32-S3 / ESP32-C3 | NeoPixel LED status bar for Bambu Lab printers. Connects to the printer via MQTT and reflects print status as colours on an addressable LED strip. Configured via a web UI at `http://bambulights.local`. |
| `espframe` | Freenove ESP32-S3 / Guition ESP32-P4 | ESPHome-based digital photo frame for [Immich](https://immich.app/) libraries. Vendored in-tree (not a submodule); see `apps/espframe/README.md` for setup. |
| `media-room-dashboard` | Freenove ESP32-S3 CYD | ESPHome-based 6-button touchscreen remote for the media room, calling Home Assistant services directly (Roku power/inputs, lamps, window lights). Board-specific hardware config is isolated behind a thin HAL (`boards/<name>.yaml`) so the app can be re-targeted at a different board later; see `apps/media-room-dashboard/README.md`. |

### Libraries

| Library | Description |
|---|---|
| `weather-client` | Shared ESP32 library that fetches current conditions (temperature, weather code) from the Open-Meteo free API over HTTPS. Used by `cyd-clock`. |
| `touch-driver` | Touch controller abstraction for ESP32 Arduino projects, with swappable backends (XPT2046 resistive, FT6336U capacitive) selected per board at compile time. Used by `cyd-clock`. |
| `ota-update-client` | GitHub Releases-based OTA update client for ESP32 Arduino projects — polls a project's `dist/<project>/manifest.json` on GitHub Pages for new firmware. Used by `cyd-clock`. |

### Tools

| Tool | Description |
|---|---|
| `esp-flasher` | Generates merged firmware binaries and ESP Web Tools manifests for the release workflow (`.github/workflows/release.yml`), and the browser-based flasher page served from GitHub Pages. |
