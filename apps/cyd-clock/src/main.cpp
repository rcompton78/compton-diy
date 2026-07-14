#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <WiFiClientSecure.h>
#include <Update.h>
#include <esp_ota_ops.h>

#include "../include/Config.h"
#include "ConfigManager.h"
#include "WeatherClient.h"
#include "OtaUpdateClient.h"
#include "TimerWidget.h"
#include "StopwatchWidget.h"
#if defined(BOARD_FREENOVE_S3)
#include "Ft6336uTouch.h"
#else
#include "Xpt2046Touch.h"
#endif

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

// Play button — bottom-left of animal zone, mirrors the treat button
static constexpr int PLAY_X  = 5;
static constexpr int PLAY_Y  = 178;
static constexpr int PLAY_W  = 50;
static constexpr int PLAY_H  = 34;

// Meds button — same size as play, stacked just above it with a small gap; only shown/hit-tested while sick
static constexpr int MEDS_X  = PLAY_X;
static constexpr int MEDS_Y  = PLAY_Y - PLAY_H - 8;
static constexpr int MEDS_W  = PLAY_W;
static constexpr int MEDS_H  = PLAY_H;

// Water button — same size as treat, stacked just above it with a small gap; always shown, since
// water (unlike meds) can be given any time, not only while the cat is thirsty
static constexpr int WATER_X  = TREAT_X;
static constexpr int WATER_Y  = TREAT_Y - TREAT_H - 8;
static constexpr int WATER_W  = TREAT_W;
static constexpr int WATER_H  = TREAT_H;

// Gamification: point award per care action, only when it actually addressed a real need
static constexpr uint32_t POINTS_TREAT = 5;
static constexpr uint32_t POINTS_PLAY  = 3;
static constexpr uint32_t POINTS_WATER = 3;
static constexpr uint32_t POINTS_MEDS  = 8;

// Gamification: store item costs
static constexpr uint32_t STORE_COST_TEDDY    = 60;
static constexpr uint32_t STORE_COST_BUNNY    = 60;
static constexpr uint32_t STORE_COST_SQUIRREL = 150;
static constexpr uint32_t STORE_COST_PENGUIN  = 150;
static constexpr uint32_t STORE_COST_BLANKET  = 40;  // per blanket color
static constexpr uint32_t STORE_COST_ROOM_THEME = 40;  // per flat-color room theme, matches blanket pricing
static constexpr uint32_t STORE_COST_STARRY_NIGHT = 200;  // premium: has real art (moon + stars), not just a flat fill

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
static constexpr uint16_t C_YARN   = 0xF811;  // play button yarn ball (magenta/berry)
static constexpr uint16_t C_ZZZ    = 0x7BFF;  // boredom "Zz" overlay (dim blue-gray)
static constexpr uint16_t C_SLEEP_DIM = 0x632C;  // dim gray-blue, for the sleep-screen clock (~40% brightness)
static constexpr uint16_t C_SICK   = 0x9E66;  // queasy cheek blush (sickly green)
static constexpr uint16_t C_MEDS   = 0xF800;  // meds button cross (red)
static constexpr uint16_t C_WATER  = 0x04FF;  // water button droplet (blue)
static constexpr uint16_t C_NEW_ITEM = 0x07FF;  // points balance flash color, hinting at an unseen store item (cyan)
static constexpr uint16_t C_BEAR     = 0x9A46;  // teddy bear peeking out beside the head (brown)
static constexpr uint16_t C_BUNNY    = 0xC618;  // grey bunny peeking out beside the head (grey)
static constexpr uint16_t C_SQUIRREL = 0xCA25;  // red squirrel peeking out beside the head (rust orange)
static constexpr uint16_t C_PENGUIN      = 0x0000;  // penguin peeking out beside the head (black)
static constexpr uint16_t C_PENGUIN_BEAK = 0xFD20;  // penguin beak/feet (orange)

// Blanket color catalog — each color is purchased separately in the store and can be
// equipped independently in the dressing room. `id` is the stable identifier used in
// store/dressing-room form posts and persisted config; never reorder/reuse indices for
// a different color, since ownership is stored as a bitmask keyed by array index.
struct BlanketColor {
    const char* id;
    const char* label;
    uint16_t base;
    uint16_t trim;
    const char* webColor;  // CSS hex approximation of `base`, for coloring its label in the config UI
};
static constexpr BlanketColor BLANKET_COLORS[] = {
    {"dusty_blue", "Dusty Blue", 0x4BB6, 0xD69A, "#6b8cae"},  // cozy dusty-blue blanket, warm cream fold trim
    {"cream",      "Cream",      0xFFFA, 0xCD51, "#e8d9b5"},  // soft cream blanket, tan fold trim
    {"blush_pink", "Blush Pink", 0xF619, 0xDB92, "#e6a8bc"},  // blush pink blanket, deeper rose fold trim
    {"lavender",   "Lavender",   0xB3FB, 0xE69E, "#b57edc"},  // lavender blanket, pale lilac fold trim
    {"lemon_yellow", "Lemon Yellow", 0xF6CB, 0xD502, "#F7D959"},  // Bambu PLA Lemon Yellow blanket, mustard-gold fold trim
    {"apple_green",  "Apple Green",  0xC711, 0x7D2A, "#C2E189"},  // Bambu PLA Apple Green blanket, deeper leaf-green fold trim
};
static constexpr int BLANKET_COLOR_COUNT = sizeof(BLANKET_COLORS) / sizeof(BLANKET_COLORS[0]);

// Forward declarations: each stuffy's sleep-scene art, defined further below alongside
// drawSleepingCat(). Declared here so the STUFFIES[] catalog can reference them directly —
// picking the equipped stuffy's shape is then a plain function-pointer call, no per-stuffy
// branching needed at the call site.
static void drawTeddyPeeking(int cx, int cy, uint16_t accentColor);
static void drawTeddyFull(int cx, int cy, uint16_t accentColor);
static void drawBunnyPeeking(int cx, int cy, uint16_t accentColor);
static void drawBunnyFull(int cx, int cy, uint16_t accentColor);
static void drawSquirrelPeeking(int cx, int cy, uint16_t accentColor);
static void drawSquirrelFull(int cx, int cy, uint16_t accentColor);
static void drawPenguinPeeking(int cx, int cy, uint16_t accentColor);
static void drawPenguinFull(int cx, int cy, uint16_t accentColor);

// Stuffy catalog — same purchase/equip model as blanket colors, so more stuffies can be
// added later without changing the store/dressing-room plumbing. `id` is the stable
// identifier used in store/dressing-room form requests; ConfigManager persists ownership
// as an `ownedStuffies` bitmask and the equipped selection as a numeric `equippedStuffy`
// index (both keyed by catalog position), not the string id. `drawPeeking`/`drawFull` are
// the sleep-scene art for this stuffy — only one stuffy is ever equipped at a time (see
// equippedStuffyIndex()), so equipping the bunny replaces the teddy bear at night rather
// than showing both.
struct Stuffy {
    const char* id;
    const char* label;
    uint32_t cost;
    void (*drawPeeking)(int cx, int cy, uint16_t accentColor);
    void (*drawFull)(int cx, int cy, uint16_t accentColor);
};
static constexpr Stuffy STUFFIES[] = {
    {"teddy",    "Teddy Bear",   STORE_COST_TEDDY,    drawTeddyPeeking,    drawTeddyFull},
    {"bunny",    "Grey Bunny",   STORE_COST_BUNNY,    drawBunnyPeeking,    drawBunnyFull},
    {"squirrel", "Red Squirrel", STORE_COST_SQUIRREL, drawSquirrelPeeking, drawSquirrelFull},
    {"penguin",  "Penguin",      STORE_COST_PENGUIN,  drawPenguinPeeking,  drawPenguinFull},
};
static constexpr int STUFFY_COUNT = sizeof(STUFFIES) / sizeof(STUFFIES[0]);

// Forward declaration: shared flat-color backdrop for themes with no dedicated art of their
// own, defined further below alongside drawSleepingCat(). Declared here so the ROOM_THEMES[]
// catalog can reference it directly, same as the stuffy draw-function forward declarations
// above. Reads its color from the equipped theme's own `bgColor` field — a future theme with
// real art (starry night, moon, fireplace) would instead point at its own dedicated function.
static void drawFlatThemeBackground(int x, int y, int w, int h);

// Forward declaration: Starry Night's dedicated backdrop (moon + fixed star field),
// defined further below alongside drawFlatThemeBackground().
static void drawStarryNightBackground(int x, int y, int w, int h);

// Room theme catalog — same purchase/equip model as blanket colors and stuffies. `id` is
// the stable identifier used in store/dressing-room form requests; ConfigManager persists
// ownership as an `ownedRoomThemes` bitmask and the equipped selection as a numeric
// `equippedRoomTheme` index (both keyed by catalog position), never reorder/reuse indices.
// `drawBackground` repaints exactly the rect it's given — the shared "erase to whatever's
// behind this" primitive used throughout the animal zone (see zoneFillRect()), not just the
// cat's own box. `bgColor` is a representative solid color for cheap operations like text
// glyph background erasure, where invoking a full rect-painter call isn't practical.
struct RoomTheme {
    const char* id;
    const char* label;
    uint32_t cost;
    uint16_t bgColor;                                   // representative solid color, for cheap text-background erasure
    void (*drawBackground)(int x, int y, int w, int h);  // repaints exactly this rect of the zone's backdrop
};
static constexpr RoomTheme ROOM_THEMES[] = {
    // Flat-color placeholders (DIY-38), picked to pair with the blanket palette. Moon /
    // fireplace themes with real art still land in a follow-up card.
    {"midnight", "Midnight", STORE_COST_ROOM_THEME, 0x18CE, drawFlatThemeBackground},  // deep navy — pairs with Dusty Blue
    {"twilight", "Twilight", STORE_COST_ROOM_THEME, 0x28C8, drawFlatThemeBackground},  // deep plum — pairs with Lavender
    {"forest",   "Forest",   STORE_COST_ROOM_THEME, 0x1143, drawFlatThemeBackground},  // deep green — pairs with Apple Green
    {"rosewood", "Rosewood", STORE_COST_ROOM_THEME, 0x30A4, drawFlatThemeBackground},  // deep wine — pairs with Blush Pink
    {"amber",    "Amber",    STORE_COST_ROOM_THEME, 0x28E2, drawFlatThemeBackground},  // warm deep brown — pairs with Cream & Lemon Yellow
    {"starry_night", "Starry Night", STORE_COST_STARRY_NIGHT, TFT_BLACK, drawStarryNightBackground},  // moon + stars on black
};
static constexpr int ROOM_THEME_COUNT = sizeof(ROOM_THEMES) / sizeof(ROOM_THEMES[0]);

// Sentinel stored in equippedBlanketColor/equippedStuffy/equippedRoomTheme to mean
// "explicitly unequipped by the user in the dressing room", as opposed to 0 which means "no
// selection made yet, fall back to the lowest-index owned item" (see
// equippedBlanketIndex()/equippedStuffyIndex()/equippedRoomThemeIndex()). Never a valid
// catalog index since all catalogs stay well under 255 entries.
static constexpr uint8_t EQUIP_NONE = 0xFF;

// Quick-pick durations
static constexpr uint32_t    PICK_SEC[] = {60, 300, 600, 1800};
static constexpr const char* PICK_LBL[] = {"+1", "+5", "+10", "+30"};
static constexpr int PICK_N = 4;

// ── Hardware ──────────────────────────────────────────────────────────────────
TFT_eSPI tft;
#if defined(BOARD_FREENOVE_S3)
Ft6336uTouch touchDriver(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_IRQ, 240, 320);
#else
Xpt2046Touch touchDriver(TOUCH_CS, TOUCH_IRQ, TOUCH_CLK, TOUCH_MISO, TOUCH_MOSI,
                         240, 320, TX_MIN, TX_MAX, TY_MIN, TY_MAX);
#endif

// ── Services ──────────────────────────────────────────────────────────────────
WiFiUDP       ntpUDP;
NTPClient     ntpClient(ntpUDP, NTP_SERVER);
ConfigManager configMgr;
WeatherClient weather;
OtaUpdateClient otaClient;
TimerWidget   timerWidget;
StopwatchWidget stopwatchWidget;
WiFiManager   wm;

enum class TimerMode { Countdown, Stopwatch };
TimerMode timerMode = TimerMode::Countdown;

// ── Animation ─────────────────────────────────────────────────────────────────
enum class CatMood    { Idle, Happy, Celebrate };
enum class CatStatus  { Content, Peckish, Hungry };   // Content=0-50%, Peckish=50-100%, Hungry=100%+
enum class CatBoredom { Entertained, Bored, VeryBored }; // mirrors CatStatus tiers
enum class CatHealth  { Healthy, Sick };              // random event, cleared by meds
enum class CatThirst  { Hydrated, Thirsty };          // random event, cleared by water

