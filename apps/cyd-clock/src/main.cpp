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

// ── Layout (portrait 240×320, USB at bottom) ─────────────────────────────────
static constexpr int CX        = 120;
static constexpr int HEADER_Y  = 0,   HEADER_H  = 40;
static constexpr int ANIMAL_Y  = 40,  ANIMAL_H  = 175;
static constexpr int PICKER_Y  = 215, PICKER_H  = 50;
static constexpr int TIMER_Y   = 265, TIMER_H   = 55;

static constexpr int CAT_CX = 120;
static constexpr int CAT_CY = 127;  // 40 + 175/2

// Touch calibration — print "Touch: x= y=" from serial to tune
static constexpr int TX_MIN = 300, TX_MAX = 3800;
static constexpr int TY_MIN = 300, TY_MAX = 3800;
static constexpr unsigned long TOUCH_DEBOUNCE_MS = 350;

// Palette
static constexpr uint16_t C_CAT  = TFT_WHITE;
static constexpr uint16_t C_PINK = 0xFC18;  // rose pink
static constexpr uint16_t C_DARK = 0x2945;  // charcoal
static constexpr uint16_t C_SEP  = 0x39E7;  // separator
static constexpr uint16_t C_BTN  = 0x2965;  // button bg
static constexpr uint16_t C_BACT = 0x065F;  // active button
static constexpr uint16_t C_DIM  = 0x7BEF;  // dim text

// Quick-pick durations
static constexpr uint32_t    PICK_SEC[] = {60, 300, 600, 1800};
static constexpr const char* PICK_LBL[] = {"1m", "5m", "10m", "30m"};
static constexpr int PICK_N = 4;

// ── Hardware ──────────────────────────────────────────────────────────────────
TFT_eSPI tft;
SPIClass touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// ── Services ──────────────────────────────────────────────────────────────────
WiFiUDP       ntpUDP;
NTPClient     ntpClient(ntpUDP, NTP_SERVER);
ConfigManager configMgr;
WeatherClient weather;
TimerWidget   timerWidget;

// ── Animation ─────────────────────────────────────────────────────────────────
enum class CatMood { Idle, Happy, Celebrate };

struct {
    CatMood mood            = CatMood::Idle;
    unsigned long since     = 0;
    bool eyeOpen            = true;
    unsigned long lastBlink = 0;
    uint8_t frame           = 0;
    unsigned long lastFrame = 0;
} cat;

// ── App state ─────────────────────────────────────────────────────────────────
int  activePick        = -1;
bool timerDonePrev     = false;
unsigned long lastWeatherFetch = 0;
unsigned long lastTouchMs      = 0;

struct { bool header, animal, picker, timerRow; } dirty = {true, true, true, true};

// ── GRAM init ─────────────────────────────────────────────────────────────────
static void clearFullGRAM() {
    tft.startWrite();
    tft.writecommand(0x2A);
    tft.writedata(0x00); tft.writedata(0x00); tft.writedata(0x00); tft.writedata(0xEF);
    tft.writecommand(0x2B);
    tft.writedata(0x00); tft.writedata(0x00); tft.writedata(0x01); tft.writedata(0x3F);
    tft.writecommand(0x2C);
    tft.pushColor(TFT_BLACK, 240 * 320);
    tft.endWrite();
}

