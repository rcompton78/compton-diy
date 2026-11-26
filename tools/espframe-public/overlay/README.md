# Espframe for Immich: 10" frame

Turn a Guition ESP32-P4 10" touchscreen (`JC8012P4A1`) into a private digital photo frame for your [Immich](https://immich.app/) photo library, with a 3D-printable desk stand.

<p align="center">
  <img src="docs/public/espframe.png" alt="Espframe showing Immich photos on a Guition ESP32-P4 touchscreen" width="700" />
</p>

This is a standalone copy of [jtenniswood/espframe](https://github.com/jtenniswood/espframe), the original project, with extra features and fixes, cut down to the 10" frame only. Fork it, build it, change it: your fork's frames only ever install your fork's firmware.

The frame shows your Immich library (or chosen albums, people, tags, memories or a date range), pairs portrait photos side by side, dims and warms the screen on a schedule, and has a built-in web page for every setting. Home Assistant works with it but isn't needed. The user guide is in [`docs/`](docs/); start with [Photo Sources](docs/photo-sources.md), [Screen Settings](docs/screen-settings.md) and [Troubleshooting](docs/troubleshooting.md).

## What you need

- A **Guition ESP32-P4 10" `JC8012P4A1`** display. The "10.1-inch ESP32P4 … ESP32 C6" board (`JC8012P4A1C_I_W_Y`) is the same hardware; clip its external antenna onto the C6 module.
- A **USB-C data cable** (not charge-only), plugged into the board's **bottom** USB-C port, labelled USB.
- An **Immich server** on your network and an [Immich API key](docs/api-key.md).
- Optional: a 3D printer for the [desk stand](hardware/stand-10/README.md).

## Which build: original or v3 chip

The board ships with two revisions of its ESP32-P4 chip, and one firmware can't run on both, so there are two builds:

- **v3 chip** (`--chip v3`): boards bought from late 2026 onwards, which have a v3.1 or later chip and a newer LCD panel. Pick this one if you're not sure and your board is new.
- **Original** (`--chip pre-v3`, the default): earlier boards.

If the screen stays black or the board keeps rebooting after installing one, install the other. Nothing is lost by trying the wrong one first. To check beforehand: `esptool` prints `ESP32-P4 (revision v3.2)` (for example) when it connects, and a serial monitor at 115200 baud shows `boot: chip revision: v3.2` when the board resets. v3.1 or later takes the v3 build, and v1.x takes the original. A v3.0 chip isn't supported by either build.

## Two ways to build and flash

Start by forking this repo on GitHub (**Fork**, top right). Then pick either path; you can use both.

### A. Let GitHub build it (no tools on your computer)

GitHub turns Actions off in new forks and never turns on Pages by itself, so do this once in **your fork**:

1. **Actions** tab → **I understand my workflows, go ahead and enable them**.
2. **Settings** → **Pages** → **Build and deployment** → **Source**: choose **GitHub Actions**.
3. **Actions** tab → **Release** → **Run workflow** (on `main`).

The first run takes 30 minutes or more (later ones reuse a cache). It runs the checks, builds both firmware builds and the stand's print files, attaches them to a new GitHub release, and publishes an install page at `https://<your-user>.github.io/<repo>/`. After that, every push to `main` makes a new release, except pushes that only change `README.md`, Markdown pages under `docs/`, `.gitignore` or files under `.claude/`. (If Pages is off, the Release run stops at its first step and says how to turn it on.) Only the newest commit on `main` is released: to retry a failed release, run the workflow again rather than re-running an old run.

To flash, open your install page in **Chrome or Edge** on a desktop computer, plug in the frame, click the **Install** button for your board's chip and pick its serial port. Frames installed from your page check your page's manifest for their build for updates (`manifest.json` for the original build, `manifest-guition-esp32-p4-jc8012p4a1-v3.json` for the v3 chip build), so they get your releases and nobody else's, and only the build for their chip.

Pull requests run the **Build** workflow instead: the checks, a compile of each firmware build and the stand build, with no release. You can also run it by hand on any branch from the Actions tab.

### B. Build and flash on your own computer

You need Linux, macOS or Windows with WSL, plus git and Python 3.11–3.13. Node.js 22 is needed only to change the web UI or `product/contract/` (see below). On Linux your user needs access to the serial port (e.g. the `dialout` group).

```bash
git clone https://github.com/<your-user>/<repo>.git
cd <repo>
scripts/firmware.sh install          # installs the pinned ESPHome into .venv (first time only)
scripts/firmware.sh compile          # builds the firmware (the first build downloads the toolchain)
scripts/firmware.sh flash --device /dev/ttyUSB0
scripts/firmware.sh logs  --device /dev/ttyUSB0
```

These build the original (pre-v3) chip build. For a v3 chip board, add `--chip v3` to `compile`, `flash` and `logs`, or put `CHIP=v3` in `.env` (below).

On macOS the port looks like `/dev/cu.usbserial-*` or `/dev/cu.usbmodem*`. On Windows, use WSL and attach the USB port to it with [usbipd-win](https://learn.microsoft.com/windows/wsl/connect-usb); it then shows up as `/dev/ttyUSB0` or `/dev/ttyACM0`. Once the frame is on Wi-Fi, `--device <frame-ip>` flashes and reads logs over the network.

A local build reports version `dev` and has **no update manifest**, so it doesn't check for updates. (A manifest URL you set on the frame under **Firmware** → **Advanced**, or restored from a backup, still applies until you clear it there or reinstall with erase.) To have a local build check your fork's releases, put the manifest URL in a `.env` file in the repo root before building:

```bash
FIRMWARE_MANIFEST_URL=https://<your-user>.github.io/<repo>/manifest.json
```

That's the original build's manifest. For a v3 chip board use the v3 build's instead, with `CHIP=v3` so every command builds it:

```bash
CHIP=v3
FIRMWARE_MANIFEST_URL=https://<your-user>.github.io/<repo>/manifest-guition-esp32-p4-jc8012p4a1-v3.json
```

Each manifest lists one build, and the other build's image can't boot your board, so `firmware.sh` refuses a URL that names the other chip's manifest.

`.env` is gitignored and read as plain `NAME=value` lines. It takes only `FIRMWARE_MANIFEST_URL`, `RELEASE_VERSION`, `CHIP` (the default `--chip`, `pre-v3` or `v3`), `DEVICE` (the default `--device` for `flash` and `logs`), `PLATFORMIO_CORE_DIR` (see [Developer notes](#developer-notes)) and `SIBLING_FIRMWARE_MANIFEST_URL` (the other build's manifest: a frame that has it saved, say from a backup of the other board, moves onto its own at boot. You rarely need it: when `FIRMWARE_MANIFEST_URL` names one of your fork's manifests, the script fills in the other, and it's refused without `FIRMWARE_MANIFEST_URL`); any other name stops the script with an error, so a misspelt setting can't be silently ignored. A `dev` build finds updates but never installs one on its own: press **Install** on the frame's web page, or also set `RELEASE_VERSION=` to a version of your own in `.env` so auto-update works. You can also set or change the URL later on the frame's web page, under **Firmware** → **Advanced**.

#### Local Wi-Fi build

`scripts/firmware.sh compile` builds the same firmware as a release: it starts a hotspot until you pick a network. For development, `devices/guition-esp32-p4-jc8012p4a1/dev.yaml` builds a frame called `immich-frame-10inch-dev` that joins your Wi-Fi straight away. It's the original (pre-v3 chip) build; there's no Wi-Fi entry point for the v3 build. Create `devices/guition-esp32-p4-jc8012p4a1/secrets.yaml` (gitignored) next to it:

```yaml
wifi_ssid: "your-network"
wifi_password: "your-password"
```

Then build and flash it with the pinned ESPHome (always go through `scripts/firmware.sh`, not a plain `esphome`, so you get the right version):

```bash
scripts/firmware.sh esphome run devices/guition-esp32-p4-jc8012p4a1/dev.yaml --device /dev/ttyUSB0
```

#### Developer notes

- **Firmware files.** `scripts/firmware.sh compile` writes `firmware.factory.bin` (full flash, for a new or erased frame) and `firmware.ota.bin` (update) to `builds/.esphome/build/immich-frame-10inch/.pioenvs/immich-frame-10inch/` (`immich-frame-10inch-v3` for `--chip v3`). A `dev.yaml` build lands under `devices/guition-esp32-p4-jc8012p4a1/.esphome/build/immich-frame-10inch-dev/` instead.
- **First compile.** It downloads several GB of ESP-IDF toolchain, so allow time and disk space. Later builds reuse it. `scripts/firmware.sh` keeps PlatformIO's toolchains and Python environment in `.platformio-core/` in the repo (gitignored), not the `~/.platformio` other PlatformIO and ESPHome projects share, so building here never changes them. Each clone or worktree downloads its own copy; to share one, set `PLATFORMIO_CORE_DIR` to a common folder in your environment or in `.env` (e.g. `PLATFORMIO_CORE_DIR=~/.cache/espframe-platformio`). Setting it to `~/.platformio` goes back to the shared folder, which a build here may change (it can recreate PlatformIO's Python environment).
- **No OTA password or API key.** OTA and the Home Assistant API are open to anyone on your network. Add `password:` under `ota:` and `encryption: key:` under `api:` in your own config if that matters to you.
- **Node.js.** Use Node.js 22, the version CI uses (`.nvmrc` pins it for `nvm use`).
- **Web smoke test.** `npm run test:web-smoke` drives a headless Chrome or Chromium; if it isn't in a standard place, set `CHROME_BIN` to its path.
- **Timezone check.** `npm run test:timezones` needs the pinned IANA data: `pip install -r scripts/requirements-timezones.txt` (into `.venv` or your own Python).

## Set up the frame

1. **Wi-Fi.** After flashing, the frame starts a Wi-Fi hotspot called **immich-frame-10inch** (**immich-frame-10inch-v3** with the v3 chip build). Join it from a phone and pick your network.
2. **Immich.** The frame shows its IP address. Open `http://<frame-ip>/` in a browser and enter your **Immich server URL** (e.g. `http://192.168.1.30:2283` or `https://photos.example.com`) and an **API key** ([permissions](docs/api-key.md)). Photos start loading.
3. **Choose photos and tune the screen** on the same web page: [Photo Sources](docs/photo-sources.md), [Screen Settings](docs/screen-settings.md), [Touch Controls](docs/touch-controls.md). For the stand's portrait mount, pick the portrait rotation that shows pictures upright.

[Home Assistant](docs/home-assistant.md), [backups](docs/backup.md) and [firmware updates](docs/firmware-update.md) each have their own page.

## Changing the code

- Device config: `devices/guition-esp32-p4-jc8012p4a1/` and the shared `common/addon/*.yaml`; the build entry points are `builds/guition-esp32-p4-jc8012p4a1.factory.yaml` and, for v3 chips, `builds/guition-esp32-p4-jc8012p4a1-v3.factory.yaml`, which adds the newer panel and a patched copy of ESPHome's `mipi_dsi` display component (`components/mipi_dsi/`, see its README). Custom C++ components are in `components/`.
- Several files are generated from `product/contract/*.json` and the web UI sources in `docs/webserver/src/` (the frame's settings page). After changing those, run `npm ci` once, then `npm run generate`, and commit the results; the Build workflow fails if they're stale. `npm run check:ci` runs the same checks as the Build workflow. `npm run check:fast` is a quicker static set that adds the product-contract, backup and compatibility checks but skips the typecheck and web tests. `npm run check:pr` is the full pre-PR set (both of those plus the browser smoke tests); the smoke tests need Chrome or Chromium, so set `CHROME_BIN` to its path if it isn't installed in a standard location.
- The firmware's ESPHome project name (shown in the boot log, the ESPHome dashboard and Home Assistant) is `project.package_name` in `product/contract/project.json`, in `author.project` form. In your own fork, set it to your name and run `npm run generate`. Update checks compare only the version, so renaming doesn't stop frames getting updates.
- Builds compile the custom components straight from this repo's `components/`, so nothing is downloaded from any other espframe repo. (`devices/guition-esp32-p4-jc8012p4a1/dev.yaml` is a local entry point with your own `secrets.yaml` for Wi-Fi; see [Local Wi-Fi build](#local-wi-fi-build). [Manual Setup](docs/manual-setup.md) covers the original project's ESPHome-dashboard install, not this repo.)
- The stand is a parametric [build123d](https://github.com/gumyr/build123d) model; see [its README](hardware/stand-10/README.md) to rebuild the print files locally.

## Licence and credit

Espframe is by [jtenniswood](https://github.com/jtenniswood/espframe) and contributors and is released under the [PolyForm Noncommercial License 1.0.0](LICENSE): free for personal and other noncommercial use, not for selling. See [NOTICE](NOTICE). Third-party components keep their own licences, listed in [`product/source-ownership.json`](product/source-ownership.json) with each licence file inside its component directory.
