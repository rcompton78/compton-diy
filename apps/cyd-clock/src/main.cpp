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

// Gamification: lifetime XP / level system, layered on top of spendable `points`.
// XP is awarded 1:1 with points from the same 4 care actions and the store cheat; it
// never decreases. Level requires a gently-increasing amount of XP each step (not flat,
// not exponential): level L->L+1 costs LEVEL_XP_BASE + LEVEL_XP_STEP*(L-1) XP.
static constexpr uint32_t LEVEL_XP_BASE = 20;
static constexpr uint32_t LEVEL_XP_STEP = 10;
static constexpr uint32_t MILESTONE_LEVEL_INTERVAL = 5;   // bonus every 5 levels
static constexpr uint32_t MILESTONE_BONUS_POINTS   = 25;  // spendable points granted at each milestone

// Gamification: store item costs
static constexpr uint32_t STORE_COST_TEDDY    = 60;
static constexpr uint32_t STORE_COST_BUNNY    = 60;
static constexpr uint32_t STORE_COST_SQUIRREL = 150;
static constexpr uint32_t STORE_COST_PENGUIN  = 150;
static constexpr uint32_t STORE_COST_UNICORN  = 150;
static constexpr uint32_t STORE_COST_BLANKET  = 40;  // per blanket color
static constexpr uint32_t STORE_COST_ROOM_THEME = 40;  // per flat-color room theme, matches blanket pricing
static constexpr uint32_t STORE_COST_STARRY_NIGHT = 200;  // premium: has real art (moon + stars), not just a flat fill
static constexpr uint32_t STORE_COST_CAT_COLOR_SOLID   = 100;  // black, grey — flat recolor
static constexpr uint32_t STORE_COST_CAT_COLOR_PATTERN = 200;  // tabby, calico — real stripe/patch art

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
static constexpr uint16_t C_UNICORN      = TFT_WHITE;  // unicorn peeking out beside the head (white)
static constexpr uint16_t C_UNICORN_HORN = 0xFC18;  // unicorn horn (pink), matches C_PINK

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
    {"tangerine",    "Tangerine",    0xFD2A, 0xD3E5, "#FFA552"},  // warm orange blanket, burnt-orange fold trim
};
static constexpr int BLANKET_COLOR_COUNT = sizeof(BLANKET_COLORS) / sizeof(BLANKET_COLORS[0]);

// Pattern accent colors for the two patterned cat colors below — kept as named constants
// since both the catalog and the pattern-drawing functions reference them.
static constexpr uint16_t C_TABBY_BASE   = 0xD46A;  // warm tan/orange base fur
static constexpr uint16_t C_TABBY_STRIPE = 0x7A23;  // dark brown stripes
static constexpr uint16_t C_CALICO_ORANGE = C_FISH;  // ginger patches — reuses the treat-button orange
static constexpr uint16_t C_CALICO_BLACK  = 0x1082;  // black patches — same tone as the solid "black" cat, for the same C_DARK contrast reason

// Tabby/calico pattern art, layered on top of the base fill. Split into a head pass
// (called after the head fill, before eyes/nose/whiskers/mouth so those still paint
// cleanly on top of any overlap) and a body pass (called after body/paws/tail, so
// patches sit on the finished silhouette). First-pass geometry — expect to tune after
// seeing it on real hardware. Forward-declared here (like the stuffy draw functions
// below) so CAT_COLORS[] can reference them directly; defined later alongside drawCat(),
// since they need `tft`, declared further down.
static void drawTabbyHeadPattern(int cx, int cy);
static void drawTabbyBodyPattern(int cx, int cy);
static void drawCalicoHeadPattern(int cx, int cy);
static void drawCalicoBodyPattern(int cx, int cy);

