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
//   - On sale Nov 20 to Dec 24 (or any time in Christmas Preview), 350 points, one year only:
//     owned exactly while ConfigManager's adventYear is the current local year.
//   - Door d opens from Dec d. Missed doors can be caught up any time until Dec 31.
//   - The same 25 doors every year (DOORS below).
namespace advent {

static constexpr int DOOR_COUNT     = 25;
static constexpr int GIFT_COUNT     = 3;
static constexpr int ORNAMENT_COUNT = 6;
static constexpr int JOKE_COUNT     = 3;

// Sale window as MMDD, inclusive at both ends.
static constexpr int SALE_START_MMDD = 1120;
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

// Door d is DOORS[d - 1].
static constexpr Door DOORS[DOOR_COUNT] = {
    {Reward::Ornament, 0},  //  1 red ball
    {Reward::Points, 10},   //  2
    {Reward::Joke, 0},      //  3
    {Reward::Points, 10},   //  4
    {Reward::Ornament, 1},  //  5 gold bell
    {Reward::Gift, 0},      //  6
    {Reward::Points, 10},   //  7
    {Reward::Points, 10},   //  8
    {Reward::Ornament, 2},  //  9 candy cane
    {Reward::Coupon, 0},    // 10
    {Reward::Joke, 1},      // 11
    {Reward::Points, 15},   // 12
    {Reward::Gift, 1},      // 13
    {Reward::Points, 15},   // 14
    {Reward::Ornament, 3},  // 15 blue ball
    {Reward::Points, 15},   // 16
    {Reward::Coupon, 0},    // 17
    {Reward::Points, 15},   // 18
    {Reward::Ornament, 4},  // 19 snowflake
    {Reward::Gift, 2},      // 20
    {Reward::Points, 25},   // 21
    {Reward::Joke, 2},      // 22
    {Reward::Points, 25},   // 23
    {Reward::Ornament, 5},  // 24 gingerbread
    {Reward::Christmas, 0}, // 25
};

struct Joke {
    const char* question;  // '\n' marks a line break on the reveal card
    const char* answer;
};
static constexpr Joke JOKES[JOKE_COUNT] = {
    {"What do you call a\ncat on the beach\nat Christmas?", "Sandy Claws!"},
    {"What is a cat's\nfavourite carol?", "Deck the Halls\nwith Balls of Yarn!"},
    {"What do snowmen\neat for breakfast?", "Frosted Flakes!"},
};

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

bool isOpened(uint32_t openedMask, int day);
// Door `day` can be opened now: it's at or before `today` and not opened yet.
bool isOpenable(uint32_t openedMask, int day, int today);
// Any door can be opened now (drives the room's pulsing gift icon).
bool anyOpenable(uint32_t openedMask, int today);
int openedCount(uint32_t openedMask);
// The door that holds ornament `k`.
int ornamentDoor(int k);

}  // namespace advent