// ── Cat drawing ───────────────────────────────────────────────────────────────
static void drawCat(int cx, int cy, CatMood mood, bool eyeOpen) {
    bool happy = (mood == CatMood::Happy || mood == CatMood::Celebrate);

    tft.fillRect(cx - 62, cy - 86, 124, 144, TFT_BLACK);

    // Ears (outer then inner pink)
    tft.fillTriangle(cx - 26, cy - 86, cx - 52, cy - 62, cx -  8, cy - 62, C_CAT);
    tft.fillTriangle(cx + 26, cy - 86, cx + 52, cy - 62, cx +  8, cy - 62, C_CAT);
    tft.fillTriangle(cx - 26, cy - 78, cx - 45, cy - 64, cx - 12, cy - 64, C_PINK);
    tft.fillTriangle(cx + 26, cy - 78, cx + 45, cy - 64, cx + 12, cy - 64, C_PINK);

    // Head
    tft.fillRoundRect(cx - 44, cy - 64, 88, 66, 20, C_CAT);

    // Eyes
    if (eyeOpen) {
        tft.fillCircle(cx - 15, cy - 37, 11, TFT_WHITE);
        tft.fillCircle(cx + 15, cy - 37, 11, TFT_WHITE);
        tft.fillCircle(cx - 14, cy - 37,  5, TFT_BLACK);
        tft.fillCircle(cx + 16, cy - 37,  5, TFT_BLACK);
        tft.fillRect(cx - 19, cy - 43, 4, 4, TFT_WHITE);
        tft.fillRect(cx + 12, cy - 43, 4, 4, TFT_WHITE);
    } else {
        tft.fillRoundRect(cx - 24, cy - 41, 20, 7, 3, C_DARK);
        tft.fillRoundRect(cx +  4, cy - 41, 20, 7, 3, C_DARK);
    }

    // Nose
    tft.fillTriangle(cx, cy - 22, cx - 4, cy - 15, cx + 4, cy - 15, C_PINK);

    // Whiskers
    tft.drawLine(cx - 12, cy - 19, cx - 46, cy - 22, C_DARK);
    tft.drawLine(cx - 12, cy - 14, cx - 46, cy - 11, C_DARK);
    tft.drawLine(cx + 12, cy - 19, cx + 46, cy - 22, C_DARK);
    tft.drawLine(cx + 12, cy - 14, cx + 46, cy - 11, C_DARK);

    // Mouth
    if (happy) {
        tft.drawLine(cx - 10, cy - 8,  cx - 3, cy - 3, C_DARK);
        tft.drawLine(cx -  3, cy - 3,  cx,     cy - 5, C_DARK);
        tft.drawLine(cx,      cy - 5,  cx + 3, cy - 3, C_DARK);
        tft.drawLine(cx +  3, cy - 3,  cx + 10,cy - 8, C_DARK);
    } else {
        tft.drawLine(cx - 6, cy - 9, cx,     cy - 6, C_DARK);
        tft.drawLine(cx,     cy - 6, cx + 6, cy - 9, C_DARK);
    }

    // Body
    tft.fillRoundRect(cx - 30, cy + 2, 60, 54, 15, C_CAT);

    // Paws
    tft.fillRoundRect(cx - 32, cy + 42, 24, 14, 7, C_CAT);
    tft.fillRoundRect(cx +  8, cy + 42, 24, 14, 7, C_CAT);
    for (int d = -4; d <= 4; d += 4) {
        tft.drawLine(cx - 20 + d, cy + 52, cx - 20 + d, cy + 56, C_DARK);
        tft.drawLine(cx + 20 + d, cy + 52, cx + 20 + d, cy + 56, C_DARK);
    }

    // Tail (right side)
    tft.fillRoundRect(cx + 26, cy + 18, 12, 36, 6, C_CAT);
    tft.fillRoundRect(cx + 14, cy + 50, 28, 10, 5, C_CAT);
}

static void drawSparkles(int cx, int cy, uint8_t frame) {
    static const int8_t  dx[]  = {  0, 40, 55, 40,  0,-40,-55,-40};
    static const int8_t  dy[]  = {-58,-42,-10, 22, 38, 22,-10,-42};
    static const uint16_t clr[] = {TFT_YELLOW, TFT_CYAN, 0xFD20, TFT_GREEN,
                                    TFT_YELLOW, TFT_CYAN, 0xFD20, TFT_GREEN};
    for (int i = 0; i < 8; i++) {
        int px = cx + dx[i], py = cy + dy[i];
        if (py < ANIMAL_Y + 2 || py > ANIMAL_Y + ANIMAL_H - 6) continue;
        tft.fillRect(px - 4, py - 4, 9, 9, TFT_BLACK);
        if ((frame + i) % 2 == 0) {
            tft.drawLine(px - 3, py, px + 3, py, clr[i]);
            tft.drawLine(px, py - 3, px, py + 3, clr[i]);
        }
    }
}

