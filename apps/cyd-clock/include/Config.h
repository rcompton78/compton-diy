#pragma once

// Display
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#if defined(BOARD_FREENOVE_S3)

#define TFT_BACKLIGHT_PIN 45

// Touch (I2C) — FT6336U capacitive touch
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_IRQ 17

#else

#define TFT_BACKLIGHT_PIN 21

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
