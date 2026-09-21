# Tamagotchi+

A virtual-pet firmware for the **Waveshare ESP32-S3-Touch-LCD-1.69** (1.69" 240×280
touch display with onboard accelerometer/gyroscope). This is the base scaffold: it
brings up the display and touch, handles Wi-Fi setup and self-updates, and shows a
placeholder main screen. Pet features are coming in later updates.

## Hardware

| Part | Details |
|---|---|
| MCU | ESP32-S3R8: 16MB flash, 8MB OPI PSRAM |
| Display | ST7789V2, 240×280 (SPI: MOSI 7, SCLK 6, CS 5, DC 4, RST 8, BL 15) |
| Touch | CST816T capacitive, I2C 0x15 (SDA 11, SCL 10, RST 13, INT 14) |
| IMU | QMI8658 6-axis, I2C 0x6B on the same bus (INT1 38). Not used yet |
| RTC | PCF85063, I2C 0x51 (INT 39). Not used yet |
| Power | SYS_EN (GPIO41) power-hold latch, driven HIGH at boot so the board stays on when running from battery |

Pin source: [Waveshare docs](https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69).

## Install (no cables, no toolchain)

1. Open **[https://rcompton78.github.io/compton-diy/](https://rcompton78.github.io/compton-diy/)**
   in Chrome or Edge (needs Web Serial) with the board plugged in over USB-C.
2. Pick **Tamagotchi+**, click **Connect**, choose the serial port and install.
3. When the install finishes, the flasher offers **Connect to Wi-Fi** (Improv serial).
   Pick your network and enter the password. The device joins right away, and the
   credentials are saved for later boots.

## First boot / Wi-Fi setup

If Wi-Fi wasn't set up from the flasher, or the saved network can't be reached, the
device opens a setup hotspot and the screen shows **Wi-Fi setup: join "Tamagotchi+ Setup"**.

- **Phone/laptop:** join the `Tamagotchi+ Setup` network. The captive portal should
  open on its own; if it doesn't, browse to `192.168.4.1`. Then pick your home network
  and enter its password.
- **Web flasher:** Improv serial keeps listening while the hotspot is up, so you can
  also reconnect the board to the flasher page and use **Connect to Wi-Fi**.

Either way, the credentials are saved to flash and the device reconnects on every boot.

## Main screen

Shows the name, firmware version, and live Wi-Fi status (network name + IP, connecting,
or setup mode). Tapping anywhere draws a ring at the touch point and increments a tap
counter, which confirms the display and touch are working.

## Staying up to date

Once connected, the device checks
`https://rcompton78.github.io/compton-diy/tamagotchi-plus/manifest.json` right away and
then every hour. If a newer release is published, it downloads it, shows a progress
screen, and reboots into the new version. Builds made outside the release workflow
report version `dev` and never auto-update, so a locally flashed build won't be
replaced by the published one.

## Development

```bash
pnpm nx run tamagotchi-plus:build                    # all boards (CI)
pnpm nx run tamagotchi-plus:build-waveshare-s3-169   # just this board
pnpm nx run tamagotchi-plus:flash-waveshare-s3-169   # build + upload over USB
pnpm nx run tamagotchi-plus:monitor                  # serial monitor
```

The serial port also carries Improv, so you may see Improv frames in the monitor.