static void clearSparkles(int cx, int cy) {
    static const int8_t dx[] = {  0, 40, 55, 40,  0,-40,-55,-40};
    static const int8_t dy[] = {-58,-42,-10, 22, 38, 22,-10,-42};
    for (int i = 0; i < 8; i++) {
        int px = cx + dx[i], py = cy + dy[i];
        if (py >= ANIMAL_Y + 2 && py <= ANIMAL_Y + ANIMAL_H - 6)
            tft.fillRect(px - 4, py - 4, 9, 9, TFT_BLACK);
    }
}

// ── Zone draws ────────────────────────────────────────────────────────────────
static void drawHeader() {
    time_t epoch = ntpClient.getEpochTime();
    struct tm* t = localtime(&epoch);

    tft.fillRect(0, HEADER_Y, 240, HEADER_H - 1, TFT_BLACK);
    tft.drawFastHLine(0, HEADER_Y + HEADER_H - 1, 240, C_SEP);

    // 12-hour time: "H:MM" in font 4, then ":SS am/pm" in font 2 next to it
    int h12 = t->tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (t->tm_hour < 12) ? "am" : "pm";

    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%d:%02d", h12, t->tm_min);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(hmBuf, 6, HEADER_Y + 7, 4);

    char ssBuf[8];
    snprintf(ssBuf, sizeof(ssBuf), ":%02d %s", t->tm_sec, ampm);
    int hmWidth = tft.textWidth(hmBuf, 4);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawString(ssBuf, 6 + hmWidth + 2, HEADER_Y + 14, 2);

    // Weather — font 2, right-aligned
    int timeEnd = 6 + hmWidth + 2 + tft.textWidth(ssBuf, 2) + 4;
    tft.fillRect(timeEnd, HEADER_Y, 240 - timeEnd, HEADER_H - 1, TFT_BLACK);
    if (weather.data().valid) {
        char wBuf[20];
        snprintf(wBuf, sizeof(wBuf), "%.0fC %s",
                 weather.data().tempC,
                 WeatherClient::descriptionForCode(weather.data().weatherCode));
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        int tw = tft.textWidth(wBuf, 2);
        int wx = max(timeEnd, 234 - tw);
        tft.drawString(wBuf, wx, HEADER_Y + 14, 2);
    } else {
        tft.setTextColor(C_DIM, TFT_BLACK);
        tft.drawString("no wx", 200, HEADER_Y + 14, 1);
    }
}

static void drawAnimal() {
    tft.fillRect(0, ANIMAL_Y, 240, ANIMAL_H, TFT_BLACK);
    int dy = (cat.mood == CatMood::Celebrate) ? ((cat.frame % 2 == 0) ? -3 : 3) : 0;
    drawCat(CAT_CX, CAT_CY + dy, cat.mood, cat.eyeOpen);

    if (cat.mood == CatMood::Happy || cat.mood == CatMood::Celebrate) {
        drawSparkles(CAT_CX, CAT_CY, cat.frame);
    }

    // Feed hint at bottom of animal zone
    tft.fillRect(0, ANIMAL_Y + ANIMAL_H - 14, 240, 14, TFT_BLACK);
    if (cat.mood == CatMood::Idle && !timerWidget.isRunning()) {
        tft.setTextColor(C_SEP, TFT_BLACK);
        tft.drawCentreString("tap to feed", CX, ANIMAL_Y + ANIMAL_H - 13, 1);
    }
}

