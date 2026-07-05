#pragma once

// Display
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define TFT_BACKLIGHT_PIN 21

// Touch (HSPI)
#define TOUCH_MOSI 32
#define TOUCH_MISO 39
#define TOUCH_CLK  25
#define TOUCH_IRQ  36

// RGB LED on CYD
#define LED_RED   4
#define LED_GREEN 16
#define LED_BLUE  17

// Filesystem paths
#define CONFIG_FILE "/config.json"

// Weather (Open-Meteo — no API key required)
#define WEATHER_API_HOST "api.open-meteo.com"
#define WEATHER_UPDATE_INTERVAL_MS (10 * 60 * 1000UL)  // 10 minutes

// NTP
#define NTP_SERVER "pool.ntp.org"
#define NTP_UPDATE_INTERVAL_MS (60 * 60 * 1000UL)  // 1 hour