struct {
    CatMood    mood              = CatMood::Idle;
    CatStatus  status            = CatStatus::Content;
    CatBoredom boredom           = CatBoredom::Entertained;
    CatHealth  health            = CatHealth::Healthy;
    CatThirst  thirst            = CatThirst::Hydrated;
    unsigned long since         = 0;
    bool eyeOpen                = true;
    unsigned long lastBlink     = 0;
    uint8_t frame               = 0;
    unsigned long lastFrame     = 0;
    unsigned long lastRumble    = 0;  // for tummy rumble animation
    bool rumbling               = false;
    unsigned long lastZzz       = 0;  // for boredom "Zz" toggle animation
    bool napping                = false;
    unsigned long lastSickCheck = 0;  // for the periodic sick-eligibility roll
    unsigned long lastThirstCheck = 0;  // for the periodic thirst-eligibility roll
} cat;

// ── App state ─────────────────────────────────────────────────────────────────
bool timerDonePrev          = false;
unsigned long lastWeatherFetch = 0;
unsigned long lastUpdateCheck  = 0;
bool lastUpdateCheckFailed     = false;  // session-only, shown on /config/update, not persisted
bool lastUpdateCheckSkipped    = false;  // session-only — check was skipped (a "dev" build), not persisted
unsigned long lastTouchMs      = 0;
unsigned long showIpUntilMs    = 0;
bool asleep                    = false;  // set each loop before handleTouch(); read by handleTouch()
unsigned long peekUntilMs      = 0;      // 0 = not peeking; mirrors the showIpUntilMs idiom
bool peekingAsleep             = false;  // true while touch-peeking during the sleep window — freezes cat state, draws the sleeping scene
bool sleepScreenActive         = false;  // true once the black sleep screen has been painted this session
unsigned long forceSickDeadlineMs = 0;   // test-only: armed via /config, 0 = not armed, not persisted
unsigned long forceThirstDeadlineMs = 0; // test-only: armed via /config, 0 = not armed, not persisted

struct { bool header, animal, picker, timerRow, eyesOnly, timerTick, headerTick, hungerLines, zzzFx, animalBg; } dirty = {true, true, true, true, false, false, false, false, false, true};
bool pointsFlashOn = false;  // toggled every ~500ms while hasNewStoreItems(); read by drawPoints()


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

// Forward declaration: paints exactly the given rect of the animal zone using whichever room
// theme is equipped (or plain black if none), defined below alongside
// equippedRoomThemeIndex(). Declared here so drawCat() can call it directly, since drawCat()
// itself is defined before the resolver it depends on.
static void zoneFillRect(int x, int y, int w, int h);

static void drawCat(int cx, int cy, CatMood mood, CatStatus status, CatBoredom boredom, CatHealth health, CatThirst thirst, bool eyeOpen) {
    bool happy   = (mood == CatMood::Happy || mood == CatMood::Celebrate);
    bool queasy  = (health == CatHealth::Sick) && !happy;
    bool thirsty = (thirst == CatThirst::Thirsty);
    bool sad     = (status == CatStatus::Hungry || boredom == CatBoredom::VeryBored || thirsty) && !happy && !queasy;

    zoneFillRect(cx - 50, cy - 88, 100, 146);

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
    } else if (queasy) {
        // Wavy zigzag — distinct from the frown, reads as "off"
        tft.drawLine(cx - 10, cy - 6,  cx - 5, cy - 3, C_DARK);
        tft.drawLine(cx -  5, cy - 3,  cx,     cy - 7, C_DARK);
        tft.drawLine(cx,      cy - 7,  cx + 5, cy - 3, C_DARK);
        tft.drawLine(cx +  5, cy - 3,  cx + 10,cy - 6, C_DARK);
    } else if (sad) {
        // Frown
        tft.drawLine(cx - 10, cy - 4,  cx - 3, cy - 9, C_DARK);
        tft.drawLine(cx -  3, cy - 9,  cx,     cy - 7, C_DARK);
        tft.drawLine(cx,      cy - 7,  cx + 3, cy - 9, C_DARK);
        tft.drawLine(cx +  3, cy - 9,  cx + 10,cy - 4, C_DARK);
    } else {
        tft.drawLine(cx - 6, cy - 9, cx,     cy - 6, C_DARK);
        tft.drawLine(cx,     cy - 6, cx + 6, cy - 9, C_DARK);
    }

    // Queasy cheek blush
    if (queasy) {
        tft.fillCircle(cx - 22, cy - 24, 5, C_SICK);
        tft.fillCircle(cx + 22, cy - 24, 5, C_SICK);
    }

    // Thirsty signal — a single water drop at the temple, dripping-sweat style
    if (thirsty) {
        int tdx = cx + 30, tdy = cy - 48;
        tft.fillTriangle(tdx, tdy - 5, tdx - 4, tdy + 3, tdx + 4, tdy + 3, C_WATER);
        tft.fillCircle(tdx, tdy + 4, 4, C_WATER);
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

// Calm, static "asleep" scene for the sleep-window peek: reuses drawCat() with a
// content/closed-eyed state (no status decorations), then layers on owned store
// items — a blanket over the body/paws/tail up to the neck, and/or a teddy bear.
// Stays within drawAnimal()'s CAT_CX±50 clear-rect bounds.

// Resolves which blanket color to display: the equipped color if it's actually owned,
// otherwise the lowest-index owned color (covers stale/out-of-range equipped state), or
// -1 if no blanket color is owned at all.
static int equippedBlanketIndex() {
    uint8_t owned = configMgr.config().ownedBlanketColors;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedBlanketColor;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < BLANKET_COLOR_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < BLANKET_COLOR_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Same resolution logic as equippedBlanketIndex(), for the stuffy catalog.
static int equippedStuffyIndex() {
    uint8_t owned = configMgr.config().ownedStuffies;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedStuffy;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < STUFFY_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < STUFFY_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Same resolution logic as equippedBlanketIndex(), for the room theme catalog.
static int equippedRoomThemeIndex() {
    uint8_t owned = configMgr.config().ownedRoomThemes;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedRoomTheme;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < ROOM_THEME_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < ROOM_THEME_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Returns the equipped room theme's representative solid color, or TFT_BLACK if none
// equipped — used for cheap operations like text glyph background erasure, where invoking
// a full themed repaint isn't practical (TFT_eSPI's font renderer only accepts a solid
// color, not a callback).
static uint16_t zoneBgColor() {
    int idx = equippedRoomThemeIndex();
    return (idx >= 0) ? ROOM_THEMES[idx].bgColor : TFT_BLACK;
}

// Repaints exactly the given rect of the animal zone using the equipped room theme's
// backdrop, or plain black if none equipped — the shared "erase to whatever's behind this"
// primitive used everywhere in drawAnimal() and its helpers instead of a hardcoded black
// fill. Keeping these erases narrow and per-element, rather than repainting the full
// 240×175 zone on every redraw, is deliberate — DIY-8 found a full-zone fill caused visible
// flicker during the ~4Hz celebration animation. dirty.animalBg (see drawAnimal()) is the
// one exception, repainting the full zone exactly once whenever the backdrop itself changes.
//
// Clips to the zone's vertical bounds first — some callers' rects (e.g. the cat's own
// clear-rect, sized with headroom for its ear tips and the celebration bounce) extend a few
// pixels above ANIMAL_Y, which is harmless against a black background but would otherwise
// paint the theme color over the header separator line.
static void zoneFillRect(int x, int y, int w, int h) {
    int y2 = y + h;
    if (y < ANIMAL_Y) y = ANIMAL_Y;
    if (y2 > ANIMAL_Y + ANIMAL_H) y2 = ANIMAL_Y + ANIMAL_H;
    h = y2 - y;
    if (h <= 0) return;

    int idx = equippedRoomThemeIndex();
    if (idx >= 0) ROOM_THEMES[idx].drawBackground(x, y, w, h);
    else tft.fillRect(x, y, w, h, TFT_BLACK);
}

// True whenever the store catalog has grown (a new stuffy, blanket color, or room theme
// shipped in a firmware update) since the last time the user opened the store page —
// cleared by handleConfigStoreGet(), which stamps the current catalog sizes as "seen".
static bool hasNewStoreItems() {
    return STUFFY_COUNT > configMgr.config().seenStuffyCount ||
           BLANKET_COLOR_COUNT > configMgr.config().seenBlanketColorCount ||
           ROOM_THEME_COUNT > configMgr.config().seenRoomThemeCount;
}

// Shared ear/head/snout/eyes/nose art reused by both teddy variants below.
static void drawTeddyHead(int bx, int by, uint16_t snoutColor) {
    tft.fillCircle(bx - 6, by - 9, 4, C_BEAR);   // left ear
    tft.fillCircle(bx + 5, by - 9, 4, C_BEAR);   // right ear
    tft.fillCircle(bx,     by,     8, C_BEAR);   // head
    tft.fillCircle(bx,     by + 3, 3, snoutColor);  // snout
    tft.fillCircle(bx - 3, by - 2, 1, C_DARK);   // left eye
    tft.fillCircle(bx + 3, by - 2, 1, C_DARK);   // right eye
    tft.fillCircle(bx,     by + 1, 1, C_DARK);   // nose
}

// Teddy bear peeking out beside the head, tucked into the blanket's top edge —
// only reads correctly when the blanket is also owned to tuck behind.
static void drawTeddyPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    drawTeddyHead(bx, by, accentColor);
}

// Full-body teddy bear sitting beside the cat — used when the teddy is owned without
// the blanket, since there's no blanket edge to tuck a lone head behind.
static void drawTeddyFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawTeddyHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 9, by + 7, 18, 26, 8, C_BEAR);  // body
    tft.fillCircle(bx, by + 18, 5, accentColor);           // belly patch
    tft.fillCircle(bx - 6, by + 33, 4, C_BEAR);            // left foot
    tft.fillCircle(bx + 6, by + 33, 4, C_BEAR);            // right foot
}

// Shared ear/head/snout/eyes/nose art reused by both bunny variants below — same layout as
// drawTeddyHead() but with tall narrow ears instead of round ones.
static void drawBunnyHead(int bx, int by, uint16_t snoutColor) {
    tft.fillEllipse(bx - 5, by - 14, 3, 8, C_BUNNY);  // left ear
    tft.fillEllipse(bx + 5, by - 14, 3, 8, C_BUNNY);  // right ear
    tft.fillCircle(bx,     by,     8, C_BUNNY);   // head
    tft.fillCircle(bx,     by + 3, 3, snoutColor);  // snout
    tft.fillCircle(bx - 3, by - 2, 1, C_DARK);   // left eye
    tft.fillCircle(bx + 3, by - 2, 1, C_DARK);   // right eye
    tft.fillCircle(bx,     by + 1, 1, C_DARK);   // nose
}

// Grey bunny peeking out beside the head, tucked into the blanket's top edge —
// only reads correctly when the blanket is also owned to tuck behind.
static void drawBunnyPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    drawBunnyHead(bx, by, accentColor);
}

// Full-body grey bunny sitting beside the cat — used when the bunny is owned without
// the blanket, since there's no blanket edge to tuck a lone head behind.
static void drawBunnyFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawBunnyHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_BUNNY);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);            // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_BUNNY);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_BUNNY);            // right foot
    tft.fillCircle(bx, by + 33, 3, TFT_WHITE);              // fluffy tail
}

// Shared ear/head/snout/eyes/nose art reused by both squirrel variants below — same layout
// as drawTeddyHead() but with smaller rounded ears, sized to leave room for the bushy tail
// arcing up behind.
static void drawSquirrelHead(int bx, int by, uint16_t snoutColor) {
    tft.fillCircle(bx - 5, by - 8, 3, C_SQUIRREL);  // left ear
    tft.fillCircle(bx + 5, by - 8, 3, C_SQUIRREL);  // right ear
    tft.fillCircle(bx,     by,     8, C_SQUIRREL);  // head
    tft.fillCircle(bx,     by + 3, 3, snoutColor);  // snout
    tft.fillCircle(bx - 3, by - 2, 1, C_DARK);   // left eye
    tft.fillCircle(bx + 3, by - 2, 1, C_DARK);   // right eye
    tft.fillCircle(bx,     by + 1, 1, C_DARK);   // nose
}

// Red squirrel peeking out beside the head, tucked into the blanket's top edge — only the
// tip and a sliver of orange show above the blanket's edge, the rest of the tail tucked
// behind it, matching the full-body pose's shoulder-poke silhouette.
static void drawSquirrelPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    tft.fillCircle(bx + 12, by + 2, 5, C_SQUIRREL);   // bit of orange poking over the shoulder
    tft.fillCircle(bx + 13, by - 4, 4, accentColor);  // white tip
    drawSquirrelHead(bx, by, accentColor);
}

