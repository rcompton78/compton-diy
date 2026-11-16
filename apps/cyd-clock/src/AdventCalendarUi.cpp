#include "AdventCalendarUi.h"

using advent::DOORS;
using advent::DOOR_COUNT;
using advent::Reward;

namespace {

// Colours (RGB565).
constexpr uint16_t C_BG          = 0x08A6;  // the Christmas Night sky navy (~#081430)
constexpr uint16_t C_GOLD        = 0xFEA0;  // ~#ffd400, same gold as the tree star
constexpr uint16_t C_DOOR_RED    = 0xB104;  // ~#b22222
constexpr uint16_t C_DOOR_GREEN  = 0x1BC6;  // ~#1f7a30, the tree green
constexpr uint16_t C_OPENED      = 0x1906;  // ~#1c2333, an opened door's empty cubby
constexpr uint16_t C_LOCKED      = 0x2967;  // ~#2a2f3a
constexpr uint16_t C_LOCKED_TEXT = 0x5B0E;  // ~#5a6070
constexpr uint16_t C_CARD        = 0x1107;  // ~#10203a
constexpr uint16_t C_DIM         = 0x8410;  // mid grey
constexpr uint16_t C_BLUE        = 0x3D9F;  // ~#38b0ff, the tree's blue bulb
constexpr uint16_t C_BROWN       = 0x9A46;  // gingerbread, same as the teddy bear
constexpr uint16_t C_WIRE        = 0x2204;  // garland wire, same as the tree's light string
constexpr uint16_t C_COUPON      = 0x07E8;  // bright green

// Layout. The grid is 5×5 doors of CELL px, PITCH apart, under the title and garland.
constexpr int TITLE_H   = 28;
constexpr int CLOSE_X   = 204;  // close button: x >= CLOSE_X, y < TITLE_H
constexpr int GARLAND_Y = 31;   // the wire's ends; it sags GARLAND_SAG px in the middle
constexpr int GARLAND_SAG = 6;
constexpr int GRID_X    = 3;
constexpr int GRID_Y    = 50;
constexpr int CELL      = 45;
constexpr int PITCH     = 47;
static_assert(DOOR_COUNT == 5 * 5, "the layout below is hard-coded to a 5x5 grid");
constexpr int FOOTER_Y  = GRID_Y + 5 * PITCH + 2;  // 287
constexpr int CARD_X = 12, CARD_Y = 52, CARD_W = 216, CARD_H = 248;
constexpr int CARD_ICON_CY = CARD_Y + 62;
constexpr int CARD_TEXT_Y  = CARD_Y + 104;
constexpr int LINE_H       = 18;

constexpr unsigned long FRAME_MS   = 70;
constexpr int DOOR_FRAMES          = 6;
constexpr unsigned long PULSE_MS   = 500;
constexpr unsigned long IDLE_CLOSE_MS = 60000;
constexpr unsigned long TAP_GUARD_MS  = 500;  // > the 350 ms touch debounce; see _acceptTapsAtMs

// 7×7 ornament art. Palette indices: 0 transparent, 1 red, 2 white, 3 gold, 4 blue, 5 brown,
// 6 dark grey.
constexpr uint16_t ORNAMENT_PALETTE[] = {0, TFT_RED, TFT_WHITE, C_GOLD, C_BLUE, C_BROWN, 0x4208};
constexpr uint8_t ORNAMENT_ART[advent::ORNAMENT_COUNT][7][7] = {
    {   // red ball
        {0, 0, 0, 3, 0, 0, 0},
        {0, 0, 1, 1, 1, 0, 0},
        {0, 1, 2, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 1, 0},
        {0, 0, 1, 1, 1, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
    },
    {   // gold bell
        {0, 0, 0, 3, 0, 0, 0},
        {0, 0, 3, 3, 3, 0, 0},
        {0, 0, 3, 3, 3, 0, 0},
        {0, 3, 3, 3, 3, 3, 0},
        {0, 3, 3, 3, 3, 3, 0},
        {3, 3, 3, 3, 3, 3, 3},
        {0, 0, 0, 6, 0, 0, 0},
    },
    {   // candy cane
        {0, 0, 2, 1, 2, 0, 0},
        {0, 1, 0, 0, 0, 1, 0},
        {0, 2, 0, 0, 0, 2, 0},
        {0, 0, 0, 0, 0, 1, 0},
        {0, 0, 0, 0, 0, 2, 0},
        {0, 0, 0, 0, 0, 1, 0},
        {0, 0, 0, 0, 0, 2, 0},
    },
    {   // blue ball
        {0, 0, 0, 3, 0, 0, 0},
        {0, 0, 4, 4, 4, 0, 0},
        {0, 4, 2, 4, 4, 4, 0},
        {0, 4, 4, 4, 4, 4, 0},
        {0, 4, 4, 4, 4, 4, 0},
        {0, 0, 4, 4, 4, 0, 0},
        {0, 0, 0, 0, 0, 0, 0},
    },
    {   // snowflake
        {0, 0, 0, 2, 0, 0, 0},
        {0, 2, 0, 2, 0, 2, 0},
        {0, 0, 2, 2, 2, 0, 0},
        {2, 2, 2, 4, 2, 2, 2},
        {0, 0, 2, 2, 2, 0, 0},
        {0, 2, 0, 2, 0, 2, 0},
        {0, 0, 0, 2, 0, 0, 0},
    },
    {   // gingerbread
        {0, 0, 5, 5, 5, 0, 0},
        {0, 0, 5, 5, 5, 0, 0},
        {5, 5, 5, 2, 5, 5, 5},
        {0, 0, 5, 5, 5, 0, 0},
        {0, 0, 5, 2, 5, 0, 0},
        {0, 5, 5, 0, 5, 5, 0},
        {0, 5, 0, 0, 0, 5, 0},
    },
};

const uint16_t GARLAND_COLORS[] = {TFT_RED, C_GOLD, C_BLUE, C_DOOR_GREEN};

int cellX(int day) { return GRID_X + ((day - 1) % 5) * PITCH; }
int cellY(int day) { return GRID_Y + ((day - 1) / 5) * PITCH; }

int garlandY(int x) {
    const int mid = 120, half = 114;
    int d = x - mid;
    return GARLAND_Y + GARLAND_SAG - GARLAND_SAG * d * d / (half * half);
}

// Draws `text` centred on x = 120, one line per '\n', starting at `y` in font 2.
// Returns the y just below the last line.
int drawCentredLines(TFT_eSPI& tft, const char* text, int y, uint16_t color, uint16_t bg) {
    tft.setTextColor(color, bg);
    char line[40];
    while (*text) {
        int n = 0;
        while (text[n] && text[n] != '\n' && n < (int)sizeof(line) - 1) {
            line[n] = text[n];
            n++;
        }
        line[n] = '\0';
        tft.drawCentreString(line, 120, y, 2);
        y += LINE_H;
        text += n;
        if (*text == '\n') text++;
    }
    return y;
}

}  // namespace

void drawAdventOrnament(TFT_eSPI& tft, int k, int cx, int cy, int scale) {
    if (k < 0 || k >= advent::ORNAMENT_COUNT) return;
    int x0 = cx - 3 * scale, y0 = cy - 3 * scale;
    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < 7; c++) {
            uint8_t p = ORNAMENT_ART[k][r][c];
            if (p) tft.fillRect(x0 + c * scale, y0 + r * scale, scale, scale, ORNAMENT_PALETTE[p]);
        }
    }
}

