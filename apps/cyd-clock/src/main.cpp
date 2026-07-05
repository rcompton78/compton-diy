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

// Portrait 240x320 (USB at top)
static constexpr int CX = 120;  // horizontal centre

// ── Hardware ─────────────────────────────────────────────────────────────────
TFT_eSPI tft;
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// ── Services ─────────────────────────────────────────────────────────────────
WiFiUDP       ntpUDP;
NTPClient     ntpClient(ntpUDP, NTP_SERVER);
ConfigManager configMgr;
WeatherClient weather;
TimerWidget   timerWidget;

// ── State ─────────────────────────────────────────────────────────────────────
enum class Screen { Clock, Timer };
Screen currentScreen = Screen::Clock;
unsigned long lastWeatherUpdate = 0;

// Clear a horizontal band and return y for next element
static void clearBand(int y, int h) {
    tft.fillRect(0, y, 240, h, TFT_BLACK);
}

// ── Clock screen ──────────────────────────────────────────────────────────────
void drawClock() {
    time_t epoch = ntpClient.getEpochTime();
    struct tm* t = localtime(&epoch);

    // HH:MM — font 7 (7-seg, 75px tall). Draw with bg so it self-erases.
    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%02d:%02d", t->tm_hour, t->tm_min);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(hmBuf, CX, 30, 7);

    // Seconds — font 4 (26px tall)
    char secBuf[3];
    snprintf(secBuf, sizeof(secBuf), "%02d", t->tm_sec);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString(secBuf, CX, 120, 4);

    // Date — font 4
    char dateBuf[12];
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawCentreString(dateBuf, CX, 165, 4);

    // Weather — variable-length text so clear the band first
    clearBand(210, 40);
    if (weather.data().valid) {
        char wBuf[40];
        snprintf(wBuf, sizeof(wBuf), "%.1fC  %s",
                 weather.data().tempC,
                 WeatherClient::descriptionForCode(weather.data().weatherCode));
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawCentreString(wBuf, CX, 220, 2);
    } else {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.drawCentreString("No weather data", CX, 220, 2);
    }
}

// ── Timer screen ──────────────────────────────────────────────────────────────
void drawTimer() {
    timerWidget.tick();

    uint32_t rem  = timerWidget.remaining();
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", rem / 60, rem % 60);

    uint16_t colour = timerWidget.isFinished() ? TFT_RED
                    : timerWidget.isRunning()   ? TFT_GREEN
                                                : TFT_YELLOW;
    tft.setTextColor(colour, TFT_BLACK);
    tft.drawCentreString(buf, CX, 110, 7);

    clearBand(220, 30);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(
        timerWidget.isRunning() ? "TAP TO PAUSE" : "TAP TO START",
        CX, 225, 2);
}

// ── Touch ─────────────────────────────────────────────────────────────────────
void handleTouch() {
    if (!touch.tirqTouched() || !touch.touched()) return;
    touch.getPoint();

    if (currentScreen == Screen::Clock) {
        currentScreen = Screen::Timer;
        tft.fillScreen(TFT_BLACK);
        if (!timerWidget.isRunning() && !timerWidget.isFinished())
            timerWidget.start(60);
    } else {
        if (timerWidget.isFinished()) {
            timerWidget.reset();
            tft.fillScreen(TFT_BLACK);
            currentScreen = Screen::Clock;
        } else if (timerWidget.isRunning()) {
            timerWidget.pause();
        } else {
            timerWidget.resume();
        }
    }
}

// ── WiFiManager with location params ─────────────────────────────────────────
void runWiFiManager(ConfigManager& cfg) {
    char latBuf[12], lonBuf[12], utcBuf[8];
    snprintf(latBuf, sizeof(latBuf), "%.4f", cfg.config().latitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", cfg.config().longitude);
    snprintf(utcBuf, sizeof(utcBuf), "%d",   cfg.config().utcOffsetSeconds);

    WiFiManagerParameter paramLat("lat", "Latitude",          latBuf, 11);
    WiFiManagerParameter paramLon("lon", "Longitude",         lonBuf, 11);
    WiFiManagerParameter paramUtc("utc", "UTC Offset (sec)",  utcBuf, 7);

    WiFiManager wm;
    wm.addParameter(&paramLat);
    wm.addParameter(&paramLon);
    wm.addParameter(&paramUtc);
    wm.setSaveParamsCallback([&]() {
        cfg.config().latitude         = atof(paramLat.getValue());
        cfg.config().longitude        = atof(paramLon.getValue());
        cfg.config().utcOffsetSeconds = atoi(paramUtc.getValue());
        cfg.save();
    });

    wm.autoConnect("CYD-Clock");
}

// ── Arduino ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(1);         // landscape first — clears all 320x240 physical pixels
    tft.fillScreen(TFT_BLACK);
    tft.setRotation(0);         // portrait 240x320, USB at top
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Starting...", CX, 150, 4);

    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(0);

    configMgr.begin();
    configMgr.load();

    runWiFiManager(configMgr);

    ntpClient.begin();
    ntpClient.setTimeOffset(configMgr.config().utcOffsetSeconds);
    ntpClient.update();

    weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
    lastWeatherUpdate = millis();

    tft.fillScreen(TFT_BLACK);  // clean slate before loop starts
}

void loop() {
    ntpClient.update();
    handleTouch();

    if (millis() - lastWeatherUpdate > WEATHER_UPDATE_INTERVAL_MS) {
        weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
        lastWeatherUpdate = millis();
    }

    if (currentScreen == Screen::Clock) drawClock();
    else                                drawTimer();

    delay(500);
}