static void drawPicker() {
    tft.fillRect(0, PICKER_Y, 240, PICKER_H, TFT_BLACK);
    tft.drawFastHLine(0, PICKER_Y, 240, C_SEP);

    constexpr int btnW = 60;
    for (int i = 0; i < PICK_N; i++) {
        int bx    = i * btnW;
        bool on   = (i == activePick);
        uint16_t bg = on ? C_BACT : C_BTN;
        uint16_t fg = on ? TFT_WHITE : C_DIM;
        tft.fillRoundRect(bx + 3, PICKER_Y + 7, btnW - 6, PICKER_H - 14, 6, bg);
        int tw = tft.textWidth(PICK_LBL[i], 2);
        tft.setTextColor(fg, bg);
        tft.drawString(PICK_LBL[i], bx + (btnW - tw) / 2, PICKER_Y + 17, 2);
    }
}

static void drawTimerRow() {
    tft.fillRect(0, TIMER_Y, 240, TIMER_H, TFT_BLACK);
    tft.drawFastHLine(0, TIMER_Y, 240, C_SEP);

    uint32_t rem = timerWidget.remaining();

    uint16_t timeCol = TFT_GREEN;
    if (timerWidget.isFinished())      timeCol = TFT_RED;
    else if (!timerWidget.isRunning()) timeCol = TFT_YELLOW;

    if (rem > 0 || timerWidget.isFinished()) {
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", rem / 60, rem % 60);
        tft.setTextColor(timeCol, TFT_BLACK);
        tft.drawString(buf, 8, TIMER_Y + 3, 6);  // font 6 = 48px tall, fits in 55px
    } else {
        tft.setTextColor(C_SEP, TFT_BLACK);
        tft.drawString("--:--", 8, TIMER_Y + 3, 6);
    }

    if (rem > 0 || timerWidget.isFinished()) {
        // Pause / resume
        tft.fillRoundRect(150, TIMER_Y + 10, 38, 34, 6, C_BTN);
        const char* plbl = timerWidget.isRunning() ? "||" : ">";
        uint16_t pcol = timerWidget.isRunning() ? TFT_YELLOW : TFT_GREEN;
        tft.setTextColor(pcol, C_BTN);
        int tw = tft.textWidth(plbl, 4);
        tft.drawString(plbl, 150 + (38 - tw) / 2, TIMER_Y + 15, 4);

        // Reset
        tft.fillRoundRect(196, TIMER_Y + 10, 38, 34, 6, C_BTN);
        tft.setTextColor(0xFD20, C_BTN);  // orange
        tw = tft.textWidth("X", 4);
        tft.drawString("X", 196 + (38 - tw) / 2, TIMER_Y + 15, 4);
    }
}

// ── Touch ─────────────────────────────────────────────────────────────────────
struct Point { int x, y; };

static bool readTouch(Point& out) {
    if (!touch.tirqTouched() || !touch.touched()) return false;
    TS_Point p = touch.getPoint();
    if (p.z < 200) return false;
    out.x = constrain(map(p.x, TX_MIN, TX_MAX, 0, 239), 0, 239);
    out.y = constrain(map(p.y, TY_MIN, TY_MAX, 0, 319), 0, 319);
    return true;
}