void drawAdventPresent(TFT_eSPI& tft, int cx, int cy, int scale, bool lit) {
    int s = scale;
    uint16_t ribbon = lit ? C_GOLD : TFT_WHITE;
    tft.fillRect(cx - 5 * s, cy - 2 * s, 10 * s, 7 * s, TFT_RED);   // box
    tft.fillRect(cx - 6 * s, cy - 5 * s, 12 * s, 3 * s, TFT_RED);   // lid
    tft.drawFastHLine(cx - 6 * s, cy - 2 * s, 12 * s, 0x8000);       // lid shadow line
    tft.fillRect(cx - s, cy - 5 * s, 2 * s, 10 * s, ribbon);          // ribbon
    int r = s + s / 2;
    tft.fillCircle(cx - 2 * s, cy - 5 * s - r + 1, r, ribbon);        // bow
    tft.fillCircle(cx + 2 * s - 1, cy - 5 * s - r + 1, r, ribbon);
}

void drawAdventStar(TFT_eSPI& tft, int cx, int cy, int r) {
    int dx = r * 87 / 100, dy = r / 2;
    tft.fillTriangle(cx, cy - r, cx - dx, cy + dy, cx + dx, cy + dy, C_GOLD);
    tft.fillTriangle(cx, cy + r, cx - dx, cy - dy, cx + dx, cy - dy, C_GOLD);
}

