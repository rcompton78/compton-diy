#include "AdventCalendar.h"

namespace advent {

// The schedule (declared in the header). constexpr so the static_asserts below can check it.
constexpr Door DOORS[DOOR_COUNT] = {
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

constexpr Joke JOKES[JOKE_COUNT] = {
    {"What do you call a\ncat on the beach\nat Christmas?", "Sandy Claws!"},
    {"What is a cat's\nfavourite carol?", "Deck the Halls\nwith Balls of Yarn!"},
    {"What do snowmen\neat for breakfast?", "Frosted Flakes!"},
};

// The schedule agreed on the card: 10 points days worth 150 in all, 3 gifts, 2 coupons,
// 3 jokes, 6 ornaments, and the Dec 25 door. Checked at compile time so an edit can't quietly
// change the mix. Recursive so each stays a constant expression.
static constexpr int countReward(Reward r, int i = 0) {
    return i >= DOOR_COUNT ? 0 : (DOORS[i].reward == r ? 1 : 0) + countReward(r, i + 1);
}
static constexpr int sumPoints(int i = 0) {
    return i >= DOOR_COUNT ? 0 : (DOORS[i].reward == Reward::Points ? DOORS[i].value : 0) + sumPoints(i + 1);
}
static constexpr int countRewardValue(Reward r, int value, int i = 0) {
    return i >= DOOR_COUNT ? 0
         : (DOORS[i].reward == r && DOORS[i].value == value ? 1 : 0) + countRewardValue(r, value, i + 1);
}
// Every value-indexed reward (gift slot, joke, ornament) must use each index exactly once.
static constexpr bool usesEachIndexOnce(Reward r, int count, int idx = 0) {
    return idx >= count ? true : countRewardValue(r, idx) == 1 && usesEachIndexOnce(r, count, idx + 1);
}
static_assert(countReward(Reward::Points) == 10, "10 points days");
static_assert(sumPoints() == 150, "the points days add up to 150");
static_assert(countReward(Reward::Gift) == GIFT_COUNT, "3 gift days");
static_assert(countReward(Reward::Coupon) == 2, "2 coupon days");
static_assert(countReward(Reward::Joke) == JOKE_COUNT, "3 joke days");
static_assert(countReward(Reward::Ornament) == ORNAMENT_COUNT, "6 ornament days");
static_assert(DOORS[DOOR_COUNT - 1].reward == Reward::Christmas && countReward(Reward::Christmas) == 1,
              "only door 25 is the Christmas door");
static_assert(usesEachIndexOnce(Reward::Gift, GIFT_COUNT) && usesEachIndexOnce(Reward::Joke, JOKE_COUNT) &&
              usesEachIndexOnce(Reward::Ornament, ORNAMENT_COUNT),
              "each gift slot, joke and ornament belongs to exactly one door");

bool isSaleDate(int mmdd) {
    return mmdd >= SALE_START_MMDD && mmdd <= SALE_END_MMDD;
}

int todayDoor(bool owned, int mmdd, bool preview, uint8_t previewDay) {
    if (!owned) return 0;
    if (preview) return (previewDay >= 1 && previewDay <= DOOR_COUNT) ? previewDay : DOOR_COUNT;
    if (mmdd < 1201 || mmdd > 1231) return 0;  // also -1 (clock not synced)
    int day = mmdd - 1200;
    return day > DOOR_COUNT ? DOOR_COUNT : day;  // Dec 26–31: catch-up only
}

bool isOpened(uint32_t openedMask, int day) {
    return day >= 1 && day <= DOOR_COUNT && (openedMask & (1u << (day - 1)));
}

bool isOpenable(uint32_t openedMask, int day, int today) {
    return day >= 1 && day <= DOOR_COUNT && day <= today && !isOpened(openedMask, day);
}

bool anyOpenable(uint32_t openedMask, int today) {
    for (int d = 1; d <= today && d <= DOOR_COUNT; d++) {
        if (!isOpened(openedMask, d)) return true;
    }
    return false;
}

// The opened-doors mask is 32 bits (ConfigManager::adventOpened, and 1u << (day - 1) above).
// Strictly below 32, since openedCount() also shifts by DOOR_COUNT.
static_assert(DOOR_COUNT < 32, "the opened-doors mask holds at most 31 doors");

int openedCount(uint32_t openedMask) {
    return __builtin_popcount(openedMask & ((1u << DOOR_COUNT) - 1));
}

int ornamentDoor(int k) {
    for (int i = 0; i < DOOR_COUNT; i++) {
        if (DOORS[i].reward == Reward::Ornament && DOORS[i].value == k) return i + 1;
    }
    return 0;
}

}  // namespace advent
