#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include "../include/Config.h"
#include "ConfigManager.h"
#include "WeatherClient.h"
#include "TimerWidget.h"

// ── Hardware ────────────────────────────────────────────────────────────────
TFT_eSPI tft;
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// ── Services ────────────────────────────────────────────────────────────────
WiFiUDP    ntpUDP;
NTPClient  ntpClient(ntpUDP, NTP_SERVER);
ConfigManager configMgr;
WeatherClient weather;
TimerWidget   timer;

// ── State ───────────────────────────────────────────────────────────────────
enum class Screen { Clock, Timer };
Screen currentScreen = Screen::Clock;

unsigned long lastWeatherUpdate = 0;

// ── Helpers ─────────────────────────────────────────────────────────────────
void drawClock() {
    tft.fillScreen(TFT_BLACK);

    time_t epoch = ntpClient.getEpochTime();
    struct tm* t = localtime(&epoch);

    char timeBuf[9];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.drawString(timeBuf, 60, 90, 7);

    char dateBuf[20];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    tft.drawString(dateBuf, 80, 155, 4);

    if (weather.data().valid) {
        char wBuf[40];
        snprintf(wBuf, sizeof(wBuf), "%.1f C  %s",
            weather.data().tempC,
            WeatherClient::descriptionForCode(weather.data().weatherCode));
        tft.drawString(wBuf, 20, 200, 2);
    }
}

void drawTimer() {
    tft.fillScreen(TFT_BLACK);
    timer.tick();

    uint32_t rem = timer.remaining();
    uint32_t mins = rem / 60;
    uint32_t secs = rem % 60;

    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", mins, secs);

    uint16_t colour = timer.isFinished() ? TFT_RED
                    : timer.isRunning()  ? TFT_GREEN
                                         : TFT_YELLOW;
    tft.setTextColor(colour, TFT_BLACK);
    tft.drawString(buf, 60, 90, 7);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(timer.isRunning() ? "TAP TO PAUSE" : "TAP TO START", 70, 180, 2);
}

void handleTouch() {
    if (!touch.tirqTouched() || !touch.touched()) return;

    TS_Point p = touch.getPoint();

    if (currentScreen == Screen::Clock) {
        currentScreen = Screen::Timer;
        if (!timer.isRunning() && !timer.isFinished()) {
            timer.start(60);
        }
    } else {
        if (timer.isFinished()) {
            timer.reset();
            currentScreen = Screen::Clock;
        } else if (timer.isRunning()) {
            timer.pause();
        } else {
            timer.resume();
        }
    }
}

// ── Arduino ─────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Backlight
    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Starting...", 10, 110, 4);

    // Touch on HSPI
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(1);

    configMgr.begin();
    configMgr.load();

    // WiFi — captive portal on first boot
    WiFiManager wm;
    wm.autoConnect("CYD-Clock");

    ntpClient.begin();
    ntpClient.setTimeOffset(configMgr.config().utcOffsetSeconds);
    ntpClient.update();

    weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
    lastWeatherUpdate = millis();

    tft.fillScreen(TFT_BLACK);
}

void loop() {
    ntpClient.update();
    handleTouch();

    if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL_MS) {
        weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
        lastWeatherUpdate = millis();
    }

    if (currentScreen == Screen::Clock) {
        drawClock();
    } else {
        drawTimer();
    }

    delay(500);
}
