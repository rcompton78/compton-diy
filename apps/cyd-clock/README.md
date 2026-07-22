# CYD Clock

A desk clock/timer firmware for the "Cheap Yellow Display" (ESP32-2432S028R) and
Freenove ESP32-S3 2.8" boards. Shows the time, date, and local weather, doubles as a
countdown timer/stopwatch, and includes a virtual pet cat with care mechanics,
gamified points/levels, and a cosmetics store (colors, accessories, stuffies, room
themes) — all configurable from a web UI served by the device itself.

## Install (no cables, no toolchain)

Flash it straight from your browser using the project's web flasher:

**[https://rcompton78.github.io/compton-diy/](https://rcompton78.github.io/compton-diy/)**

1. Open that page in Chrome or Edge (Web Serial support required) on a computer with
   the board connected over USB.
2. Find the **CYD Clock** entry and pick the board variant that matches your
   hardware:
   - **cyd** — ESP32-2432S028R ("Cheap Yellow Display")
   - **freenove-s3** — Freenove ESP32-S3 2.8" display board
3. Click **Connect**, select the serial port, and follow the prompts to flash.

## First boot

1. The device boots into a WiFi setup portal. On your phone or laptop, connect to
   the WiFi network **`CYD-Clock`**, then open a browser to **`192.168.4.1`** and
   enter your home WiFi credentials.
2. Once connected, the screen shows **"Complete setup at: `<device IP>`"**. Open
   that IP in a browser on a device on the same network.
3. The setup page lets you name your cat, pick a starter color, and set your
   location (latitude/longitude) and UTC offset for weather and local time. Submit
   to finish — no reboot needed, the clock screen takes over immediately.

You can revisit settings, the cosmetics store, backups, and firmware update options
any time afterward at the same IP address.

## Staying up to date

Both boards check GitHub for new firmware periodically and can download and install
updates automatically (toggle this at `/config/update` on the device's web UI, which
also shows the current version and lets you trigger a manual check). You can also
re-flash anytime via the web flasher above.