// Full-body red squirrel sitting beside the cat — used when the squirrel is owned without
// the blanket, since there's no blanket edge to tuck a lone head behind. The bushy tail
// stays tucked mostly behind the body, with just its tip and a bit of orange curling up
// over the shoulder — the premium stuffy's distinguishing silhouette.
static void drawSquirrelFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    tft.fillCircle(bx + 10, by + 12, 6, C_SQUIRREL);   // tail base, tucked behind the body
    tft.fillCircle(bx + 13, by + 4,  5, C_SQUIRREL);   // a bit of orange curling up
    tft.fillCircle(bx + 14, by - 2,  4, accentColor);  // white tip poking over the shoulder
    drawSquirrelHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_SQUIRREL);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);               // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_SQUIRREL);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_SQUIRREL);            // right foot
}

// Shared head art reused by both penguin variants below — unlike the other stuffies'
// snout-and-fur-color heads, the penguin's face patch is always the blanket accent color
// (its distinguishing white-face silhouette) while eyes sit on top as white-with-pupil
// dots, since a plain dark dot wouldn't show against the penguin's black head.
static void drawPenguinHead(int bx, int by, uint16_t faceColor) {
    tft.fillCircle(bx,     by,     10, C_PENGUIN);      // head
    tft.fillCircle(bx,     by + 4, 5,  faceColor);      // white face patch
    tft.fillCircle(bx - 4, by - 2, 2,  TFT_WHITE);      // left eye white
    tft.fillCircle(bx + 4, by - 2, 2,  TFT_WHITE);      // right eye white
    tft.fillCircle(bx - 4, by - 2, 1,  C_DARK);         // left pupil
    tft.fillCircle(bx + 4, by - 2, 1,  C_DARK);         // right pupil
    tft.fillTriangle(bx - 3, by + 3, bx + 3, by + 3, bx, by + 8, C_PENGUIN_BEAK);  // beak
}

// Penguin peeking out beside the head, tucked into the blanket's top edge —
// only reads correctly when the blanket is also owned to tuck behind.
static void drawPenguinPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    drawPenguinHead(bx, by, accentColor);
}

// Full-body penguin sitting beside the cat — used when the penguin is owned without the
// blanket, since there's no blanket edge to tuck a lone head behind. Flippers and
// orange feet stay fixed to C_PENGUIN/C_PENGUIN_BEAK rather than the accent color, so the
// premium stuffy's silhouette reads the same regardless of which blanket is equipped.
static void drawPenguinFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawPenguinHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 9, by + 7, 18, 26, 9, C_PENGUIN);      // body
    tft.fillRoundRect(bx - 5, by + 10, 10, 20, 5, accentColor);   // white belly patch
    tft.fillTriangle(bx - 9, by + 15, bx - 11, by + 23, bx - 6, by + 24, C_PENGUIN);  // left flipper
    tft.fillTriangle(bx + 9, by + 15, bx + 11, by + 23, bx + 6, by + 24, C_PENGUIN);  // right flipper
    tft.fillTriangle(bx - 6, by + 33, bx - 9, by + 37, bx - 2, by + 37, C_PENGUIN_BEAK);  // left foot
    tft.fillTriangle(bx + 6, by + 33, bx + 9, by + 37, bx + 2, by + 37, C_PENGUIN_BEAK);  // right foot
}

// Deeply-closed, sleepy eyes for the sleep-window peek — thinner and gently curled at
// the outer corners than drawEyes()'s blink-style dash, so the cat reads as actually
// asleep rather than mid-blink.
static void drawSleepyEyes(int cx, int cy) {
    tft.fillRect(cx - 28, cy - 50, 56, 26, C_CAT);  // restore head colour before drawing eyes
    tft.fillRoundRect(cx - 24, cy - 39, 20, 3, 1, C_DARK);
    tft.fillRoundRect(cx +  4, cy - 39, 20, 3, 1, C_DARK);
    tft.drawLine(cx - 24, cy - 39, cx - 27, cy - 42, C_DARK);  // outer curl, left eye
    tft.drawLine(cx + 24, cy - 39, cx + 27, cy - 42, C_DARK);  // outer curl, right eye
}

// Shared flat-color backdrop for placeholder themes with no dedicated art yet (DIY-38).
// Reads the equipped theme's own bgColor rather than hardcoding a color, so every
// flat-color entry in ROOM_THEMES[] can point at this one function.
static void drawFlatThemeBackground(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, zoneBgColor());
}

// Fixed star field for the Starry Night theme — pre-generated positions rather than
// re-randomized per call, so repeated partial redraws (see zoneFillRect()) always agree.
// `r` is the fill radius; 0 means a single pixel.
struct Star { int16_t x, y; uint8_t r; };
static constexpr Star STARRY_NIGHT_STARS[] = {
    {60, 55, 0}, {85, 90, 1}, {115, 65, 0}, {145, 100, 0}, {170, 60, 1},
    {195, 85, 0}, {215, 130, 1}, {60, 145, 0}, {100, 160, 0}, {135, 178, 1},
    {175, 155, 0}, {205, 178, 0}, {40, 115, 1}, {225, 55, 0}, {150, 45, 0},
    {90, 45, 1}, {220, 105, 0}, {15, 165, 0},
};
static constexpr int STARRY_NIGHT_STAR_COUNT = sizeof(STARRY_NIGHT_STARS) / sizeof(STARRY_NIGHT_STARS[0]);

// "Starry Night" room theme: a small moon near the top-left of the zone, plus a fixed
// scatter of stars, on a plain black backdrop. tft.setViewport(..., false) clips every draw
// call below to exactly the requested (x,y,w,h) rect while keeping absolute screen
// coordinates — this lets the theme draw its whole scene unconditionally on every call,
// including the frequent small erasures elsewhere in drawAnimal() (a sparkle box, the
// points corner, etc.), and have only the relevant slice actually reach the display.
static void drawStarryNightBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, TFT_BLACK);

    // Moon — top-left of the zone, with a couple of small craters for texture
    tft.fillCircle(24, ANIMAL_Y + 22, 10, 0xFFDB);   // pale warm-white moon
    tft.fillCircle(21, ANIMAL_Y + 19, 2, 0xEF7A);    // crater
    tft.fillCircle(28, ANIMAL_Y + 26, 1, 0xEF7A);    // crater

    for (int i = 0; i < STARRY_NIGHT_STAR_COUNT; i++) {
        const Star& s = STARRY_NIGHT_STARS[i];
        if (s.r == 0) tft.drawPixel(s.x, s.y, TFT_WHITE);
        else          tft.fillCircle(s.x, s.y, s.r, TFT_WHITE);
    }

    tft.resetViewport();
}

static void drawSleepingCat(int cx, int cy) {
    drawCat(cx, cy, CatMood::Idle, CatStatus::Content, CatBoredom::Entertained,
            CatHealth::Healthy, CatThirst::Hydrated, /*eyeOpen=*/false);
    drawSleepyEyes(cx, cy);

    int  blanketIdx = equippedBlanketIndex();
    bool hasBlanket = blanketIdx >= 0;
    int  stuffyIdx  = equippedStuffyIndex();
    // The stuffy's snout/belly accent reuses the blanket's trim color when a blanket is
    // equipped (so they read as a matching set), falling back to the default trim
    // color of the first catalog entry when no blanket is owned.
    uint16_t accentColor = BLANKET_COLORS[hasBlanket ? blanketIdx : 0].trim;

    if (hasBlanket) {
        const BlanketColor& color = BLANKET_COLORS[blanketIdx];
        // Blanket — covers body/paws/tail, starts right at the neckline
        tft.fillRoundRect(cx - 40, cy, 80, 60, 12, color.base);
        tft.fillRect(cx - 40, cy + 4, 80, 6, color.trim);  // folded-edge trim
    }

    if (stuffyIdx >= 0) {
        // Only one stuffy is ever equipped at a time, so switching the dressing-room
        // selection (e.g. teddy → bunny) simply swaps which catalog entry's art draws here.
        const Stuffy& stuffy = STUFFIES[stuffyIdx];
        if (hasBlanket) stuffy.drawPeeking(cx, cy, accentColor);
        else             stuffy.drawFull(cx, cy, accentColor);
    }
}

