#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <WiFiManager.h>
#include <WiFi.h>
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

// Treat button — bottom-right of animal zone
static constexpr int TREAT_X  = 185;  // left edge
static constexpr int TREAT_Y  = 178;  // top edge
static constexpr int TREAT_W  = 50;
static constexpr int TREAT_H  = 34;

// Touch calibration — print "Touch: x= y=" from serial to tune
static constexpr int TX_MIN = 300, TX_MAX = 3800;
static constexpr int TY_MIN = 300, TY_MAX = 3800;
static constexpr unsigned long TOUCH_DEBOUNCE_MS = 350;

// Palette
static constexpr uint16_t C_CAT   = TFT_WHITE;
static constexpr uint16_t C_PINK  = 0xFC18;  // rose pink
static constexpr uint16_t C_DARK  = 0x2945;  // charcoal
static constexpr uint16_t C_SEP   = 0x39E7;  // separator
static constexpr uint16_t C_BTN   = 0x2965;  // button bg
static constexpr uint16_t C_BACT  = 0x065F;  // active button
static constexpr uint16_t C_DIM   = 0x7BEF;  // dim text
static constexpr uint16_t C_FISH   = 0xFD20;  // treat button fish (orange)
static constexpr uint16_t C_RUMBLE = 0xFC98;  // hunger line animation (warm peach)

// Quick-pick durations
static constexpr uint32_t    PICK_SEC[] = {60, 300, 600, 1800};
static constexpr const char* PICK_LBL[] = {"+1", "+5", "+10", "+30"};
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
WiFiManager   wm;

// ── Animation ─────────────────────────────────────────────────────────────────
enum class CatMood   { Idle, Happy, Celebrate };
enum class CatStatus { Content, Peckish, Hungry };  // Content=0-50%, Peckish=50-100%, Hungry=100%+

struct {
    CatMood   mood              = CatMood::Idle;
    CatStatus status            = CatStatus::Content;
    unsigned long since         = 0;
    bool eyeOpen                = true;
    unsigned long lastBlink     = 0;
    uint8_t frame               = 0;
    unsigned long lastFrame     = 0;
    unsigned long lastRumble    = 0;  // for tummy rumble animation
    bool rumbling               = false;
} cat;

// ── App state ─────────────────────────────────────────────────────────────────
bool timerDonePrev          = false;
unsigned long lastWeatherFetch = 0;
unsigned long lastTouchMs      = 0;
unsigned long showIpUntilMs    = 0;

struct { bool header, animal, picker, timerRow, eyesOnly, timerTick, headerTick, hungerLines; } dirty = {true, true, true, true, false, false, false, false};


// ── Cat drawing ───────────────────────────────────────────────────────────────
static void drawEyes(int cx, int cy, bool eyeOpen) {
    tft.fillRect(cx - 28, cy - 50, 56, 26, C_CAT);  // restore head colour before drawing eyes
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
}

