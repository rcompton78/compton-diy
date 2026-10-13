---
title: Troubleshooting Espframe Install and Immich Setup
description: Fix common Espframe setup problems, including web installer failures, wrong firmware after an update, WiFi setup, Immich connection errors, API key permissions, and missing photos.
---

# Troubleshooting Espframe Install and Immich Setup

Use this page when Espframe does not flash, connect to WiFi, reach Immich, or show photos from your library.

## Web Installer Problems

If the browser installer cannot find the display, first check the basics:

- Use Chrome or Edge on a desktop computer.
- Use the bottom USB-C port on the Guition ESP32-P4 display.
- Use a USB-C data cable, not a charge-only cable.
- Close any other serial monitor, ESPHome dashboard, or flashing tool.

See [USB Flashing Help for Guition ESP32-P4](/usb-flashing) for more detail.

## Wrong Firmware After an Update

Before per-device update manifests, a Waveshare ESP32-S3-Touch-LCD-7 could auto-update to the Freenove ESP32-S3 firmware, because both boards are ESP32-S3 and shared one manifest. The 7" then shows a black screen (the backlight stays off) and appears on the network as **Immich Frame Freenove S3** / `immich-frame-freenove-s3`.

The Freenove firmware still joins your WiFi, so you can recover without opening the frame. Use either:

- **USB:** open [ESPHome Web](https://web.esphome.io/) in Chrome or Edge, connect the 7" over USB, choose **Install**, and select `espframe-waveshare-esp32-s3-touch-lcd-7.bin` from the latest [release](https://github.com/rcompton78/compton-diy/releases) or from `https://rcompton78.github.io/compton-diy/espframe/`.
- **Network:** push the Waveshare OTA image (`espframe-waveshare-esp32-s3-touch-lcd-7-ota.bin`, from the same places) over ESPHome's native OTA, port 3232. From a compton-diy checkout:

  ```bash
  scripts/espframe-esphome.sh upload apps/espframe/builds/waveshare-esp32-s3-touch-lcd-7.factory.yaml \
    --device <frame-ip> --file espframe-waveshare-esp32-s3-touch-lcd-7-ota.bin
  ```

  This speaks ESPHome's OTA protocol, so Arduino's `espota.py` won't work here.

Before this fix, the espframe web installer had a single **Install** button that picked the Freenove image for any ESP32-S3. It now has a separate button per device; pick the Waveshare one.

After recovering, the frame checks its own manifest. If it was flashed with firmware older than this fix, see [the old shared manifest](/firmware-update#the-old-shared-manifest).

## WiFi Setup Problems

After flashing, Espframe should either ask for WiFi details or create a setup hotspot named **immich-frame-10inch**.

If you do not see the frame on your network:

- Check that the WiFi name and password were entered correctly.
- Make sure the frame is close enough to the access point.
- Look again for the **immich-frame-10inch** hotspot from a phone or laptop.
- Reboot the frame and wait for the setup screen to appear.

## Immich Connection Problems

The Immich server URL must include `http://` or `https://`. Local IP addresses and domain names both work, for example:

```text
http://192.168.1.30:2283
https://photos.example.com
```

If the frame cannot connect:

- Open the same URL from a browser on the same network.
- Confirm the Immich server is running.
- Check that the frame and Immich server can reach each other across your network.
- If using HTTPS, confirm the address works cleanly from another device.

## API Key Problems

Espframe needs a read-only Immich API key. If the key is missing permissions, photos or metadata may fail to load.

Create a fresh key using the recommended [Immich API key permissions for Espframe](/api-key), then paste it into the frame web UI.

## Photos Do Not Appear

If the frame connects but does not show the photos you expect:

- Start with **All Photos** as the source to confirm the basic connection works.
- Check that favorites, albums, people, or memories exist in Immich before selecting those sources.
- Confirm album and person UUIDs were copied from the Immich URL correctly.
- Review [Espframe Photo Sources for Immich](/photo-sources) for each source type.
- Disable date filtering temporarily if the selected range may exclude all photos.

## Screen or Display Issues

Display behavior is configured from the frame web UI:

- Use [screen brightness and display settings](/screen-settings) for brightness, schedules, and rotation.
- Use [screen tone and night warmth](/screen-tone) if the panel looks too blue or too warm.
- Use [touch controls](/touch-controls) if you need wake, sleep, or next-photo gestures.

## Manual ESPHome Builds

If you are building locally instead of using the web installer, start with [ESPHome Manual Setup for Espframe](/manual-setup). Manual setup is useful when you want direct control over YAML substitutions, secrets, and local build behavior.