void AdventCalendarUi::open(unsigned long now) {
    _open = true;
    _phase = Phase::Grid;
    _saveFailed = false;
    _jokeAnswerShown = false;
    _lastActivityMs = now;
    _acceptTapsAtMs = now + TAP_GUARD_MS;
    _nextPulseMs = now;
    drawAll();
}

void AdventCalendarUi::drawAll() {
    _today = _host.todayDoor();
    _tft.fillScreen(C_BG);
    drawTitle();
    drawGarland();
    for (int d = 1; d <= DOOR_COUNT; d++) drawCell(d);
    drawDefaultFooter();
}

void AdventCalendarUi::drawTitle() {
    char title[32];
    int year = _host.year();
    if (year > 0) snprintf(title, sizeof(title), "Advent %d", year);
    else snprintf(title, sizeof(title), "Advent");
    _tft.setTextColor(TFT_WHITE, C_BG);
    _tft.drawString(title, 8, 7, 2);
    if (_host.isPreview()) {
        _tft.setTextColor(C_GOLD, C_BG);
        _tft.drawString("PREVIEW", 110, 11, 1);
    }
    // Close button.
    _tft.fillRoundRect(CLOSE_X + 4, 3, 30, 22, 4, C_LOCKED);
    _tft.setTextColor(TFT_WHITE, C_LOCKED);
    _tft.drawCentreString("X", CLOSE_X + 19, 6, 2);
}

void AdventCalendarUi::drawGarland() {
    uint32_t opened = _host.openedMask();
    int px = 4, py = garlandY(4);
    for (int x = 10; x <= 236; x += 6) {
        int y = garlandY(x);
        _tft.drawLine(px, py, x, y, C_WIRE);
        px = x;
        py = y;
    }
    // One bulb per door, lit once that door is opened.
    for (int i = 0; i < DOOR_COUNT; i++) {
        int x = 12 + i * 9;
        int y = garlandY(x) + 4;
        _tft.fillCircle(x, y, 3, C_BG);
        if (advent::isOpened(opened, i + 1)) _tft.fillCircle(x, y, 3, GARLAND_COLORS[i % 4]);
        else _tft.fillCircle(x, y, 1, C_LOCKED_TEXT);
    }
}

