#pragma once
#include <stdint.h>

// Advent calendar (COM-655): a one-year legendary store item with one door a day from Dec 1 to
// 25. This header is the calendar's pure logic: the fixed door schedule, the jokes and the
// date math. It has no globals and touches neither the screen nor ConfigManager, so every
// function takes what it needs (the local MMDD date, the opened-doors mask, the Preview
// settings) as arguments. The on-device overlay lives in AdventCalendarUi.h; the store, admin
// page and touch/loop glue live in main.cpp.
//
// Rules (decided on the card):
//   - On sale Nov 15 to Dec 24 (or any time in Christmas Preview), 350 points, one year only:
//     owned exactly while ConfigManager's adventYear is the current local year.
//   - Door d opens from Dec d. Missed doors can be caught up any time until Dec 31.
//   - The same 25 doors every year (DOORS, defined in AdventCalendar.cpp).
namespace advent {

static constexpr int DOOR_COUNT     = 25;
static constexpr int GIFT_COUNT     = 3;
static constexpr int ORNAMENT_COUNT = 6;
static constexpr int JOKE_COUNT     = 3;

// Sale window as MMDD, inclusive at both ends.
static constexpr int SALE_START_MMDD = 1115;
static constexpr int SALE_END_MMDD   = 1224;

// Points from the Dec 25 door (it also gives a Christmas voucher), and what a gift door gives
// instead when there's no store item left to gift.
static constexpr uint32_t CHRISTMAS_POINTS     = 100;
static constexpr uint32_t GIFT_FALLBACK_POINTS = 50;

enum class Reward : uint8_t {
    Points,     // value = points
    Gift,       // value = gift slot 0..GIFT_COUNT-1 (which byte of adventGifts records it)
    Coupon,     // one 50%-off coupon
    Joke,       // value = index into JOKES
    Ornament,   // value = ornament 0..ORNAMENT_COUNT-1 (see drawAdventOrnament() in AdventCalendarUi.h)
    Christmas,  // CHRISTMAS_POINTS plus one Christmas voucher
};

struct Door {
    Reward reward;
    uint8_t value;
};

// Door d is DOORS[d - 1]. Defined once in AdventCalendar.cpp (which static_asserts the mix),
// so each file that includes this header doesn't get its own copy (COM-386 flash diet).
extern const Door DOORS[DOOR_COUNT];

struct Joke {
    const char* question;  // '\n' marks a line break on the reveal card
    const char* answer;
};
extern const Joke JOKES[JOKE_COUNT];

// Whether the calendar is sold on this local date (MMDD; -1 = clock not synced yet). Ignores
// Preview and ownership, which the caller adds.
bool isSaleDate(int mmdd);

/**
 * Which door is "today", the highest door that may be opened. 0 means none: the calendar isn't
 * owned, or it's not December (and not Preview).
 *
 * @param owned      Whether the calendar is owned this year.
 * @param mmdd       Today's local MMDD, or -1 while the clock isn't synced.
 * @param preview    Whether the Christmas theme week is in Preview.
 * @param previewDay The admin page's Preview day: 0 = all doors, 1–25 = that day.
 */
int todayDoor(bool owned, int mmdd, bool preview, uint8_t previewDay);

// The helpers below take door numbers 1..DOOR_COUNT. 0 is a real value in this design ("no
// door": todayDoor() and ornamentDoor() return it), and any day outside 1..DOOR_COUNT is simply
// never opened or openable, so callers can pass those results straight through.

// Whether door `day` is opened in `openedMask` (bit day-1). False for a day outside 1..DOOR_COUNT.
bool isOpened(uint32_t openedMask, int day);
// Whether door `day` can be opened now: a real door, at or before `today`, and not opened yet.
bool isOpenable(uint32_t openedMask, int day, int today);
// Whether any door can be opened now (drives the room's pulsing gift icon). `today` = todayDoor().
bool anyOpenable(uint32_t openedMask, int today);
// How many doors are opened; mask bits beyond the DOOR_COUNT doors are ignored.
int openedCount(uint32_t openedMask);
// The door that holds ornament `k`, or 0 when `k` isn't an ornament (out of range).
int ornamentDoor(int k);

}  // namespace advent