static void drawCat(int cx, int cy, CatMood mood, CatStatus status, bool eyeOpen) {
    bool happy  = (mood == CatMood::Happy || mood == CatMood::Celebrate);
    bool hungry = (status == CatStatus::Hungry) && !happy;

    tft.fillRect(cx - 50, cy - 88, 100, 146, TFT_BLACK);

    // Ears
    tft.fillTriangle(cx - 16, cy - 86, cx - 32, cy - 62, cx -  2, cy - 62, C_CAT);
    tft.fillTriangle(cx + 16, cy - 86, cx + 32, cy - 62, cx +  2, cy - 62, C_CAT);
    tft.fillTriangle(cx - 16, cy - 80, cx - 28, cy - 64, cx -  5, cy - 64, C_PINK);
    tft.fillTriangle(cx + 16, cy - 80, cx + 28, cy - 64, cx +  5, cy - 64, C_PINK);

    // Head (always white — hunger only affects body)
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
    } else if (hungry) {
        // Frown
        tft.drawLine(cx - 10, cy - 4,  cx - 3, cy - 9, C_DARK);
        tft.drawLine(cx -  3, cy - 9,  cx,     cy - 7, C_DARK);
        tft.drawLine(cx,      cy - 7,  cx + 3, cy - 9, C_DARK);
        tft.drawLine(cx +  3, cy - 9,  cx + 10,cy - 4, C_DARK);
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

static void drawHungerLines(int cx, int cy, bool show) {
    // Three short horizontal lines on the tummy — manga hunger growl effect
    int bx = cx - 12, by = cy + 18;
    uint16_t col = show ? C_RUMBLE : C_CAT;  // erase with body colour to avoid flicker
    tft.drawFastHLine(bx,      by,      14, col);
    tft.drawFastHLine(bx +  2, by +  8, 10, col);
    tft.drawFastHLine(bx,      by + 16, 14, col);
}

static void drawTreatBtn() {
    tft.fillRoundRect(TREAT_X, TREAT_Y, TREAT_W, TREAT_H, 6, C_BTN);

    // Fish: body ellipse, tail triangle, eye dot
    int fcx = TREAT_X + 20, fcy = TREAT_Y + 17;
    // Body
    for (int r = 10; r >= 6; r--)
        tft.drawEllipse(fcx, fcy, r, 7, C_FISH);
    tft.fillEllipse(fcx, fcy, 6, 6, C_FISH);
    // Tail
    tft.fillTriangle(fcx + 10, fcy, fcx + 22, fcy - 7, fcx + 22, fcy + 7, C_FISH);
    // Eye
    tft.fillCircle(fcx - 5, fcy - 2, 2, TFT_BLACK);
}

// ── Zone draws ────────────────────────────────────────────────────────────────
static void drawHeaderTick() {
    time_t epoch = ntpClient.getEpochTime();
    struct tm* t = localtime(&epoch);
    int h12 = t->tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char* ampm = (t->tm_hour < 12) ? "am" : "pm";
    char hmBuf[6];
    snprintf(hmBuf, sizeof(hmBuf), "%d:%02d", h12, t->tm_min);
    char ssBuf[8];
    snprintf(ssBuf, sizeof(ssBuf), ":%02d %s", t->tm_sec, ampm);
    // Font 2 is fixed-width so drawing with bg colour self-erases previous value
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawString(ssBuf, 6 + tft.textWidth(hmBuf, 4) + 2, HEADER_Y + 14, 2);
}

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

    // Weather / IP — font 2, right-aligned
    int timeEnd = 6 + hmWidth + 2 + tft.textWidth(ssBuf, 2) + 4;
    tft.fillRect(timeEnd, HEADER_Y, 240 - timeEnd, HEADER_H - 1, TFT_BLACK);
    if (showIpUntilMs > millis()) {
        String ip = WiFi.localIP().toString();
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        int tw = tft.textWidth(ip.c_str(), 1);
        tft.drawString(ip.c_str(), max(timeEnd, 238 - tw), HEADER_Y + 15, 1);
    } else if (weather.data().valid) {
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
    // Clear only sparkle positions and the cat's bounding box (including ±3px bounce).
    // Avoids wiping the full 240×175 zone which causes a visible black flash.
    clearSparkles(CAT_CX, CAT_CY);
    tft.fillRect(CAT_CX - 50, CAT_CY - 91, 100, 152, TFT_BLACK);

    int dy = (cat.mood == CatMood::Celebrate) ? ((cat.frame % 2 == 0) ? -3 : 3) : 0;
    drawCat(CAT_CX, CAT_CY + dy, cat.mood, cat.status, cat.eyeOpen);

    if (cat.mood == CatMood::Happy || cat.mood == CatMood::Celebrate) {
        drawSparkles(CAT_CX, CAT_CY, cat.frame);
    }

    // Hunger lines on tummy (Peckish or Hungry, only painted when rumbling)
    if (cat.status == CatStatus::Peckish || cat.status == CatStatus::Hungry) {
        drawHungerLines(CAT_CX, CAT_CY, cat.rumbling);
    }

    // Clear hint area and draw treat button
    tft.fillRect(0, ANIMAL_Y + ANIMAL_H - 27, TREAT_X - 2, 27, TFT_BLACK);
    drawTreatBtn();
}

static void drawPicker() {
    tft.fillRect(0, PICKER_Y, 240, PICKER_H, TFT_BLACK);
    tft.drawFastHLine(0, PICKER_Y, 240, C_SEP);

    constexpr int btnW = 60;
    for (int i = 0; i < PICK_N; i++) {
        int bx = i * btnW;
        tft.fillRoundRect(bx + 3, PICKER_Y + 7, btnW - 6, PICKER_H - 14, 6, C_BTN);
        int tw = tft.textWidth(PICK_LBL[i], 2);
        tft.setTextColor(TFT_WHITE, C_BTN);
        tft.drawString(PICK_LBL[i], bx + (btnW - tw) / 2, PICKER_Y + 17, 2);
    }
}

static void drawTimerDigits() {
    uint32_t rem = timerWidget.remaining();
    uint16_t col = timerWidget.isFinished() ? TFT_RED
                 : timerWidget.isRunning()  ? TFT_GREEN
                                            : TFT_YELLOW;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", rem / 60, rem % 60);
    tft.setTextColor(col, TFT_BLACK);  // bg colour self-erases previous digits
    tft.drawString(buf, 8, TIMER_Y + 3, 6);
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
        if (timerWidget.isRunning()) {
            // Pause
            tft.fillRoundRect(150, TIMER_Y + 10, 38, 34, 6, C_BTN);
            tft.setTextColor(TFT_YELLOW, C_BTN);
            int tw = tft.textWidth("||", 4);
            tft.drawString("||", 150 + (38 - tw) / 2, TIMER_Y + 15, 4);

            // Reset (X)
            tft.fillRoundRect(196, TIMER_Y + 10, 38, 34, 6, C_BTN);
            tft.setTextColor(0xFD20, C_BTN);  // orange
            tw = tft.textWidth("X", 4);
            tft.drawString("X", 196 + (38 - tw) / 2, TIMER_Y + 15, 4);
        } else if (timerWidget.isFinished()) {
            // Finished: single wide reset button
            tft.fillRoundRect(150, TIMER_Y + 10, 84, 34, 6, C_BTN);
            tft.setTextColor(0xFD20, C_BTN);
            int tw = tft.textWidth("0", 4);
            tft.drawString("0", 150 + (84 - tw) / 2, TIMER_Y + 15, 4);
        } else {
            // Paused: resume + reset
            tft.fillRoundRect(150, TIMER_Y + 10, 38, 34, 6, C_BTN);
            tft.setTextColor(TFT_GREEN, C_BTN);
            int tw = tft.textWidth(">", 4);
            tft.drawString(">", 150 + (38 - tw) / 2, TIMER_Y + 15, 4);

            tft.fillRoundRect(196, TIMER_Y + 10, 38, 34, 6, C_BTN);
            tft.setTextColor(0xFD20, C_BTN);
            tw = tft.textWidth("0", 4);
            tft.drawString("0", 196 + (38 - tw) / 2, TIMER_Y + 15, 4);
        }
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

    if (p.y < HEADER_Y + HEADER_H && p.x < 120) {
        showIpUntilMs = now + 7000;
        dirty.header  = true;
    } else if (p.y >= TIMER_Y) {
        if (timerWidget.isRunning()) {
            if (p.x >= 196) {
                timerWidget.reset();
                if (cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
                dirty.timerRow = true;
            } else if (p.x >= 150) {
                timerWidget.pause();
                dirty.timerRow = true;
            }
        } else if (timerWidget.isFinished() && p.x >= 150) {
            timerWidget.reset();
            if (cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
            dirty.timerRow = true;
        } else if (timerWidget.remaining() > 0) {
            if (p.x >= 196) {
                timerWidget.reset();
                dirty.timerRow = true;
            } else if (p.x >= 150) {
                timerWidget.resume();
                dirty.timerRow = true;
            }
        }
    } else if (p.y >= PICKER_Y) {
        int idx = constrain(p.x / (240 / PICK_N), 0, PICK_N - 1);
        bool wasFinished = timerWidget.isFinished();
        timerWidget.addTime(PICK_SEC[idx]);
        if (wasFinished && cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
        dirty.timerRow = true;
    } else if (p.y >= ANIMAL_Y) {
        // Treat button hit test
        if (p.x >= TREAT_X && p.x <= TREAT_X + TREAT_W &&
            p.y >= TREAT_Y && p.y <= TREAT_Y + TREAT_H) {
            // Feed the cat
            cat.mood   = CatMood::Celebrate;
            cat.status = CatStatus::Content;
            cat.since  = now;
            cat.frame  = 0;
            cat.rumbling = false;
            // Persist last treat time
            time_t epoch = ntpClient.getEpochTime();
            if (epoch > 0) {
                configMgr.config().lastTreatEpoch = (uint32_t)epoch;
                configMgr.save();
            }
            dirty.animal = true;
        }
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

    // Blink (idle only) — eye-only redraw to avoid full-zone flash
    if (cat.mood == CatMood::Idle) {
        if ( cat.eyeOpen && now - cat.lastBlink > 4000) { cat.eyeOpen = false; cat.lastBlink = now; dirty.eyesOnly = true; }
        if (!cat.eyeOpen && now - cat.lastBlink >  150) { cat.eyeOpen = true;                       dirty.eyesOnly = true; }
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

static void updateCatStatus() {
    time_t epoch = ntpClient.getEpochTime();
    if (epoch <= 0) return;  // NTP not synced yet

    uint32_t lastTreat = configMgr.config().lastTreatEpoch;
    uint32_t threshold = (uint32_t)configMgr.config().hungerMinutes * 60u;
    uint32_t now32     = (uint32_t)epoch;
    uint32_t elapsed;
    if (lastTreat == 0)          elapsed = threshold + 1;   // never fed → start hungry
    else if (now32 >= lastTreat) elapsed = now32 - lastTreat;
    else                         elapsed = 0;               // NTP clock step back → treat as just fed

    CatStatus prev = cat.status;
    if (elapsed < threshold / 2)
        cat.status = CatStatus::Content;
    else if (elapsed < threshold)
        cat.status = CatStatus::Peckish;
    else
        cat.status = CatStatus::Hungry;

    if (cat.status != prev) dirty.animal = true;

    // Tummy rumble: 5 s cycle for Peckish, 2 s cycle for Hungry — only redraws the lines
    bool shouldRumble = (cat.status == CatStatus::Peckish || cat.status == CatStatus::Hungry)
                        && cat.mood == CatMood::Idle;
    if (shouldRumble) {
        unsigned long now = millis();
        unsigned long interval = (cat.status == CatStatus::Hungry) ? 2000UL : 5000UL;
        if (!cat.rumbling && now - cat.lastRumble > interval) {
            cat.rumbling   = true;
            cat.lastRumble = now;
            dirty.hungerLines = true;
        } else if (cat.rumbling && now - cat.lastRumble > 400) {
            cat.rumbling   = false;
            cat.lastRumble = now;
            dirty.hungerLines = true;
        }
    } else if (cat.rumbling) {
        cat.rumbling = false;
        dirty.hungerLines = true;
    }
}

// ── Config web page ───────────────────────────────────────────────────────────

static const char CONFIG_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CYD Clock · Config</title>
<style>
*{box-sizing:border-box}
body{font-family:sans-serif;max-width:500px;margin:0 auto;padding:20px;background:#111;color:#ddd}
h2{margin-top:0}
h3{margin:20px 0 10px;font-size:1rem;color:#aaa;border-bottom:1px solid #333;padding-bottom:6px}
label{display:block;font-size:.82rem;color:#888;margin-bottom:2px}
input{display:block;width:100%;padding:8px;margin-bottom:14px;background:#1e1e1e;color:#ddd;border:1px solid #333;border-radius:5px}
.row{display:flex;gap:8px}
.row input{flex:1;margin-bottom:0}
button{padding:9px 16px;background:#0070f3;color:#fff;border:none;border-radius:5px;cursor:pointer}
button:hover{background:#005ec4}
.drop{margin:8px 0 14px;border:1px solid #333;border-radius:5px;max-height:200px;overflow-y:auto;display:none}
.city{padding:10px 12px;cursor:pointer;border-bottom:1px solid #222}
.city:hover,.city:focus{background:#1e1e1e;outline:none}
.city small{color:#666}
.banner{padding:10px;border-radius:5px;margin-bottom:14px}
.ok{background:#063}
</style>
</head><body>
<h2>Configuration</h2>
%%MSG%%
<form method="POST" action="/save-config">

<h3>Weather location</h3>
<label>City search</label>
<div class="row">
<input id="wcs" placeholder="e.g. Paris, Toronto…" oninput="deb('w',this.value)">
<button type="button" onclick="search('w')">Search</button>
</div>
<div id="wres" class="drop"></div>
<label>Latitude</label>
<input name="lat" id="lat" value="%%LAT%%">
<label>Longitude</label>
<input name="lon" id="lon" value="%%LON%%">

<h3>Cat hunger</h3>
<label>Minutes until hungry</label>
<input name="hunger" id="hunger" type="number" min="1" max="1440" value="%%HUNGER%%">

<h3>Timezone (for clock)</h3>
<label>City search</label>
<div class="row">
<input id="tcs" placeholder="e.g. London, New York…" oninput="deb('t',this.value)">
<button type="button" onclick="search('t')">Search</button>
</div>
<div id="tres" class="drop"></div>
<label>UTC Offset (seconds)</label>
<input name="utc" id="utc" type="number" value="%%UTC%%">

<button type="submit" style="width:100%;margin-top:8px">Save</button>
</form>
<script>
const tm={};
function deb(k,v){clearTimeout(tm[k]);if(v.length>1)tm[k]=setTimeout(()=>search(k),500)}
async function search(k){
  const q=document.getElementById(k+'cs').value.trim();
  if(!q)return;
  const el=document.getElementById(k+'res');
  el.style.display='block';el.innerHTML='<div class="city">Searching…</div>';
  try{
    const r=await fetch('https://geocoding-api.open-meteo.com/v1/search?name='+encodeURIComponent(q)+'&count=8&language=en&format=json');
    const d=await r.json();
    if(!d.results||!d.results.length){el.innerHTML='<div class="city">No results</div>';return;}
    el.innerHTML='';
    d.results.forEach(c=>{
      const div=document.createElement('div');
      div.className='city';div.tabIndex=0;
      const b=document.createElement('strong');b.textContent=c.name;div.appendChild(b);
      if(c.admin1){div.appendChild(document.createTextNode(', '+c.admin1));}
      const sm=document.createElement('small');sm.textContent=' '+c.country;div.appendChild(sm);
      const fn=()=>pick(k,c.latitude,c.longitude,c.timezone||'');
      div.addEventListener('click',fn);
      div.addEventListener('keydown',e=>{if(e.key==='Enter')fn();});
      el.appendChild(div);
    });
  }catch(e){el.innerHTML='<div class="city">Network error</div>';}
}
function utcFromTz(tz){
  try{
    const p=new Intl.DateTimeFormat('en',{timeZone:tz,timeZoneName:'longOffset'}).formatToParts(new Date());
    const s=p.find(x=>x.type==='timeZoneName').value;
    const m=s.match(/GMT([+-]?)(\d{2}):(\d{2})/);
    return m?(m[1]==='-'?-1:1)*(+m[2]*3600+ +m[3]*60):null;
  }catch(e){return null;}
}
function pick(k,lat,lon,tz){
  document.getElementById(k+'res').style.display='none';
  document.getElementById(k+'cs').value='';
  if(k==='w'){
    document.getElementById('lat').value=lat;
    document.getElementById('lon').value=lon;
  } else {
    const off=utcFromTz(tz);
    if(off!==null)document.getElementById('utc').value=off;
  }
}
</script>
</body></html>
)html";

static void handleConfigGet() {
    String page = String(FPSTR(CONFIG_HTML));
    page.replace("%%LAT%%",    String(configMgr.config().latitude,  4));
    page.replace("%%LON%%",    String(configMgr.config().longitude, 4));
    page.replace("%%UTC%%",    String(configMgr.config().utcOffsetSeconds));
    page.replace("%%HUNGER%%", String(configMgr.config().hungerMinutes));
    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Settings saved.</div>";
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

static void handleConfigPost() {
    float lat    = wm.server->arg("lat").toFloat();
    float lon    = wm.server->arg("lon").toFloat();
    int   utc    = wm.server->arg("utc").toInt();
    int   hunger = wm.server->arg("hunger").toInt();
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f || utc < -50400 || utc > 50400
        || hunger < 1 || hunger > 1440) {
        wm.server->send(400, "text/plain", "Invalid values");
        return;
    }
    configMgr.config().latitude         = lat;
    configMgr.config().longitude        = lon;
    configMgr.config().utcOffsetSeconds = utc;
    configMgr.config().hungerMinutes    = hunger;
    configMgr.save();
    ntpClient.setTimeOffset(utc);
    lastWeatherFetch = millis() - WEATHER_UPDATE_INTERVAL_MS - 1;
    dirty.header = true;
    wm.server->sendHeader("Location", "/config?saved=1");
    wm.server->send(302, "text/plain", "");
}

// ── WiFi setup screens ────────────────────────────────────────────────────────
static void drawWifiConnecting(const String& ssid) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("Connecting to", CX, 90, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString(ssid.c_str(), CX, 130, 4);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("please wait...", CX, 200, 2);
}

static void drawWifiPortal() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Connect to WiFi:", CX, 70, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString("CYD-Clock", CX, 110, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("then open browser:", CX, 175, 2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawCentreString("192.168.4.1", CX, 210, 4);
}

// ── WiFiManager ───────────────────────────────────────────────────────────────
static void runWiFiManager(ConfigManager& cfg) {
    (void)cfg;  // config now managed exclusively via /config web page
    wm.setAPCallback([](WiFiManager*) { drawWifiPortal(); });
    wm.setCustomMenuHTML("<form action='/config' method='get'><button>Configuration</button></form><br/>");
    const char* menu[] = {"wifi", "custom", "info", "sep", "update", "exit"};
    wm.setMenu(menu, 6);
    wm.autoConnect("CYD-Clock");
    wm.startWebPortal();
    wm.server->on("/config",      HTTP_GET,  handleConfigGet);
    wm.server->on("/save-config", HTTP_POST, handleConfigPost);
}

// ── Arduino ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
    tft.invertDisplay(false);  // ST7789_Init.h hardcodes INVON; this panel needs INVOFF
    tft.fillScreen(TFT_BLACK);
    touchSPI.begin(TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI, TOUCH_CS);
    touch.begin(touchSPI);
    touch.setRotation(0);

    configMgr.begin();
    configMgr.load();

    {
        WiFi.mode(WIFI_STA);  // init driver so esp_wifi_get_config can read NVS
        String ssid = wm.getWiFiSSID();
        if (ssid.length() > 0)
            drawWifiConnecting(ssid);
        else
            drawWifiPortal();
    }

    runWiFiManager(configMgr);

    ntpClient.begin();
    ntpClient.setTimeOffset(configMgr.config().utcOffsetSeconds);
    ntpClient.update();

    weather.fetch(configMgr.config().latitude, configMgr.config().longitude);
    lastWeatherFetch = millis();

    tft.fillScreen(TFT_BLACK);

    cat.lastBlink = millis();
    // Status seeded on first updateCatStatus() call once NTP is synced
    // dirty flags are all true at declaration — first loop draws everything
}

void loop() {
    wm.process();
    ntpClient.update();
    handleTouch();

    unsigned long now = millis();

    // IP display expiry
    if (showIpUntilMs > 0 && now >= showIpUntilMs) {
        showIpUntilMs = 0;
        dirty.header  = true;
    }

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

    // Cat animation and hunger status
    updateCatAnim();
    updateCatStatus();

    // Header tick — full redraw on minute change, seconds-only otherwise
    {
        static int prevSec = -1;
        static int prevMin = -1;
        time_t ep = ntpClient.getEpochTime();
        struct tm* tm = localtime(&ep);
        if (tm->tm_sec != prevSec) {
            prevSec = tm->tm_sec;
            if (tm->tm_min != prevMin) { prevMin = tm->tm_min; dirty.header = true; }
            else                        { dirty.headerTick = true; }
        }
    }

    // Timer row refresh while running
    {
        static unsigned long lastTimerDraw = 0;
        if (timerWidget.isRunning() && now - lastTimerDraw >= 500) {
            lastTimerDraw   = now;
            dirty.timerTick = true;
        }
    }

    if (dirty.header)             { drawHeader();      dirty.header     = false; dirty.headerTick = false; }
    else if (dirty.headerTick)   { drawHeaderTick();  dirty.headerTick = false; }
    if (dirty.animal)             { drawAnimal();                                dirty.animal = false; dirty.eyesOnly = false; dirty.hungerLines = false; }
    else if (dirty.eyesOnly)      { drawEyes(CAT_CX, CAT_CY, cat.eyeOpen);     dirty.eyesOnly = false; }
    else if (dirty.hungerLines)   { drawHungerLines(CAT_CX, CAT_CY, cat.rumbling); dirty.hungerLines = false; }
    if (dirty.picker)        { drawPicker();                            dirty.picker    = false; }
    if (dirty.timerRow)      { drawTimerRow();  dirty.timerRow  = false; dirty.timerTick = false; }
    else if (dirty.timerTick){ drawTimerDigits(); dirty.timerTick = false; }

    delay(50);
}