// Cat color catalog — same purchase/equip model as blanket colors. White (C_CAT) is the
// default and always available, so it's not itself a catalog entry: an unowned/unequipped
// state (equippedCatColorIndex() == -1) simply falls back to C_CAT rather than needing a
// "free" bit. `id` is the stable identifier used in store/dressing-room form posts and
// persisted config; never reorder/reuse indices for a different color, since ownership is
// stored as a bitmask keyed by array index. `drawHeadPattern`/`drawBodyPattern` are nullptr
// for flat colors; tabby/calico layer real stripe/patch art on top of `fill` via drawCat().
struct CatColor {
    const char* id;
    const char* label;
    uint16_t fill;
    const char* webColor;  // CSS hex approximation of `fill`, for coloring its label in the config UI
    bool cuteEyes;  // smaller eyes with a higher pupil-to-sclera ratio, instead of the default wide-eyed look
    uint32_t cost;
    void (*drawHeadPattern)(int cx, int cy);
    void (*drawBodyPattern)(int cx, int cy);
};
static constexpr CatColor CAT_COLORS[] = {
    // Not literal 0x0000 — C_DARK (whiskers/mouth/closed-eye lines) is a charcoal
    // 0x2945, which would nearly vanish against pure black. This dark grey keeps those
    // details visible while still reading as "black cat".
    // webColor is #fff, not the near-black fill above — that's for the store/dressing-room
    // label text, which would be nearly invisible against the page's own dark background.
    {"black",  "Black",  0x1082, "#ffffff", false, STORE_COST_CAT_COLOR_SOLID,   nullptr, nullptr},
    {"grey",   "Grey",   0x8410, "#808080", true,  STORE_COST_CAT_COLOR_SOLID,   nullptr, nullptr},
    {"tabby",  "Tabby",  C_TABBY_BASE, "#c89050", false, STORE_COST_CAT_COLOR_PATTERN, drawTabbyHeadPattern,  drawTabbyBodyPattern},
    {"calico", "Calico", TFT_WHITE,    "#ffffff", false, STORE_COST_CAT_COLOR_PATTERN, drawCalicoHeadPattern, drawCalicoBodyPattern},
};
static constexpr int CAT_COLOR_COUNT = sizeof(CAT_COLORS) / sizeof(CAT_COLORS[0]);

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
static void drawUnicornPeeking(int cx, int cy, uint16_t accentColor);
static void drawUnicornFull(int cx, int cy, uint16_t accentColor);

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
    {"unicorn",  "White Unicorn", STORE_COST_UNICORN, drawUnicornPeeking,  drawUnicornFull},
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
    const char* webColor;  // CSS hex approximation of `bgColor`, brightened for legibility on the
                            // config UI's dark page background; nullptr for themes whose backdrop
                            // isn't a straight color (see DIY-42), so the label falls back to white
};
static constexpr RoomTheme ROOM_THEMES[] = {
    // Flat-color placeholders (DIY-38), picked to pair with the blanket palette. Moon /
    // fireplace themes with real art still land in a follow-up card.
    {"midnight", "Midnight", STORE_COST_ROOM_THEME, 0x18CE, drawFlatThemeBackground, "#5B7FBD"},  // deep navy — pairs with Dusty Blue
    {"twilight", "Twilight", STORE_COST_ROOM_THEME, 0x28C8, drawFlatThemeBackground, "#9B72CF"},  // deep plum — pairs with Lavender
    {"forest",   "Forest",   STORE_COST_ROOM_THEME, 0x1143, drawFlatThemeBackground, "#4CAF6D"},  // deep green — pairs with Apple Green
    {"rosewood", "Rosewood", STORE_COST_ROOM_THEME, 0x30A4, drawFlatThemeBackground, "#C2687D"},  // deep wine — pairs with Blush Pink
    {"amber",    "Amber",    STORE_COST_ROOM_THEME, 0x28E2, drawFlatThemeBackground, "#D9A441"},  // warm deep brown — pairs with Cream & Lemon Yellow
    {"starry_night", "Starry Night", STORE_COST_STARRY_NIGHT, TFT_BLACK, drawStarryNightBackground, nullptr},  // moon + stars on black — not a straight color, label stays white
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
bool setupPromptActive         = false;  // true once the first-run "complete setup at <ip>" screen has been painted this session
unsigned long forceSickDeadlineMs = 0;   // test-only: armed via /config, 0 = not armed, not persisted
unsigned long forceThirstDeadlineMs = 0; // test-only: armed via /config, 0 = not armed, not persisted

struct { bool header, animal, picker, timerRow, eyesOnly, timerTick, headerTick, hungerLines, zzzFx, animalBg; } dirty = {true, true, true, true, false, false, false, false, false, true};
bool pointsFlashOn = false;  // toggled every ~500ms while hasNewStoreItems(); read by drawPoints()
bool saleFlashOn = false;    // toggled every ~500ms while the black cat flash sale is active; read by drawSaleFlash()

// Level-up fireworks: a full-screen takeover (distinct from the small in-zone Celebrate
// animation) that plays once when awardXp() crosses a level boundary, with the milestone
// bonus (if any) flashing on top. Mirrors the sleepScreenActive/setupPromptActive pattern
// of owning the screen directly rather than going through the zone-scoped dirty flags.
struct {
    bool active            = false;
    unsigned long since     = 0;  // millis() when triggered
    unsigned long lastFrame = 0;  // millis() of last frame advance
    uint8_t frame           = 0;  // advances every FIREWORKS_FRAME_MS, drives burst radius/color
    uint32_t bonusPoints    = 0;  // milestone bonus earned this level-up, 0 if none
} fireworks;
static constexpr unsigned long FIREWORKS_DURATION_MS = 2500;
static constexpr unsigned long FIREWORKS_FRAME_MS    = 150;


// Forward declarations: resolves the cat's current equipped color index, its body/fill
// color (the equipped store color if owned, else white/C_CAT), and whether it uses the
// smaller "cute" eye style — all defined below alongside equippedBlanketIndex(). Declared
// here so drawEyes()/drawCat() can call them directly, since they're defined before the
// resolvers they depend on.
static int equippedCatColorIndex();
static uint16_t catBodyColor();
static bool catHasCuteEyes();

// Tabby/calico pattern art bodies (declared earlier alongside CAT_COLORS[]). Head pass
// runs after the head fill but before eyes/nose/whiskers/mouth, so those still paint
// cleanly on top of any overlap; body pass runs after body/paws/tail, so patches sit on
// the finished silhouette. First-pass geometry — expect to tune after real hardware.
static void drawTabbyHeadPattern(int cx, int cy) {
    // Cheek stripes only — no forehead "M" mark
    tft.drawLine(cx - 38, cy - 55, cx - 30, cy - 50, C_TABBY_STRIPE);
    tft.drawLine(cx + 38, cy - 55, cx + 30, cy - 50, C_TABBY_STRIPE);
}
static void drawTabbyBodyPattern(int cx, int cy) {
    // Body bands
    tft.fillRect(cx - 22, cy + 12, 44, 4, C_TABBY_STRIPE);
    tft.fillRect(cx - 22, cy + 26, 44, 4, C_TABBY_STRIPE);
    // Tail rings
    tft.fillRect(cx + 26, cy + 22, 12, 3, C_TABBY_STRIPE);
    tft.fillRect(cx + 26, cy + 36, 12, 3, C_TABBY_STRIPE);
}
static void drawCalicoHeadPattern(int cx, int cy) {
    tft.fillCircle(cx + 28, cy - 50, 12, C_CALICO_ORANGE);
    tft.fillCircle(cx - 26, cy - 30, 10, C_CALICO_BLACK);
}
static void drawCalicoBodyPattern(int cx, int cy) {
    tft.fillCircle(cx - 15, cy + 20, 12, C_CALICO_ORANGE);
    tft.fillCircle(cx + 12, cy + 35, 10, C_CALICO_BLACK);
}

// Draws just the eye shapes (sclera/pupil/glint, or a closed dash) into an already
// head-colored rect. Shared by drawEyes() (blink-only partial redraw) and drawCat()
// (full redraw) so the two can never drift out of sync. `cute` selects the grey cat's
// smaller eyes with a higher pupil-to-sclera ratio; the default/black look is unchanged.
static void drawEyeShapes(int cx, int cy, bool eyeOpen, bool cute) {
    if (!cute) {
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
        return;
    }
    if (eyeOpen) {
        tft.fillCircle(cx - 13, cy - 36, 7, TFT_WHITE);
        tft.fillCircle(cx + 13, cy - 36, 7, TFT_WHITE);
        tft.fillCircle(cx - 12, cy - 36, 4, TFT_BLACK);
        tft.fillCircle(cx + 14, cy - 36, 4, TFT_BLACK);
        tft.fillRect(cx - 15, cy - 40, 3, 3, TFT_WHITE);
        tft.fillRect(cx + 11, cy - 40, 3, 3, TFT_WHITE);
    } else {
        tft.fillRoundRect(cx - 21, cy - 39, 16, 6, 3, C_DARK);
        tft.fillRoundRect(cx +  5, cy - 39, 16, 6, 3, C_DARK);
    }
}

// ── Cat drawing ───────────────────────────────────────────────────────────────
static void drawEyes(int cx, int cy, bool eyeOpen) {
    tft.fillRect(cx - 28, cy - 50, 56, 26, catBodyColor());  // restore head colour before drawing eyes
    drawEyeShapes(cx, cy, eyeOpen, catHasCuteEyes());
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
    uint16_t col = catBodyColor();
    int colorIdx = equippedCatColorIndex();

    zoneFillRect(cx - 50, cy - 88, 100, 146);

    // Ears
    tft.fillTriangle(cx - 16, cy - 86, cx - 32, cy - 62, cx -  2, cy - 62, col);
    tft.fillTriangle(cx + 16, cy - 86, cx + 32, cy - 62, cx +  2, cy - 62, col);
    tft.fillTriangle(cx - 16, cy - 80, cx - 28, cy - 64, cx -  5, cy - 64, C_PINK);
    tft.fillTriangle(cx + 16, cy - 80, cx + 28, cy - 64, cx +  5, cy - 64, C_PINK);

    // Head (always the equipped cat color — hunger only affects body)
    tft.fillRoundRect(cx - 44, cy - 64, 88, 66, 20, col);
    if (colorIdx >= 0 && CAT_COLORS[colorIdx].drawHeadPattern) {
        CAT_COLORS[colorIdx].drawHeadPattern(cx, cy);
    }

    // Eyes
    drawEyeShapes(cx, cy, eyeOpen, catHasCuteEyes());

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
    tft.fillRoundRect(cx - 30, cy + 2, 60, 54, 15, col);

    // Paws
    tft.fillRoundRect(cx - 32, cy + 42, 24, 14, 7, col);
    tft.fillRoundRect(cx +  8, cy + 42, 24, 14, 7, col);
    for (int d = -4; d <= 4; d += 4) {
        tft.drawLine(cx - 20 + d, cy + 52, cx - 20 + d, cy + 56, C_DARK);
        tft.drawLine(cx + 20 + d, cy + 52, cx + 20 + d, cy + 56, C_DARK);
    }

    // Tail (right side)
    tft.fillRoundRect(cx + 26, cy + 18, 12, 36, 6, col);
    tft.fillRoundRect(cx + 14, cy + 50, 28, 10, 5, col);

    if (colorIdx >= 0 && CAT_COLORS[colorIdx].drawBodyPattern) {
        CAT_COLORS[colorIdx].drawBodyPattern(cx, cy);
    }
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

// Same resolution logic as equippedBlanketIndex(), for the cat color catalog.
static int equippedCatColorIndex() {
    uint8_t owned = configMgr.config().ownedCatColors;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedCatColor;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < CAT_COLOR_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Resolves the cat's current body/fill color — the equipped catalog color if owned, else
// C_CAT (white), the always-available default. Read wherever the cat sprite is drawn or
// erased, so purchasing/equipping a new color repaints correctly everywhere at once.
static uint16_t catBodyColor() {
    int idx = equippedCatColorIndex();
    return idx >= 0 ? CAT_COLORS[idx].fill : C_CAT;
}

// Whether the equipped cat color uses the smaller "cute" eye style — false (the
// default/big-eyed look) for white and any color that doesn't opt in.
static bool catHasCuteEyes() {
    int idx = equippedCatColorIndex();
    return idx >= 0 && CAT_COLORS[idx].cuteEyes;
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

// True whenever the store catalog has grown (a new stuffy, blanket color, room theme, or
// cat color shipped in a firmware update) since the last time the user opened the store
// page — cleared by handleConfigStoreGet(), which stamps the current catalog sizes as "seen".
static bool hasNewStoreItems() {
    return STUFFY_COUNT > configMgr.config().seenStuffyCount ||
           BLANKET_COLOR_COUNT > configMgr.config().seenBlanketColorCount ||
           ROOM_THEME_COUNT > configMgr.config().seenRoomThemeCount ||
           CAT_COLOR_COUNT > configMgr.config().seenCatColorCount;
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

// Shared ear/horn/head/snout/eyes/nose art reused by both unicorn variants below — same
// layout as drawTeddyHead() but with a pink horn between the ears, the unicorn's
// distinguishing feature. Horn is centered in x so it never overlaps the ears on either
// side, so draw order relative to them doesn't matter.
static void drawUnicornHead(int bx, int by, uint16_t snoutColor) {
    tft.fillCircle(bx - 6, by - 9, 4, C_UNICORN);   // left ear
    tft.fillCircle(bx + 6, by - 9, 4, C_UNICORN);   // right ear
    tft.fillCircle(bx,     by,     8, C_UNICORN);   // head
    tft.fillTriangle(bx - 2, by - 6, bx + 2, by - 6, bx, by - 18, C_UNICORN_HORN);  // horn
    tft.fillCircle(bx,     by + 3, 3, snoutColor);  // snout
    tft.fillCircle(bx - 3, by - 2, 1, C_DARK);   // left eye
    tft.fillCircle(bx + 3, by - 2, 1, C_DARK);   // right eye
    tft.fillCircle(bx,     by + 1, 1, C_DARK);   // nose
}

// White unicorn peeking out beside the head, tucked into the blanket's top edge —
// only reads correctly when the blanket is also owned to tuck behind.
static void drawUnicornPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    drawUnicornHead(bx, by, accentColor);
}

// Full-body white unicorn sitting beside the cat — used when the unicorn is owned without
// the blanket, since there's no blanket edge to tuck a lone head behind. Tail matches the
// horn's pink rather than the white body, so the silhouette still reads as distinct from
// the plain white body/belly patch.
static void drawUnicornFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawUnicornHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_UNICORN);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);              // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_UNICORN);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_UNICORN);            // right foot
    tft.fillCircle(bx, by + 33, 3, C_UNICORN_HORN);           // pink tail
}

// Deeply-closed, sleepy eyes for the sleep-window peek — thinner and gently curled at
// the outer corners than drawEyes()'s blink-style dash, so the cat reads as actually
// asleep rather than mid-blink.
static void drawSleepyEyes(int cx, int cy) {
    tft.fillRect(cx - 28, cy - 50, 56, 26, catBodyColor());  // restore head colour before drawing eyes
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
    uint16_t col = show ? C_RUMBLE : catBodyColor();  // erase with body colour to avoid flicker
    tft.drawFastHLine(bx,      by,      14, col);
    tft.drawFastHLine(bx +  2, by +  8, 10, col);
    tft.drawFastHLine(bx,      by + 16, 14, col);
}

static void drawBoredomZzz(int cx, int cy, bool show) {
    // "Zz" cloud, entirely outside the head/ears — one to the right, two cascading
    // up-and-out to the left. The right-side one sits low, below the level/points/sale
    // badge column (which now runs from ANIMAL_Y to ANIMAL_Y+70ish), so they never collide.
    static const int8_t dx[] = { 70, -70, -70 };
    static const int8_t dy[] = { -3, -40, -60 };
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

// Cumulative XP required to reach `level` (level 1 = 0 XP). Gentle arithmetic-increment
// curve: each level costs LEVEL_XP_STEP more XP than the last, starting from
// LEVEL_XP_BASE — not flat, not exponential. Closed form of the arithmetic series.
static uint32_t xpForLevel(uint32_t level) {
    if (level <= 1) return 0;
    uint32_t n = level - 1;
    return n * (2 * LEVEL_XP_BASE + (n - 1) * LEVEL_XP_STEP) / 2;
}

// Highest level reachable with `xp` total lifetime XP.
static uint32_t levelForXp(uint32_t xp) {
    uint32_t level = 1;
    while (xpForLevel(level + 1) <= xp) level++;
    return level;
}

static uint32_t xpToNextLevel(uint32_t xp) {
    return xpForLevel(levelForXp(xp) + 1) - xp;
}

// Defined near loop() below, alongside updateFireworksAnim() — forward-declared here so
// awardXp() (further down) can kick it off on a level-up, mirroring isBlackCatSaleActive()'s
// forward-declaration for drawSaleFlash() just below.
static void triggerFireworks(uint32_t bonusPoints);

// Right-hand badge column x-start: past x=164 so it doesn't clip the cat's head
// (drawCat()'s head is a fillRoundRect(cx-44, cy-64, 88, 66, ...), reaching x=164 at
// its right edge — the ear triangles alone only reach x=152, but the head box behind
// them is wider and is what actually gets clipped if the column starts too far left),
// plus a couple more px so it also clears the whisker line tips (reach x=166).
static constexpr int BADGE_COL_X = 168;
static constexpr int BADGE_COL_W = 240 - BADGE_COL_X;

// Level badge — a small "medal" with the level number centered inside, rather than
// plain "Lv N" text, for a friendlier achievement-badge look. Sits at the top of the
// right-hand column, just under the header's weather text. Right-aligned to the same
// x=234 edge points/sale's text hugs, rather than centered in the column — the column's
// width only exists to give points/sale's variable-width text room to grow leftward, and
// centering the fixed-size medal within it made the medal look off (shifted left of
// where the eye expects it, under the text above/below it).
static constexpr int LEVEL_MEDAL_R = 13;                    // outer rim radius
static constexpr int LEVEL_MEDAL_CX = 234 - LEVEL_MEDAL_R;  // right edge lands on 234, matching drawPoints()/drawSaleFlash()
static constexpr int LEVEL_MEDAL_CY = ANIMAL_Y + LEVEL_MEDAL_R + 3;
static constexpr int LEVEL_BADGE_H  = LEVEL_MEDAL_R * 2 + 8;  // clear-rect height, own slot at the column's top

// Medal color cycles through the rainbow every MILESTONE_LEVEL_INTERVAL levels (a new
// color each tier: 1-5 red, 6-10 orange, ... ), then settles on gold once the rainbow is
// exhausted and stays gold until more colors are added here.
static constexpr uint16_t MEDAL_RAINBOW[] = {
    TFT_RED, 0xFD20 /*orange*/, TFT_YELLOW, TFT_GREEN, TFT_BLUE, 0x4810 /*indigo*/, 0x780F /*violet*/,
};
static constexpr int MEDAL_RAINBOW_N = sizeof(MEDAL_RAINBOW) / sizeof(MEDAL_RAINBOW[0]);
static constexpr uint16_t MEDAL_GOLD = 0xFEA0;

// Web-page equivalents of MEDAL_RAINBOW/MEDAL_GOLD above, same order/tiers, so the
// /config/badges page's medal matches the on-device one's color. Kept as a separate
// array (CSS hex, not RGB565) rather than converting at request time — simpler than a
// bit-twiddling RGB565->RGB888 conversion for a handful of fixed colors.
static const char* const MEDAL_RAINBOW_WEB[] = {
    "#ff3b30", "#ff9500", "#ffcc00", "#34c759", "#0a5fff", "#4b0082", "#8e44ad",
};
static constexpr const char* MEDAL_GOLD_WEB = "#ffd700";

// Medal color tier for a given level — shared by drawLevelBadge() (device) and
// medalColorHexForLevel() (web), so both change color at the same moment. Uses `level`
// directly (not level-1): the color changes exactly ON the milestone level itself
// (5, 10, 15, ...), coinciding with the milestone-bonus fireworks, rather than one level
// later — levels 1-4 are tier 0, level 5 (the first milestone) jumps straight to tier 1,
// levels 5-9 stay tier 1, level 10 jumps to tier 2, and so on.
static uint32_t medalTierForLevel(uint32_t level) {
    return level / MILESTONE_LEVEL_INTERVAL;
}

// Same tier logic as drawLevelBadge(), for the /config/badges page's medal.
static const char* medalColorHexForLevel(uint32_t level) {
    uint32_t tier = medalTierForLevel(level);
    return (tier < (uint32_t)MEDAL_RAINBOW_N) ? MEDAL_RAINBOW_WEB[tier] : MEDAL_GOLD_WEB;
}

// Darkens an RGB565 color by ~25%, for the medal's bevel face relative to its rim —
// works for any hue, not just gold, since the rainbow tiers all reuse this.
static uint16_t darkenRgb565(uint16_t c) {
    uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (uint8_t)(r * 3 / 4);
    g = (uint8_t)(g * 3 / 4);
    b = (uint8_t)(b * 3 / 4);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static void drawLevelBadge() {
    zoneFillRect(BADGE_COL_X, ANIMAL_Y, BADGE_COL_W, LEVEL_BADGE_H);
    uint32_t level = levelForXp(configMgr.config().totalXp);
    uint32_t tier = medalTierForLevel(level);
    uint16_t tierColor = (tier < (uint32_t)MEDAL_RAINBOW_N) ? MEDAL_RAINBOW[tier] : MEDAL_GOLD;
    uint16_t rim  = tierColor;
    uint16_t face = darkenRgb565(tierColor);
    tft.fillCircle(LEVEL_MEDAL_CX, LEVEL_MEDAL_CY, LEVEL_MEDAL_R, rim);
    tft.fillCircle(LEVEL_MEDAL_CX, LEVEL_MEDAL_CY, LEVEL_MEDAL_R - 3, face);
    char lvlBuf[8];
    snprintf(lvlBuf, sizeof(lvlBuf), "%lu", (unsigned long)level);
    tft.setTextColor(TFT_BLACK, face);
    // MC_DATUM (middle-center) lets TFT_eSPI center the digits itself, both horizontally
    // and vertically, using the font's own metrics — reset to the default TL_DATUM
    // (top-left) afterward since every other draw* function in this file assumes it.
    tft.setTextDatum(MC_DATUM);
    tft.drawString(lvlBuf, LEVEL_MEDAL_CX, LEVEL_MEDAL_CY, 2);
    tft.setTextDatum(TL_DATUM);
}

// Points balance — stacked below the level medal in the same right-hand column. Flashes
// between yellow and cyan while hasNewStoreItems() is true, as a hint to check the store
// for something new.
static void drawPoints() {
    char ptsBuf[16];
    snprintf(ptsBuf, sizeof(ptsBuf), "%lu pts", (unsigned long)configMgr.config().points);
    zoneFillRect(BADGE_COL_X, ANIMAL_Y + LEVEL_BADGE_H, BADGE_COL_W, 20);
    uint16_t color = (hasNewStoreItems() && pointsFlashOn) ? C_NEW_ITEM : TFT_YELLOW;
    tft.setTextColor(color, zoneBgColor());
    int ptsWidth = tft.textWidth(ptsBuf, 2);
    tft.drawString(ptsBuf, 234 - ptsWidth, ANIMAL_Y + LEVEL_BADGE_H + 4, 2);
}

// Forward declaration: true during the black cat flash sale window, defined below
// alongside isInSleepWindow(). Declared here so drawSaleFlash() can call it directly.
static bool isBlackCatSaleActive();

// "SALE" banner at the bottom of the column, below the points balance, flashing
// red/yellow while the black cat flash sale is active. Same clear-rect x-start as the
// other two badges (past the head's right edge).
static void drawSaleFlash() {
    zoneFillRect(BADGE_COL_X, ANIMAL_Y + LEVEL_BADGE_H + 20, BADGE_COL_W, 16);
    if (!isBlackCatSaleActive()) return;
    const char* label = "SALE!";
    uint16_t color = saleFlashOn ? TFT_RED : TFT_YELLOW;
    tft.setTextColor(color, zoneBgColor());
    int w = tft.textWidth(label, 2);
    tft.drawString(label, 234 - w, ANIMAL_Y + LEVEL_BADGE_H + 22, 2);
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

    // Right-hand badge column (level/points/sale) — drawn after drawCat()/drawSleepingCat()
    // since both repaint their own clear rect internally and would otherwise wipe this out.
    drawLevelBadge();
    drawPoints();
    drawSaleFlash();

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

// Awards XP 1:1 alongside points. XP is a separate, monotonically-increasing lifetime
// counter — never spent, never reduced by store purchases. Also grants a one-time bonus
// to the spendable points balance for every MILESTONE_LEVEL_INTERVAL level crossed by
// this single award (a big cheat grant can cross more than one milestone at once) that
// hasn't already paid out a bonus before — tracked via highestMilestoneLevel, so resetting
// totalXp back to 0 (see handleConfigBadgesResetPost()) and re-leveling can't re-farm the
// same bonus. Kicks off the level-up fireworks animation, passing along the total bonus
// points earned this award so it can flash alongside it. Does not call configMgr.save()
// itself — callers already save after their own mutation.
static void awardXp(uint32_t amount) {
    if (amount == 0) return;
    uint32_t oldLevel = levelForXp(configMgr.config().totalXp);
    configMgr.config().totalXp += amount;
    uint32_t newLevel = levelForXp(configMgr.config().totalXp);
    uint32_t bonusEarned = 0;
    for (uint32_t lvl = oldLevel + 1; lvl <= newLevel; lvl++) {
        if (lvl % MILESTONE_LEVEL_INTERVAL == 0 && lvl > configMgr.config().highestMilestoneLevel) {
            bonusEarned += MILESTONE_BONUS_POINTS;
            configMgr.config().highestMilestoneLevel = lvl;
        }
    }
    if (bonusEarned > 0) configMgr.config().points += bonusEarned;
    if (newLevel > oldLevel) {
        // Skip the routine small in-zone Celebrate animation (bob + sparkles) for this
        // touch — the care-action handler that called us (if any) already set cat.mood
        // to Celebrate just before this; overriding it back to Idle here means the cat
        // will simply be resting once the fireworks takeover ends, instead of playing a
        // second, smaller celebration on top of/after the big one.
        cat.mood = CatMood::Idle;
        dirty.animal = true;  // redraw the level badge
        triggerFireworks(bonusEarned);
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
            awardXp(pointsAwarded);
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
    if (cat.mood == CatMood::Celebrate && now - cat.since > 3000) {
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

// ── Black cat flash sale (2026-07-18 3pm – 2026-07-20 7am device local time) ───────
// A one-time promo window, not a recurring daily discount — DIY-54 found the previous
// daily-recurring version (a bare 3pm-10pm time-of-day check with no date bound) had no
// way to actually end: every day it silently turned back on at 3pm forever. Expressed as
// a single absolute start/end instant instead, so it runs once and stays off afterward.
// YYYYMMDDHHMM, compared as a plain int64 — needs 64 bits since a value like
// 202607181500 overflows a 32-bit long on this platform.
static constexpr int64_t BLACK_CAT_SALE_START = 202607181500LL;  // 2026-07-18 15:00
static constexpr int64_t BLACK_CAT_SALE_END   = 202607200700LL;  // 2026-07-20 07:00
static constexpr uint32_t BLACK_CAT_SALE_PRICE = 50;  // half off STORE_COST_CAT_COLOR_SOLID

// Self-contained (fetches its own local time) so it can be called from the store HTTP
// handlers as well as loop(), not just places that already have nowMinutes in scope.
static bool isBlackCatSaleActive() {
    time_t epoch = ntpClient.getEpochTime();
    time_t utcCheck = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utcCheck <= 1000000000) return false;  // not NTP-synced yet — sanity check, matches loop()'s
    struct tm* t = localtime(&epoch);
    int64_t now = (int64_t)(t->tm_year + 1900) * 100000000LL
                + (int64_t)(t->tm_mon + 1)     * 1000000LL
                + (int64_t)t->tm_mday          * 10000LL
                + (int64_t)t->tm_hour          * 100LL
                + (int64_t)t->tm_min;
    return now >= BLACK_CAT_SALE_START && now < BLACK_CAT_SALE_END;
}

// Level-up fireworks — a full-screen takeover distinct from the small in-zone Celebrate
// animation (drawSparkles()/CatMood::Celebrate), reserved for the rarer, bigger moment of
// actually leveling up. Modeled on updateSleepScreen()'s full fillScreen() ownership below:
// this owns the whole 240x320 screen directly for FIREWORKS_DURATION_MS rather than going
// through the zone-scoped dirty flags, then hands the screen back via a forced full repaint.
struct FireworksBurst { int cx, cy; };
static constexpr FireworksBurst FIREWORKS_BURSTS[] = {
    { 60, 100}, {180, 130}, {120, 220},
};
static constexpr int FIREWORKS_BURST_N = 3;

static void triggerFireworks(uint32_t bonusPoints) {
    fireworks.active      = true;
    fireworks.since       = millis();
    fireworks.lastFrame   = 0;
    fireworks.frame       = 0;
    fireworks.bonusPoints = bonusPoints;
    tft.fillScreen(TFT_BLACK);
}

// Called every loop() iteration while fireworks.active; advances/draws a frame at most
// every FIREWORKS_FRAME_MS, and ends the takeover (forcing a full UI repaint) once
// FIREWORKS_DURATION_MS has elapsed. Takes its own fresh millis() reading rather than a
// timestamp from the caller — loop() captures `now` once at the top of the iteration,
// before handleTouch() runs, but triggerFireworks() (called from inside handleTouch(),
// via awardXp()) stamps fireworks.since with a *later* millis() value than that. Using
// the caller's stale `now` here made `now - fireworks.since` underflow (unsigned
// arithmetic) into a huge value on the very first check, ending the animation
// immediately after just the initial fillScreen() — the "one flash, no celebration" bug.
static void updateFireworksAnim() {
    unsigned long now = millis();
    if (now - fireworks.since > FIREWORKS_DURATION_MS) {
        fireworks.active = false;
        // Full clear + force every zone to repaint, same recovery used when the sleep
        // screen / setup prompt takeover ends.
        tft.fillScreen(TFT_BLACK);
        dirty.header = dirty.animal = dirty.picker = dirty.timerRow = true;
        dirty.animalBg = true;
        return;
    }
    if (now - fireworks.lastFrame < FIREWORKS_FRAME_MS) return;
    fireworks.lastFrame = now;
    fireworks.frame++;

    tft.fillScreen(TFT_BLACK);  // redraw-over-black each tick — same cost as sparkle erase/redraw, at screen scale
    static const uint16_t burstColors[] = {TFT_YELLOW, TFT_CYAN, 0xFD20, TFT_GREEN, TFT_MAGENTA, TFT_RED};
    for (int b = 0; b < FIREWORKS_BURST_N; b++) {
        int cx = FIREWORKS_BURSTS[b].cx, cy = FIREWORKS_BURSTS[b].cy;
        uint16_t color = burstColors[(fireworks.frame + b * 2) % 6];
        int radius = 6 + ((fireworks.frame + b * 3) % 8) * 6;  // grows and cycles per burst
        tft.fillCircle(cx, cy, 3, color);
        for (int i = 0; i < 8; i++) {
            float angle = (i * 45.0f) * PI / 180.0f;
            int ex = cx + (int)(radius * cosf(angle));
            int ey = cy + (int)(radius * sinf(angle));
            tft.drawLine(cx, cy, ex, ey, color);
        }
    }

    if (fireworks.bonusPoints > 0 && (fireworks.frame % 2 == 0)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "+%lu pts!", (unsigned long)fireworks.bonusPoints);
        uint16_t color = (fireworks.frame % 4 == 0) ? TFT_YELLOW : TFT_WHITE;
        tft.setTextColor(color, TFT_BLACK);
        tft.drawCentreString(buf, CX, 150, 4);
    }
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
.medal{width:64px;height:64px;border-radius:50%;display:flex;align-items:center;justify-content:center;
  font-size:1.6rem;font-weight:bold;color:#000;margin:0 auto 14px;box-shadow:inset 0 0 0 5px rgba(0,0,0,.25)}
)css";

// WiFiManager's own generated pages (root menu, wifi scan/connect, info, exit, the
// stock /update upload page) use its built-in light-blue theme by default. WiFiManager
// inserts setCustomHeadElement()'s HTML right after its own <style> block in <head>
// (see getHTTPHead() in WiFiManager.cpp), so CONFIG_STYLE's generic body/button/input
// selectors — same rules the /config/* pages use — win the cascade and restyle those
// pages to match, with no per-page duplication needed. Must be a persistent buffer:
// WiFiManager stores the raw `const char*` it's given, not a copy, so passing a
// temporary String's c_str() here would leave it pointing at freed memory.
static const String WM_CUSTOM_HEAD = "<style>" + String(FPSTR(CONFIG_STYLE)) + "</style>";

// Served at "/" once setup is complete — see handleRootPage()'s comment for why this
// exists instead of falling back to WiFiManager's own root menu. Mirrors the `menu[]`
// array passed to wm.setMenu() in runWiFiManager() (wifi/info/exit), plus a link into the
// Cat Control Panel itself.
static const char ROOT_MENU_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel</title>
<style>%%STYLE%%</style>
</head><body>
<h2>Cat Control Panel</h2>
<a class="nav" href="/config">Cat Control Panel</a>
<a class="nav" href="/wifi">Configure WiFi</a>
<a class="nav" href="/info">Info</a>
<a class="nav" href="/exit">Exit</a>
</body></html>
)html";

static const char CONFIG_HOME_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/">&larr; Main Menu</a>
<h2>Cat Control Panel</h2>
<a class="nav" href="/config/cat">Cat</a>
<a class="nav" href="/config/city">City (weather &amp; timezone)</a>
<a class="nav" href="/config/store">Store</a>
<a class="nav" href="/config/dress">Dressing Room</a>
<a class="nav" href="/config/badges">Badges</a>
<a class="nav" href="/config/backup">Backup</a>
<a class="nav" href="/config/update">Firmware Update</a>
</body></html>
)html";

static const char CONFIG_BADGES_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel &middot; Badges</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Badges</h2>
%%MSG%%
<div class="medal" style="background:%%MEDALCOLOR%%">%%LEVEL%%</div>
<p>Lifetime XP: <strong>%%XP%%</strong><br>
XP to next level: <strong>%%XPTONEXT%%</strong></p>
<p class="dim">This is lifetime experience, separate from your spendable Points balance
&mdash; visit the Store to see and spend Points.</p>
<p>Bonus: +%%BONUS%% points every %%INTERVAL%% levels.<br>
Milestones reached: <strong>%%MILESTONES%%</strong></p>

<h3>Danger Zone</h3>
<p>Resets your lifetime XP and level back to 0/1. This does not affect your spendable
Points balance or anything you've already bought in the Store. This cannot be undone.</p>
<form method="POST" action="/save-config/badges-reset">
<input type="text" name="confirm" placeholder="type reset to confirm">
<button type="submit" style="width:100%;margin-top:8px">Reset Badge Progress</button>
</form>

</body></html>
)html";

static const char CONFIG_SETUP_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Setup</title>
<style>%%STYLE%%</style>
</head><body>
<h2>Welcome!</h2>
%%MSG%%
<form method="POST" action="/save-config/setup">

<div id="step1">
<h3>Pick your cat's color</h3>
%%COLOR_OPTIONS%%
<button type="button" style="width:100%;margin-top:8px"
  onclick="document.getElementById('step1').style.display='none';document.getElementById('step2').style.display='block'">Next</button>
</div>

<div id="step2" style="display:none">
<h3>Name your cat</h3>
<label>Name</label>
<input name="name" id="name" maxlength="16" placeholder="Biscuit">
<button type="button" style="width:100%;margin-top:8px"
  onclick="document.getElementById('step2').style.display='none';document.getElementById('step3').style.display='block'">Next</button>
</div>

<div id="step3" style="display:none">
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

<button type="submit" style="width:100%;margin-top:8px">Finish &amp; go to the store</button>
</div>

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

<h3>Danger Zone</h3>
<p>Wipes cat name/schedule, city/timezone, and all store/points state back to defaults. This cannot be undone.</p>
<form method="POST" action="/save-config/reset">
<input type="text" name="confirm" placeholder="type reset to confirm">
<button type="submit" style="width:100%;margin-top:8px">Reset Everything</button>
</form>

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

<h3>Manual upload</h3>
<p>Upload a <code>.bin</code> file directly from your browser, e.g. one downloaded from a GitHub release you don't want to wait for.</p>
<p><a href="/update">Upload firmware manually &rarr;</a></p>
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
<form id="cheatForm" method="POST" action="/save-config/cheat" style="display:none;margin-top:8px">
<div class="row">
<input type="number" name="amount" placeholder="points to add">
<button type="submit">Add</button>
</div>
</form>
%%MSG%%
<div class="balance">Points: <strong>%%POINTS%%</strong></div>

<h3>Cat Colors</h3>
%%CAT_COLOR_ITEMS%%

<h3>Stuffies (night only)</h3>
%%STUFFY_ITEMS%%

<h3>Blankets (night only)</h3>
%%BLANKET_ITEMS%%

<h3>Room Themes</h3>
%%ROOM_THEME_ITEMS%%

<script>
// Easter egg: tap the "Store" heading 7 times in a row (no other tap in between)
// to reveal a text field + button that grants that many points.
(function(){
    var taps = 0;
    var title = document.getElementById('storeTitle');
    var cheat = document.getElementById('cheatForm');
    document.addEventListener('click', function(e){
        if (e.target === title) {
            taps++;
            if (taps >= 7) { cheat.style.display = 'block'; }
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

<h3>Cat Colors</h3>
%%CAT_COLOR_OPTIONS%%

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

// Every config/store page renders live device state (points, owned items, flash-sale
// status, ...) fresh per request — but without an explicit no-store, a browser is free to
// serve a stale cached copy indefinitely instead of re-fetching (e.g. a tab left open
// during the black cat sale would keep showing the SALE badge long after it actually ended).
static void sendHtmlPage(const String& page) {
    wm.server->sendHeader("Cache-Control", "no-store");
    wm.server->send(200, "text/html", page);
}

static void handleConfigHome() {
    if (!configMgr.config().setupComplete) {
        wm.server->sendHeader("Location", "/setup");
        wm.server->send(302, "text/plain", "");
        return;
    }
    String page = String(FPSTR(CONFIG_HOME_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    sendHtmlPage(page);
}

// First-run setup wizard: reached via the on-device "complete setup at <ip>" screen (see
// drawSetupPrompt()) once Wi-Fi is connected but configMgr.config().setupComplete is still
// false. Only offers the free-tier solid colors (STORE_COST_CAT_COLOR_SOLID) — tabby/calico
// stay store-only purchases, same as the "maybe" colors DIY-48's card described.
static void handleSetupGet() {
    String page = String(FPSTR(CONFIG_SETUP_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));

    String colorOptions = "<label class='pick'><input type='radio' name='catColor' value='none' checked> "
                           "<span>White</span></label>";
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (CAT_COLORS[i].cost != STORE_COST_CAT_COLOR_SOLID) continue;
        colorOptions += "<label class='pick'><input type='radio' name='catColor' value='";
        colorOptions += CAT_COLORS[i].id;
        colorOptions += "'> <span style='color:" + String(CAT_COLORS[i].webColor) + "'>"
                       + String(CAT_COLORS[i].label) + "</span></label>";
    }
    page.replace("%%COLOR_OPTIONS%%", colorOptions);
    page.replace("%%LAT%%", String(configMgr.config().latitude,  4));
    page.replace("%%LON%%", String(configMgr.config().longitude, 4));
    page.replace("%%UTC%%", String(configMgr.config().utcOffsetSeconds));

    String msg = "";
    if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "city") msg = "<div class='banner err'>Please check your City/Timezone values.</div>";
        else msg = "<div class='banner err'>Please enter a name (max 16 characters).</div>";
    }
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

static void handleSetupPost() {
    String color = wm.server->arg("catColor");
    String name = wm.server->arg("name");
    name.trim();
    if (name.length() == 0) name = "Biscuit";
    bool nameOk = name.length() <= 16;
    for (size_t i = 0; nameOk && i < name.length(); ++i) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7F) nameOk = false;
    }
    if (!nameOk) {
        wm.server->sendHeader("Location", "/setup?err=name");
        wm.server->send(302, "text/plain", "");
        return;
    }

    float lat = wm.server->arg("lat").toFloat();
    float lon = wm.server->arg("lon").toFloat();
    int   utc = wm.server->arg("utc").toInt();
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f || utc < -50400 || utc > 50400) {
        wm.server->sendHeader("Location", "/setup?err=city");
        wm.server->send(302, "text/plain", "");
        return;
    }

    configMgr.config().catName = name;
    configMgr.config().points  = 70;
    if (color == "none") {
        configMgr.config().equippedCatColor = EQUIP_NONE;
    } else {
        int idx = -1;
        for (int i = 0; i < CAT_COLOR_COUNT; i++) {
            if (color == CAT_COLORS[i].id && CAT_COLORS[i].cost == STORE_COST_CAT_COLOR_SOLID) { idx = i; break; }
        }
        if (idx >= 0) {
            configMgr.config().ownedCatColors  |= (1 << idx);
            configMgr.config().equippedCatColor = (uint8_t)idx;
        } else {
            configMgr.config().equippedCatColor = EQUIP_NONE;
        }
    }
    configMgr.config().latitude         = lat;
    configMgr.config().longitude        = lon;
    configMgr.config().utcOffsetSeconds = utc;
    configMgr.config().setupComplete = true;
    configMgr.save();

    // Same side effects handleConfigCityPost() applies when latitude/longitude/utc change.
    ntpClient.setTimeOffset(utc);
    lastWeatherFetch = millis() - WEATHER_UPDATE_INTERVAL_MS - 1;
    dirty.header = dirty.animal = dirty.animalBg = dirty.picker = dirty.timerRow = true;
    wm.server->sendHeader("Location", "/config/store?welcome=1");
    wm.server->send(302, "text/plain", "");
}

// Registered unconditionally (see runWiFiManager()) so "/" always resolves to something,
// regardless of setup state at any point in the device's lifetime — WiFiManager registers
// its own "/" handler too, but WM_WebServer matches handlers in registration order and
// stops at the first match, and this one is registered before any of WiFiManager's own
// server->on() calls (see the registration comment in runWiFiManager()), so it always wins
// and WiFiManager's own root handler never actually runs. Sends first-time visitors
// straight into the wizard; once setup is complete, renders a minimal menu covering the
// same links WiFiManager's own root menu would (wifi/info/exit, per the `menu[]` array
// below), since handleRoot() on the WiFiManager instance itself is protected and can't be
// called directly to fall back to it.
static void handleRootPage() {
    if (!configMgr.config().setupComplete) {
        wm.server->sendHeader("Location", "/setup");
        wm.server->send(302, "text/plain", "");
        return;
    }
    String page = String(FPSTR(ROOT_MENU_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    sendHtmlPage(page);
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
    sendHtmlPage(page);
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
    sendHtmlPage(page);
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
        configMgr.config().seenCatColorCount     = (uint8_t)CAT_COLOR_COUNT;
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
        String themeColor = ROOM_THEMES[i].webColor ? String(ROOM_THEMES[i].webColor) : "#fff";
        roomThemeItems += "<div class='item'><span style='color:" + themeColor + "'>" + String(ROOM_THEMES[i].label) + "</span>";
        roomThemeItems += storeItemAction(ROOM_THEMES[i].id, owned, ROOM_THEMES[i].cost, points);
        roomThemeItems += "</div>\n";
    }
    page.replace("%%ROOM_THEME_ITEMS%%", roomThemeItems);
    // White isn't a CAT_COLORS[] entry (it's the always-available default, equipped via
    // EQUIP_NONE — see the comment above that catalog) but the wizard and Dress page both
    // list it as an explicit choice, so show it here too rather than have it look missing.
    String catColorItems = "<div class='item'><span>Cat - White</span><span class='owned'>Owned</span></div>\n";
    bool blackCatOnSale = isBlackCatSaleActive();
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        bool owned = configMgr.config().ownedCatColors & (1 << i);
        bool onSale = blackCatOnSale && strcmp(CAT_COLORS[i].id, "black") == 0;
        uint32_t itemCost = onSale ? BLACK_CAT_SALE_PRICE : CAT_COLORS[i].cost;
        catColorItems += "<div class='item'><span style='color:" + String(CAT_COLORS[i].webColor) + "'>Cat - " + String(CAT_COLORS[i].label) + "</span>";
        if (onSale) catColorItems += " <span style='color:#ff4444;font-weight:bold'>\xF0\x9F\x94\xA5 SALE</span>";
        catColorItems += storeItemAction(CAT_COLORS[i].id, owned, itemCost, points);
        catColorItems += "</div>\n";
    }
    page.replace("%%CAT_COLOR_ITEMS%%", catColorItems);
    String msg = "";
    if (wm.server->hasArg("welcome")) {
        msg = "<div class='banner ok'>Welcome! Here's 70 points to get started.</div>";
    } else if (wm.server->hasArg("cheat")) {
        msg = "<div class='banner ok'>Points added!</div>";
    } else if (wm.server->hasArg("saved")) {
        msg = "<div class='banner ok'>Purchase complete.</div>";
    } else if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "funds") msg = "<div class='banner err'>Not enough points.</div>";
        else if (err == "owned") msg = "<div class='banner err'>You already own that item.</div>";
        else if (err == "save") msg = "<div class='banner err'>Purchase failed to save — please try again.</div>";
    }
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

static void handleConfigBadgesGet() {
    uint32_t xp = configMgr.config().totalXp;
    uint32_t level = levelForXp(xp);
    String page = String(FPSTR(CONFIG_BADGES_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    String msg = "";
    if (wm.server->hasArg("reset")) {
        msg = "<div class='banner ok'>Badge progress has been reset.</div>";
    } else if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "resetConfirm") msg = "<div class='banner err'>Type \"reset\" exactly to confirm.</div>";
        else if (err == "resetSave") msg = "<div class='banner err'>Reset failed to save — please try again.</div>";
    }
    page.replace("%%MSG%%", msg);
    page.replace("%%LEVEL%%", String(level));
    page.replace("%%MEDALCOLOR%%", String(medalColorHexForLevel(level)));
    page.replace("%%XP%%", String(xp));
    page.replace("%%XPTONEXT%%", String(xpToNextLevel(xp)));
    page.replace("%%MILESTONES%%", String(level / MILESTONE_LEVEL_INTERVAL));
    page.replace("%%BONUS%%", String(MILESTONE_BONUS_POINTS));
    page.replace("%%INTERVAL%%", String(MILESTONE_LEVEL_INTERVAL));
    sendHtmlPage(page);
}

// Zeros lifetime XP (and therefore the derived level, back to 1) without touching the
// separate spendable Points balance or anything already bought in the Store — this only
// rewinds badge/medal progress. Lives in the Badges page's own "Danger Zone". Requires
// typing "reset" to guard against an accidental submit, same pattern as
// handleConfigResetPost(). Deliberately does NOT reset highestMilestoneLevel — otherwise
// re-leveling after this reset would re-pay milestone bonus points already earned, an
// unlimited points farm via repeated resets (see awardXp()).
static void handleConfigBadgesResetPost() {
    if (wm.server->arg("confirm") != "reset") {
        wm.server->sendHeader("Location", "/config/badges?err=resetConfirm");
        wm.server->send(302, "text/plain", "");
        return;
    }
    configMgr.config().totalXp = 0;
    if (!configMgr.save()) {
        wm.server->sendHeader("Location", "/config/badges?err=resetSave");
        wm.server->send(302, "text/plain", "");
        return;
    }
    dirty.animal = true;  // redraw the on-device medal at level 1
    wm.server->sendHeader("Location", "/config/badges?reset=1");
    wm.server->send(302, "text/plain", "");
}

// Easter egg: grants an arbitrary number of points, reached only via the hidden field
// revealed by tapping the store heading 7 times in a row (see CONFIG_STORE_HTML's inline
// script). Non-positive or unreasonably large amounts are silently ignored rather than
// erroring — this is a debug cheat, not a validated form.
static void handleConfigStoreCheatPost() {
    long amount = wm.server->arg("amount").toInt();
    if (amount > 0 && amount <= 1000000) {
        configMgr.config().points += (uint32_t)amount;
        awardXp((uint32_t)amount);
        configMgr.save();
    }
    wm.server->sendHeader("Location", "/config/store?cheat=1");
    wm.server->send(302, "text/plain", "");
}

// Wipes the entire config — cat name/schedule, city/timezone, and all gamification
// state — back to AppConfig's defaults. Lives in the Backup page's "Danger Zone" (see
// CONFIG_BACKUP_HTML). Requires typing "reset" to guard against an accidental submit.
static void handleConfigResetPost() {
    if (wm.server->arg("confirm") != "reset") {
        wm.server->sendHeader("Location", "/config/backup?err=resetConfirm");
        wm.server->send(302, "text/plain", "");
        return;
    }
    configMgr.resetToDefaults();
    if (!configMgr.save()) {
        wm.server->sendHeader("Location", "/config/backup?err=resetSave");
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
    wm.server->sendHeader("Location", "/config/backup?reset=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigBackupGet() {
    String page = String(FPSTR(CONFIG_BACKUP_HTML));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    page.replace("%%EXPORT_JSON%%", htmlEscape(configMgr.exportBackupJson()));
    String msg = "";
    if (wm.server->hasArg("saved")) {
        msg = "<div class='banner ok'>Backup restored.</div>";
    } else if (wm.server->hasArg("reset")) {
        msg = "<div class='banner ok'>Everything has been reset.</div>";
    } else if (wm.server->hasArg("err")) {
        String err = wm.server->arg("err");
        if (err == "empty") msg = "<div class='banner err'>Paste or choose a backup file first.</div>";
        else if (err == "parse") msg = "<div class='banner err'>That doesn't look like a valid backup file.</div>";
        else if (err == "save") msg = "<div class='banner err'>Restore failed to save — please try again.</div>";
        else if (err == "resetConfirm") msg = "<div class='banner err'>Type \"reset\" exactly to confirm.</div>";
        else if (err == "resetSave") msg = "<div class='banner err'>Reset failed to save — please try again.</div>";
    }
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
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
    sendHtmlPage(page);
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
        sendHtmlPage(page);
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
    int stuffyIdx = -1, blanketIdx = -1, roomThemeIdx = -1, catColorIdx = -1;
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
            if (roomThemeIdx >= 0) {
                cost = ROOM_THEMES[roomThemeIdx].cost;
                alreadyOwned = configMgr.config().ownedRoomThemes & (1 << roomThemeIdx);
            } else {
                for (int i = 0; i < CAT_COLOR_COUNT; i++) {
                    if (item == CAT_COLORS[i].id) { catColorIdx = i; break; }
                }
                if (catColorIdx < 0) {
                    wm.server->send(400, "text/plain", "Unknown item");
                    return;
                }
                cost = (strcmp(CAT_COLORS[catColorIdx].id, "black") == 0 && isBlackCatSaleActive())
                    ? BLACK_CAT_SALE_PRICE : CAT_COLORS[catColorIdx].cost;
                alreadyOwned = configMgr.config().ownedCatColors & (1 << catColorIdx);
            }
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
    } else if (catColorIdx >= 0) {
        configMgr.config().ownedCatColors |= (1 << catColorIdx);
        configMgr.config().equippedCatColor = catColorIdx;  // newly bought color becomes equipped
    } else {
        configMgr.config().ownedStuffies |= (1 << stuffyIdx);
        configMgr.config().equippedStuffy = stuffyIdx;  // newly bought stuffy becomes equipped
    }
    if (!configMgr.save()) {
        // Roll back in-memory state since persistence failed.
        configMgr.config().points += cost;
        if (blanketIdx >= 0) configMgr.config().ownedBlanketColors &= ~(1 << blanketIdx);
        else if (roomThemeIdx >= 0) configMgr.config().ownedRoomThemes &= ~(1 << roomThemeIdx);
        else if (catColorIdx >= 0) configMgr.config().ownedCatColors &= ~(1 << catColorIdx);
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
            String themeColor = ROOM_THEMES[i].webColor ? String(ROOM_THEMES[i].webColor) : "#fff";
            themeOptions += "> <span style='color:" + themeColor + "'>"
                          + String(ROOM_THEMES[i].label) + "</span></label>";
        }
    }
    page.replace("%%ROOM_THEME_OPTIONS%%", themeOptions);

    uint8_t ownedCatColors = configMgr.config().ownedCatColors;
    int equippedCatColorIdx = equippedCatColorIndex();
    String catColorOptions = "";
    catColorOptions += "<label class='pick'><input type='radio' name='catColor' value='none'";
    if (equippedCatColorIdx < 0) catColorOptions += " checked";
    catColorOptions += "> White</label>";
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (!(ownedCatColors & (1 << i))) continue;
        catColorOptions += "<label class='pick'><input type='radio' name='catColor' value='";
        catColorOptions += CAT_COLORS[i].id;
        catColorOptions += "'";
        if (i == equippedCatColorIdx) catColorOptions += " checked";
        catColorOptions += "> <span style='color:" + String(CAT_COLORS[i].webColor) + "'>"
                          + String(CAT_COLORS[i].label) + "</span></label>";
    }
    page.replace("%%CAT_COLOR_OPTIONS%%", catColorOptions);

    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Saved.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
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

    String catColorId = wm.server->arg("catColor");
    if (catColorId == "none") {
        configMgr.config().equippedCatColor = EQUIP_NONE;  // falls back to white, catBodyColor()
        changed = true;
    } else if (catColorId.length() > 0) {
        int idx = -1;
        for (int i = 0; i < CAT_COLOR_COUNT; i++) {
            if (catColorId == CAT_COLORS[i].id) { idx = i; break; }
        }
        if (idx < 0 || !(configMgr.config().ownedCatColors & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        configMgr.config().equippedCatColor = idx;
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

static void drawSetupPrompt() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString("Almost there!", CX, 70, 2);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("Complete setup at:", CX, 110, 2);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawCentreString(WiFi.localIP().toString().c_str(), CX, 150, 4);
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

    OtaCheckResult result = otaClient.checkForUpdate(OTA_MANIFEST_URL, FIRMWARE_VERSION, OTA_ASSET_NAME);
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
    wm.setClass("invert");  // dark-mode base for elements CONFIG_STYLE doesn't target (.msg, dt/dd, etc.)
    wm.setCustomHeadElement(WM_CUSTOM_HEAD.c_str());
    wm.setCustomMenuHTML("<form action='/config' method='get'><button>Cat Control Panel</button></form><br/>");
    // "update" (WiFiManager's stock browser-upload OTA page) is intentionally left out
    // of this menu — it's surfaced instead from /config/update, alongside the DIY-41
    // auto-update controls. The route itself (server->on(R_update, ...)) is registered
    // unconditionally by WiFiManager regardless of menu membership, so /update still works.
    const char* menu[] = {"wifi", "custom", "info", "sep", "exit"};
    wm.setMenu(menu, 5);
    // Register every custom route from inside setWebServerCallback rather than after
    // wm.autoConnect() returns. The callback fires right after WiFiManager (re)creates its
    // webserver object, before any of its own server->on() calls (see
    // WiFiManager::setupHTTPServer()) — including the (re)creation that happens inside
    // autoConnect()'s blocking startConfigPortal() when there are no saved credentials. If
    // registration happened after autoConnect() instead, none of these routes — /setup
    // included — would exist while a first-time user is connected to the AP and the portal
    // is still blocking, since autoConnect() doesn't return until the portal exits.
    // WM_WebServer also matches handlers in registration order and stops at the first match,
    // so "/" being registered first here wins over WiFiManager's own root handler (see
    // handleRootPage()'s comment for why that's unconditional, not just while setup is
    // incomplete).
    wm.setWebServerCallback([]() {
        wm.server->on("/",                     HTTP_GET,  handleRootPage);
        wm.server->on("/config",               HTTP_GET,  handleConfigHome);
        wm.server->on("/setup",                HTTP_GET,  handleSetupGet);
        wm.server->on("/config/cat",           HTTP_GET,  handleConfigCatGet);
        wm.server->on("/config/city",          HTTP_GET,  handleConfigCityGet);
        wm.server->on("/config/store",         HTTP_GET,  handleConfigStoreGet);
        wm.server->on("/config/dress",         HTTP_GET,  handleConfigDressGet);
        wm.server->on("/config/badges",        HTTP_GET,  handleConfigBadgesGet);
        wm.server->on("/config/backup",        HTTP_GET,  handleConfigBackupGet);
        wm.server->on("/config/backup/export", HTTP_GET,  handleConfigBackupExportGet);
        wm.server->on("/config/update",        HTTP_GET,  handleConfigUpdateGet);
        wm.server->on("/config/update/check",  HTTP_POST, handleConfigUpdateCheckPost);
        wm.server->on("/save-config/setup",        HTTP_POST, handleSetupPost);
        wm.server->on("/save-config/cat",          HTTP_POST, handleConfigCatPost);
        wm.server->on("/save-config/city",         HTTP_POST, handleConfigCityPost);
        wm.server->on("/save-config/store",        HTTP_POST, handleConfigStorePost);
        wm.server->on("/save-config/cheat",        HTTP_POST, handleConfigStoreCheatPost);
        wm.server->on("/save-config/reset",        HTTP_POST, handleConfigResetPost);
        wm.server->on("/save-config/badges-reset", HTTP_POST, handleConfigBadgesResetPost);
        wm.server->on("/save-config/dress",        HTTP_POST, handleConfigDressPost);
        wm.server->on("/save-config/backup",       HTTP_POST, handleConfigBackupPost);
        wm.server->on("/save-config/update",       HTTP_POST, handleConfigUpdatePost);
    });
    wm.autoConnect("CYD-Clock");
    wm.startWebPortal();
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

        // Level-up fireworks — a full-screen takeover, so it fully owns the loop iteration
        // while active and skips the sleep screen / setup prompt / normal dirty-flag redraw
        // below. If the sleep window kicks in mid-animation, just cancel silently rather than
        // fighting for the screen — the level/bonus points were already recorded by awardXp(),
        // only the celebratory animation is skipped; nothing else is lost.
        if (fireworks.active) {
            if (sleepingNow) {
                fireworks.active = false;
            } else {
                updateFireworksAnim();
                delay(50);
                return;
            }
        }

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

    // First-run setup: hold on the "complete setup at <ip>" screen until the wizard (cat
    // color + name) has been finished — mirrors the sleepScreenActive pattern above, painting
    // once per transition rather than every loop iteration.
    if (!configMgr.config().setupComplete) {
        if (!setupPromptActive) {
            drawSetupPrompt();
            setupPromptActive = true;
        }
        delay(50);
        return;
    }
    if (setupPromptActive) {
        setupPromptActive = false;
        tft.fillScreen(TFT_BLACK);
        dirty.header = dirty.animal = dirty.animalBg = dirty.picker = dirty.timerRow = true;
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

    // Sale flash — while the black cat flash sale is active, toggle the "SALE!" banner's
    // color every 500ms; repaint once more with the flash off the moment the sale window
    // ends so it doesn't linger.
    {
        static unsigned long lastSaleFlash = 0;
        static bool wasSaleActive = false;
        bool saleActive = isBlackCatSaleActive();
        if (saleActive && now - lastSaleFlash >= 500) {
            lastSaleFlash = now;
            saleFlashOn = !saleFlashOn;
            drawSaleFlash();
        } else if (!saleActive && wasSaleActive) {
            saleFlashOn = false;
            drawSaleFlash();
        }
        wasSaleActive = saleActive;
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