static void drawSparkles(int cx, int cy, uint8_t frame) {
    static const int8_t  dx[]  = {  0, 40, 55, 40,  0,-40,-55,-40};
    static const int8_t  dy[]  = {-58,-42,-10, 22, 38, 22,-10,-42};
    static const uint16_t clr[] = {TFT_YELLOW, TFT_CYAN, 0xFD20, TFT_GREEN,
                                    TFT_YELLOW, TFT_CYAN, 0xFD20, TFT_GREEN};
    for (int i = 0; i < 8; i++) {
        int px = cx + dx[i], py = cy + dy[i];
        if (py < ANIMAL_Y + 2 || py > ANIMAL_Y + ANIMAL_H - 6) continue;
        zoneFillRect(px - 4, py - 4, 9, 9);
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
            zoneFillRect(px - 4, py - 4, 9, 9);
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

static void drawBoredomZzz(int cx, int cy, bool show) {
    // "Zz" cloud, entirely outside the head/ears — one to the right, two cascading
    // up-and-out to the left.
    static const int8_t dx[] = { 70, -70, -70 };
    static const int8_t dy[] = {-40, -40, -60 };
    for (int i = 0; i < 3; i++) {
        int x = cx + dx[i], y = cy + dy[i];
        zoneFillRect(x - 2, y - 10, 22, 14);
        if (show) {
            tft.setTextColor(C_ZZZ, zoneBgColor());
            tft.drawString("Zz", x, y - 8, 1);
        }
    }
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

static void drawPlayBtn() {
    tft.fillRoundRect(PLAY_X, PLAY_Y, PLAY_W, PLAY_H, 6, C_BTN);

    // Yarn ball: filled circle body, crossing "wound yarn" lines, loose end
    int ycx = PLAY_X + 20, ycy = PLAY_Y + 17;
    tft.fillCircle(ycx, ycy, 10, C_YARN);
    tft.drawLine(ycx - 8, ycy - 5, ycx + 8, ycy + 5, C_BTN);
    tft.drawLine(ycx - 8, ycy + 5, ycx + 8, ycy - 5, C_BTN);
    tft.drawLine(ycx - 6, ycy,     ycx + 6, ycy,     C_BTN);
    tft.drawLine(ycx + 8, ycy + 6, ycx + 14, ycy + 10, C_YARN);
}

static void drawMedsBtn() {
    tft.fillRoundRect(MEDS_X, MEDS_Y, MEDS_W, MEDS_H, 6, C_BTN);

    // Pill bottle: rounded rect body + cap, red cross on the front
    int bx = MEDS_X + MEDS_W / 2 - 12, by = MEDS_Y + 6;
    tft.fillRoundRect(bx, by, 24, 22, 4, TFT_WHITE);
    tft.fillRect(bx + 4, by - 4, 16, 5, C_DIM);
    tft.fillRect(bx + 10, by + 6, 4, 10, C_MEDS);
    tft.fillRect(bx + 7, by + 9, 10, 4, C_MEDS);
}

static void drawWaterBtn() {
    tft.fillRoundRect(WATER_X, WATER_Y, WATER_W, WATER_H, 6, C_BTN);

    // Water droplet: triangle tip + rounded body
    int wcx = WATER_X + WATER_W / 2, wcy = WATER_Y + 8;
    tft.fillTriangle(wcx, wcy - 6, wcx - 8, wcy + 6, wcx + 8, wcy + 6, C_WATER);
    tft.fillCircle(wcx, wcy + 8, 8, C_WATER);
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

// Points balance — top-right of the cat's head, just under the header's weather text.
// Own clear rect since this sits to the right of the cat's bounding box, outside
// drawAnimal()'s clear rect — starts past x=152 so it doesn't clip the cat's right ear
// (drawCat()'s ear triangle reaches up to roughly x=152, y=65). Flashes between yellow and
// cyan while hasNewStoreItems() is true, as a hint to check the store for something new.
static void drawPoints() {
    char ptsBuf[16];
    snprintf(ptsBuf, sizeof(ptsBuf), "%lu pts", (unsigned long)configMgr.config().points);
    zoneFillRect(155, ANIMAL_Y, 85, 20);
    uint16_t color = (hasNewStoreItems() && pointsFlashOn) ? C_NEW_ITEM : TFT_YELLOW;
    tft.setTextColor(color, zoneBgColor());
    int ptsWidth = tft.textWidth(ptsBuf, 2);
    tft.drawString(ptsBuf, 234 - ptsWidth, ANIMAL_Y + 4, 2);
}

static void drawAnimal() {
    if (dirty.animalBg) {
        zoneFillRect(0, ANIMAL_Y, 240, ANIMAL_H);
        dirty.animalBg = false;
    }
    // Clear only sparkle positions and the cat's bounding box (including ±3px bounce).
    // Avoids wiping the full 240×175 zone on every redraw — DIY-8 found that caused visible
    // flicker during the ~4Hz celebration animation. dirty.animalBg above is the one
    // deliberate exception, firing only when the backdrop itself actually changes.
    clearSparkles(CAT_CX, CAT_CY);
    zoneFillRect(CAT_CX - 50, CAT_CY - 91, 100, 152);

    if (peekingAsleep) {
        drawSleepingCat(CAT_CX, CAT_CY);
    } else {
        int dy = (cat.mood == CatMood::Celebrate) ? ((cat.frame % 2 == 0) ? -3 : 3) : 0;
        drawCat(CAT_CX, CAT_CY + dy, cat.mood, cat.status, cat.boredom, cat.health, cat.thirst, cat.eyeOpen);

        if (cat.mood == CatMood::Happy || cat.mood == CatMood::Celebrate) {
            drawSparkles(CAT_CX, CAT_CY, cat.frame);
        }

        // Hunger lines on tummy (Peckish or Hungry, only painted when rumbling)
        if (cat.status == CatStatus::Peckish || cat.status == CatStatus::Hungry) {
            drawHungerLines(CAT_CX, CAT_CY, cat.rumbling);
        }
    }

    // Points balance — drawn after drawCat()/drawSleepingCat() since both repaint their own
    // clear rect internally and would otherwise wipe this out.
    drawPoints();

    // Boredom "Zz" overlay — sits outside the main clear rect, so always redraw/erase
    // here regardless of tier; cat.napping already encodes whether it should show
    // (true whenever Bored/VeryBored, independent of hunger status). Forced off while
    // peeking during the sleep window, since status animations shouldn't show then.
    drawBoredomZzz(CAT_CX, CAT_CY, !peekingAsleep && cat.napping);

    // Name label stays visible even while peeking during the sleep window — only the
    // action buttons are hidden, since the scene should be calm/non-interactive there.
    zoneFillRect(PLAY_X + PLAY_W, ANIMAL_Y + ANIMAL_H - 27,
                 TREAT_X - (PLAY_X + PLAY_W) - 2, 27);
    tft.setTextColor(C_DIM, zoneBgColor());
    tft.drawCentreString(configMgr.config().catName.c_str(), CX, ANIMAL_Y + ANIMAL_H - 22, 2);

    if (!peekingAsleep) {
        drawTreatBtn();
        drawPlayBtn();
        drawWaterBtn();  // always available, stacked above the treat button

        // Meds button — only visible while sick, stacked above the play button
        zoneFillRect(MEDS_X, MEDS_Y, MEDS_W, MEDS_H);
        if (cat.health == CatHealth::Sick) drawMedsBtn();
    }
}

static void drawPicker() {
    tft.fillRect(0, PICKER_Y, 240, PICKER_H, TFT_BLACK);
    tft.drawFastHLine(0, PICKER_Y, 240, C_SEP);

    if (timerMode != TimerMode::Countdown) return;  // no quick-pick durations in stopwatch mode

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
    char buf[8];  // "100:00\0" — widest possible mm:ss once minutes hit 3 digits
    uint16_t col;
    if (timerMode == TimerMode::Countdown) {
        uint32_t rem = timerWidget.remaining();
        col = timerWidget.isFinished() ? TFT_RED
            : timerWidget.isRunning()  ? TFT_GREEN
                                       : TFT_YELLOW;
        snprintf(buf, sizeof(buf), "%02d:%02d", rem / 60, rem % 60);
    } else {
        uint32_t el = stopwatchWidget.elapsedSeconds();
        col = stopwatchWidget.isRunning() ? TFT_GREEN : TFT_YELLOW;
        snprintf(buf, sizeof(buf), "%02d:%02d", el / 60, el % 60);
    }
    tft.setTextColor(col, TFT_BLACK);  // bg colour self-erases previous digits
    tft.drawString(buf, 8, TIMER_Y + 3, 6);
}

// Draws one timer-row control button at the given slot and label/colour.
static void drawTimerBtn(int x, int w, const char* label, uint16_t col) {
    tft.fillRoundRect(x, TIMER_Y + 10, w, 34, 6, C_BTN);
    tft.setTextColor(col, C_BTN);
    int tw = tft.textWidth(label, 4);
    tft.drawString(label, x + (w - tw) / 2, TIMER_Y + 15, 4);
}

// Running state: pause (left) + reset/stop (right) — shared by countdown and stopwatch.
static void drawPauseStopButtons() {
    drawTimerBtn(150, 38, "||", TFT_YELLOW);
    drawTimerBtn(196, 38, "X", 0xFD20);  // orange
}

// Paused state (elapsed/remaining > 0): resume (left) + clear (right) — shared by both modes.
static void drawResumeClearButtons() {
    drawTimerBtn(150, 38, ">", TFT_GREEN);
    drawTimerBtn(196, 38, "0", 0xFD20);
}

static void drawTimerRow() {
    tft.fillRect(0, TIMER_Y, 240, TIMER_H, TFT_BLACK);
    tft.drawFastHLine(0, TIMER_Y, 240, C_SEP);

    if (timerMode == TimerMode::Countdown) {
        uint32_t rem = timerWidget.remaining();

        uint16_t timeCol = TFT_GREEN;
        if (timerWidget.isFinished())      timeCol = TFT_RED;
        else if (!timerWidget.isRunning()) timeCol = TFT_YELLOW;

        if (rem > 0 || timerWidget.isFinished()) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%02d:%02d", rem / 60, rem % 60);
            tft.setTextColor(timeCol, TFT_BLACK);
            tft.drawString(buf, 8, TIMER_Y + 3, 6);  // font 6 = 48px tall, fits in 55px
        } else {
            tft.setTextColor(C_SEP, TFT_BLACK);
            tft.drawString("--:--", 8, TIMER_Y + 3, 6);
        }

        if (rem > 0 || timerWidget.isFinished()) {
            if (timerWidget.isRunning()) {
                drawPauseStopButtons();
            } else if (timerWidget.isFinished()) {
                // Finished: single wide reset button
                drawTimerBtn(150, 84, "0", 0xFD20);
            } else {
                drawResumeClearButtons();
            }
        }
        return;
    }

    // Stopwatch mode
    uint32_t el = stopwatchWidget.elapsedSeconds();
    uint16_t timeCol = stopwatchWidget.isRunning() ? TFT_GREEN : TFT_YELLOW;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", el / 60, el % 60);
    tft.setTextColor(timeCol, TFT_BLACK);
    tft.drawString(buf, 8, TIMER_Y + 3, 6);

    if (stopwatchWidget.isRunning()) {
        drawPauseStopButtons();
    } else if (el == 0) {
        // Idle: single wide start button
        drawTimerBtn(150, 84, ">", TFT_GREEN);
    } else {
        drawResumeClearButtons();
    }
}

// Persists a care action's timestamp as UTC and, if the action addressed a genuine
// need, awards points — sharing one NTP/epoch/save sequence across treat/play/meds/water.
static void persistCareAction(uint32_t& lastEpochField, bool wasNeeded, uint32_t pointsAwarded) {
    time_t epoch = ntpClient.getEpochTime();
    time_t utc   = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utc > 1000000000) {  // sanity: must be a real NTP-synced time (post-2001)
        lastEpochField = (uint32_t)utc;
        if (wasNeeded) {
            configMgr.config().points += pointsAwarded;
            dirty.animal = true;
        }
        configMgr.save();
    }
}

// ── Touch ─────────────────────────────────────────────────────────────────────
static void handleTouch() {
    TouchPoint p;
    if (!touchDriver.read(p)) return;
    unsigned long now = millis();
    if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) return;
    lastTouchMs = now;

    Serial.printf("Touch x=%d y=%d\n", p.x, p.y);

    if (asleep || peekingAsleep) {
        peekUntilMs = now + 7000;  // (re)start/extend the peek on any touch, so fiddling with
                                    // the timer doesn't let the sleep window reclaim the screen
        bool interactiveDuringSleep =
            (p.y < HEADER_Y + HEADER_H && p.x < 120) ||  // header: tap to show IP
            (p.y >= PICKER_Y);                            // picker + timer/stopwatch row
        if (!interactiveDuringSleep) return;  // animal zone: peek only, no action dispatch
    }

    if (p.y < HEADER_Y + HEADER_H && p.x < 120) {
        showIpUntilMs = now + 7000;
        dirty.header  = true;
    } else if (p.y >= TIMER_Y && p.x < 150) {
        // Digits zone toggles between countdown timer and stopwatch mode
        timerMode = (timerMode == TimerMode::Countdown) ? TimerMode::Stopwatch : TimerMode::Countdown;
        timerWidget.reset();
        stopwatchWidget.reset();
        if (cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
        dirty.timerRow = true;
        dirty.picker   = true;
    } else if (p.y >= TIMER_Y) {
        if (timerMode == TimerMode::Countdown) {
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
        } else {
            // Stopwatch mode
            if (stopwatchWidget.isRunning()) {
                if (p.x >= 196) {
                    stopwatchWidget.reset();
                    dirty.timerRow = true;
                } else if (p.x >= 150) {
                    stopwatchWidget.pause();
                    dirty.timerRow = true;
                }
            } else if (stopwatchWidget.elapsedSeconds() == 0) {
                if (p.x >= 150) {
                    stopwatchWidget.start();
                    dirty.timerRow = true;
                }
            } else {
                if (p.x >= 196) {
                    stopwatchWidget.reset();
                    dirty.timerRow = true;
                } else if (p.x >= 150) {
                    stopwatchWidget.resume();
                    dirty.timerRow = true;
                }
            }
        }
    } else if (p.y >= PICKER_Y) {
        if (timerMode == TimerMode::Countdown) {
            int idx = constrain(p.x / (240 / PICK_N), 0, PICK_N - 1);
            bool wasFinished = timerWidget.isFinished();
            timerWidget.addTime(PICK_SEC[idx]);
            if (wasFinished && cat.mood == CatMood::Celebrate) { cat.mood = CatMood::Idle; dirty.animal = true; }
            dirty.timerRow = true;
        }
    } else if (p.y >= ANIMAL_Y) {
        // Treat button hit test
        if (p.x >= TREAT_X && p.x <= TREAT_X + TREAT_W &&
            p.y >= TREAT_Y && p.y <= TREAT_Y + TREAT_H) {
            // Feed the cat
            bool wasNeeded = (cat.status == CatStatus::Peckish || cat.status == CatStatus::Hungry);
            cat.mood   = CatMood::Celebrate;
            cat.status = CatStatus::Content;
            cat.since  = now;
            cat.frame  = 0;
            cat.rumbling = false;
            // Persist last treat time as UTC (subtract offset so timezone changes don't shift hunger)
            persistCareAction(configMgr.config().lastTreatEpoch, wasNeeded, POINTS_TREAT);
            dirty.animal = true;
        } else if (p.x >= PLAY_X && p.x <= PLAY_X + PLAY_W &&
                   p.y >= PLAY_Y && p.y <= PLAY_Y + PLAY_H) {
            // Play with the cat
            bool wasNeeded = (cat.boredom == CatBoredom::Bored || cat.boredom == CatBoredom::VeryBored);
            cat.mood    = CatMood::Celebrate;
            cat.boredom = CatBoredom::Entertained;
            cat.since   = now;
            cat.frame   = 0;
            cat.napping = false;
            // Persist last play time as UTC (subtract offset so timezone changes don't shift boredom)
            persistCareAction(configMgr.config().lastPlayEpoch, wasNeeded, POINTS_PLAY);
            dirty.animal = true;
        } else if (cat.health == CatHealth::Sick &&
                   p.x >= MEDS_X && p.x <= MEDS_X + MEDS_W &&
                   p.y >= MEDS_Y && p.y <= MEDS_Y + MEDS_H) {
            // Give meds
            cat.mood   = CatMood::Celebrate;
            cat.health = CatHealth::Healthy;
            cat.since  = now;
            cat.frame  = 0;
            // Persist last meds time as UTC (subtract offset so timezone changes don't shift the cooldown).
            // Hit-test above already requires CatHealth::Sick to reach this branch, so meds always help.
            persistCareAction(configMgr.config().lastMedsEpoch, /*wasNeeded=*/true, POINTS_MEDS);
            dirty.animal = true;
        } else if (p.x >= WATER_X && p.x <= WATER_X + WATER_W &&
                   p.y >= WATER_Y && p.y <= WATER_Y + WATER_H) {
            // Give water — always available, unlike meds which only responds while sick
            bool wasNeeded = (cat.thirst == CatThirst::Thirsty);
            cat.mood   = CatMood::Celebrate;
            cat.thirst = CatThirst::Hydrated;
            cat.since  = now;
            cat.frame  = 0;
            // Persist last water time as UTC (subtract offset so timezone changes don't shift the cooldown)
            persistCareAction(configMgr.config().lastWaterEpoch, wasNeeded, POINTS_WATER);
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
    time_t utc   = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utc <= 1000000000) return;  // NTP not synced yet (pre-2001 or un-synced offset-only value)

    uint32_t lastTreat = configMgr.config().lastTreatEpoch;
    uint32_t threshold = (uint32_t)configMgr.config().hungerMinutes * 60u;
    uint32_t now32     = (uint32_t)utc;
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

    // Tummy rumble: 3.5 s cycle for Peckish, 1.5 s cycle for Hungry — only redraws the lines
    bool shouldRumble = (cat.status == CatStatus::Peckish || cat.status == CatStatus::Hungry)
                        && cat.mood == CatMood::Idle;
    if (shouldRumble) {
        unsigned long now = millis();
        unsigned long interval = (cat.status == CatStatus::Hungry) ? 1500UL : 3500UL;
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

static void updateCatBoredom() {
    time_t epoch = ntpClient.getEpochTime();
    time_t utc   = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utc <= 1000000000) return;  // NTP not synced yet (pre-2001 or un-synced offset-only value)

    uint32_t lastPlay  = configMgr.config().lastPlayEpoch;
    uint32_t threshold = (uint32_t)configMgr.config().boredomMinutes * 60u;
    uint32_t now32     = (uint32_t)utc;
    uint32_t elapsed;
    if (lastPlay == 0)           elapsed = threshold + 1;   // never played → start bored
    else if (now32 >= lastPlay)  elapsed = now32 - lastPlay;
    else                         elapsed = 0;                // NTP clock step back → treat as just played

    CatBoredom prev = cat.boredom;
    if (elapsed < threshold / 2)
        cat.boredom = CatBoredom::Entertained;
    else if (elapsed < threshold)
        cat.boredom = CatBoredom::Bored;
    else
        cat.boredom = CatBoredom::VeryBored;

    if (cat.boredom != prev) dirty.animal = true;

    // "Zz" toggle: 3.5 s cycle for Bored, 1.5 s cycle for VeryBored — plays independently of
    // hunger status (drawn outside the head, so it never visually collides with the tummy lines)
    bool shouldNap = (cat.boredom == CatBoredom::Bored || cat.boredom == CatBoredom::VeryBored)
                     && cat.mood == CatMood::Idle;
    if (shouldNap) {
        unsigned long now = millis();
        unsigned long interval = (cat.boredom == CatBoredom::VeryBored) ? 1500UL : 3500UL;
        if (!cat.napping && now - cat.lastZzz > interval) {
            cat.napping = true;
            cat.lastZzz = now;
            dirty.zzzFx = true;
        } else if (cat.napping && now - cat.lastZzz > 400) {
            cat.napping = false;
            cat.lastZzz = now;
            dirty.zzzFx = true;
        }
    } else if (cat.napping) {
        cat.napping = false;
        dirty.zzzFx = true;
    }
}

// Random-onset check cadence/odds for the sick event, once the cooldown has elapsed —
// mirrors the random(10000, 15001) idiom used for the sleep-screen clock cadence.
static constexpr unsigned long SICK_CHECK_INTERVAL_MS       = 60000UL;  // ~1 min between rolls
static constexpr int           SICK_TRIGGER_PER_MILLE       = 2;        // ~0.2% chance per roll

static void updateCatHealth() {
    // Test-only forced trigger, armed via the /config page's "force sick" field
    if (forceSickDeadlineMs != 0 && millis() >= forceSickDeadlineMs) {
        forceSickDeadlineMs = 0;
        if (cat.health == CatHealth::Healthy) {
            cat.health   = CatHealth::Sick;
            dirty.animal = true;
        }
        return;
    }

    if (cat.health == CatHealth::Sick) return;  // already sick, waiting for meds

    time_t epoch = ntpClient.getEpochTime();
    time_t utc   = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utc <= 1000000000) return;  // NTP not synced yet (pre-2001 or un-synced offset-only value)

    uint32_t lastMeds  = configMgr.config().lastMedsEpoch;
    uint32_t threshold = (uint32_t)configMgr.config().sickCooldownHours * 3600u;
    uint32_t now32     = (uint32_t)utc;
    uint32_t elapsed;
    if (lastMeds == 0)          elapsed = threshold + 1;   // never medicated → immediately eligible
    else if (now32 >= lastMeds) elapsed = now32 - lastMeds;
    else                        elapsed = 0;                // NTP clock step back → treat as just medicated

    if (elapsed < threshold) return;  // still in cooldown, not yet eligible

    unsigned long now = millis();
    if (now - cat.lastSickCheck < SICK_CHECK_INTERVAL_MS) return;
    cat.lastSickCheck = now;

    if ((int)random(1000) < SICK_TRIGGER_PER_MILLE) {
        cat.health   = CatHealth::Sick;
        dirty.animal = true;
    }
}

// Random-onset check cadence/odds for the thirst event, checked from the moment the cat is
// last watered (no cooldown gate) — small odds per roll, backstopped by a guaranteed forced
// trigger once `thirstForceMinutes` elapses without water so thirst can't go unbounded long
// on bad luck alone (unlike updateCatHealth()'s sick event, which has no forced deadline).
static constexpr unsigned long THIRST_CHECK_INTERVAL_MS = 60000UL;  // ~1 min between rolls
static constexpr int           THIRST_TRIGGER_PER_MILLE = 2;        // ~0.2% chance per roll

static void updateCatThirst() {
    // Test-only forced trigger, armed via the /config page's "force thirsty" field
    if (forceThirstDeadlineMs != 0 && millis() >= forceThirstDeadlineMs) {
        forceThirstDeadlineMs = 0;
        if (cat.thirst == CatThirst::Hydrated) {
            cat.thirst   = CatThirst::Thirsty;
            dirty.animal = true;
        }
        return;
    }

    if (cat.thirst == CatThirst::Thirsty) return;  // already thirsty, waiting for water

    time_t epoch = ntpClient.getEpochTime();
    time_t utc   = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utc <= 1000000000) return;  // NTP not synced yet (pre-2001 or un-synced offset-only value)

    uint32_t lastWater      = configMgr.config().lastWaterEpoch;
    uint32_t forceThreshold = (uint32_t)configMgr.config().thirstForceMinutes * 60u;
    uint32_t now32          = (uint32_t)utc;
    uint32_t elapsed;
    if (lastWater == 0)          elapsed = forceThreshold;   // never watered → immediately at the force deadline
    else if (now32 >= lastWater) elapsed = now32 - lastWater;
    else                         elapsed = 0;                 // NTP clock step back → treat as just watered

    if (elapsed >= forceThreshold) {
        cat.thirst   = CatThirst::Thirsty;
        dirty.animal = true;
        return;
    }

    unsigned long now = millis();
    if (now - cat.lastThirstCheck < THIRST_CHECK_INTERVAL_MS) return;
    cat.lastThirstCheck = now;

    if ((int)random(1000) < THIRST_TRIGGER_PER_MILLE) {
        cat.thirst   = CatThirst::Thirsty;
        dirty.animal = true;
    }
}

// ── Sleep window ──────────────────────────────────────────────────────────────
static bool isInSleepWindow(int nowMinutes) {
    int bed  = configMgr.config().sleepBedMinutes;
    int wake = configMgr.config().sleepWakeMinutes;
    if (bed == wake) return false;               // degenerate: treat as "sleep disabled"
    if (bed < wake) return nowMinutes >= bed && nowMinutes < wake;   // same-day window
    return nowMinutes >= bed || nowMinutes < wake;                   // wraps midnight
}

struct SleepPos { int x, y; };
static constexpr SleepPos SLEEP_CLOCK_POS[] = {
    { 20,  60}, {100,  60}, { 20, 160}, {100, 160}, { 20, 250}, {100, 250},
};
static constexpr int SLEEP_CLOCK_POS_N = 6;

static void updateSleepScreen(unsigned long now) {
    static unsigned long nextMoveMs = 0;
    static uint8_t posIdx = 0;
    static bool hasDrawn = false;
    static int lastX = 0, lastY = 0, lastW = 0;

    if (!sleepScreenActive) {
        tft.fillScreen(TFT_BLACK);   // once per sleep session, not every frame
        sleepScreenActive = true;
        hasDrawn = false;
        nextMoveMs = 0;              // force immediate first draw
    }
    if (now >= nextMoveMs) {
        if (hasDrawn) {
            // Erase exactly what was drawn — text width varies with digit count (e.g.
            // "9:05" vs "12:34"), so a fixed-size erase rect can leave stray pixels behind.
            tft.fillRect(lastX - 4, lastY - 4, lastW + 8, 40, TFT_BLACK);
        }
        posIdx = (posIdx + 1) % SLEEP_CLOCK_POS_N;

        time_t epoch = ntpClient.getEpochTime();
        struct tm* t = localtime(&epoch);
        int h12 = t->tm_hour % 12; if (h12 == 0) h12 = 12;
        char buf[6];
        snprintf(buf, sizeof(buf), "%d:%02d", h12, t->tm_min);

        tft.setTextColor(C_SLEEP_DIM, TFT_BLACK);
        int x = SLEEP_CLOCK_POS[posIdx].x, y = SLEEP_CLOCK_POS[posIdx].y;
        tft.drawString(buf, x, y, 4);
        lastX = x;
        lastY = y;
        lastW = tft.textWidth(buf, 4);
        hasDrawn = true;
        nextMoveMs = now + random(10000, 15001);  // 10-15s cadence
    }
}

// ── Config web page ───────────────────────────────────────────────────────────

static const char CONFIG_STYLE[] PROGMEM = R"css(
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
.nav{display:block;width:100%;padding:16px;margin-bottom:14px;background:#1e1e1e;color:#ddd;border:1px solid #333;border-radius:8px;text-align:center;text-decoration:none;font-size:1.1rem}
.nav:hover{background:#262626}
.back{display:inline-block;margin-bottom:14px;color:#888;text-decoration:none;font-size:.85rem}
.back:hover{color:#ddd}
.balance{font-size:1.4rem;margin-bottom:14px}
.item{display:flex;justify-content:space-between;align-items:center;padding:14px;margin-bottom:10px;background:#1e1e1e;border:1px solid #333;border-radius:8px}
.item button{margin:0}
.item button:disabled{background:#333;color:#777;cursor:not-allowed}
.owned{color:#4b6}
.err{background:#631}
.pick{display:flex;align-items:center;gap:8px;margin-bottom:8px;font-size:1rem;color:#ddd}
.pick input{display:inline-block;width:auto;margin:0}
)css";

static const char CONFIG_HOME_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel</title>
<style>%%STYLE%%</style>
</head><body>
<h2>Cat Control Panel</h2>
<a class="nav" href="/config/cat">Cat</a>
<a class="nav" href="/config/city">City (weather &amp; timezone)</a>
<a class="nav" href="/config/store">Store</a>
<a class="nav" href="/config/dress">Dressing Room</a>
<a class="nav" href="/config/backup">Backup</a>
<a class="nav" href="/config/update">Firmware Update</a>
</body></html>
)html";

static const char CONFIG_CAT_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Cat Config</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Cat</h2>
%%MSG%%
<form method="POST" action="/save-config/cat">

<h3>Cat name</h3>
<label>Name</label>
<input name="name" id="name" maxlength="16" value="%%NAME%%">

<h3>Cat hunger</h3>
<label>Minutes until hungry</label>
<input name="hunger" id="hunger" type="number" min="1" max="1440" value="%%HUNGER%%">

<h3>Cat boredom</h3>
<label>Minutes until bored</label>
<input name="boredom" id="boredom" type="number" min="1" max="1440" value="%%BOREDOM%%">

<h3>Cat health</h3>
<label>Minimum hours between sick events</label>
<input name="sickCooldown" id="sickCooldown" type="number" min="1" max="168" value="%%SICKCOOLDOWN%%">
<label>Force sick in N minutes (test only, 0 = off)</label>
<input name="forceSickMinutes" id="forceSickMinutes" type="number" min="0" max="1440" value="%%FORCESICK%%">
<label style="margin-top:-8px">%%FORCESICKSTATUS%%</label>

<h3>Cat thirst</h3>
<label>Force thirsty after N minutes without water</label>
<input name="thirstForceMinutes" id="thirstForceMinutes" type="number" min="1" max="1440" value="%%THIRSTFORCEMINUTES%%">
<label>Force thirsty in N minutes (test only, 0 = off)</label>
<input name="forceThirstMinutes" id="forceThirstMinutes" type="number" min="0" max="1440" value="%%FORCETHIRST%%">
<label style="margin-top:-8px">%%FORCETHIRSTSTATUS%%</label>

<h3>Cat sleep</h3>
<label>Bed time</label>
<input name="sleepBed" id="sleepBed" type="time" value="%%SLEEPBED%%">
<label>Wake time</label>
<input name="sleepWake" id="sleepWake" type="time" value="%%SLEEPWAKE%%">

<button type="submit" style="width:100%;margin-top:8px">Save</button>
</form>
</body></html>
)html";

static const char CONFIG_CITY_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · City Config</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>City</h2>
%%MSG%%
<form method="POST" action="/save-config/city">

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

static const char CONFIG_BACKUP_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Backup</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Backup</h2>
%%MSG%%

<h3>Export</h3>
<p>Your entire config — cat name/schedule, city/timezone, and store/points state. Copy this, or use the download link, and save it somewhere safe.</p>
<textarea readonly rows="6" style="width:100%">%%EXPORT_JSON%%</textarea>
<p><a href="/config/backup/export" download="cat-clock-backup.json">Download as file</a></p>

<h3>Import</h3>
<form method="POST" action="/save-config/backup">
<label for="importFile">Choose a backup file</label>
<input type="file" id="importFile" accept="application/json">
<textarea name="json" id="importJson" rows="6" style="width:100%" placeholder="...or paste backup JSON here"></textarea>
<button type="submit" style="width:100%;margin-top:8px">Restore</button>
</form>
<script>
document.getElementById('importFile').addEventListener('change', function(e){
    var file = e.target.files[0];
    if (!file) return;
    var reader = new FileReader();
    reader.onload = function(evt){ document.getElementById('importJson').value = evt.target.result; };
    reader.readAsText(file);
});
</script>

</body></html>
)html";

static const char CONFIG_UPDATE_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Firmware Update</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Firmware Update</h2>
%%MSG%%

<h3>Version</h3>
<p>Running: <strong>%%CURRENT_VERSION%%</strong></p>
<p>Last checked: %%LAST_CHECKED%%</p>

<form method="POST" action="/save-config/update">
<label><input type="checkbox" name="autoUpdate" %%AUTOUPDATE_CHECKED%%> Automatically check for and install updates</label>
<button type="submit" style="width:100%;margin-top:8px">Save</button>
</form>

<form method="POST" action="/config/update/check" style="margin-top:12px">
<button type="submit" style="width:100%">Check now</button>
</form>
</body></html>
)html";

static const char CONFIG_STORE_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Store</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2 id="storeTitle">Store</h2>
%%MSG%%
<div class="balance">Points: <strong>%%POINTS%%</strong></div>

<h3>Stuffies (night only)</h3>
%%STUFFY_ITEMS%%

<h3>Blankets (night only)</h3>
%%BLANKET_ITEMS%%

<h3>Room Themes</h3>
%%ROOM_THEME_ITEMS%%

<form id="cheatForm" method="POST" action="/save-config/cheat" style="display:none;margin-top:12px">
<button type="submit">+50 points</button>
</form>
<form id="resetForm" method="POST" action="/save-config/reset" style="display:none;margin-top:12px">
<input type="text" name="confirm" placeholder="type reset to confirm">
<button type="submit" style="width:100%;margin-top:8px">Reset Everything</button>
</form>
<script>
// Easter egg: tap the "Store" heading 7 times in a row (no other tap in between)
// to reveal a cheat button that grants 50 points, and a reset-everything form.
(function(){
    var taps = 0;
    var title = document.getElementById('storeTitle');
    var cheat = document.getElementById('cheatForm');
    var reset = document.getElementById('resetForm');
    document.addEventListener('click', function(e){
        if (e.target === title) {
            taps++;
            if (taps >= 7) { cheat.style.display = 'block'; reset.style.display = 'block'; }
        } else {
            taps = 0;
        }
    });
})();
</script>

</body></html>
)html";

static const char CONFIG_DRESS_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Dressing Room</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Dressing Room</h2>
%%MSG%%
<form method="POST" action="/save-config/dress">

<h3>Stuffies (night only)</h3>
%%STUFFY_OPTIONS%%

<h3>Blankets (night only)</h3>
%%BLANKET_OPTIONS%%

<h3>Room Themes</h3>
%%ROOM_THEME_OPTIONS%%

<button type="submit" style="width:100%;margin-top:8px">Save</button>
</form>
</body></html>
)html";

static String htmlEscape(const String& s) {
    String out;
    out.reserve(s.length());
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#x27;"; break;
            default:   out += c;
        }
    }
    return out;
}

static String minutesToHHMM(int minutes) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return String(buf);
}

static bool parseHHMM(const String& s, int& outMinutes) {
    int h, m;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    outMinutes = h * 60 + m;
    return true;
}

static void drawOtaProgress(size_t written, size_t total);
static OtaCheckResult performUpdateCheckOnly();
static void applyFoundUpdate(const OtaCheckResult& result);
static void performUpdateCheck();

static void handleConfigHome() {
    String page = String(FPSTR(CONFIG_HOME_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    wm.server->send(200, "text/html", page);
}

static void handleConfigCatGet() {
    String page = String(FPSTR(CONFIG_CAT_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    page.replace("%%HUNGER%%", String(configMgr.config().hungerMinutes));
    page.replace("%%BOREDOM%%", String(configMgr.config().boredomMinutes));
    page.replace("%%SICKCOOLDOWN%%", String(configMgr.config().sickCooldownHours));
    page.replace("%%THIRSTFORCEMINUTES%%", String(configMgr.config().thirstForceMinutes));
    {
        unsigned long now = millis();
        String status;
        int remainMin = 0;
        if (forceThirstDeadlineMs != 0 && forceThirstDeadlineMs > now) {
            unsigned long remainMs = forceThirstDeadlineMs - now;
            remainMin = (int)((remainMs + 59999UL) / 60000UL);  // round up so it doesn't show 0 right after arming
            status = "Armed — thirsty in ~" + String(remainMin) + " min.";
        }
        page.replace("%%FORCETHIRST%%", String(remainMin));
        page.replace("%%FORCETHIRSTSTATUS%%", status);
    }
    {
        unsigned long now = millis();
        String status;
        int remainMin = 0;
        if (forceSickDeadlineMs != 0 && forceSickDeadlineMs > now) {
            unsigned long remainMs = forceSickDeadlineMs - now;
            remainMin = (int)((remainMs + 59999UL) / 60000UL);  // round up so it doesn't show 0 right after arming
            status = "Armed — sick in ~" + String(remainMin) + " min.";
        }
        page.replace("%%FORCESICK%%", String(remainMin));
        page.replace("%%FORCESICKSTATUS%%", status);
    }
    page.replace("%%SLEEPBED%%",  minutesToHHMM(configMgr.config().sleepBedMinutes));
    page.replace("%%SLEEPWAKE%%", minutesToHHMM(configMgr.config().sleepWakeMinutes));
    page.replace("%%NAME%%", htmlEscape(configMgr.config().catName));
    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Settings saved.</div>";
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

static void handleConfigCityGet() {
    String page = String(FPSTR(CONFIG_CITY_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    page.replace("%%LAT%%", String(configMgr.config().latitude,  4));
    page.replace("%%LON%%", String(configMgr.config().longitude, 4));
    page.replace("%%UTC%%", String(configMgr.config().utcOffsetSeconds));
    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Settings saved.</div>";
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

// Renders the buy button/owned-label markup for one store item.
static String storeItemAction(const char* item, bool owned, uint32_t cost, uint32_t points) {
    if (owned) return "<span class='owned'>Owned</span>";
    String html = "<form method='POST' action='/save-config/store'>";
    html += "<input type='hidden' name='item' value='" + String(item) + "'>";
    html += "<button type='submit'";
    if (points < cost) html += " disabled";
    html += ">Buy for " + String(cost) + "</button></form>";
    return html;
}

static void handleConfigStoreGet() {
    // Clear the on-device points flash by stamping the current catalog sizes as "seen" —
    // any store visit acknowledges every item added up to this point.
    if (hasNewStoreItems()) {
        configMgr.config().seenStuffyCount       = (uint8_t)STUFFY_COUNT;
        configMgr.config().seenBlanketColorCount = (uint8_t)BLANKET_COLOR_COUNT;
        configMgr.config().seenRoomThemeCount    = (uint8_t)ROOM_THEME_COUNT;
        configMgr.save();
    }
    String page = String(FPSTR(CONFIG_STORE_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    uint32_t points = configMgr.config().points;
    page.replace("%%POINTS%%", String(points));
    String stuffyItems = "";
    for (int i = 0; i < STUFFY_COUNT; i++) {
        bool owned = configMgr.config().ownedStuffies & (1 << i);
        stuffyItems += "<div class='item'><span>" + String(STUFFIES[i].label) + "</span>";
        stuffyItems += storeItemAction(STUFFIES[i].id, owned, STUFFIES[i].cost, points);
        stuffyItems += "</div>\n";
    }
    page.replace("%%STUFFY_ITEMS%%", stuffyItems);
    String blanketItems = "";
    for (int i = 0; i < BLANKET_COLOR_COUNT; i++) {
        bool owned = configMgr.config().ownedBlanketColors & (1 << i);
        blanketItems += "<div class='item'><span style='color:" + String(BLANKET_COLORS[i].webColor) + "'>Blanket - " + String(BLANKET_COLORS[i].label) + "</span>";
        blanketItems += storeItemAction(BLANKET_COLORS[i].id, owned, STORE_COST_BLANKET, points);
        blanketItems += "</div>\n";
    }
    page.replace("%%BLANKET_ITEMS%%", blanketItems);
    String roomThemeItems = "";
    for (int i = 0; i < ROOM_THEME_COUNT; i++) {
        bool owned = configMgr.config().ownedRoomThemes & (1 << i);
        roomThemeItems += "<div class='item'><span>" + String(ROOM_THEMES[i].label) + "</span>";
        roomThemeItems += storeItemAction(ROOM_THEMES[i].id, owned, ROOM_THEMES[i].cost, points);
        roomThemeItems += "</div>\n";
    }
    page.replace("%%ROOM_THEME_ITEMS%%", roomThemeItems);
    String msg = "";
    if (wm.server->hasArg("cheat")) {
        msg = "<div class='banner ok'>+50 points!</div>";
    } else if (wm.server->hasArg("reset")) {
        msg = "<div class='banner ok'>Everything has been reset.</div>";
    } else if (wm.server->hasArg("saved")) {
        msg = "<div class='banner ok'>Purchase complete.</div>";
    } else if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "funds") msg = "<div class='banner err'>Not enough points.</div>";
        else if (err == "owned") msg = "<div class='banner err'>You already own that item.</div>";
        else if (err == "save") msg = "<div class='banner err'>Purchase failed to save — please try again.</div>";
        else if (err == "resetConfirm") msg = "<div class='banner err'>Type \"reset\" exactly to confirm.</div>";
        else if (err == "resetSave") msg = "<div class='banner err'>Reset failed to save — please try again.</div>";
    }
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

// Easter egg: grants 50 points, reached only via the hidden cheat button revealed by
// tapping the store heading 7 times in a row (see CONFIG_STORE_HTML's inline script).
static void handleConfigStoreCheatPost() {
    configMgr.config().points += 50;
    configMgr.save();
    wm.server->sendHeader("Location", "/config/store?cheat=1");
    wm.server->send(302, "text/plain", "");
}

// Easter egg: wipes the entire config — cat name/schedule, city/timezone, and all
// gamification state — back to AppConfig's defaults, reached only via the hidden reset
// form revealed alongside the cheat button (see CONFIG_STORE_HTML's inline script).
// Requires typing "reset" to guard against an accidental tap/submit.
static void handleConfigResetPost() {
    if (wm.server->arg("confirm") != "reset") {
        wm.server->sendHeader("Location", "/config/store?err=resetConfirm");
        wm.server->send(302, "text/plain", "");
        return;
    }
    configMgr.resetToDefaults();
    if (!configMgr.save()) {
        wm.server->sendHeader("Location", "/config/store?err=resetSave");
        wm.server->send(302, "text/plain", "");
        return;
    }
    // Same side effects handleConfigCityPost() applies when latitude/longitude/utc change,
    // since resetToDefaults() resets those too.
    ntpClient.setTimeOffset(configMgr.config().utcOffsetSeconds);
    lastWeatherFetch = millis() - WEATHER_UPDATE_INTERVAL_MS - 1;
    dirty.header = true;
    dirty.animal = true;
    dirty.animalBg = true;  // reset clears any equipped room theme back to default
    wm.server->sendHeader("Location", "/config/store?reset=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigBackupGet() {
    String page = String(FPSTR(CONFIG_BACKUP_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    page.replace("%%EXPORT_JSON%%", htmlEscape(configMgr.exportBackupJson()));
    String msg = "";
    if (wm.server->hasArg("saved")) {
        msg = "<div class='banner ok'>Backup restored.</div>";
    } else if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "empty") msg = "<div class='banner err'>Paste or choose a backup file first.</div>";
        else if (err == "parse") msg = "<div class='banner err'>That doesn't look like a valid backup file.</div>";
        else if (err == "save") msg = "<div class='banner err'>Restore failed to save — please try again.</div>";
    }
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

static void handleConfigBackupExportGet() {
    wm.server->sendHeader("Content-Disposition", "attachment; filename=\"cat-clock-backup.json\"");
    wm.server->send(200, "application/json", configMgr.exportBackupJson());
}

static void handleConfigBackupPost() {
    String json = wm.server->arg("json");
    json.trim();
    if (json.length() == 0) {
        wm.server->sendHeader("Location", "/config/backup?err=empty");
        wm.server->send(302, "text/plain", "");
        return;
    }
    if (!configMgr.importBackupJson(json)) {
        wm.server->sendHeader("Location", "/config/backup?err=parse");
        wm.server->send(302, "text/plain", "");
        return;
    }
    if (!configMgr.save()) {
        wm.server->sendHeader("Location", "/config/backup?err=save");
        wm.server->send(302, "text/plain", "");
        return;
    }
    // Same side effects handleConfigCityPost() applies when latitude/longitude/utc change,
    // since a restored backup may carry a different city/timezone than what's currently set.
    ntpClient.setTimeOffset(configMgr.config().utcOffsetSeconds);
    lastWeatherFetch = millis() - WEATHER_UPDATE_INTERVAL_MS - 1;
    dirty.header = true;
    dirty.animal = true;  // name/points/owned/equipped items may have changed
    dirty.animalBg = true;  // restored backup may carry a different equipped room theme
    wm.server->sendHeader("Location", "/config/backup?saved=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigUpdateGet() {
    String page = String(FPSTR(CONFIG_UPDATE_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    page.replace("%%CURRENT_VERSION%%", htmlEscape(FIRMWARE_VERSION));
    String lastChecked = "never";
    if (configMgr.config().lastUpdateCheckEpoch > 0) {
        uint32_t now = ntpClient.getEpochTime();
        uint32_t ago = now > configMgr.config().lastUpdateCheckEpoch ? now - configMgr.config().lastUpdateCheckEpoch : 0;
        lastChecked = htmlEscape(configMgr.config().lastUpdateCheckVersion) + " (" + String(ago / 60) + " min ago)";
    }
    page.replace("%%LAST_CHECKED%%", lastChecked);
    page.replace("%%AUTOUPDATE_CHECKED%%", configMgr.config().autoUpdateEnabled ? "checked" : "");
    String msg = "";
    if (wm.server->hasArg("saved")) {
        msg = "<div class='banner ok'>Saved.</div>";
    } else if (wm.server->hasArg("checked")) {
        // A found-and-applied update responds separately and reboots before reaching
        // this page (see handleConfigUpdateCheckPost), so landing here after a check
        // always means one of: skipped, failed, or already up to date.
        if (lastUpdateCheckSkipped) msg = "<div class='banner ok'>Check skipped — this is a dev build with nothing to compare against.</div>";
        else if (lastUpdateCheckFailed) msg = "<div class='banner err'>Check failed — see serial log.</div>";
        else msg = "<div class='banner ok'>Already up to date.</div>";
    }
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

static void handleConfigUpdatePost() {
    configMgr.config().autoUpdateEnabled = wm.server->hasArg("autoUpdate");
    configMgr.save();
    wm.server->sendHeader("Location", "/config/update?saved=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigUpdateCheckPost() {
    bool wasEnabled = configMgr.config().autoUpdateEnabled;
    configMgr.config().autoUpdateEnabled = true;  // manual check always runs regardless of the toggle
    OtaCheckResult result = performUpdateCheckOnly();
    configMgr.config().autoUpdateEnabled = wasEnabled;
    configMgr.save();

    if (!lastUpdateCheckFailed && !lastUpdateCheckSkipped && result.updateAvailable) {
        // Respond now, before applyFoundUpdate() blocks on the download+flash+reboot —
        // otherwise the browser just sees a dropped connection instead of this message.
        String page = "<!DOCTYPE html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<style>" + String(FPSTR(CONFIG_STYLE)) + "</style></head><body>"
            "<h2>Found " + htmlEscape(result.latestVersion) + "</h2>"
            "<p>Installing now — the device will reboot when done. This page won't update further.</p>"
            "</body></html>";
        wm.server->send(200, "text/html", page);
        applyFoundUpdate(result);  // reboots on success; only returns on failure
        return;
    }

    wm.server->sendHeader("Location", "/config/update?checked=0");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigCatPost() {
    int   hunger  = wm.server->arg("hunger").toInt();
    int   boredom = wm.server->arg("boredom").toInt();
    int   sickCooldown = wm.server->arg("sickCooldown").toInt();
    int   forceSickMinutes = wm.server->arg("forceSickMinutes").toInt();
    int   thirstForceMinutes = wm.server->arg("thirstForceMinutes").toInt();
    int   forceThirstMinutes = wm.server->arg("forceThirstMinutes").toInt();
    int   sleepBed = 0, sleepWake = 0;
    bool  sleepBedOk  = parseHHMM(wm.server->arg("sleepBed"),  sleepBed);
    bool  sleepWakeOk = parseHHMM(wm.server->arg("sleepWake"), sleepWake);
    String name = wm.server->arg("name");
    name.trim();
    if (name.length() == 0) name = "Biscuit";
    bool nameOk = name.length() <= 16;
    for (size_t i = 0; nameOk && i < name.length(); ++i) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7F) nameOk = false;
    }
    if (hunger < 1 || hunger > 1440 || boredom < 1 || boredom > 1440
        || sickCooldown < 1 || sickCooldown > 168
        || forceSickMinutes < 0 || forceSickMinutes > 1440
        || thirstForceMinutes < 1 || thirstForceMinutes > 1440
        || forceThirstMinutes < 0 || forceThirstMinutes > 1440
        || !sleepBedOk || !sleepWakeOk || !nameOk) {
        wm.server->send(400, "text/plain", "Invalid values");
        return;
    }
    configMgr.config().hungerMinutes    = hunger;
    configMgr.config().boredomMinutes   = boredom;
    configMgr.config().sickCooldownHours = sickCooldown;
    if (forceSickMinutes > 0) forceSickDeadlineMs = millis() + (unsigned long)forceSickMinutes * 60000UL;
    configMgr.config().thirstForceMinutes = thirstForceMinutes;
    if (forceThirstMinutes > 0) forceThirstDeadlineMs = millis() + (unsigned long)forceThirstMinutes * 60000UL;
    configMgr.config().sleepBedMinutes  = sleepBed;
    configMgr.config().sleepWakeMinutes = sleepWake;
    configMgr.config().catName          = name;
    configMgr.save();
    dirty.animal = true;
    wm.server->sendHeader("Location", "/config/cat?saved=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigCityPost() {
    float lat = wm.server->arg("lat").toFloat();
    float lon = wm.server->arg("lon").toFloat();
    int   utc = wm.server->arg("utc").toInt();
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f || utc < -50400 || utc > 50400) {
        wm.server->send(400, "text/plain", "Invalid values");
        return;
    }
    configMgr.config().latitude         = lat;
    configMgr.config().longitude        = lon;
    configMgr.config().utcOffsetSeconds = utc;
    configMgr.save();
    ntpClient.setTimeOffset(utc);
    lastWeatherFetch = millis() - WEATHER_UPDATE_INTERVAL_MS - 1;
    dirty.header = true;
    wm.server->sendHeader("Location", "/config/city?saved=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigStorePost() {
    String item = wm.server->arg("item");
    uint32_t cost;
    bool alreadyOwned;
    int stuffyIdx = -1, blanketIdx = -1, roomThemeIdx = -1;
    for (int i = 0; i < STUFFY_COUNT; i++) {
        if (item == STUFFIES[i].id) { stuffyIdx = i; break; }
    }
    if (stuffyIdx >= 0) {
        cost = STUFFIES[stuffyIdx].cost;
        alreadyOwned = configMgr.config().ownedStuffies & (1 << stuffyIdx);
    } else {
        for (int i = 0; i < BLANKET_COLOR_COUNT; i++) {
            if (item == BLANKET_COLORS[i].id) { blanketIdx = i; break; }
        }
        if (blanketIdx >= 0) {
            cost = STORE_COST_BLANKET;
            alreadyOwned = configMgr.config().ownedBlanketColors & (1 << blanketIdx);
        } else {
            for (int i = 0; i < ROOM_THEME_COUNT; i++) {
                if (item == ROOM_THEMES[i].id) { roomThemeIdx = i; break; }
            }
            if (roomThemeIdx < 0) {
                wm.server->send(400, "text/plain", "Unknown item");
                return;
            }
            cost = ROOM_THEMES[roomThemeIdx].cost;
            alreadyOwned = configMgr.config().ownedRoomThemes & (1 << roomThemeIdx);
        }
    }

    if (alreadyOwned) {
        wm.server->sendHeader("Location", "/config/store?err=owned");
        wm.server->send(302, "text/plain", "");
        return;
    }
    if (configMgr.config().points < cost) {
        wm.server->sendHeader("Location", "/config/store?err=funds");
        wm.server->send(302, "text/plain", "");
        return;
    }

    configMgr.config().points -= cost;
    if (blanketIdx >= 0) {
        configMgr.config().ownedBlanketColors |= (1 << blanketIdx);
        configMgr.config().equippedBlanketColor = blanketIdx;  // newly bought color becomes equipped
    } else if (roomThemeIdx >= 0) {
        configMgr.config().ownedRoomThemes |= (1 << roomThemeIdx);
        configMgr.config().equippedRoomTheme = roomThemeIdx;  // newly bought theme becomes equipped
    } else {
        configMgr.config().ownedStuffies |= (1 << stuffyIdx);
        configMgr.config().equippedStuffy = stuffyIdx;  // newly bought stuffy becomes equipped
    }
    if (!configMgr.save()) {
        // Roll back in-memory state since persistence failed.
        configMgr.config().points += cost;
        if (blanketIdx >= 0) configMgr.config().ownedBlanketColors &= ~(1 << blanketIdx);
        else if (roomThemeIdx >= 0) configMgr.config().ownedRoomThemes &= ~(1 << roomThemeIdx);
        else configMgr.config().ownedStuffies &= ~(1 << stuffyIdx);
        wm.server->sendHeader("Location", "/config/store?err=save");
        wm.server->send(302, "text/plain", "");
        return;
    }
    dirty.animal = true;
    dirty.animalBg = true;  // a newly bought room theme auto-equips, changing the backdrop
    wm.server->sendHeader("Location", "/config/store?saved=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigDressGet() {
    String page = String(FPSTR(CONFIG_DRESS_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));

    uint8_t owned = configMgr.config().ownedBlanketColors;
    int equippedIdx = equippedBlanketIndex();
    String options = "";
    if (owned == 0) {
        options = "<p style='color:#888'>Not owned yet — visit the Store.</p>";
    } else {
        options += "<label class='pick'><input type='radio' name='blanketColor' value='none'";
        if (equippedIdx < 0) options += " checked";
        options += "> None</label>";
        for (int i = 0; i < BLANKET_COLOR_COUNT; i++) {
            if (!(owned & (1 << i))) continue;
            options += "<label class='pick'><input type='radio' name='blanketColor' value='";
            options += BLANKET_COLORS[i].id;
            options += "'";
            if (i == equippedIdx) options += " checked";
            options += "> <span style='color:" + String(BLANKET_COLORS[i].webColor) + "'>"
                     + String(BLANKET_COLORS[i].label) + "</span></label>";
        }
    }
    page.replace("%%BLANKET_OPTIONS%%", options);

    uint8_t ownedStuffies = configMgr.config().ownedStuffies;
    int equippedStuffyIdx = equippedStuffyIndex();
    String stuffyOptions = "";
    if (ownedStuffies == 0) {
        stuffyOptions = "<p style='color:#888'>Not owned yet — visit the Store.</p>";
    } else {
        stuffyOptions += "<label class='pick'><input type='radio' name='stuffy' value='none'";
        if (equippedStuffyIdx < 0) stuffyOptions += " checked";
        stuffyOptions += "> None</label>";
        for (int i = 0; i < STUFFY_COUNT; i++) {
            if (!(ownedStuffies & (1 << i))) continue;
            stuffyOptions += "<label class='pick'><input type='radio' name='stuffy' value='";
            stuffyOptions += STUFFIES[i].id;
            stuffyOptions += "'";
            if (i == equippedStuffyIdx) stuffyOptions += " checked";
            stuffyOptions += "> " + String(STUFFIES[i].label) + "</label>";
        }
    }
    page.replace("%%STUFFY_OPTIONS%%", stuffyOptions);

    uint8_t ownedThemes = configMgr.config().ownedRoomThemes;
    int equippedThemeIdx = equippedRoomThemeIndex();
    String themeOptions = "";
    if (ownedThemes == 0) {
        themeOptions = "<p style='color:#888'>Not owned yet — visit the Store.</p>";
    } else {
        themeOptions += "<label class='pick'><input type='radio' name='roomTheme' value='none'";
        if (equippedThemeIdx < 0) themeOptions += " checked";
        themeOptions += "> None</label>";
        for (int i = 0; i < ROOM_THEME_COUNT; i++) {
            if (!(ownedThemes & (1 << i))) continue;
            themeOptions += "<label class='pick'><input type='radio' name='roomTheme' value='";
            themeOptions += ROOM_THEMES[i].id;
            themeOptions += "'";
            if (i == equippedThemeIdx) themeOptions += " checked";
            themeOptions += "> " + String(ROOM_THEMES[i].label) + "</label>";
        }
    }
    page.replace("%%ROOM_THEME_OPTIONS%%", themeOptions);

    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Saved.</div>";
    page.replace("%%MSG%%", msg);
    wm.server->send(200, "text/html", page);
}

static void handleConfigDressPost() {
    bool changed = false;

    String colorId = wm.server->arg("blanketColor");
    if (colorId == "none") {
        configMgr.config().equippedBlanketColor = EQUIP_NONE;
        changed = true;
    } else if (colorId.length() > 0) {
        int idx = -1;
        for (int i = 0; i < BLANKET_COLOR_COUNT; i++) {
            if (colorId == BLANKET_COLORS[i].id) { idx = i; break; }
        }
        if (idx < 0 || !(configMgr.config().ownedBlanketColors & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        configMgr.config().equippedBlanketColor = idx;
        changed = true;
    }

    String stuffyId = wm.server->arg("stuffy");
    if (stuffyId == "none") {
        configMgr.config().equippedStuffy = EQUIP_NONE;
        changed = true;
    } else if (stuffyId.length() > 0) {
        int idx = -1;
        for (int i = 0; i < STUFFY_COUNT; i++) {
            if (stuffyId == STUFFIES[i].id) { idx = i; break; }
        }
        if (idx < 0 || !(configMgr.config().ownedStuffies & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        configMgr.config().equippedStuffy = idx;
        changed = true;
    }

    String roomThemeId = wm.server->arg("roomTheme");
    if (roomThemeId == "none") {
        configMgr.config().equippedRoomTheme = EQUIP_NONE;
        changed = true;
    } else if (roomThemeId.length() > 0) {
        int idx = -1;
        for (int i = 0; i < ROOM_THEME_COUNT; i++) {
            if (roomThemeId == ROOM_THEMES[i].id) { idx = i; break; }
        }
        if (idx < 0 || !(configMgr.config().ownedRoomThemes & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        configMgr.config().equippedRoomTheme = idx;
        changed = true;
    }

    if (changed) {
        configMgr.save();
        dirty.animal = true;
        dirty.animalBg = true;  // equip/unequip changes the backdrop directly
    }
    wm.server->sendHeader("Location", "/config/dress?saved=1");
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

static void drawOtaProgress(size_t written, size_t total) {
    static int lastPct = -1;
    int pct = total > 0 ? (int)(written * 100 / total) : 0;
    if (pct == lastPct) return;
    lastPct = pct;

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("Updating firmware", CX, 90, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    tft.drawCentreString(pctStr, CX, 130, 4);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("do not power off", CX, 200, 2);
}

// Check step only — shared by the periodic loop() check and the manual "check now"
// button. Split out from the apply step so handleConfigUpdateCheckPost() can respond
// to the browser with "found, installing" *before* blocking on the download below.
static OtaCheckResult performUpdateCheckOnly() {
    lastUpdateCheckFailed  = false;
    lastUpdateCheckSkipped = false;
    if (!configMgr.config().autoUpdateEnabled) return OtaCheckResult{};

    OtaCheckResult result = otaClient.checkForUpdate(GITHUB_RELEASES_LATEST_URL, FIRMWARE_VERSION, OTA_ASSET_NAME);
    if (result.skipped) {
        lastUpdateCheckSkipped = true;
        return result;
    }
    if (result.checkFailed) {
        lastUpdateCheckFailed = true;
        return result;  // stay silent on the main screen, retry next interval
    }

    configMgr.config().lastUpdateCheckVersion = result.latestVersion;
    configMgr.config().lastUpdateCheckEpoch   = ntpClient.getEpochTime();
    configMgr.save();
    return result;
}

// Download + flash step — blocks (no async path without FreeRTOS task juggling);
// drawOtaProgress() is what keeps that from looking frozen. Reboots on success, so
// this only returns on failure.
static void applyFoundUpdate(const OtaCheckResult& result) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("Update found", CX, 90, 2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawCentreString(result.latestVersion.c_str(), CX, 130, 4);
    delay(1200);  // let it be read before the progress screen takes over

    drawOtaProgress(0, 1);
    OtaApplyResult applyResult = otaClient.applyUpdate(result.downloadUrl, drawOtaProgress);
    if (applyResult == OtaApplyResult::Success) {
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        tft.drawCentreString("Rebooting...", CX, 130, 4);
        delay(500);
        ESP.restart();
    }

    // Flash failed or download failed — running firmware is untouched, retry next interval.
    lastUpdateCheckFailed = true;
    dirty.header = dirty.animal = dirty.picker = dirty.timerRow = true;  // screen was overwritten
    dirty.animalBg = true;
}

static void performUpdateCheck() {
    OtaCheckResult result = performUpdateCheckOnly();
    if (lastUpdateCheckFailed || lastUpdateCheckSkipped || !result.updateAvailable) return;
    applyFoundUpdate(result);
}

// ── WiFiManager ───────────────────────────────────────────────────────────────
static void runWiFiManager(ConfigManager& cfg) {
    (void)cfg;  // config now managed exclusively via /config web page
    wm.setAPCallback([](WiFiManager*) { drawWifiPortal(); });
    wm.setTitle("Cat Control Panel");
    wm.setCustomMenuHTML("<form action='/config' method='get'><button>Cat Control Panel</button></form><br/>");
    const char* menu[] = {"wifi", "custom", "info", "sep", "update", "exit"};
    wm.setMenu(menu, 6);
    wm.autoConnect("CYD-Clock");
    wm.startWebPortal();
    wm.server->on("/config",           HTTP_GET,  handleConfigHome);
    wm.server->on("/config/cat",       HTTP_GET,  handleConfigCatGet);
    wm.server->on("/config/city",      HTTP_GET,  handleConfigCityGet);
    wm.server->on("/config/store",     HTTP_GET,  handleConfigStoreGet);
    wm.server->on("/config/dress",     HTTP_GET,  handleConfigDressGet);
    wm.server->on("/config/backup",        HTTP_GET,  handleConfigBackupGet);
    wm.server->on("/config/backup/export", HTTP_GET,  handleConfigBackupExportGet);
    wm.server->on("/config/update",        HTTP_GET,  handleConfigUpdateGet);
    wm.server->on("/config/update/check",  HTTP_POST, handleConfigUpdateCheckPost);
    wm.server->on("/save-config/cat",  HTTP_POST, handleConfigCatPost);
    wm.server->on("/save-config/city", HTTP_POST, handleConfigCityPost);
    wm.server->on("/save-config/store", HTTP_POST, handleConfigStorePost);
    wm.server->on("/save-config/cheat", HTTP_POST, handleConfigStoreCheatPost);
    wm.server->on("/save-config/reset", HTTP_POST, handleConfigResetPost);
    wm.server->on("/save-config/dress", HTTP_POST, handleConfigDressPost);
    wm.server->on("/save-config/backup", HTTP_POST, handleConfigBackupPost);
    wm.server->on("/save-config/update", HTTP_POST, handleConfigUpdatePost);
}

// ── Arduino ───────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);

    tft.init();
    tft.setRotation(0);
#if defined(BOARD_CYD)
    tft.invertDisplay(false);  // ST7789_Init.h hardcodes INVON; this panel needs INVOFF
#endif
    tft.fillScreen(TFT_BLACK);
    touchDriver.begin();

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
    lastUpdateCheck  = millis();

    // Confirms this boot is good before the *next* auto-update can overwrite the other
    // OTA slot — see STATUS.md's DIY-41 notes on what this actually guarantees under
    // PlatformIO's prebuilt Arduino core.
    if (weather.data().valid) {
        esp_ota_mark_app_valid_cancel_rollback();
    }

    tft.fillScreen(TFT_BLACK);

    cat.lastBlink = millis();
    // Status seeded on first updateCatStatus() call once NTP is synced
    // dirty flags are all true at declaration — first loop draws everything
}

void loop() {
    wm.process();
    ntpClient.update();

    unsigned long now = millis();
    if (peekUntilMs > 0 && now >= peekUntilMs) peekUntilMs = 0;  // peek expired

    {
        time_t epoch = ntpClient.getEpochTime();
        time_t utcCheck = epoch - (time_t)configMgr.config().utcOffsetSeconds;
        bool ntpSynced = utcCheck > 1000000000;  // sanity: must be a real NTP-synced time (post-2001)
        struct tm* tmNow = localtime(&epoch);
        int nowMinutes = tmNow->tm_hour * 60 + tmNow->tm_min;
        bool inSleepWindow = ntpSynced && isInSleepWindow(nowMinutes);
        asleep = inSleepWindow && peekUntilMs == 0;

        handleTouch();  // may start a peek if `asleep` was true

        bool sleepingNow = inSleepWindow && peekUntilMs == 0;  // re-check post-touch
        bool wasPeekingAsleep = peekingAsleep;
        peekingAsleep = inSleepWindow && peekUntilMs > 0;       // re-check post-touch, mirrors sleepingNow
        // Force a clean repaint on any peekingAsleep transition — covers the sleep
        // window itself ending mid-peek, which otherwise wouldn't force a redraw and
        // could leave the blanket/bear scene stuck on screen after waking.
        if (peekingAsleep != wasPeekingAsleep) dirty.animal = true;
        if (sleepingNow) {
            updateSleepScreen(now);
            delay(50);
            return;
        }
        if (sleepScreenActive) {
            sleepScreenActive = false;
            // Full clear: the sleep clock's last-drawn position may sit outside the
            // partial clear-rects the zone draws use, leaving stray digits behind otherwise.
            tft.fillScreen(TFT_BLACK);
            dirty.header = dirty.animal = dirty.picker = dirty.timerRow = true;  // clean full repaint on wake/peek
            dirty.animalBg = true;  // fillScreen just wiped the zone's backdrop too
        }
    }

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

    // Firmware update check — blocks the loop if it finds and applies an update
    // (see performUpdateCheck()); otherwise a quick network round-trip at most.
    if (now - lastUpdateCheck > UPDATE_CHECK_INTERVAL_MS) {
        lastUpdateCheck = now;
        performUpdateCheck();
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

    // Cat animation, hunger, boredom, and health status — frozen while peeking during
    // the sleep window so the sleeping scene stays calm and static (no blinking/hunger/
    // boredom/sick/thirst state changes).
    if (!peekingAsleep) {
        updateCatAnim();
        updateCatStatus();
        updateCatBoredom();
        updateCatHealth();
        updateCatThirst();
    }

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

    // Points flash — while the store has an unseen new item, toggle the points balance's
    // color every 500ms; repaint once more with the flash off the moment it's cleared
    // (i.e. the user just opened the store) so it doesn't linger mid-flash.
    {
        static unsigned long lastFlash = 0;
        static bool wasNew = false;
        bool isNew = hasNewStoreItems();
        if (isNew && now - lastFlash >= 500) {
            lastFlash = now;
            pointsFlashOn = !pointsFlashOn;
            drawPoints();
        } else if (!isNew && wasNew) {
            pointsFlashOn = false;
            drawPoints();
        }
        wasNew = isNew;
    }

    // Timer row refresh while running
    {
        static unsigned long lastTimerDraw = 0;
        bool activeAndRunning = (timerMode == TimerMode::Countdown) ? timerWidget.isRunning()
                                                                     : stopwatchWidget.isRunning();
        if (activeAndRunning && now - lastTimerDraw >= 500) {
            lastTimerDraw   = now;
            dirty.timerTick = true;
        }
    }

    if (dirty.header)             { drawHeader();      dirty.header     = false; dirty.headerTick = false; }
    else if (dirty.headerTick)   { drawHeaderTick();  dirty.headerTick = false; }
    if (dirty.animal)             { drawAnimal();                                dirty.animal = false; dirty.eyesOnly = false; dirty.hungerLines = false; dirty.zzzFx = false; }
    else if (dirty.eyesOnly)      { drawEyes(CAT_CX, CAT_CY, cat.eyeOpen);     dirty.eyesOnly = false; }
    else if (dirty.hungerLines)   { drawHungerLines(CAT_CX, CAT_CY, cat.rumbling); dirty.hungerLines = false; }
    else if (dirty.zzzFx)         { drawBoredomZzz(CAT_CX, CAT_CY, cat.napping);   dirty.zzzFx = false; }
    if (dirty.picker)        { drawPicker();                            dirty.picker    = false; }
    if (dirty.timerRow)      { drawTimerRow();  dirty.timerRow  = false; dirty.timerTick = false; }
    else if (dirty.timerTick){ drawTimerDigits(); dirty.timerTick = false; }

    delay(50);
}
