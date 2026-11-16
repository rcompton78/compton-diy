#pragma once
#include <TFT_eSPI.h>
#include "AdventCalendar.h"

// The Advent calendar's on-device screen (COM-655): a full-screen takeover with a garland, a 5×5
// grid of doors, a door-opening animation and a reveal card. It also owns the calendar's art
// (ornaments, the present, the star), which main.cpp reuses for the room's gift icon and the
// ornaments on the Christmas Night tree.
//
// It never reaches into main.cpp: everything it needs to know or change goes through the
// AdventUiHost callbacks, so main.cpp keeps ownership of config, saving and rewards. Like the
// level-up fireworks, it owns the whole 240×320 screen while open; main.cpp's loop() skips its
// normal zone redraws meanwhile and repaints everything once isOpen() goes false.

struct AdventUiHost {
    uint32_t (*openedMask)();  // ConfigManager's adventOpened
    int (*todayDoor)();        // advent::todayDoor() for right now
    bool (*isPreview)();       // Christmas Preview: tapping an opened door replays its animation
    int (*year)();             // local year for the title, or <= 0 while the clock isn't synced
    // Grants door `day`'s reward, marks it opened and saves. Returns false (with nothing changed)
    // if the save failed.
    bool (*openDoor)(int day);
    // What gift slot `slot` gave, as a display name, or nullptr when it gave the points fallback.
    const char* (*giftLabel)(int slot);
};

class AdventCalendarUi {
public:
    AdventCalendarUi(TFT_eSPI& tft, const AdventUiHost& host) : _tft(tft), _host(host) {}

    bool isOpen() const { return _open; }
    // Takes over the screen and draws the calendar.
    void open(unsigned long now);
    // Leaves the screen as is; the caller repaints its own UI.
    void close() { _open = false; }
    // A debounced tap anywhere on the screen while open.
    void handleTouch(int x, int y, unsigned long now);
    // Animation frames, today's-door pulse and the idle auto-close. Call every loop() while open.
    void update(unsigned long now);

private:
    enum class Phase : uint8_t { Grid, Animating, Reveal };

    void drawAll();
    void drawTitle();
    void drawGarland();
    void drawCell(int day);
    void drawDoorFrame(int day, int frame);
    void drawReveal();
    void drawFooter(const char* text);
    void drawDefaultFooter();
    void startReveal(int day, bool animate, unsigned long now);

    TFT_eSPI& _tft;
    AdventUiHost _host;
    bool _open = false;
    Phase _phase = Phase::Grid;
    int _today = 0;
    int _day = 0;              // door being animated or revealed
    int _frame = 0;
    bool _saveFailed = false;  // the reveal shows a "couldn't save" card instead
    bool _jokeAnswerShown = false;
    bool _pulseOn = false;
    unsigned long _nextFrameMs = 0;
    unsigned long _nextPulseMs = 0;
    unsigned long _lastActivityMs = 0;
    // Taps before this are ignored. The touch drivers report a held press again every debounce
    // period (350 ms), so the press that opened the calendar or changed its screen would
    // otherwise land a second time on whatever is now under the finger, e.g. a door.
    unsigned long _acceptTapsAtMs = 0;
};

// Shared art. Each draws onto whatever is already there (no background fill).
// Ornament `k` (0..advent::ORNAMENT_COUNT-1), 7×7 art pixels of `scale` screen pixels each,
// centred on (cx, cy): red ball, gold bell, candy cane, blue ball, snowflake, gingerbread.
void drawAdventOrnament(TFT_eSPI& tft, int k, int cx, int cy, int scale);
// A wrapped present centred on (cx, cy), about 12×13 px at scale 1. `lit` swaps the ribbon
// colour, which is how the room's gift icon pulses.
void drawAdventPresent(TFT_eSPI& tft, int cx, int cy, int scale, bool lit);
// A six-point gold star (two opposed triangles) of radius `r` centred on (cx, cy).
void drawAdventStar(TFT_eSPI& tft, int cx, int cy, int r);
