---
title: Espframe Firmware Updates
description: Configure Espframe over-the-air and HTTP firmware updates from GitHub, including auto-update, check frequency, and manual checks.
---

# Espframe Firmware Updates

OTA and HTTP updates from GitHub. Each device checks its own stable firmware manifest on GitHub Pages. Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

| Device | Default manifest URL |
|--------|----------------------|
| Guition ESP32-P4 10" | `https://rcompton78.github.io/compton-diy/espframe/manifest-guition-esp32-p4-jc8012p4a1.json` |
| Waveshare ESP32-S3-Touch-LCD-7 7" | `https://rcompton78.github.io/compton-diy/espframe/manifest-waveshare-esp32-s3-touch-lcd-7.json` |
| Freenove ESP32-S3 CYD | `https://rcompton78.github.io/compton-diy/espframe/manifest-freenove-s3.json` |

Each of these lists a single build. ESPHome installs the first build in a manifest whose chip family matches the device, and the Waveshare 7" and the Freenove board are both ESP32-S3, so a manifest listing both would install whichever came first on either board.

### The old shared manifest

Devices used to share `https://rcompton78.github.io/compton-diy/espframe/manifest.json`, and a saved manifest URL survives reflashing. That URL now lists only the ESP32-P4 build, so an ESP32-S3 device still pointed at it finds no compatible build and stops updating rather than installing another board's firmware. Firmware from this release onward moves a device off the shared URL onto its own manifest at boot.

A device still on older firmware has to get the new firmware once some other way:

- set **Stable Manifest URL** (under **Firmware** → **Advanced**) to the device's manifest URL from the table above, then press **Check for Update** and install, or
- flash it over USB or ESPHome OTA (see [Troubleshooting](/troubleshooting#wrong-firmware-after-an-update)).

The 10-inch frame keeps updating from the shared URL and migrates on its next update.

During OTA updates, Espframe keeps the current backlight state while the update starts. If Home Assistant or the screen sleep control had already turned the display off, the screen stays off after the update reboot until it is woken again.

<!-- ESPFRAME:SETTINGS_TABLE firmware_controls START -->
| Control | Type | Default | Description |
|---------|------|---------|-------------|
| **Auto Update** | Switch | On | Check at selected frequency and install when available |
| **Update Frequency** | Select | Daily | Hourly, Daily, Weekly, or Monthly |
| **Stable Manifest URL** | Text | Device default | Advanced: custom stable update manifest |
<!-- ESPFRAME:SETTINGS_TABLE firmware_controls END -->

| Status or action | Type | Description |
|------------------|------|-------------|
| **Version** | Text sensor | Installed version |
| **Check for Update** | Button | Check stable firmware; does not install |

**Check for Update** only checks for updates; it does not install. To install, use the **Install** button that appears when a stable update is available, or turn on **Auto Update** so the device installs at the selected frequency.


## ESP32-C6 Wi-Fi Coprocessor Updates

The 10-inch ESP32-P4 frame also exposes separate controls in the WiFi section for the ESP32-C6 Wi-Fi coprocessor firmware. These use ESPHome's hosted ESP32-C6 firmware manifest, not the Espframe display firmware manifest.

| Status or action | Type | Description |
|------------------|------|-------------|
| **ESP32-C6: Update Available** | Text sensor | Shows whether the coprocessor firmware is up to date |
| **ESP32-C6: Current Firmware** | Text sensor | Installed ESP32-C6 firmware version |
| **ESP32-C6: Available Firmware** | Text sensor | Latest compatible ESP32-C6 firmware version |
| **Firmware ESP32-C6: Check for Update** | Button | Check for a coprocessor firmware update |
| **Firmware ESP32-C6: Install Update** | Button | Install the available coprocessor firmware update |

Advanced users can point the stable manifest URL at another ESP-Web-Tools style manifest. Use a full http:// or https:// URL. Firmware downloads then follow the paths declared by that manifest.