void AdventCalendarUi::drawCell(int day) {
    int x = cellX(day), y = cellY(day);
    int cx = x + CELL / 2, cy = y + CELL / 2;
    uint32_t opened = _host.openedMask();
    char num[4];
    snprintf(num, sizeof(num), "%d", day);

    if (advent::isOpened(opened, day)) {
        _tft.fillRoundRect(x, y, CELL, CELL, 4, C_OPENED);
        _tft.setTextColor(C_DIM, C_OPENED);
        _tft.drawString(num, x + 3, y + 2, 1);
        const advent::Door& door = DOORS[day - 1];
        char buf[8];
        switch (door.reward) {
            case Reward::Points:
                snprintf(buf, sizeof(buf), "+%u", (unsigned)door.value);
                _tft.setTextColor(TFT_YELLOW, C_OPENED);
                _tft.drawCentreString(buf, cx, cy - 5, 2);
                break;
            case Reward::Gift:
                drawAdventPresent(_tft, cx, cy + 3, 2, true);
                break;
            case Reward::Coupon:
                _tft.setTextColor(C_COUPON, C_OPENED);
                _tft.drawCentreString("50%", cx, cy - 5, 2);
                break;
            case Reward::Joke:
                _tft.setTextColor(TFT_CYAN, C_OPENED);
                _tft.drawCentreString("Ha!", cx, cy - 5, 2);
                break;
            case Reward::Ornament:
                drawAdventOrnament(_tft, door.value, cx, cy + 2, 3);
                break;
            case Reward::Christmas:
                drawAdventStar(_tft, cx, cy + 2, 14);
                break;
        }
        return;
    }

    if (day > _today) {  // locked
        _tft.fillRoundRect(x, y, CELL, CELL, 4, C_LOCKED);
        _tft.setTextColor(C_LOCKED_TEXT, C_LOCKED);
        _tft.drawCentreString(num, cx, cy - 12, 4);
        return;
    }

    // Closed and openable: alternating red and green doors with a gold frame; today's door has a
    // thicker one that pulses white/gold. The frame is a rounded rect in the frame colour with the
    // door inset over it (fillRoundRect only: drawRoundRect would link in another ~800 B, COM-386).
    uint16_t door = ((day - 1) % 5 + (day - 1) / 5) % 2 ? C_DOOR_GREEN : C_DOOR_RED;
    uint16_t frame = (day == _today && _pulseOn) ? TFT_WHITE : C_GOLD;
    int inset = day == _today ? 2 : 1;
    _tft.fillRoundRect(x, y, CELL, CELL, 4, frame);
    _tft.fillRoundRect(x + inset, y + inset, CELL - 2 * inset, CELL - 2 * inset, 4 - inset, door);
    _tft.setTextColor(TFT_WHITE, door);
    _tft.drawCentreString(num, cx, cy - 12, 4);
}

// Frame `frame` (1..DOOR_FRAMES) of door `day` swinging open on its left hinge: the door panel
// narrows towards the hinge over an empty cubby.
void AdventCalendarUi::drawDoorFrame(int day, int frame) {
    int x = cellX(day), y = cellY(day);
    _tft.fillRoundRect(x, y, CELL, CELL, 4, C_OPENED);
    int w = CELL * (DOOR_FRAMES - frame) / DOOR_FRAMES;
    if (w <= 0) return;
    uint16_t door = ((day - 1) % 5 + (day - 1) / 5) % 2 ? C_DOOR_GREEN : C_DOOR_RED;
    _tft.fillRect(x, y, w, CELL, door);
    _tft.drawFastVLine(x + w - 1, y, CELL, C_GOLD);
}

void AdventCalendarUi::drawFooter(const char* text) {
    _tft.fillRect(0, FOOTER_Y, 240, 320 - FOOTER_Y, C_BG);
    _tft.setTextColor(TFT_WHITE, C_BG);
    _tft.drawCentreString(text, 120, FOOTER_Y + 8, 2);
}

void AdventCalendarUi::drawDefaultFooter() {
    uint32_t opened = _host.openedMask();
    char buf[40];
    if (_today == 0) {
        snprintf(buf, sizeof(buf), "The doors open on Dec 1");
    } else if (!advent::isOpened(opened, _today)) {
        snprintf(buf, sizeof(buf), "Tap door %d to open it!", _today);
    } else if (advent::anyOpenable(opened, _today)) {
        snprintf(buf, sizeof(buf), "Catch up on missed doors!");
    } else if (_today >= DOOR_COUNT) {
        snprintf(buf, sizeof(buf), "Merry Christmas!");
    } else {
        snprintf(buf, sizeof(buf), "Come back tomorrow for door %d", _today + 1);
    }
    drawFooter(buf);
}

