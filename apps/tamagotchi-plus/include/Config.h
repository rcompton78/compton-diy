#pragma once

// Firmware version, injected via platformio.ini from RELEASE_VERSION
// (scripts/pio.sh defaults it to "dev" outside the release workflow).
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

#define DEVICE_NAME "Tamagotchi+"

// Display (portrait, with the USB-C port on the left edge of the screen). The panel has
// rounded corners, so keep anything important at least ~20px in from each corner.
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 280

#if defined(BOARD_WAVESHARE_S3_169)

// Waveshare ESP32-S3-Touch-LCD-1.69 — pins per https://docs.waveshare.com/ESP32-S3-Touch-LCD-1.69
// (the LCD SPI pins are passed as platformio.ini build flags so TFT_eSPI sees them).
#define TFT_BACKLIGHT_PIN 15

// Shared I2C bus: CST816T touch (0x15), QMI8658 IMU (0x6B), PCF85063 RTC (0x51)
#define I2C_SDA   11
#define I2C_SCL   10
#define TOUCH_RST 13
#define TOUCH_IRQ 14
#define IMU_INT1  38  // not used yet — reserved for pet features
#define RTC_INT   39  // not used yet

// Power-hold latch: must be driven HIGH early in boot or the board powers off when
// running from battery once the power button is released.
#define SYS_EN_PIN  41
#define SYS_OUT_PIN 40

// Passive buzzer (not used yet). Held LOW so it doesn't idle-drain/heat the board.
#define BUZZER_PIN 42

// Release asset filename for this board, set by tools/esp-flasher/generate_release.py
// (a lone --build variant has no -<env> suffix).
#define OTA_ASSET_NAME "tamagotchi-plus-ota.bin"

#endif

// Wi-Fi provisioning. First boot (or saved network unreachable) opens a WiFiManager
// captive portal on this AP; Improv serial (the web flasher's "Connect to Wi-Fi" step)
// works alongside it at the same time.
#define WIFI_SETUP_AP_NAME "Tamagotchi+ Setup"
#define WIFI_CONNECT_TIMEOUT_S 20

// Firmware auto-update. Polls this app's own manifest.json on GitHub Pages
// (regenerated fresh on every push regardless of what else changed) rather
// than the GitHub Releases API — see libs/ota-update-client.
#define UPDATE_CHECK_INTERVAL_MS (60 * 60 * 1000UL)  // 1 hour
#define OTA_MANIFEST_URL "https://rcompton78.github.io/compton-diy/tamagotchi-plus/manifest.json"
