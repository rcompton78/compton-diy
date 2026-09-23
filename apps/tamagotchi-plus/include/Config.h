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

// Battery sense: B+ through a 200K/100K divider into ADC1_CH0 (so B+ = 3 × pin voltage).
// The ETA6098 charger's STAT output is not routed anywhere, so there is no charge-status pin.
#define BAT_ADC_PIN       1
#define BAT_DIVIDER_RATIO 3.0f
// How far B+ sags below its resting voltage under the normal running load (backlight +
// Wi-Fi). Measured on hardware (COM-298): ~4.17V resting cell read 4.02V running on battery.
#define BAT_LOAD_SAG_V    0.15f

// Passive buzzer (not used yet). Held LOW so it doesn't idle-drain/heat the board.
#define BUZZER_PIN 42

// Release asset filename for this board, set by tools/esp-flasher/generate_release.py
// (a lone --build variant has no -<env> suffix).
#define OTA_ASSET_NAME "tamagotchi-plus-ota.bin"

#else
#error "No board selected: define BOARD_WAVESHARE_S3_169 (see platformio.ini)"
#endif

// Wi-Fi provisioning. First boot (or saved network unreachable) opens a WiFiManager
// captive portal on this AP; Improv serial (the web flasher's "Connect to Wi-Fi" step)
// works alongside it at the same time.
#define WIFI_SETUP_AP_NAME "Tamagotchi+ Setup"
#define WIFI_CONNECT_TIMEOUT_S 20

// ── Hatch timer (COM-299) ─────────────────────────────────────────────────────
// The egg hatches after a random 3–5 minutes of POWERED-ON time. Elapsed time is
// accumulated and persisted; powering the device off pauses the egg rather than ageing it
// (same as Tamagotchi Paradise), so never store a wall-clock deadline. This is also why
// the PCF85063 RTC isn't needed here — there is nothing to measure across a power-off.
#define HATCH_MIN_MS (3UL * 60 * 1000)
#define HATCH_MAX_MS (5UL * 60 * 1000)

// Test mode: one fixed, short window so the six crack stages and the pop can be watched
// end to end without waiting minutes per flash. MUST be 0 for a real build.
#define HATCH_TEST_MODE 1
#define HATCH_TEST_MS   (30UL * 1000)

#if HATCH_TEST_MODE
#warning "HATCH_TEST_MODE is ON: the egg hatches in HATCH_TEST_MS, not the real 3-5 minutes. Set it to 0 before releasing."
#endif

// How often the accumulated hatch time is flushed to NVS. Long enough to keep flash
// writes to a handful per hatch, short enough that a reset loses almost nothing.
#define HATCH_SAVE_MS (10UL * 1000)

// Press and hold this long to wipe the hatch state and start a fresh egg.
#define EGG_RESET_HOLD_MS 5000

// Firmware auto-update. Polls this app's own manifest.json on GitHub Pages
// (regenerated fresh on every push regardless of what else changed) rather
// than the GitHub Releases API — see libs/ota-update-client.
#define UPDATE_CHECK_INTERVAL_MS (60 * 60 * 1000UL)  // 1 hour
#define OTA_MANIFEST_URL "https://rcompton78.github.io/compton-diy/tamagotchi-plus/manifest.json"