void AdventCalendarUi::startReveal(int day, bool animate, unsigned long now) {
    _day = day;
    _jokeAnswerShown = false;
    if (animate) {
        _phase = Phase::Animating;
        _frame = 0;
        _nextFrameMs = now;
    } else {
        _phase = Phase::Reveal;
        drawReveal();
    }
}

void AdventCalendarUi::drawReveal() {
    _tft.fillRoundRect(CARD_X, CARD_Y, CARD_W, CARD_H, 8, C_GOLD);  // a 2 px gold frame
    _tft.fillRoundRect(CARD_X + 2, CARD_Y + 2, CARD_W - 4, CARD_H - 4, 6, C_CARD);
    char head[16];
    snprintf(head, sizeof(head), "Dec %d", _day);
    _tft.setTextColor(C_DIM, C_CARD);
    _tft.drawCentreString(head, 120, CARD_Y + 10, 2);

    const char* hint = "Tap to close";
    if (_saveFailed) {
        drawCentredLines(_tft, "Couldn't save.\nPlease try again.", CARD_TEXT_Y - 30, TFT_RED, C_CARD);
    } else {
        const advent::Door& door = DOORS[_day - 1];
        char buf[64];  // the longest text (the Christmas card) is 52 chars
        switch (door.reward) {
            case Reward::Points:
                snprintf(buf, sizeof(buf), "%u", (unsigned)door.value);
                _tft.setTextColor(TFT_YELLOW, C_CARD);
                _tft.drawCentreString(buf, 120, CARD_ICON_CY - 24, 6);
                drawCentredLines(_tft, "bonus points!", CARD_TEXT_Y, TFT_WHITE, C_CARD);
                break;
            case Reward::Gift: {
                drawAdventPresent(_tft, 120, CARD_ICON_CY + 6, 4, true);
                const char* label = _host.giftLabel(door.value);
                if (label) {
                    int y = drawCentredLines(_tft, "A surprise gift:", CARD_TEXT_Y, TFT_WHITE, C_CARD);
                    y = drawCentredLines(_tft, label, y, TFT_YELLOW, C_CARD);
                    drawCentredLines(_tft, "Find it in the\nDressing Room.", y + 6, TFT_WHITE, C_CARD);
                } else {
                    snprintf(buf, sizeof(buf), "Nothing left to gift!\nHere's %lu points\ninstead.",
                             (unsigned long)advent::GIFT_FALLBACK_POINTS);
                    drawCentredLines(_tft, buf, CARD_TEXT_Y, TFT_WHITE, C_CARD);
                }
                break;
            }
            case Reward::Coupon:
                _tft.setTextColor(C_COUPON, C_CARD);
                _tft.drawCentreString("50% OFF", 120, CARD_ICON_CY - 12, 4);
                drawCentredLines(_tft, "A coupon for one\nitem in the store.\nIt never expires!", CARD_TEXT_Y,
                                 TFT_WHITE, C_CARD);
                break;
            case Reward::Joke: {
                const advent::Joke& joke = advent::JOKES[door.value];
                int y = drawCentredLines(_tft, joke.question, CARD_Y + 50, TFT_WHITE, C_CARD);
                if (_jokeAnswerShown) drawCentredLines(_tft, joke.answer, y + 20, TFT_YELLOW, C_CARD);
                else hint = "Tap for the answer";
                break;
            }
            case Reward::Ornament:
                drawAdventOrnament(_tft, door.value, 120, CARD_ICON_CY, 7);
                drawCentredLines(_tft, "A new ornament\nfor your tree!", CARD_TEXT_Y, TFT_WHITE, C_CARD);
                break;
            case Reward::Christmas:
                drawAdventStar(_tft, 120, CARD_ICON_CY, 34);
                snprintf(buf, sizeof(buf), "Merry Christmas!\n+%lu points and a\nChristmas voucher:",
                         (unsigned long)advent::CHRISTMAS_POINTS);
                {
                    int y = drawCentredLines(_tft, buf, CARD_TEXT_Y, TFT_WHITE, C_CARD);
                    drawCentredLines(_tft, "pick one item free\nin the store!", y, TFT_YELLOW, C_CARD);
                }
                break;
        }
    }
    _tft.setTextColor(C_DIM, C_CARD);
    _tft.drawCentreString(hint, 120, CARD_Y + CARD_H - 20, 1);
}

