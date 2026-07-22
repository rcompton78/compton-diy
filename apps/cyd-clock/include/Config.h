#pragma once

// Firmware version, injected via platformio.ini from RELEASE_VERSION
// (scripts/pio.sh defaults it to "dev" outside the release workflow).
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif

// Display
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#if defined(BOARD_FREENOVE_S3)

#define TFT_BACKLIGHT_PIN 45

// Release asset filename for this board, set by tools/esp-flasher/generate_release.py
#define OTA_ASSET_NAME "cyd-clock-freenove-s3-ota.bin"

// Touch (I2C) — FT6336U capacitive touch
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_IRQ 17

#else

#define TFT_BACKLIGHT_PIN 21

// Release asset filename for this board, set by tools/esp-flasher/generate_release.py
#define OTA_ASSET_NAME "cyd-clock-cyd-ota.bin"

// Touch (HSPI) — XPT2046 resistive touch. TOUCH_CS is also passed as a
// platformio.ini build flag so TFT_eSPI (included before this header) sees it.
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define TOUCH_IRQ  36

// RGB LED on CYD
#define LED_RED   4
#define LED_GREEN 16
#define LED_BLUE  17

#endif

// Filesystem paths
#define CONFIG_FILE "/config.json"

// Weather (Open-Meteo — no API key required)
#define WEATHER_API_HOST "api.open-meteo.com"
#define WEATHER_UPDATE_INTERVAL_MS (10 * 60 * 1000UL)  // 10 minutes

// NTP
#define NTP_SERVER "pool.ntp.org"
#define NTP_UPDATE_INTERVAL_MS (60 * 60 * 1000UL)  // 1 hour

// Firmware auto-update. Polls this app's own manifest.json on GitHub Pages
// (regenerated fresh on every push regardless of what else changed) rather
// than the GitHub Releases API, so a release that only touched another app in
// this monorepo can't make this check fail — see libs/ota-update-client.
#define UPDATE_CHECK_INTERVAL_MS (60 * 60 * 1000UL)  // 1 hour
#define OTA_MANIFEST_URL "https://rcompton78.github.io/compton-diy/cyd-clock/manifest.json"

// Flash sales (DIY-79). cat-buddy-api (DIY-59, compton-apps repo) is the source of truth
// for which item is on sale and its start/end window; the device polls GET
// /flash-sale/current on this interval. CAT_BUDDY_API_URL/CAT_BUDDY_API_TOKEN are injected at
// build time (see platformio.ini + scripts/pio.sh), same pattern as FIRMWARE_VERSION.
// scripts/pio.sh defaults CAT_BUDDY_API_URL to the real prod endpoint and CAT_BUDDY_API_TOKEN
// to empty, then lets a gitignored apps/cyd-clock/.env override either — e.g. point a local
// build at a LAN dev cat-buddy-api instance (http://192.168.1.50:PORT/flash-sale/current) with
// its dev token, without touching this file. The #ifndef fallbacks below only matter for a
// build that bypasses scripts/pio.sh entirely.
#define FLASH_SALE_POLL_INTERVAL_MS (15 * 60 * 1000UL)  // 15 minutes
#ifndef CAT_BUDDY_API_URL
#define CAT_BUDDY_API_URL "https://cat-buddy.richcompton.com/api/flash-sale/current"
#endif
#ifndef CAT_BUDDY_API_TOKEN
#define CAT_BUDDY_API_TOKEN ""
#endif

// TLS trust anchor for cat-buddy-api, same role as OtaUpdateClient's GITHUB_TRUST_ANCHORS —
// in fact the exact same cert. cat-buddy.richcompton.com's chain (Nginx Proxy Manager /
// Let's Encrypt, confirmed against the live cert chain 2026-07-22: leaf -> YE2 -> Root YE ->
// ISRG Root X2 -> ISRG Root X1) roots at the same ISRG Root X1 GitHub Pages uses, just via a
// newer/different intermediate path — pinning this root, rather than any intermediate,
// means it keeps working across Let's Encrypt's periodic intermediate rotation. If
// cat-buddy-api ever moves to a different issuing CA, polls fail closed (skipped, logged)
// rather than falling back to WiFiClientSecure::setInsecure().
#define CAT_BUDDY_API_CA_CERT \
"-----BEGIN CERTIFICATE-----\n" \
"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\n" \
"-----END CERTIFICATE-----\n"