static void handleTouch() {
    Point p;
    if (!readTouch(p)) return;
    unsigned long now = millis();
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    lastTouchMs = now;

    Serial.printf("Touch x=%d y=%d\n", p.x, p.y);

    if (p.y >= TIMER_Y) {
        if (p.x >= 196 && (timerWidget.remaining() > 0 || timerWidget.isFinished())) {
            timerWidget.reset();
            activePick = -1;
            if (cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
            dirty.picker   = true;
            dirty.timerRow = true;
        } else if (timerWidget.remaining() > 0 || timerWidget.isRunning()) {
            if (timerWidget.isRunning()) timerWidget.pause();
            else                         timerWidget.resume();
            dirty.timerRow = true;
        }
    } else if (p.y >= PICKER_Y) {
        int idx = constrain(p.x / (240 / PICK_N), 0, PICK_N - 1);
        activePick = idx;
        timerWidget.start(PICK_SEC[idx]);
        dirty.picker   = true;
        dirty.timerRow = true;
    } else if (p.y >= ANIMAL_Y) {
        cat.mood  = CatMood::Happy;
        cat.since = now;
        cat.frame = 0;
        dirty.animal = true;
    }
}

// ── Cat animation tick ────────────────────────────────────────────────────────
static void updateCatAnim() {
    unsigned long now = millis();
    bool changed = false;

    if (cat.mood == CatMood::Happy && now - cat.since > 3000) {
        cat.mood = CatMood::Idle; changed = true;
    }
    if (cat.mood == CatMood::Celebrate && now - cat.since > 6000) {
        cat.mood = CatMood::Idle; changed = true;
    }

    // Blink (idle only)
    if (cat.mood == CatMood::Idle) {
        if ( cat.eyeOpen && now - cat.lastBlink > 4000) { cat.eyeOpen = false; cat.lastBlink = now; changed = true; }
        if (!cat.eyeOpen && now - cat.lastBlink >  150) { cat.eyeOpen = true;                       changed = true; }
    } else if (!cat.eyeOpen) {
        cat.eyeOpen = true; changed = true;
    }

    // Sparkle / bounce frame advance
    if (cat.mood != CatMood::Idle && now - cat.lastFrame > 250) {
        cat.frame++;
        cat.lastFrame = now;
        changed = true;
    }

    if (changed) dirty.animal = true;
}

// ── WiFiManager ───────────────────────────────────────────────────────────────
static void runWiFiManager(ConfigManager& cfg) {
    char latBuf[12], lonBuf[12], utcBuf[8];
    snprintf(latBuf, sizeof(latBuf), "%.4f", cfg.config().latitude);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", cfg.config().longitude);
    snprintf(utcBuf, sizeof(utcBuf), "%d",   cfg.config().utcOffsetSeconds);

    WiFiManagerParameter paramLat("lat", "Latitude",         latBuf, 11);
    WiFiManagerParameter paramLon("lon", "Longitude",        lonBuf, 11);
    WiFiManagerParameter paramUtc("utc", "UTC Offset (sec)", utcBuf, 7);

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
    clearFullGRAM();
    tft.setRotation(0);
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
    lastWeatherFetch = millis();

    tft.fillScreen(TFT_BLACK);

    cat.lastBlink = millis();
    // dirty flags are all true at declaration — first loop draws everything
}

void loop() {
    ntpClient.update();
    handleTouch();

    unsigned long now = millis();

    // Weather refresh
    if (now - lastWeatherFetch > WEATHER_UPDATE_INTERVAL_MS) {
        weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
        lastWeatherFetch = now;
        dirty.header = true;
    }

    // Timer tick + finished detection
    timerWidget.tick();
    bool timerDoneNow = timerWidget.isFinished();
    if (timerDoneNow && !timerDonePrev) {
        cat.mood  = CatMood::Celebrate;
        cat.since = now;
        cat.frame = 0;
        dirty.animal   = true;
        dirty.timerRow = true;
    }
    timerDonePrev = timerDoneNow;

    // Cat animation
    updateCatAnim();

    // Header tick (once per second)
    {
        static int prevSec = -1;
        time_t ep = ntpClient.getEpochTime();
        struct tm* tm = localtime(&ep);
        if (tm->tm_sec != prevSec) { prevSec = tm->tm_sec; dirty.header = true; }
    }

    // Timer row refresh while running
    {
        static unsigned long lastTimerDraw = 0;
        if (timerWidget.isRunning() && now - lastTimerDraw >= 500) {
            lastTimerDraw  = now;
            dirty.timerRow = true;
        }
    }

    if (dirty.header)   { drawHeader();   dirty.header   = false; }
    if (dirty.animal)   { drawAnimal();   dirty.animal   = false; }
    if (dirty.picker)   { drawPicker();   dirty.picker   = false; }
    if (dirty.timerRow) { drawTimerRow(); dirty.timerRow = false; }

    delay(50);
}
