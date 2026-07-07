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

## Projects

### Apps

| Project | Board | Description |
|---|---|---|
| `cyd-clock` | ESP32-2432S028 (Cheap Yellow Display) | Clock and countdown timer. Shows NTP time, current weather (Open-Meteo), and a touch-operated countdown timer with cat animation. Configures WiFi and location/timezone via a built-in web portal. |
| `bambu-status-bar` | ESP32-S3 / ESP32-C3 | NeoPixel LED status bar for Bambu Lab printers. Connects to the printer via MQTT and reflects print status as colours on an addressable LED strip. Configured via a web UI at `http://bambulights.local`. |

### Libraries

| Library | Description |
|---|---|
| `weather-client` | Shared ESP32 library that fetches current conditions (temperature, weather code) from the Open-Meteo free API over HTTPS. Used by `cyd-clock`. |