void AdventCalendarUi::handleTouch(int x, int y, unsigned long now) {
    if (!_open) return;
    _lastActivityMs = now;
    // A held press repeats every 350 ms (< TAP_GUARD_MS), so pushing the guard out on every
    // ignored repeat keeps it shut until the finger lifts; a new tap needs a short release first.
    bool held = (long)(now - _acceptTapsAtMs) < 0;
    _acceptTapsAtMs = now + TAP_GUARD_MS;  // every handled tap changes the screen too
    if (held) return;

    if (_phase == Phase::Animating) return;  // let the swing finish
    if (_phase == Phase::Reveal) {
        bool isJoke = !_saveFailed && DOORS[_day - 1].reward == Reward::Joke;
        if (isJoke && !_jokeAnswerShown) {
            _jokeAnswerShown = true;
            drawReveal();
            return;
        }
        _phase = Phase::Grid;
        _saveFailed = false;
        drawAll();
        return;
    }

    if (y < TITLE_H) {
        if (x >= CLOSE_X) close();
        return;
    }
    if (y < GRID_Y || y >= GRID_Y + 5 * PITCH || x < GRID_X) return;
    int col = (x - GRID_X) / PITCH, row = (y - GRID_Y) / PITCH;
    if (col > 4) return;
    int day = row * 5 + col + 1;

    // If the day turned since the grid was drawn, show the new day first rather than acting on a
    // stale grid (update() makes the same check on its pulse).
    int today = _host.todayDoor();
    if (today != _today) {
        if (today == 0) close();
        else drawAll();
        return;
    }
    uint32_t opened = _host.openedMask();
    if (advent::isOpened(opened, day)) {
        // Shows what was inside. In Preview it replays the whole opening, for testing; nothing
        // is granted again either way.
        startReveal(day, _host.isPreview(), now);
    } else if (advent::isOpenable(opened, day, _today)) {
        if (_host.openDoor(day)) {
            startReveal(day, true, now);
        } else {
            _saveFailed = true;
            startReveal(day, false, now);
        }
    } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "Door %d opens on Dec %d", day, day);
        drawFooter(buf);
    }
}

void AdventCalendarUi::update(unsigned long now) {
    if (!_open) return;
    if (now - _lastActivityMs > IDLE_CLOSE_MS) {
        close();
        return;
    }
    if (_phase == Phase::Animating) {
        if ((long)(now - _nextFrameMs) < 0) return;
        _nextFrameMs = now + FRAME_MS;
        if (++_frame <= DOOR_FRAMES) {
            drawDoorFrame(_day, _frame);
        } else {
            drawGarland();  // light the new bulb before the card covers the grid
            _phase = Phase::Reveal;
            drawReveal();
        }
        return;
    }
    if (_phase == Phase::Grid && (long)(now - _nextPulseMs) >= 0) {
        _nextPulseMs = now + PULSE_MS;
        // The day can turn while the calendar is open (midnight, or Jan 1 ending it).
        int today = _host.todayDoor();
        if (today != _today) {
            if (today == 0) close();
            else drawAll();
            return;
        }
        if (_today >= 1 && !advent::isOpened(_host.openedMask(), _today)) {
            _pulseOn = !_pulseOn;
            drawCell(_today);
        }
    }
}
