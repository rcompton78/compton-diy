#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
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
static constexpr uint32_t STORE_COST_SNOWMAN  = 150;
static constexpr uint32_t STORE_COST_PIKACHU  = 300;  // top tier: licensed-character premium, priced above the squirrel/penguin/unicorn/snowman 150 tier
static constexpr uint32_t STORE_COST_EEVEE    = 300;  // same top tier as Pikachu — also a licensed-character premium
static constexpr uint32_t STORE_COST_BLANKET  = 40;  // per blanket color
static constexpr uint32_t STORE_COST_ROOM_THEME = 40;  // per flat-color room theme, matches blanket pricing
static constexpr uint32_t STORE_COST_STARRY_NIGHT = 200;  // premium: has real art (moon + stars), not just a flat fill
static constexpr uint32_t STORE_COST_SIX_SEVEN = 200;  // premium: has real art (rotated digit pairs), not just a flat fill
static constexpr uint32_t STORE_COST_SKY = 200;  // premium: has real art (clouds + rainbow), not just a flat fill
static constexpr uint32_t STORE_COST_CAT_COLOR_SOLID   = 100;  // black, grey — flat recolor
static constexpr uint32_t STORE_COST_CAT_COLOR_PATTERN = 200;  // tabby, calico — real stripe/patch art
static constexpr uint32_t STORE_COST_ACCESSORY_BOW = 50;
static constexpr uint32_t STORE_COST_ACCESSORY_GLASSES = 50;  // matches bow pricing — same "flat recolor-tier" accessory
static constexpr uint32_t STORE_COST_RIGHT_ARM_SLOT = 200;  // one-time unlock, not per-stuffy
static constexpr uint32_t STORE_COST_HOCKEY_STICK = 100;
static constexpr uint32_t STORE_COST_MAGIC_WAND = 100;
// Legendary tier (COM-382): the top-of-store price shared by every legendary item (COM-375 to
// COM-378 reuse it too), just above the Pikachu/Eevee 300 tier. Any item priced at or above it
// gets the gold "LEGENDARY" store tag — see appendStoreItemTags() — so keep every non-legendary price
// below it.
static constexpr uint32_t STORE_COST_LEGENDARY = 350;

// Touch calibration — print "Touch: x= y=" from serial to tune
static constexpr int TX_MIN = 300, TX_MAX = 3800;
static constexpr int TY_MIN = 300, TY_MAX = 3800;
static constexpr unsigned long TOUCH_DEBOUNCE_MS = 350;

// Palette
static constexpr uint16_t C_CAT   = TFT_WHITE;
static constexpr uint16_t C_PINK  = 0xFD1A;  // rose pink — also the nose color, so this brightens both
static constexpr uint16_t C_BOW_MAGENTA = 0xF81F;  // deliberately distinct from C_PINK (the
                                                    // inner-ear color) so the bow reads as a
                                                    // separate object rather than blending in
static constexpr uint16_t C_BOW_HOT_PINK  = 0xF8B2;  // brighter/truer pink than C_BOW_MAGENTA
static constexpr uint16_t C_BOW_PURPLE    = 0x939B;  // medium purple
static constexpr uint16_t C_BOW_BABY_BLUE = 0x8E7E;  // baby blue
// Below: DIY-107 bow/blanket color parity — matches the six blanket colors that didn't
// already have a bow counterpart, reusing each blanket's own `base` value (see
// BLANKET_COLORS below) so the two slots look identical when equipped together.
static constexpr uint16_t C_BOW_DUSTY_BLUE    = 0x4BB6;  // matches the Dusty Blue blanket
static constexpr uint16_t C_BOW_CREAM         = 0xFFFA;  // matches the Cream blanket
static constexpr uint16_t C_BOW_LEMON_YELLOW  = 0xF6CB;  // matches the Lemon Yellow blanket
static constexpr uint16_t C_BOW_APPLE_GREEN   = 0xC711;  // matches the Apple Green blanket
static constexpr uint16_t C_BOW_TANGERINE     = 0xFD2A;  // matches the Tangerine blanket
static constexpr uint16_t C_BOW_FLAMINGO_PINK = 0xFD16;  // matches the Flamingo Pink blanket
static constexpr uint16_t C_GLASSES_RIM  = 0xFC18;  // pink rim (same pink family as the unicorn horn)
static constexpr uint16_t C_GLASSES_LENS = 0x2945;  // dark tinted lens — same charcoal as C_DARK
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
static constexpr uint16_t C_UNICORN_HORN = 0xFC18;  // unicorn horn (pink) — kept as its own constant
                                                     // rather than reusing C_PINK, so retuning one
                                                     // doesn't shift the other
static constexpr uint16_t C_SNOWMAN        = 0xDEFB;  // snowman peeking out beside the head (pale snow-blue, distinct from plain TFT_WHITE)
static constexpr uint16_t C_SNOWMAN_COAL   = 0x0000;  // snowman hat/eyes/buttons (coal black)
static constexpr uint16_t C_SNOWMAN_CARROT = 0xFD20;  // snowman nose (orange) — same hex as C_FISH/C_PENGUIN_BEAK by
                                                       // coincidence, not intentional reuse; kept as its own constant
                                                       // so retuning one doesn't silently shift the other two
static constexpr uint16_t C_PIKACHU        = 0xFFE0;  // pikachu peeking out beside the head (bright yellow)
static constexpr uint16_t C_PIKACHU_CHEEK  = 0xF800;  // pikachu's red cheek patches — fixed, not accent-colored, since it's the character's signature feature
static constexpr uint16_t C_PIKACHU_MARK   = 0x0000;  // pikachu ear tips and tail-bolt tip (black)
static constexpr uint16_t C_EEVEE       = 0xC4AC;  // eevee peeking out beside the head (warm tan/brown fur, ~#c4966a)
static constexpr uint16_t C_EEVEE_DARK  = 0x8AE7;  // eevee's ear tips — a noticeably darker brown than the body fur (~#8c5d3c)
static constexpr uint16_t C_EEVEE_LIGHT = 0xEED7;  // eevee's neck ruff, inner ears, and tail tip — light beige, brightened further (~#eeddc0) so it reads as clearly distinct from the C_EEVEE body brown on-device rather than blending in; fixed like pikachu's cheeks, not accent-colored
static constexpr uint16_t C_EEVEE_BLACK = 0x0000;  // eevee's eyes, nose, and mouth (COM-266) — pure black like C_PIKACHU_MARK, distinct from the C_EEVEE_DARK ear-tip brown and from the shared charcoal C_DARK used for other stuffies' faces
static constexpr uint16_t C_PARTY_HAT      = 0x939B;  // party hat cone — same purple as C_BOW_PURPLE
static constexpr uint16_t C_PARTY_HAT_TRIM = 0xF6CB;  // party hat base band/pompom — same yellow as C_BOW_LEMON_YELLOW
// Bold, saturated colors rather than pastels — the birthday theme's own room-theme backdrop
// (0xFCF3, pale pink, see drawBirthdayBackground()) is what these render against once the
// theme applies both, so anything close to that pale pink washes out (learned the hard way
// from the cupcake stuffy this replaced, DIY-108).
static constexpr uint16_t C_BALLOON_GLASSES_RED  = TFT_RED;     // one balloon lens
static constexpr uint16_t C_BALLOON_GLASSES_BLUE = 0x001F;      // the other balloon lens (true blue)

// Halloween theme-week colors (COM-379). The Haunted Night backdrop is dark purple, so the
// same DIY-108 contrast lesson applies in reverse: nothing that has to read against the sky
// may be a dark purple/black without an outline (hence the witch hat's lavender rim), and the
// bright pieces (moon, pumpkin, candy corn, ghost) are saturated, flat fills with no fine detail.
static constexpr uint16_t C_HAUNTED_SKY      = 0x2888;  // deep purple sky (~#2a1245)
static constexpr uint16_t C_HAUNTED_STAR     = 0xCDDD;  // faint lavender stars (~#c8b8e8)
static constexpr uint16_t C_HAUNTED_MOON     = 0xFCC3;  // big orange moon (~#ff9a1f)
static constexpr uint16_t C_HAUNTED_CRATER   = 0xE3C2;  // darker orange moon craters (~#e07a10)
static constexpr uint16_t C_HAUNTED_HILL     = 0x1044;  // near-black hill along the floor (~#140b20)
static constexpr uint16_t C_PUMPKIN          = 0xFBC0;  // jack-o'-lantern skin, candy-corn middle band (~#ff7a00)
static constexpr uint16_t C_PUMPKIN_RIDGE    = 0xD2E0;  // jack-o'-lantern ridges (~#d45f00)
static constexpr uint16_t C_PUMPKIN_STEM     = 0x3BE5;  // jack-o'-lantern stem (~#3a7d2c)
static constexpr uint16_t C_CANDLE_GLOW      = 0xFF09;  // lit jack-o'-lantern face, witch-hat buckle (~#ffe14d)
static constexpr uint16_t C_GHOST            = 0xF79F;  // Oct 31 ghost (~#f2f2ff)

// Hogwarts at Night colors (COM-378). The sky doubles as the theme's bgColor, so the lake along
// the floor (behind the cat's name) is the same flat navy and text erases leave no boxes there.
static constexpr uint16_t C_HOGWARTS_SKY    = 0x1129;  // navy sky and lake (~#10244a)
static constexpr uint16_t C_HOGWARTS_CASTLE = 0x0000;  // black castle silhouette
static constexpr uint16_t C_HOGWARTS_WINDOW = 0xFE68;  // warm lit window (~#ffcc44)
static constexpr uint16_t C_HOGWARTS_GLINT  = 0xA3C5;  // dim gold window reflection on the lake (~#a07828)
static constexpr uint16_t C_HOGWARTS_TAIL   = 0x8DDF;  // shooting-star tail (~#8fb8ff)
static constexpr uint16_t C_WITCH_HAT        = 0x0000;  // witch hat cone/brim (black)
static constexpr uint16_t C_WITCH_HAT_RIM    = 0x9B5A;  // 1px lavender outline so the black hat reads on the sky and on a black cat (~#9b6bd6)
static constexpr uint16_t C_WITCH_HAT_BAND   = 0xFC40;  // witch hat band, candy-corn temple arms (~#ff8a00)
static constexpr uint16_t C_CANDY_CORN_TOP   = 0xFE83;  // candy-corn lens wide yellow band (~#ffd21f)

// Legendary Harry Potter items (COM-382). The Sorting Hat is a patched brown with a near-black
// outline, so it reads on pale furs and pale themes; its mid-brown fill is what separates it from a
// black cat and the dark themes. The crest's house colors are saturated flat fills inside a C_DARK
// outline, the same contrast recipe as the candy-corn lenses above.
static constexpr uint16_t C_SORTING_HAT       = 0x8B07;  // hat body (~#8a6038)
static constexpr uint16_t C_SORTING_HAT_PATCH = 0x51C3;  // shaded flank and brim fold (~#56391f)
static constexpr uint16_t C_SORTING_HAT_LINE  = 0x28E2;  // outline, crumple and face creases (~#2b1d12)
static constexpr uint16_t C_CREST_RED    = 0xA8C4;  // Gryffindor quarter (~#ae1820)
static constexpr uint16_t C_CREST_GREEN  = 0x1BC7;  // Slytherin quarter (~#1c7a3c)
static constexpr uint16_t C_CREST_BLUE   = 0x2298;  // Ravenclaw quarter (~#2350c0)
static constexpr uint16_t C_CREST_YELLOW = 0xF623;  // Hufflepuff quarter (~#f5c518)
static constexpr uint16_t C_CREST_GOLD   = 0xED86;  // centre plate behind the "H" (~#e8b030)

// Toy catalog colors (DIY-110) — first entry is the mini hockey stick.
static constexpr uint16_t C_HOCKEY_SHAFT = 0xA145;  // wood-tone shaft (tan/brown)
static constexpr uint16_t C_HOCKEY_TAPE  = 0x0000;  // tape color on a light background (black);
                                                     // flips to white on a dark background — see
                                                     // drawHockeyStickHeld()'s tapeColor computation.
                                                     // Shared by both taped regions (grip + blade).
static constexpr uint16_t C_WAND_SHAFT = 0xA145;    // same wood-tone shaft as the hockey stick
static constexpr uint16_t C_WAND_TIP   = 0xFEA0;    // star tip (gold/yellow)

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
    {"dusty_blue",    "Dusty Blue",    0x4BB6, 0xD69A, "#6b8cae"},  // cozy dusty-blue blanket, warm cream fold trim
    {"cream",         "Cream",         0xFFFA, 0xCD51, "#e8d9b5"},  // soft cream blanket, tan fold trim
    {"blush_pink",    "Blush Pink",    0xF619, 0xDB92, "#e6a8bc"},  // blush pink blanket, deeper rose fold trim
    {"lavender",      "Lavender",      0xB3FB, 0xE69E, "#b57edc"},  // lavender blanket, pale lilac fold trim
    {"lemon_yellow",  "Lemon Yellow",  0xF6CB, 0xD502, "#F7D959"},  // Bambu PLA Lemon Yellow blanket, mustard-gold fold trim
    {"apple_green",   "Apple Green",   0xC711, 0x7D2A, "#C2E189"},  // Bambu PLA Apple Green blanket, deeper leaf-green fold trim
    {"tangerine",     "Tangerine",     0xFD2A, 0xD3E5, "#FFA552"},  // warm orange blanket, burnt-orange fold trim
    {"flamingo_pink", "Flamingo Pink", 0xFD16, 0xBBD1, "#FCA3B7"},  // vibrant flamingo-pink blanket, deeper rose fold trim
    // Below: DIY-107 bow/blanket color parity — matches the four bow accessory colors that
    // didn't already have a blanket counterpart, using each bow's own base color as `base`
    // (see C_BOW_* above) so the two slots look identical when equipped together.
    {"magenta",       "Magenta",       0xF81F, 0xA014, "#FF00FF"},  // matches the Magenta bow, deeper magenta fold trim
    {"hot_pink_blanket", "Hot Pink",   0xF8B2, 0xA06B, "#FF1493"},  // matches the Hot Pink bow, deeper rose fold trim; id suffixed "_blanket" since "hot_pink" is already the cat color's id (assertStoreIdsUnique() requires globally unique ids)
    {"purple",        "Purple",        0x939B, 0x5A51, "#9370DB"},  // matches the Purple bow, deeper purple fold trim
    {"baby_blue",     "Baby Blue",     0x8E7E, 0x5413, "#89CFF0"},  // matches the Baby Blue bow, deeper blue fold trim
};
static constexpr int BLANKET_COLOR_COUNT = sizeof(BLANKET_COLORS) / sizeof(BLANKET_COLORS[0]);
static_assert(BLANKET_COLOR_COUNT <= 16, "ownedBlanketColors bitmask is uint16_t");

// Pattern accent colors for the two patterned cat colors below — kept as named constants
// since both the catalog and the pattern-drawing functions reference them.
static constexpr uint16_t C_TABBY_BASE   = 0xD46A;  // warm tan/orange base fur
static constexpr uint16_t C_TABBY_STRIPE = 0x7A23;  // dark brown stripes
static constexpr uint16_t C_CALICO_ORANGE = C_FISH;  // ginger patches — reuses the treat-button orange
static constexpr uint16_t C_CALICO_BLACK  = 0x1082;  // black patches — same tone as the solid "black" cat, for the same C_DARK contrast reason

// Two solid pink shades, shipped as separate purchasable colors rather than one — kept
// as their own constants rather than reusing C_PINK/C_BOW_HOT_PINK, so retuning one doesn't
// shift the others.
static constexpr uint16_t C_CAT_PINK_SOFT = 0xFE7A;  // light pastel pink fur (~#ffd1dc)
static constexpr uint16_t C_CAT_PINK_HOT  = 0xFB56;  // hot pink fur (~#ff69b4)

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
    {"black",    "Black",    0x1082,          "#ffffff", false, STORE_COST_CAT_COLOR_SOLID,   nullptr,               nullptr},
    {"grey",     "Grey",     0x8410,          "#808080", true,  STORE_COST_CAT_COLOR_SOLID,   nullptr,               nullptr},
    {"tabby",    "Tabby",    C_TABBY_BASE,    "#c89050", false, STORE_COST_CAT_COLOR_PATTERN, drawTabbyHeadPattern,  drawTabbyBodyPattern},
    {"calico",   "Calico",   TFT_WHITE,       "#ffffff", false, STORE_COST_CAT_COLOR_PATTERN, drawCalicoHeadPattern, drawCalicoBodyPattern},
    {"pink",     "Pink",     C_CAT_PINK_SOFT, "#ffd1dc", false, STORE_COST_CAT_COLOR_SOLID,   nullptr,               nullptr},
    {"hot_pink", "Hot Pink", C_CAT_PINK_HOT,  "#ff69b4", false, STORE_COST_CAT_COLOR_SOLID,   nullptr,               nullptr},
};
static constexpr int CAT_COLOR_COUNT = sizeof(CAT_COLORS) / sizeof(CAT_COLORS[0]);
static_assert(CAT_COLOR_COUNT <= CAT_NAME_SLOTS, "increase ConfigManager's CAT_NAME_SLOTS to fit CAT_COLORS");

// Forward-declared for the same reason as the tabby/calico pattern functions above —
// ACCESSORIES[] needs to reference these before drawCat() (and `tft`) are declared. All four
// share one shape (drawBowShape(), defined alongside these) and differ only by color.
static void drawBowMagenta(int cx, int cy);
static void drawBowHotPink(int cx, int cy);
static void drawBowPurple(int cx, int cy);
static void drawBowBabyBlue(int cx, int cy);
static void drawBowDustyBlue(int cx, int cy);
static void drawBowCream(int cx, int cy);
static void drawBowLemonYellow(int cx, int cy);
static void drawBowAppleGreen(int cx, int cy);
static void drawBowTangerine(int cx, int cy);
static void drawBowFlamingoPink(int cx, int cy);

// Forward-declared for the same reason as the bow functions above. Unlike the bows, this one
// isn't store-purchasable — see isStoreAccessory() below.
static void drawPartyHat(int cx, int cy);
static void drawWitchHat(int cx, int cy);
static void drawSortingHat(int cx, int cy);

// Accessory catalog — same purchase/equip model as cat colors, but layered independently
// on top of whichever cat color is equipped (an accessory works with any fur color). `id` is
// the stable identifier used in store/dressing-room form posts and persisted config; never
// reorder/reuse indices, since ownership is stored as a bitmask keyed by array index. Note
// "bow"'s label is "Magenta Bow", not "Pink Bow" — id stays as originally shipped (it's the
// persisted/bitmask key) even though the store-facing name changed once "Hot Pink" was added
// as the brighter, truer pink.
struct Accessory {
    const char* id;
    const char* label;
    uint32_t cost;
    const char* webColor;  // CSS hex approximation, for coloring its label in the config UI
    void (*draw)(int cx, int cy);  // paints the accessory onto the cat sprite
};
static constexpr Accessory ACCESSORIES[] = {
    {"bow",           "Magenta Bow",   STORE_COST_ACCESSORY_BOW, "#FF00FF", drawBowMagenta},
    {"bow_hot_pink",  "Hot Pink Bow",  STORE_COST_ACCESSORY_BOW, "#FF1493", drawBowHotPink},
    {"bow_purple",    "Purple Bow",    STORE_COST_ACCESSORY_BOW, "#9370DB", drawBowPurple},
    {"bow_baby_blue", "Baby Blue Bow", STORE_COST_ACCESSORY_BOW, "#89CFF0", drawBowBabyBlue},
    // Below: DIY-107 bow/blanket color parity — matches the six blanket colors that didn't
    // already have a bow counterpart.
    {"bow_dusty_blue",    "Dusty Blue Bow",    STORE_COST_ACCESSORY_BOW, "#6b8cae", drawBowDustyBlue},
    {"bow_cream",         "Cream Bow",         STORE_COST_ACCESSORY_BOW, "#e8d9b5", drawBowCream},
    {"bow_lemon_yellow",  "Lemon Yellow Bow",  STORE_COST_ACCESSORY_BOW, "#F7D959", drawBowLemonYellow},
    {"bow_apple_green",   "Apple Green Bow",   STORE_COST_ACCESSORY_BOW, "#C2E189", drawBowAppleGreen},
    {"bow_tangerine",     "Tangerine Bow",     STORE_COST_ACCESSORY_BOW, "#FFA552", drawBowTangerine},
    {"bow_flamingo_pink", "Flamingo Pink Bow", STORE_COST_ACCESSORY_BOW, "#FCA3B7", drawBowFlamingoPink},
    // Below: theme-week exclusives (DIY-108), at the fixed ACCESSORY_IDX_* indices below. Never
    // store-purchasable (store rendering/purchase code skips them via isStoreAccessory()); owned
    // only for the duration of an active theme week, granted/revoked by
    // applyThemeWeekCosmetics()/revertThemeWeekCosmetics().
    {"party_hat", "Party Hat", 0, "#9370DB", drawPartyHat},
    {"witch_hat", "Witch Hat", 0, "#FF8A00", drawWitchHat},  // Halloween (COM-379)
    // Below: store-purchasable again (COM-382). Appended after the theme-week block rather than
    // inserted before it: ownership is a bitmask keyed by index, so shifting the party/witch hat
    // down a slot would hand their bits to a different item on any device (or backup) that owned
    // one mid-theme-week. Store code skips the theme-week entries with isStoreAccessory() instead
    // of assuming the purchasable entries are a contiguous prefix.
    {"sorting_hat", "Sorting Hat", STORE_COST_LEGENDARY, "#C8955A", drawSortingHat},
};
static constexpr int ACCESSORY_COUNT = sizeof(ACCESSORIES) / sizeof(ACCESSORIES[0]);
static_assert(ACCESSORY_COUNT <= 16, "ownedAccessories bitmask is uint16_t");
// Catalog index of each theme-week-exclusive accessory, for applyThemeWeekCosmetics()/
// revertThemeWeekCosmetics(). Fixed, not derived from the catalog size, since store-purchasable
// entries now also sit after them (the Sorting Hat, COM-382). A future theme's hat is appended
// with cost 0 and gets its own fixed index here.
static constexpr int ACCESSORY_IDX_PARTY_HAT = 10;
static constexpr int ACCESSORY_IDX_WITCH_HAT = 11;
static_assert(ACCESSORY_COUNT > ACCESSORY_IDX_WITCH_HAT, "theme-week accessory indices out of range");
static_assert(ACCESSORIES[ACCESSORY_IDX_PARTY_HAT].cost == 0 && ACCESSORIES[ACCESSORY_IDX_WITCH_HAT].cost == 0,
              "theme-week exclusives must be cost 0, or isStoreAccessory() would put them in the store");

/**
 * Whether an ACCESSORIES[] entry can be bought in the store. The invariant is
 * **store-purchasable ⇔ cost > 0**: theme-week exclusives must be cost 0, and every real store
 * item must cost more than 0, so a cost-0 entry is always a deliberate "not for sale". Store
 * rendering and purchase code use this rather than an index bound, since the store-purchasable
 * entries aren't a contiguous prefix: the theme-week exclusives sit in the middle of the catalog
 * (see the Sorting Hat).
 *
 * @param i Index into ACCESSORIES[].
 * @return true if the entry has a price, false for the cost-0 theme-week exclusives.
 */
static constexpr bool isStoreAccessory(int i) {
    return ACCESSORIES[i].cost > 0;
}

// Counts the store-purchasable ACCESSORIES[] entries from index `i` onward (recursive, so it
// stays a C++11 constexpr function).
static constexpr int countStoreAccessories(int i) {
    return i >= ACCESSORY_COUNT ? 0 : (isStoreAccessory(i) ? 1 : 0) + countStoreAccessories(i + 1);
}
// Count of normal, store-purchasable accessories, derived from isStoreAccessory() so it can't
// drift when a theme week appends another cost-0 entry. hasNewStoreItems() compares the seen
// count against this, so theme-exclusive entries never trip the "new store items!" badge.
static constexpr int ACCESSORY_STORE_COUNT = countStoreAccessories(0);

// Forward-declared for the same reason as the bow functions above — GLASSES[] needs this
// before drawCat() (and `tft`) are declared.
static void drawSunglassesPinkRim(int cx, int cy);

// Forward-declared for the same reason. Unlike the sunglasses above, this one isn't
// store-purchasable — see GLASSES_STORE_COUNT below.
static void drawBalloonSunglasses(int cx, int cy);
static void drawCandyCornSunglasses(int cx, int cy);

// Glasses catalog — a separate accessory slot from ACCESSORIES[] (own store section, own
// dressing-room control, own owned/equipped bitmask), even though the struct shape and
// purchase/equip model are identical. Kept as its own catalog rather than folded into
// ACCESSORIES[] because glasses sit on the face (drawn after the eyes) while the head
// accessories above sit on the head (drawn before the eyes) — mixing the two into one
// catalog/bitmask would mean drawCat() couldn't just draw "the equipped accessory" in one
// place, but would need to branch on which kind it is anyway.
struct Glasses {
    const char* id;
    const char* label;
    uint32_t cost;
    const char* webColor;  // CSS hex approximation, for coloring its label in the config UI
    void (*draw)(int cx, int cy);  // paints the glasses onto the cat sprite, over the eyes
};
static constexpr Glasses GLASSES[] = {
    {"sunglasses_pink", "Pink-Rim Sunglasses", STORE_COST_ACCESSORY_GLASSES, "#FF80C0", drawSunglassesPinkRim},
    // Below: theme-week exclusive (DIY-108) — MUST stay appended after every real
    // store-purchasable entry above; see GLASSES_STORE_COUNT.
    {"balloon_sunglasses", "Birthday Balloon Sunglasses", 0, "#FF0000", drawBalloonSunglasses},
    {"candy_corn_sunglasses", "Candy Corn Sunglasses", 0, "#FFD21F", drawCandyCornSunglasses},  // Halloween (COM-379)
};
static constexpr int GLASSES_COUNT = sizeof(GLASSES) / sizeof(GLASSES[0]);
static_assert(GLASSES_COUNT <= 8, "ownedGlasses bitmask is uint8_t");
// Count of normal, store-purchasable glasses — everything before the theme-week-exclusive block
// above. Store rendering/purchase/seen-count logic bounds itself to this, not the full
// GLASSES_COUNT, so theme-exclusive entries never show up as purchasable and never trip the
// "new store items!" badge (see hasNewStoreItems()). The subtracted number is the size of that
// exclusive block: bump it whenever a theme week appends another entry.
static constexpr int GLASSES_STORE_COUNT = GLASSES_COUNT - 2;
static constexpr int GLASSES_IDX_BALLOON    = GLASSES_STORE_COUNT;
static constexpr int GLASSES_IDX_CANDY_CORN = GLASSES_STORE_COUNT + 1;

// Forward-declared for the same reason as the bow functions above.
static void drawHogwartsCrest(int cx, int cy);

// Chest badge catalog (COM-382) — a fourth worn-cosmetic slot alongside ACCESSORIES[] (head),
// GLASSES[] (face) and the right-arm items, with its own store section, dressing-room control and
// owned/equipped bitmask. Same struct shape and purchase/equip model as GLASSES[]; kept separate
// for the same reason glasses are: it draws at its own point in drawCat() (last, over the chest).
// `id` is the persisted/form key; never reorder or reuse indices.
struct Badge {
    const char* id;
    const char* label;
    uint32_t cost;
    const char* webColor;  // CSS hex approximation, for coloring its label in the config UI
    void (*draw)(int cx, int cy);  // paints the badge onto the cat's chest
};
static constexpr Badge BADGES[] = {
    {"hogwarts_crest", "Hogwarts Crest", STORE_COST_LEGENDARY, "#E8B030", drawHogwartsCrest},
};
static constexpr int BADGE_COUNT = sizeof(BADGES) / sizeof(BADGES[0]);
static_assert(BADGE_COUNT <= 8, "ownedBadges bitmask is uint8_t");
// Count of store-purchasable badges. No theme-week badges exist yet, so it's the whole catalog;
// if one is ever appended, subtract it here like GLASSES_STORE_COUNT does.
static constexpr int BADGE_STORE_COUNT = BADGE_COUNT;

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
static void drawSnowmanPeeking(int cx, int cy, uint16_t accentColor);
static void drawSnowmanFull(int cx, int cy, uint16_t accentColor);
static void drawPikachuPeeking(int cx, int cy, uint16_t accentColor);
static void drawPikachuFull(int cx, int cy, uint16_t accentColor);
static void drawEeveePeeking(int cx, int cy, uint16_t accentColor);
static void drawEeveeFull(int cx, int cy, uint16_t accentColor);
static void drawTeddyHeld(int cx, int cy, uint16_t accentColor);
static void drawBunnyHeld(int cx, int cy, uint16_t accentColor);
static void drawSquirrelHeld(int cx, int cy, uint16_t accentColor);
static void drawPenguinHeld(int cx, int cy, uint16_t accentColor);
static void drawUnicornHeld(int cx, int cy, uint16_t accentColor);
static void drawSnowmanHeld(int cx, int cy, uint16_t accentColor);
static void drawPikachuHeld(int cx, int cy, uint16_t accentColor);
static void drawEeveeHeld(int cx, int cy, uint16_t accentColor);
static void drawTeddyHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawBunnyHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawSquirrelHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawPenguinHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawUnicornHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawSnowmanHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawPikachuHeldPeeking(int cx, int cy, uint16_t accentColor);
static void drawEeveeHeldPeeking(int cx, int cy, uint16_t accentColor);

// Stuffy catalog — same purchase/equip model as blanket colors, so more stuffies can be
// added later without changing the store/dressing-room plumbing. `id` is the stable
// identifier used in store/dressing-room form requests; ConfigManager persists ownership
// as an `ownedStuffies` bitmask and the equipped selection as a numeric `equippedStuffy`
// index (both keyed by catalog position), not the string id. `drawPeeking`/`drawFull` are
// the sleep-scene art for this stuffy — only one stuffy is ever equipped there at a time
// (see equippedStuffyIndex()), so equipping the bunny replaces the teddy bear at night
// rather than showing both. `drawHeld`/`drawHeldPeeking` are a separate pair for the
// right-arm slot (equippedStuffyRightIndex(), DIY-64), independent of the left slot above —
// the same stuffy can be drawPeeking/drawFull on the left and drawHeld on the right at once.
// `drawHeld` mirrors `drawFull` exactly (full head+body, anchored on the right instead of
// the left) and `drawHeldPeeking` mirrors `drawPeeking` (head only), so the right-arm slot
// looks exactly like the left one, just facing the other way. drawRightArmStuffy() (see
// below) picks between them: `drawHeld` for day and blanket-less night, `drawHeldPeeking`
// once a blanket would otherwise cover `drawHeld`'s body.
struct Stuffy {
    const char* id;
    const char* label;
    uint32_t cost;
    void (*drawPeeking)(int cx, int cy, uint16_t accentColor);
    void (*drawFull)(int cx, int cy, uint16_t accentColor);
    void (*drawHeld)(int cx, int cy, uint16_t accentColor);
    void (*drawHeldPeeking)(int cx, int cy, uint16_t accentColor);
};
static constexpr Stuffy STUFFIES[] = {
    {"teddy",    "Teddy Bear",   STORE_COST_TEDDY,    drawTeddyPeeking,    drawTeddyFull,    drawTeddyHeld,    drawTeddyHeldPeeking},
    {"bunny",    "Grey Bunny",   STORE_COST_BUNNY,    drawBunnyPeeking,    drawBunnyFull,    drawBunnyHeld,    drawBunnyHeldPeeking},
    {"squirrel", "Red Squirrel", STORE_COST_SQUIRREL, drawSquirrelPeeking, drawSquirrelFull, drawSquirrelHeld, drawSquirrelHeldPeeking},
    {"penguin",  "Penguin",      STORE_COST_PENGUIN,  drawPenguinPeeking,  drawPenguinFull,  drawPenguinHeld,  drawPenguinHeldPeeking},
    {"unicorn",  "White Unicorn", STORE_COST_UNICORN, drawUnicornPeeking,  drawUnicornFull,  drawUnicornHeld, drawUnicornHeldPeeking},
    {"snowman",  "Snowman",      STORE_COST_SNOWMAN,  drawSnowmanPeeking,  drawSnowmanFull,  drawSnowmanHeld, drawSnowmanHeldPeeking},
    {"pikachu",  "Pikachu",      STORE_COST_PIKACHU,  drawPikachuPeeking,  drawPikachuFull,  drawPikachuHeld, drawPikachuHeldPeeking},
    {"eevee",    "Eevee",        STORE_COST_EEVEE,    drawEeveePeeking,    drawEeveeFull,    drawEeveeHeld,   drawEeveeHeldPeeking},
};
static constexpr int STUFFY_COUNT = sizeof(STUFFIES) / sizeof(STUFFIES[0]);
static_assert(STUFFY_COUNT <= 16, "ownedStuffies bitmask is uint16_t");

// Forward declaration: the mini hockey stick's art, defined further below alongside
// drawTeddyHead() and friends. Declared here so the TOYS[] catalog can reference it
// directly, same as the stuffy draw-function forward declarations above.
static void drawHockeyStickHeld(int cx, int cy);
// Forward declaration: the magic wand's art (DIY-111), same reasoning as drawHockeyStickHeld()
// above — declared here so the TOYS[] catalog can reference it directly.
static void drawMagicWandHeld(int cx, int cy);

// Toy catalog (DIY-110) — a category parallel to STUFFIES[] above, sharing the right-arm
// slot (rightArmSlotUnlocked, DIY-64) with the right-arm stuffy rather than getting a slot of
// its own: on-device testing showed the toy read better held in the same right-arm spot than
// the originally-specced left arm, but that spot is already a held item's home, so the two
// are mutually exclusive sub-categories of one slot (equippedRightArmKind picks which, if
// either) rather than each getting independent space — see equippedToyIndex() and
// drawRightArmToy(). Equipping a toy there requires the same rightArmSlotUnlocked purchase
// as a stuffy does; owning the toy itself (ownedToys) is still its own, separate purchase,
// same as owning a stuffy is separate from unlocking the slot. `id` is the stable identifier
// used in store/dressing-room form requests; ConfigManager persists ownership as an
// `ownedToys` bitmask and the equipped selection as a numeric `equippedToy` index (both
// keyed by catalog position), not the string id. Only one toy is ever equipped at a time —
// unlike stuffies there's no second-copy purchase, since a toy only ever occupies the one
// shared right-arm spot, never two at once.
struct Toy {
    const char* id;
    const char* label;
    uint32_t cost;
    void (*drawHeld)(int cx, int cy);
};
static constexpr Toy TOYS[] = {
    {"hockey_stick", "Mini Hockey Stick", STORE_COST_HOCKEY_STICK, drawHockeyStickHeld},
    {"magic_wand", "Magic Wand", STORE_COST_MAGIC_WAND, drawMagicWandHeld},
};
static constexpr int TOY_COUNT = sizeof(TOYS) / sizeof(TOYS[0]);
static_assert(TOY_COUNT <= 16, "ownedToys bitmask is uint16_t");

// Forward declaration: shared flat-color backdrop for themes with no dedicated art of their
// own, defined further below alongside drawSleepingCat(). Declared here so the ROOM_THEMES[]
// catalog can reference it directly, same as the stuffy draw-function forward declarations
// above. Reads its color from the equipped theme's own `bgColor` field — a future theme with
// real art (starry night, moon, fireplace) would instead point at its own dedicated function.
static void drawFlatThemeBackground(int x, int y, int w, int h);

// Forward declaration: Starry Night's dedicated backdrop (moon + fixed star field),
// defined further below alongside drawFlatThemeBackground().
static void drawStarryNightBackground(int x, int y, int w, int h);

// Forward declaration: "6-7" meme theme's dedicated backdrop (rotated digit pairs on
// black), defined further below alongside drawStarryNightBackground().
static void drawSixSevenBackground(int x, int y, int w, int h);

// Forward declaration: "Clear Sky" theme's dedicated backdrop (light blue sky, scattered
// clouds, small rainbow), defined further below alongside drawSixSevenBackground().
static void drawSkyBackground(int x, int y, int w, int h);

// Forward declaration: theme-week-exclusive (DIY-108) "Birthday" backdrop (confetti +
// balloons), defined further below alongside drawSkyBackground().
static void drawBirthdayBackground(int x, int y, int w, int h);

// Forward declaration: theme-week-exclusive (COM-379) "Haunted Night" backdrop (orange moon,
// bats, jack-o'-lantern, plus a ghost on Oct 31), defined further below alongside
// drawBirthdayBackground().
static void drawHauntedNightBackground(int x, int y, int w, int h);

// Forward declaration: legendary (COM-378) "Hogwarts at Night" backdrop (castle silhouette with
// lit windows under a starry sky), defined further below alongside drawHauntedNightBackground().
static void drawHogwartsNightBackground(int x, int y, int w, int h);

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
    {"midnight",           "Midnight",      STORE_COST_ROOM_THEME,   0x18CE,    drawFlatThemeBackground,   "#5B7FBD"},  // deep navy — pairs with Dusty Blue
    {"twilight",           "Twilight",      STORE_COST_ROOM_THEME,   0x28C8,    drawFlatThemeBackground,   "#9B72CF"},  // deep plum — pairs with Lavender
    {"forest",             "Forest",        STORE_COST_ROOM_THEME,   0x1143,    drawFlatThemeBackground,   "#4CAF6D"},  // deep green — pairs with Apple Green
    {"rosewood",           "Rosewood",      STORE_COST_ROOM_THEME,   0x30A4,    drawFlatThemeBackground,   "#C2687D"},  // deep wine — pairs with Blush Pink
    {"amber",              "Amber",         STORE_COST_ROOM_THEME,   0x28E2,    drawFlatThemeBackground,   "#D9A441"},  // warm deep brown — pairs with Cream & Lemon Yellow
    {"flamingo_pink_room", "Flamingo Pink", STORE_COST_ROOM_THEME,   0xFD16,    drawFlatThemeBackground,   "#FCA3B7"},  // vibrant flamingo-pink — pairs with the Flamingo Pink blanket (DIY-82); id suffixed "_room" since "flamingo_pink" is already the blanket color's id (assertStoreIdsUnique() requires globally unique ids)
    {"starry_night",       "Starry Night",  STORE_COST_STARRY_NIGHT, TFT_BLACK, drawStarryNightBackground, nullptr},  // moon + stars on black — not a straight color, label stays white
    {"six_seven",          "6-7",           STORE_COST_SIX_SEVEN,    TFT_BLACK, drawSixSevenBackground,    nullptr},  // rotated "67" digit pairs on black (DIY-87) — not a straight color, label stays white
    {"clear_sky",          "Clear Sky",     STORE_COST_SKY,          0x5D9C,    drawSkyBackground,         nullptr},  // light blue sky + clouds + rainbow (DIY-90) — not a straight color, label stays white
    // Below: theme-week exclusive (DIY-108) — MUST stay appended after every real
    // store-purchasable entry above; see ROOM_THEME_STORE_COUNT.
    {"birthday", "Birthday", 0, 0xFCF3, drawBirthdayBackground, nullptr},  // confetti + balloons — not a straight color, label stays white
    {"haunted_night", "Haunted Night", 0, C_HAUNTED_SKY, drawHauntedNightBackground, nullptr},  // Halloween (COM-379): moon + bats + jack-o'-lantern — label stays white
    // Below: store-purchasable again (COM-378). Appended after the theme-week block for the same
    // reason as the Sorting Hat in ACCESSORIES[]: ownership is an index-keyed bitmask, so store
    // code filters with isStoreRoomTheme() rather than assuming a contiguous prefix.
    {"hogwarts_night", "Hogwarts at Night", STORE_COST_LEGENDARY, C_HOGWARTS_SKY, drawHogwartsNightBackground, "#E8B030"},  // legendary: castle + lit windows + shooting star, gold label
};
static constexpr int ROOM_THEME_COUNT = sizeof(ROOM_THEMES) / sizeof(ROOM_THEMES[0]);
static_assert(ROOM_THEME_COUNT <= 16, "ownedRoomThemes bitmask is uint16_t");
// Catalog index of each theme-week-exclusive room theme. Fixed, not derived from the catalog
// size, since a store-purchasable theme (Hogwarts at Night, COM-378) now sits after them.
static constexpr int ROOM_THEME_IDX_BIRTHDAY       = 9;
static constexpr int ROOM_THEME_IDX_HAUNTED_NIGHT  = 10;
static constexpr int ROOM_THEME_IDX_HOGWARTS_NIGHT = 11;
static_assert(ROOM_THEME_COUNT > ROOM_THEME_IDX_HOGWARTS_NIGHT, "room theme indices out of range");
static_assert(ROOM_THEMES[ROOM_THEME_IDX_BIRTHDAY].cost == 0 && ROOM_THEMES[ROOM_THEME_IDX_HAUNTED_NIGHT].cost == 0,
              "theme-week exclusives must be cost 0, or isStoreRoomTheme() would put them in the store");

/**
 * Whether a ROOM_THEMES[] entry can be bought in the store. Same **store-purchasable ⇔ cost > 0**
 * invariant as isStoreAccessory(): the cost-0 theme-week exclusives sit in the middle of the
 * catalog, ahead of Hogwarts at Night.
 *
 * @param i Index into ROOM_THEMES[].
 * @return true if the entry has a price, false for the cost-0 theme-week exclusives.
 */
static constexpr bool isStoreRoomTheme(int i) {
    return ROOM_THEMES[i].cost > 0;
}

// Counts the store-purchasable ROOM_THEMES[] entries from index `i` onward (recursive, so it
// stays a C++11 constexpr function).
static constexpr int countStoreRoomThemes(int i) {
    return i >= ROOM_THEME_COUNT ? 0 : (isStoreRoomTheme(i) ? 1 : 0) + countStoreRoomThemes(i + 1);
}
// Count of normal, store-purchasable room themes, derived from isStoreRoomTheme() — see
// ACCESSORY_STORE_COUNT's comment for why.
static constexpr int ROOM_THEME_STORE_COUNT = countStoreRoomThemes(0);

// Sentinel stored in equippedBlanketColor/equippedStuffy/equippedRoomTheme to mean
// "explicitly unequipped by the user in the dressing room", as opposed to 0 which means "no
// selection made yet, fall back to the lowest-index owned item" (see
// equippedBlanketIndex()/equippedStuffyIndex()/equippedRoomThemeIndex()). Never a valid
// catalog index since all catalogs stay well under 255 entries.
static constexpr uint8_t EQUIP_NONE = 0xFF;

// Right-arm slot sub-category discriminator (DIY-110 restructure) — the right-arm slot
// (rightArmSlotUnlocked, DIY-64) can hold exactly one of a stuffy or a toy, never both,
// mutually exclusive rather than one silently hiding the other. `equippedStuffyRight` and
// `equippedToy` each keep their own catalog index regardless of which is currently active —
// only `equippedRightArmKind` says which one (if either) actually renders; see
// equippedStuffyRightIndex()/equippedToyIndex().
static constexpr uint8_t RIGHT_ARM_KIND_NONE   = 0;
static constexpr uint8_t RIGHT_ARM_KIND_STUFFY = 1;
static constexpr uint8_t RIGHT_ARM_KIND_TOY    = 2;

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

struct Cat {
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
bool otaUpdatePending          = false;  // an update was found; flash+reboot deferred until awake and past setup
OtaCheckResult pendingOtaUpdate;         // valid only while otaUpdatePending is true
unsigned long forceSickDeadlineMs = 0;   // test-only: armed via /config, 0 = not armed, not persisted
unsigned long forceThirstDeadlineMs = 0; // test-only: armed via /config, 0 = not armed, not persisted

struct Dirty { bool header, animal, picker, timerRow, eyesOnly, timerTick, headerTick, hungerLines, zzzFx, animalBg; } dirty = {true, true, true, true, false, false, false, false, false, true};
bool pointsFlashOn = false;  // toggled every ~500ms while hasNewStoreItems(); read by drawPoints()
bool saleFlashOn = false;    // toggled every ~500ms while a flash sale is active; read by drawSaleFlash()

// Level-up fireworks: a full-screen takeover (distinct from the small in-zone Celebrate
// animation) that plays once when awardXp() crosses a level boundary, with the milestone
// bonus (if any) flashing on top. Mirrors the sleepScreenActive/setupPromptActive pattern
// of owning the screen directly rather than going through the zone-scoped dirty flags.
struct Fireworks {
    bool active            = false;
    unsigned long since     = 0;  // millis() when triggered
    unsigned long lastFrame = 0;  // millis() of last frame advance
    uint8_t frame           = 0;  // advances every FIREWORKS_FRAME_MS, drives burst radius/color
    uint32_t bonusPoints    = 0;  // milestone bonus earned this level-up, 0 if none
} fireworks;
static constexpr unsigned long FIREWORKS_DURATION_MS = 4500;
static constexpr unsigned long FIREWORKS_FRAME_MS    = 150;


// Forward declarations: resolves the cat's current equipped color index, its body/fill
// color (the equipped store color if owned, else white/C_CAT), and whether it uses the
// smaller "cute" eye style — all defined below alongside equippedBlanketIndex(). Declared
// here so drawEyes()/drawCat() can call them directly, since they're defined before the
// resolvers they depend on.
static int equippedCatColorIndex();
static uint16_t catBodyColor();
static bool catHasCuteEyes();
static int equippedAccessoryIndex();
static int equippedGlassesIndex();
static int equippedBadgeIndex();
static int equippedStuffyRightIndex();
static int equippedBlanketIndex();

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

// Accessory art (declared earlier alongside ACCESSORIES[]). Called from drawCat() after the
// head fill/pattern but before eyes, same layering rule as the head patterns above — first-pass
// geometry, expect to tune after real hardware. Every bow color shares this one shape (only the
// fill color differs — see the three thin wrappers below), sitting at the base of the right ear
// (near where the ear triangle at (cx+32, cy-62) meets the head), angled shallowly along that
// ear-to-head edge so it reads as resting against the head rather than floating over the ear
// tip. Every point stays at y <= cy-57, a margin above drawEyes()'s partial blink-redraw rect
// (cx-28..+28, cy-50..-24 — see drawEyes() below) so blinking never erases part of the bow.
static void drawBowShape(int cx, int cy, uint16_t color) {
    // Two wings meeting at a center knot; apex is the knot, each wing's base corners are offset
    // along the ear-edge diagonal (axis) and perpendicular to it (width), mirrored to either side.
    tft.fillTriangle(cx + 25, cy - 70, cx + 16, cy - 83, cx + 10, cy - 66, color);
    tft.fillTriangle(cx + 25, cy - 70, cx + 40, cy - 74, cx + 34, cy - 57, color);
    tft.fillCircle(cx + 25, cy - 70, 5, color);
}
static void drawBowMagenta(int cx, int cy)  { drawBowShape(cx, cy, C_BOW_MAGENTA); }
static void drawBowHotPink(int cx, int cy)  { drawBowShape(cx, cy, C_BOW_HOT_PINK); }
static void drawBowPurple(int cx, int cy)   { drawBowShape(cx, cy, C_BOW_PURPLE); }
static void drawBowBabyBlue(int cx, int cy) { drawBowShape(cx, cy, C_BOW_BABY_BLUE); }
static void drawBowDustyBlue(int cx, int cy)    { drawBowShape(cx, cy, C_BOW_DUSTY_BLUE); }
static void drawBowCream(int cx, int cy)        { drawBowShape(cx, cy, C_BOW_CREAM); }
static void drawBowLemonYellow(int cx, int cy)  { drawBowShape(cx, cy, C_BOW_LEMON_YELLOW); }
static void drawBowAppleGreen(int cx, int cy)   { drawBowShape(cx, cy, C_BOW_APPLE_GREEN); }
static void drawBowTangerine(int cx, int cy)    { drawBowShape(cx, cy, C_BOW_TANGERINE); }
static void drawBowFlamingoPink(int cx, int cy) { drawBowShape(cx, cy, C_BOW_FLAMINGO_PINK); }

// Party hat art (theme-week exclusive, DIY-108). Centered on top of the head, between the
// ears, rather than off to one side like the bows above — apex tops out at cy-86, matching
// the ears' own apex height (see drawCat()'s ear triangles), so it stays inside the cat's
// zoneFillRect() clear rect (cy-88) with a couple pixels of headroom.
static void drawPartyHat(int cx, int cy) {
    tft.fillTriangle(cx - 13, cy - 63, cx + 13, cy - 63, cx, cy - 86, C_PARTY_HAT);
    tft.fillRect(cx - 13, cy - 65, 26, 4, C_PARTY_HAT_TRIM);      // base band
    tft.fillCircle(cx, cy - 86, 4, C_PARTY_HAT_TRIM);             // pompom
    tft.fillCircle(cx - 5, cy - 72, 2, C_PARTY_HAT_TRIM);         // polka dots
    tft.fillCircle(cx + 4, cy - 76, 2, C_PARTY_HAT_TRIM);
}

/**
 * Witch hat (theme-week exclusive, COM-379): a black cone whose tip bends over to the right,
 * on a wide brim, with an orange band and yellow buckle. Each black shape is first painted 1px
 * larger in lavender (C_WITCH_HAT_RIM) to form an outline, so the hat still reads against the
 * Haunted Night sky and on a black cat. The apex stops at cy-87, inside drawCat()'s cy-88
 * clear rect, the same budget as drawPartyHat().
 *
 * @param cx Cat center x (CAT_CX).
 * @param cy Cat center y, including any bounce offset.
 */
static void drawWitchHat(int cx, int cy) {
    // Outline pass: the same three shapes, 1px larger.
    tft.fillEllipse(cx, cy - 63, 27, 5, C_WITCH_HAT_RIM);
    tft.fillTriangle(cx - 15, cy - 62, cx + 13, cy - 62, cx + 5, cy - 87, C_WITCH_HAT_RIM);
    tft.fillTriangle(cx + 5, cy - 87, cx + 16, cy - 81, cx + 3, cy - 79, C_WITCH_HAT_RIM);
    // Brim, cone, bent tip.
    tft.fillEllipse(cx, cy - 63, 26, 4, C_WITCH_HAT);
    tft.fillTriangle(cx - 14, cy - 63, cx + 12, cy - 63, cx + 5, cy - 85, C_WITCH_HAT);
    tft.fillTriangle(cx + 5, cy - 85, cx + 14, cy - 81, cx + 4, cy - 80, C_WITCH_HAT);
    // Band + buckle.
    tft.fillRect(cx - 12, cy - 70, 23, 4, C_WITCH_HAT_BAND);
    tft.fillRect(cx - 3, cy - 71, 6, 6, C_CANDLE_GLOW);
    tft.fillRect(cx - 1, cy - 69, 2, 2, C_WITCH_HAT);
}

/**
 * Sorting Hat (legendary, COM-382): a patched brown hat with a wide, droopy brim, a cone crumpled
 * at a kink, a tip that flops to the left, and a face creased into it. It differs from the witch
 * hat above in colour (brown, not black), silhouette (the tip bends left and droops) and detail
 * (no band or buckle). It sits low over the brow (brim at cy-57), as the Sorting Hat slumps onto
 * Harry, which buys extra height under the cy-87 apex limit that drawCat()'s clear rect sets. The
 * brim outline's lowest row is cy-52, just above drawEyes()'s blink rect (cy-50), so blinking
 * never erases part of the hat. Each body shape is first painted 1px larger in the outline colour.
 *
 * @param cx Cat center x (CAT_CX).
 * @param cy Cat center y, including any bounce offset.
 */
static void drawSortingHat(int cx, int cy) {
    // Outline pass.
    tft.fillEllipse(cx, cy - 57, 32, 5, C_SORTING_HAT_LINE);
    tft.fillTriangle(cx - 17, cy - 57, cx + 17, cy - 57, cx + 2, cy - 83, C_SORTING_HAT_LINE);
    tft.fillTriangle(cx - 6, cy - 74, cx + 7, cy - 79, cx - 10, cy - 87, C_SORTING_HAT_LINE);
    tft.fillTriangle(cx - 11, cy - 87, cx - 5, cy - 83, cx - 21, cy - 77, C_SORTING_HAT_LINE);
    // Brim, lower cone, kinked upper cone, drooping tip.
    tft.fillEllipse(cx, cy - 57, 31, 4, C_SORTING_HAT);
    tft.fillTriangle(cx - 15, cy - 58, cx + 15, cy - 58, cx + 2, cy - 81, C_SORTING_HAT);
    tft.fillTriangle(cx - 5, cy - 75, cx + 6, cy - 78, cx - 10, cy - 86, C_SORTING_HAT);
    tft.fillTriangle(cx - 10, cy - 86, cx - 6, cy - 83, cx - 20, cy - 78, C_SORTING_HAT);
    // Shaded right flank and brim fold, then the crumple line at the kink.
    tft.fillTriangle(cx + 4, cy - 58, cx + 15, cy - 58, cx + 2, cy - 81, C_SORTING_HAT_PATCH);
    tft.fillEllipse(cx + 16, cy - 56, 13, 2, C_SORTING_HAT_PATCH);
    tft.drawLine(cx - 6, cy - 75, cx + 6, cy - 78, C_SORTING_HAT_LINE);
    // Creased face: two brow folds and a seam mouth.
    tft.drawLine(cx - 9, cy - 70, cx - 4, cy - 68, C_SORTING_HAT_LINE);
    tft.drawLine(cx + 1, cy - 68, cx + 7, cy - 70, C_SORTING_HAT_LINE);
    tft.drawLine(cx - 6, cy - 62, cx - 1, cy - 61, C_SORTING_HAT_LINE);
    tft.drawLine(cx - 1, cy - 61, cx + 5, cy - 62, C_SORTING_HAT_LINE);
}

// Glasses art (declared earlier alongside GLASSES[]). Called from drawCat() after the eyes
// (unlike the bow above, which is drawn before them) so the round lenses sit over the eye
// shapes drawn by drawEyeShapes() at (cx-15/+15, cy-37, r=11). Round rim + round lens per
// eye, joined by a bridge. Note: drawEyes()'s partial blink redraw only repaints the eye
// rect, not these glasses, so blinking is skipped entirely while glasses are equipped (see
// the cat-state update loop) rather than trying to keep a partial redraw path in sync here.
static void drawSunglassesPinkRim(int cx, int cy) {
    tft.fillCircle(cx - 15, cy - 37, 14, C_GLASSES_RIM);
    tft.fillCircle(cx - 15, cy - 37, 11, C_GLASSES_LENS);
    tft.fillCircle(cx + 15, cy - 37, 14, C_GLASSES_RIM);
    tft.fillCircle(cx + 15, cy - 37, 11, C_GLASSES_LENS);
    tft.fillRect(cx - 4, cy - 40, 8, 4, C_GLASSES_RIM);  // bridge
    // Temple arms — from each lens's outer edge back toward the ear/side of the head, so the
    // band reads as wrapping around toward the back rather than the lenses floating free.
    for (int w = -2; w <= 2; w++) {
        tft.drawLine(cx - 29, cy - 37 + w, cx - 43, cy - 47 + w, C_GLASSES_RIM);
        tft.drawLine(cx + 29, cy - 37 + w, cx + 43, cy - 47 + w, C_GLASSES_RIM);
    }
}

// Theme-week-exclusive glasses art (DIY-108) — same layering/temple-arm mechanics as
// drawSunglassesPinkRim() above (see its comment), but each lens is a bold single-color
// balloon silhouette (one red, one blue — see C_BALLOON_GLASSES_RED/BLUE) with a small knot
// and a string hanging down, rather than a rim+lens pair. Deliberately no internal detail on
// the balloons themselves — a flat fill reads clearly at this resolution, same lesson learned
// from the cupcake stuffy this replaced (fine detail/pastel colors don't survive the real
// screen's size and the birthday theme's own pale-pink backdrop).
static void drawBalloonSunglasses(int cx, int cy) {
    tft.fillCircle(cx - 15, cy - 37, 12, C_BALLOON_GLASSES_RED);
    tft.fillTriangle(cx - 18, cy - 26, cx - 12, cy - 26, cx - 15, cy - 22, C_BALLOON_GLASSES_RED);  // knot
    tft.drawLine(cx - 15, cy - 22, cx - 15, cy - 16, C_DARK);  // string

    tft.fillCircle(cx + 15, cy - 37, 12, C_BALLOON_GLASSES_BLUE);
    tft.fillTriangle(cx + 12, cy - 26, cx + 18, cy - 26, cx + 15, cy - 22, C_BALLOON_GLASSES_BLUE);  // knot
    tft.drawLine(cx + 15, cy - 22, cx + 15, cy - 16, C_DARK);  // string

    tft.fillRect(cx - 4, cy - 40, 8, 4, C_GLASSES_RIM);  // bridge
    for (int w = -2; w <= 2; w++) {
        tft.drawLine(cx - 27, cy - 37 + w, cx - 43, cy - 47 + w, C_GLASSES_RIM);
        tft.drawLine(cx + 27, cy - 37 + w, cx + 43, cy - 47 + w, C_GLASSES_RIM);
    }
}

/**
 * One candy-corn lens for drawCandyCornSunglasses(): a point-down triangle split into three
 * equal-height bands (yellow, orange, white tip) with a charcoal outline. The white tip needs
 * that outline to stay visible on a white cat. Each band below the top one is drawn as the
 * whole remaining triangle, and the band above is painted over it, so no trapezoid math is
 * needed.
 *
 * @param lx Lens center x (the eye's x).
 * @param top Y of the lens's wide top edge.
 * @param tip Y of the lens's bottom point.
 * @param hw Half-width of the top edge.
 */
static void drawCandyCornLens(int lx, int top, int tip, int hw) {
    int h = tip - top;
    tft.fillTriangle(lx - hw, top, lx + hw, top, lx, tip, TFT_WHITE);
    // Orange covers the top two thirds: a triangle whose bottom edge sits at the 2/3 line,
    // using the lens's own slope (half-width shrinks linearly to 0 at the tip).
    int y2 = top + h * 2 / 3, hw2 = hw / 3;
    tft.fillTriangle(lx - hw, top, lx + hw, top, lx - hw2, y2, C_PUMPKIN);
    tft.fillTriangle(lx + hw, top, lx + hw2, y2, lx - hw2, y2, C_PUMPKIN);
    int y1 = top + h / 3, hw1 = hw * 2 / 3;
    tft.fillTriangle(lx - hw, top, lx + hw, top, lx - hw1, y1, C_CANDY_CORN_TOP);
    tft.fillTriangle(lx + hw, top, lx + hw1, y1, lx - hw1, y1, C_CANDY_CORN_TOP);
    tft.drawTriangle(lx - hw, top, lx + hw, top, lx, tip, C_DARK);
}

/**
 * Theme-week-exclusive glasses (COM-379): a candy-corn lens over each eye, a charcoal bridge
 * and orange temple arms. Same layering as drawSunglassesPinkRim(): called after the eyes, and
 * the cat skips blinking while any glasses are equipped. The lens narrows to a point where
 * the round eye doesn't, so the eye rect is first repainted with fur color (the same erase
 * drawEyes() does) to keep the sclera from peeking out beside the tip. The lenses read as
 * opaque shades. Like a blink, that erase also covers a tabby/calico head pattern in this rect.
 *
 * @param cx Cat center x (CAT_CX).
 * @param cy Cat center y, including any bounce offset.
 */
static void drawCandyCornSunglasses(int cx, int cy) {
    tft.fillRect(cx - 28, cy - 50, 56, 26, catBodyColor());
    drawCandyCornLens(cx - 15, cy - 51, cy - 22, 15);
    drawCandyCornLens(cx + 15, cy - 51, cy - 22, 15);
    tft.fillRect(cx - 4, cy - 48, 8, 3, C_DARK);  // bridge
    for (int w = -1; w <= 1; w++) {
        tft.drawLine(cx - 30, cy - 48 + w, cx - 43, cy - 54 + w, C_WITCH_HAT_BAND);
        tft.drawLine(cx + 30, cy - 48 + w, cx + 43, cy - 54 + w, C_WITCH_HAT_BAND);
    }
}

// Top edge of the chest badge (COM-382), relative to the cat's center y. Shared by
// drawHogwartsCrest() and drawHungerLines(), which moves its rumble lines below the badge.
static constexpr int BADGE_TOP_DY = 6;
static constexpr int BADGE_BOTTOM_DY = BADGE_TOP_DY + 24;

/**
 * Hogwarts crest chest badge (legendary, COM-382): a 21×25 px shield with a C_DARK outline,
 * quartered red, green, blue and yellow around a gold plate with a dark "H". It sits on the upper
 * chest (cx-10..+10, cy+6..+30), clear of the paws, a held stuffy (from cx+29) and a held toy (from
 * cx+24). drawCat() draws it last, over the tabby/calico body pattern; drawSleepingCat() draws it
 * again on top of the blanket, so it reads as pinned there.
 *
 * @param cx Cat center x (CAT_CX).
 * @param cy Cat center y, including any bounce offset.
 */
static void drawHogwartsCrest(int cx, int cy) {
    int y = cy + BADGE_TOP_DY;
    // Shield outline: a square top and a pointed bottom.
    tft.fillRect(cx - 10, y, 21, 15, C_DARK);
    tft.fillTriangle(cx - 10, y + 14, cx + 10, y + 14, cx, y + 24, C_DARK);
    // House quarters, leaving a 1px dark cross between them.
    tft.fillRect(cx - 9, y + 1, 9, 7, C_CREST_RED);
    tft.fillRect(cx + 1, y + 1, 9, 7, C_CREST_GREEN);
    tft.fillRect(cx - 9, y + 9, 9, 5, C_CREST_BLUE);
    tft.fillTriangle(cx - 9, y + 14, cx - 1, y + 14, cx - 1, y + 22, C_CREST_BLUE);
    tft.fillRect(cx + 1, y + 9, 9, 5, C_CREST_YELLOW);
    tft.fillTriangle(cx + 1, y + 14, cx + 9, y + 14, cx + 1, y + 22, C_CREST_YELLOW);
    // Outlined gold plate with an "H".
    tft.fillRect(cx - 5, y + 4, 11, 10, C_DARK);
    tft.fillRect(cx - 4, y + 5, 9, 8, C_CREST_GOLD);
    tft.drawFastVLine(cx - 2, y + 6, 6, C_DARK);
    tft.drawFastVLine(cx + 2, y + 6, 6, C_DARK);
    tft.drawFastHLine(cx - 2, y + 8, 5, C_DARK);
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

static void drawCat(int cx, int cy, CatStatus status, CatBoredom boredom, CatHealth health, CatThirst thirst, bool eyeOpen) {
    bool queasy  = (health == CatHealth::Sick);
    bool thirsty = (thirst == CatThirst::Thirsty);
    bool needy   = (status == CatStatus::Hungry || thirsty) && !queasy;
    bool happy   = !queasy && !needy && status == CatStatus::Content && thirst == CatThirst::Hydrated &&
                   boredom == CatBoredom::Entertained;
    bool sad     = needy;
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

    // Accessories (independent of cat color — a bow works with any fur)
    int accessoryIdx = equippedAccessoryIndex();
    if (accessoryIdx >= 0 && ACCESSORIES[accessoryIdx].draw) {
        ACCESSORIES[accessoryIdx].draw(cx, cy);
    }

    // Eyes
    drawEyeShapes(cx, cy, eyeOpen, catHasCuteEyes());

    // Glasses (drawn after the eyes, unlike the head accessories above, since they sit on
    // the face over the eyes rather than on the head)
    int glassesIdx = equippedGlassesIndex();
    if (glassesIdx >= 0 && GLASSES[glassesIdx].draw) {
        GLASSES[glassesIdx].draw(cx, cy);
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

    // Right-arm stuffy slot (DIY-64) is drawn by drawRightArmStuffy(), called separately by
    // each scene (drawAnimal() for day, drawSleepingCat() for night) rather than from here —
    // now that its pose is a full-size mirror of the left slot's drawFull()/drawPeeking(), it
    // needs to know whether a blanket is covering the body to pick the right one, which
    // drawCat() itself has no reason to know about.
    if (colorIdx >= 0 && CAT_COLORS[colorIdx].drawBodyPattern) {
        CAT_COLORS[colorIdx].drawBodyPattern(cx, cy);
    }

    // Chest badge (COM-382) — last, so it sits on top of the body pattern
    int badgeIdx = equippedBadgeIndex();
    if (badgeIdx >= 0) BADGES[badgeIdx].draw(cx, cy);
}

// Calm, static "asleep" scene for the sleep-window peek: reuses drawCat() with a
// content/closed-eyed state (no status decorations), then layers on owned store
// items — a blanket over the body/paws/tail up to the neck, and/or a teddy bear.
// Stays within drawAnimal()'s CAT_CX±50 clear-rect bounds.

// Resolves which blanket color to display: the equipped color if it's actually owned,
// otherwise the lowest-index owned color (covers stale/out-of-range equipped state), or
// -1 if no blanket color is owned at all.
static int equippedBlanketIndex() {
    uint16_t owned = configMgr.config().ownedBlanketColors;
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
    uint16_t owned = configMgr.config().ownedStuffies;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedStuffy;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < STUFFY_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < STUFFY_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Resolver for the right-arm slot's stuffy sub-category (DIY-64) — deliberately does NOT
// fall back to the lowest-owned stuffy like equippedStuffyIndex() and every other
// equipped*Index() above. Unlocking the slot is a separate purchase from owning any given
// stuffy, so there's no "first purchase" moment to auto-equip from; the user always picks
// explicitly in the dressing room, and an unlocked-but-never-equipped slot just stays empty.
// Also requires equippedRightArmKind == RIGHT_ARM_KIND_STUFFY (DIY-110) — the slot holds
// exactly one of a stuffy or a toy, so a stale equippedStuffyRight index left over from
// before the user switched the slot to a toy must never render.
static int equippedStuffyRightIndex() {
    if (!configMgr.config().rightArmSlotUnlocked) return -1;
    if (configMgr.config().equippedRightArmKind != RIGHT_ARM_KIND_STUFFY) return -1;
    uint8_t eq = configMgr.config().equippedStuffyRight;
    if (eq == EQUIP_NONE) return -1;
    if (eq < STUFFY_COUNT && (configMgr.config().ownedStuffies & (1 << eq))) return eq;
    return -1;  // previously-equipped stuffy no longer owned somehow — just show nothing
}

// Same resolution logic as equippedBlanketIndex(), for the room theme catalog.
static int equippedRoomThemeIndex() {
    uint16_t owned = configMgr.config().ownedRoomThemes;
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

// Same resolution logic as equippedBlanketIndex(), for the accessory catalog.
static int equippedAccessoryIndex() {
    uint16_t owned = configMgr.config().ownedAccessories;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedAccessory;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < ACCESSORY_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < ACCESSORY_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Same resolution logic as equippedBlanketIndex(), for the glasses catalog.
static int equippedGlassesIndex() {
    uint8_t owned = configMgr.config().ownedGlasses;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedGlasses;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < GLASSES_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < GLASSES_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Same resolution logic as equippedBlanketIndex(), for the chest badge catalog (COM-382).
static int equippedBadgeIndex() {
    uint8_t owned = configMgr.config().ownedBadges;
    if (owned == 0) return -1;
    uint8_t eq = configMgr.config().equippedBadge;
    if (eq == EQUIP_NONE) return -1;  // user explicitly unequipped
    if (eq < BADGE_COUNT && (owned & (1 << eq))) return eq;
    for (int i = 0; i < BADGE_COUNT; i++) {
        if (owned & (1 << i)) return i;
    }
    return -1;
}

// Resolver for the right-arm slot's toy sub-category (DIY-110) — mirrors
// equippedStuffyRightIndex() exactly (same rightArmSlotUnlocked gate, same "no fallback,
// user picks explicitly" behavior, same kind check so a stale index from before switching
// the slot to a stuffy never renders), since a toy occupies the very same physical slot.
static int equippedToyIndex() {
    if (!configMgr.config().rightArmSlotUnlocked) return -1;
    if (configMgr.config().equippedRightArmKind != RIGHT_ARM_KIND_TOY) return -1;
    uint8_t eq = configMgr.config().equippedToy;
    if (eq == EQUIP_NONE) return -1;
    if (eq < TOY_COUNT && (configMgr.config().ownedToys & (1 << eq))) return eq;
    return -1;  // previously-equipped toy no longer owned somehow — just show nothing
}

// Per-cat-color name lookup/assignment, keyed the same way as equippedCatColorIndex()
// (idx == -1 means the white default cat). Falls back to catNameDefault for any color
// that hasn't been individually named yet — covers both a freshly-bought color and a
// pre-migration device where only the single legacy name existed.
static String getCatName(int idx) {
    const String& n = (idx < 0) ? configMgr.config().catNameWhite : configMgr.config().catNames[idx];
    return n.length() ? n : configMgr.config().catNameDefault;
}
static void setCatName(int idx, const String& name) {
    if (idx < 0) configMgr.config().catNameWhite = name;
    else configMgr.config().catNames[idx] = name;
}

// Trims and rejects names over 16 characters or containing control characters. Leaves an
// empty name as empty rather than defaulting it — an empty per-color name is meaningful to
// setCatName()/getCatName() as "clear to fallback", so only the setup wizard (which has no
// fallback yet to clear to) should turn an empty submission into "Biscuit". Shared by the
// setup wizard and the dressing room so every per-cat-color name gets the same validation.
static bool sanitizeCatName(String& name) {
    name.trim();
    if (name.length() > 16) return false;
    for (size_t i = 0; i < name.length(); ++i) {
        if ((unsigned char)name[i] < 0x20 || (unsigned char)name[i] == 0x7F) return false;
    }
    return true;
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
    // *_STORE_COUNT, not the full *_COUNT — the theme-week-exclusive entries appended to
    // those catalogs (DIY-108) are never store-purchasable, so they must never trip this
    // badge either.
    return STUFFY_COUNT > configMgr.config().seenStuffyCount ||
           BLANKET_COLOR_COUNT > configMgr.config().seenBlanketColorCount ||
           ROOM_THEME_STORE_COUNT > configMgr.config().seenRoomThemeCount ||
           CAT_COLOR_COUNT > configMgr.config().seenCatColorCount ||
           ACCESSORY_STORE_COUNT > configMgr.config().seenAccessoryCount ||
           GLASSES_STORE_COUNT > configMgr.config().seenGlassesCount ||
           BADGE_STORE_COUNT > configMgr.config().seenBadgeCount ||
           TOY_COUNT > configMgr.config().seenToyCount ||
           !configMgr.config().seenRightArmSlot;
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

// Right-arm slot pose (DIY-64) — an exact mirror of drawTeddyFull() (same size/shape, same
// bx-relative offsets — teddy's body/feet are already left-right symmetric around bx, so no
// offset needs sign-flipping), just anchored on the right (cx+38 vs. the left's cx-38).
static void drawTeddyHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawTeddyHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 9, by + 7, 18, 26, 8, C_BEAR);  // body
    tft.fillCircle(bx, by + 18, 5, accentColor);           // belly patch
    tft.fillCircle(bx - 6, by + 33, 4, C_BEAR);            // left foot
    tft.fillCircle(bx + 6, by + 33, 4, C_BEAR);            // right foot
}

// Night-only right-arm variant (DIY-64) — the blanket in drawSleepingCat() would otherwise
// cover drawTeddyHeld()'s body, so this mirrors drawTeddyPeeking() instead (head only, same
// cx+40/cy-6 vs. the left's cx-40/cy-6), drawn on top of the blanket.
static void drawTeddyHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    drawTeddyHead(bx, by, accentColor);
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

// Right-arm slot pose (DIY-64) — see drawTeddyHeld() for placement rationale.
static void drawBunnyHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawBunnyHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_BUNNY);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);            // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_BUNNY);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_BUNNY);            // right foot
    tft.fillCircle(bx, by + 33, 3, TFT_WHITE);              // fluffy tail
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale.
static void drawBunnyHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    drawBunnyHead(bx, by, accentColor);
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

// Right-arm slot pose (DIY-64) — an exact mirror of drawSquirrelFull(). Unlike teddy/bunny,
// the tail-tip poke is a one-sided feature (not a symmetric pair), so its x offsets are
// sign-flipped (bx+10 → bx-10 etc.) to poke toward the body on this side too, rather than
// literally translating drawSquirrelFull()'s art and having the tail poke away from the cat.
static void drawSquirrelHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    tft.fillCircle(bx - 10, by + 12, 6, C_SQUIRREL);   // tail base, tucked behind the body
    tft.fillCircle(bx - 13, by + 4,  5, C_SQUIRREL);   // a bit of orange curling up
    tft.fillCircle(bx - 14, by - 2,  4, accentColor);  // white tip poking over the shoulder
    drawSquirrelHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_SQUIRREL);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);               // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_SQUIRREL);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_SQUIRREL);            // right foot
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale. Tail bump
// mirrored to poke toward the body (leftward) rather than drawSquirrelPeeking()'s rightward
// poke, keeping the same "over the near shoulder" silhouette on this side.
static void drawSquirrelHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    tft.fillCircle(bx - 12, by + 2, 5, C_SQUIRREL);   // bit of orange poking over the shoulder
    tft.fillCircle(bx - 13, by - 4, 4, accentColor);  // white tip
    drawSquirrelHead(bx, by, accentColor);
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

// Right-arm slot pose (DIY-64) — see drawTeddyHeld() for placement rationale. Flippers/feet
// dropped to keep the pose compact; head + body silhouette is enough to read as a penguin.
static void drawPenguinHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawPenguinHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 9, by + 7, 18, 26, 9, C_PENGUIN);      // body
    tft.fillRoundRect(bx - 5, by + 10, 10, 20, 5, accentColor);   // white belly patch
    tft.fillTriangle(bx - 9, by + 15, bx - 11, by + 23, bx - 6, by + 24, C_PENGUIN);  // left flipper
    tft.fillTriangle(bx + 9, by + 15, bx + 11, by + 23, bx + 6, by + 24, C_PENGUIN);  // right flipper
    tft.fillTriangle(bx - 6, by + 33, bx - 9, by + 37, bx - 2, by + 37, C_PENGUIN_BEAK);  // left foot
    tft.fillTriangle(bx + 6, by + 33, bx + 9, by + 37, bx + 2, by + 37, C_PENGUIN_BEAK);  // right foot
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale.
static void drawPenguinHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    drawPenguinHead(bx, by, accentColor);
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

// Right-arm slot pose (DIY-64) — see drawTeddyHeld() for placement rationale.
static void drawUnicornHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawUnicornHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 8, by + 7, 16, 24, 8, C_UNICORN);  // body
    tft.fillCircle(bx, by + 17, 4, accentColor);              // belly patch
    tft.fillCircle(bx - 5, by + 31, 4, C_UNICORN);            // left foot
    tft.fillCircle(bx + 5, by + 31, 4, C_UNICORN);            // right foot
    tft.fillCircle(bx, by + 33, 3, C_UNICORN_HORN);           // pink tail
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale.
static void drawUnicornHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    drawUnicornHead(bx, by, accentColor);
}

// Shared hat/head/face art reused by both snowman variants below (DIY-89). Body stays a
// fixed C_SNOWMAN (snow-blue) regardless of blanket, same as C_UNICORN/C_PENGUIN — the
// scarf is the one detail that takes the accent color, so the snowman doesn't visually
// vanish into a matching white/pale blanket the way an all-fixed-white silhouette would.
static void drawSnowmanHead(int bx, int by, uint16_t scarfColor) {
    tft.fillRect(bx - 8, by - 13, 16, 5, C_SNOWMAN_COAL);        // hat brim — bottom edge overlaps into the head circle below so there's no gap between hat and head
    tft.fillRect(bx - 5, by - 21, 10, 8, C_SNOWMAN_COAL);        // hat body
    tft.fillCircle(bx, by, 9, C_SNOWMAN);                        // head — painted after the hat, so it cleanly covers the brim's center while leaving the brim's edges visible as a rim
    tft.fillRect(bx - 9, by + 6, 18, 4, scarfColor);              // scarf
    tft.fillCircle(bx - 3, by - 2, 1, C_SNOWMAN_COAL);            // left eye
    tft.fillCircle(bx + 3, by - 2, 1, C_SNOWMAN_COAL);            // right eye
    tft.fillTriangle(bx, by + 1, bx, by + 3, bx + 5, by + 2, C_SNOWMAN_CARROT);  // carrot nose
    tft.fillCircle(bx - 3, by + 3, 1, C_SNOWMAN_COAL);            // left mouth coal
    tft.fillCircle(bx,     by + 4, 1, C_SNOWMAN_COAL);            // center mouth coal
    tft.fillCircle(bx + 3, by + 3, 1, C_SNOWMAN_COAL);            // right mouth coal
}

// Snowman peeking out beside the head, tucked into the blanket's top edge —
// only reads correctly when the blanket is also owned to tuck behind.
static void drawSnowmanPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    drawSnowmanHead(bx, by, accentColor);
}

// Body + buttons shared by drawSnowmanFull()/drawSnowmanHeld() below — unlike teddy (whose
// feet differ by side), the snowman's body is radially symmetric, so left and right poses
// need no mirroring beyond the (bx, by) anchor drawSnowmanHead() already receives.
static void drawSnowmanBody(int bx, int by) {
    tft.fillCircle(bx, by + 21, 12, C_SNOWMAN);                   // body
    tft.fillCircle(bx, by + 15, 1, C_SNOWMAN_COAL);               // top button
    tft.fillCircle(bx, by + 21, 1, C_SNOWMAN_COAL);               // middle button
    tft.fillCircle(bx, by + 27, 1, C_SNOWMAN_COAL);               // bottom button
}

// Full-body snowman sitting beside the cat — used when the snowman is owned without the
// blanket, since there's no blanket edge to tuck a lone head behind. Buttons stay fixed to
// C_SNOWMAN_COAL rather than the accent color, matching the fixed-detail convention the
// other stuffies use for their non-accent features.
static void drawSnowmanFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawSnowmanHead(bx, by, accentColor);
    drawSnowmanBody(bx, by);
}

// Right-arm slot pose (DIY-64) — see drawTeddyHeld() for placement rationale.
static void drawSnowmanHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawSnowmanHead(bx, by, accentColor);
    drawSnowmanBody(bx, by);
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale.
static void drawSnowmanHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    drawSnowmanHead(bx, by, accentColor);
}

// Shared ear/head/cheek/eyes/nose art reused by both pikachu variants below (DIY-112). Ears
// are pointed triangles with black tips rather than the round/oval ears the other stuffies
// use, and the red cheek patches are pikachu's signature feature so they stay fixed to
// C_PIKACHU_CHEEK rather than taking the accent color — only the small chin patch takes
// `chinColor` (accentColor), matching the other stuffies' one-accented-detail convention.
static void drawPikachuHead(int bx, int by, uint16_t chinColor) {
    tft.fillTriangle(bx - 11, by - 8, bx - 4, by - 8, bx - 8, by - 25, C_PIKACHU);       // left ear
    tft.fillTriangle(bx - 9,  by - 18, bx - 6, by - 18, bx - 8, by - 25, C_PIKACHU_MARK); // left ear tip
    tft.fillTriangle(bx + 4,  by - 8, bx + 11, by - 8, bx + 8, by - 25, C_PIKACHU);       // right ear
    tft.fillTriangle(bx + 6,  by - 18, bx + 9, by - 18, bx + 8, by - 25, C_PIKACHU_MARK); // right ear tip
    tft.fillCircle(bx,     by,     10, C_PIKACHU);        // head
    tft.fillCircle(bx - 8, by + 3, 4, C_PIKACHU_CHEEK);   // left cheek
    tft.fillCircle(bx + 8, by + 3, 4, C_PIKACHU_CHEEK);   // right cheek
    tft.fillCircle(bx,     by + 5, 3, chinColor);         // chin patch
    tft.fillCircle(bx - 4, by - 3, 1, C_DARK);   // left eye
    tft.fillCircle(bx + 4, by - 3, 1, C_DARK);   // right eye
    tft.fillCircle(bx,     by + 2, 1, C_DARK);   // nose
}

// Lightning-bolt tail zigzag shared by the full-body pikachu poses below — drawn as two
// offset triangles for the bolt's angular kink, with a black tip mirroring the ear tips.
// `dir` is +1 for the left-slot poses (bolt kinks rightward, over the near shoulder) and -1
// for the right-arm slot (kinks leftward), matching drawSquirrelHeld()'s sign-flip approach
// so the tail always pokes toward the cat's body rather than away from it — needed to stay
// inside drawAnimal()'s CAT_CX ± 50 redraw clear rect, which an away-pointing tail overshoots.
static void drawPikachuTail(int bx, int by, int dir) {
    tft.fillTriangle(bx + dir * 10, by + 20, bx + dir * 20, by + 13, bx + dir * 14, by + 8, C_PIKACHU);       // tail lower half
    tft.fillTriangle(bx + dir * 14, by + 8,  bx + dir * 23, by + 3,  bx + dir * 16, by - 8, C_PIKACHU);       // tail upper half
    tft.fillTriangle(bx + dir * 19, by - 3,  bx + dir * 23, by + 3,  bx + dir * 16, by - 8, C_PIKACHU_MARK);  // dark tip
}

// Pikachu peeking out beside the head, tucked into the blanket's top edge — only reads
// correctly when the blanket is also owned to tuck behind. A bit of the lightning-bolt tail
// pokes over the shoulder, matching drawSquirrelPeeking()'s partial-tail treatment.
static void drawPikachuPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    tft.fillTriangle(bx + 13, by + 5, bx + 20, by - 3, bx + 14, by - 8, C_PIKACHU);      // bit of tail poking over the shoulder
    tft.fillTriangle(bx + 16, by - 5, bx + 20, by - 3, bx + 14, by - 8, C_PIKACHU_MARK); // dark tip
    drawPikachuHead(bx, by, accentColor);
}

// Full-body pikachu sitting beside the cat — used when pikachu is owned without the
// blanket, since there's no blanket edge to tuck a lone head behind.
static void drawPikachuFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawPikachuTail(bx, by, 1);
    drawPikachuHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 10, by + 8, 20, 30, 10, C_PIKACHU);  // body
    tft.fillCircle(bx, by + 21, 5, accentColor);                // chest patch
    tft.fillCircle(bx - 6, by + 39, 5, C_PIKACHU);              // left foot
    tft.fillCircle(bx + 6, by + 39, 5, C_PIKACHU);              // right foot
}

// Right-arm slot pose (DIY-64) — an exact mirror of drawPikachuFull(), with the tail's `dir`
// flipped (see drawPikachuTail()) so it still pokes toward the cat's body on this side.
static void drawPikachuHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawPikachuTail(bx, by, -1);
    drawPikachuHead(bx, by, accentColor);
    tft.fillRoundRect(bx - 10, by + 8, 20, 30, 10, C_PIKACHU);  // body
    tft.fillCircle(bx, by + 21, 5, accentColor);                // chest patch
    tft.fillCircle(bx - 6, by + 39, 5, C_PIKACHU);              // left foot
    tft.fillCircle(bx + 6, by + 39, 5, C_PIKACHU);              // right foot
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale. Tail bit
// mirrored to poke toward the body (leftward) rather than drawPikachuPeeking()'s rightward
// poke, keeping the same "over the near shoulder" silhouette on this side.
static void drawPikachuHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    tft.fillTriangle(bx - 13, by + 5, bx - 20, by - 3, bx - 14, by - 8, C_PIKACHU);      // bit of tail poking over the shoulder
    tft.fillTriangle(bx - 16, by - 5, bx - 20, by - 3, bx - 14, by - 8, C_PIKACHU_MARK); // dark tip
    drawPikachuHead(bx, by, accentColor);
}

// Shared ear/head/ruff/face art reused by both eevee variants (DIY-113). Scaled to match
// Pikachu's proportions (10px head) rather than the smaller teddy/bunny/squirrel scale. Ears
// are large and pointed with a darker-brown outer tip and light-beige inner sliver near the
// base (fox-like, per reference — not Pikachu's plain black-tipped ear), and the light-beige
// neck ruff is a wide U-shaped collar below the jaw (several overlapping circles), not a
// small chest patch. Both C_EEVEE_DARK/C_EEVEE_LIGHT are fixed, pikachu-cheeks-style, since
// they're breed-defining traits, not customizable — so unlike drawTeddyHead()/
// drawPikachuHead() this helper takes no accent parameter, and Eevee's full-body poses have
// no accent-colored belly patch at all (see drawEeveeFull()). Eyes are bigger/rounder than
// the other stuffies' single-pixel dots; eyes, nose, and mouth are all black (COM-266),
// distinct from the ear-tip brown; a small smile is added below the nose, unlike any other stuffy.
static void drawEeveeHead(int bx, int by) {
    tft.fillTriangle(bx - 13, by - 6, bx - 3, by - 6, bx - 9, by - 28, C_EEVEE);        // left ear
    tft.fillTriangle(bx - 10, by - 19, bx - 6, by - 19, bx - 9, by - 28, C_EEVEE_DARK); // left ear darker-brown tip
    tft.fillTriangle(bx - 9,  by - 8,  bx - 5, by - 8,  bx - 7, by - 16, C_EEVEE_LIGHT); // left ear inner
    tft.fillTriangle(bx + 3,  by - 6, bx + 13, by - 6, bx + 9, by - 28, C_EEVEE);       // right ear
    tft.fillTriangle(bx + 6,  by - 19, bx + 10, by - 19, bx + 9, by - 28, C_EEVEE_DARK); // right ear darker-brown tip
    tft.fillTriangle(bx + 5,  by - 8,  bx + 9, by - 8,  bx + 7, by - 16, C_EEVEE_LIGHT); // right ear inner
    tft.fillCircle(bx,      by,      10, C_EEVEE);        // head
    // Neck ruff — a U-shaped collar sitting below the jawline, dipping lowest at the center,
    // rather than a solid blob against the chin — keeps it from reading as a beard covering
    // the muzzle. Drawn before the face features below, which sit on top of/above it.
    tft.fillCircle(bx - 9, by + 9,  4, C_EEVEE_LIGHT);  // neck ruff, left jaw
    tft.fillCircle(bx + 9, by + 9,  4, C_EEVEE_LIGHT);  // neck ruff, right jaw
    tft.fillCircle(bx - 4, by + 12, 4, C_EEVEE_LIGHT);  // neck ruff, left dip connector
    tft.fillCircle(bx + 4, by + 12, 4, C_EEVEE_LIGHT);  // neck ruff, right dip connector
    tft.fillCircle(bx,     by + 15, 5, C_EEVEE_LIGHT);  // neck ruff, lowest center point
    // Face — eyes, nose, and a small smile on the muzzle, clearly above the ruff.
    tft.fillCircle(bx - 4, by - 2, 2, C_EEVEE_BLACK);   // left eye (black, big and round)
    tft.fillCircle(bx + 4, by - 2, 2, C_EEVEE_BLACK);   // right eye (black, big and round)
    tft.fillCircle(bx,     by + 3, 1, C_EEVEE_BLACK);   // nose
    tft.drawLine(bx - 2, by + 5, bx,     by + 6, C_EEVEE_BLACK);  // mouth, left half of smile
    tft.drawLine(bx,     by + 6, bx + 2, by + 5, C_EEVEE_BLACK);  // mouth, right half of smile
}

// Tapered fox-tail shape shared by the full-body eevee poses below — a 3-triangle strip
// (base outer/inner, a shared mid edge, then a single triangle collapsing to a point) rather
// than pikachu's zigzag-bolt geometry, which left the light-beige tip as a near-degenerate
// sliver (barely visible) not reliably joined to the brown segment (a visible gap at the
// bend, from the two pieces only sharing a single vertex rather than a full edge). Here the
// beige tip triangle shares its entire base edge (O1-I1) with the brown trapezoid's last
// triangle, guaranteeing no gap, and tapers from that full-width edge down to a single point,
// giving the tip real visible area instead of a sliver. `dir` is +1 for the left-slot poses
// (tail kinks rightward, over the near shoulder) and -1 for the right-arm slot.
static void drawEeveeTail(int bx, int by, int dir) {
    tft.fillTriangle(bx + dir * 11, by + 20, bx + dir * 6,  by + 16, bx + dir * 20, by + 10, C_EEVEE);       // tail base, outer half
    tft.fillTriangle(bx + dir * 6,  by + 16, bx + dir * 20, by + 10, bx + dir * 14, by + 7,  C_EEVEE);       // tail base, inner half — shares the O1-I1 edge below with the tip
    tft.fillTriangle(bx + dir * 20, by + 10, bx + dir * 14, by + 7,  bx + dir * 23, by - 6,  C_EEVEE_LIGHT); // tapered light-beige tip, joined along the full O1-I1 edge
}

// Eevee peeking out beside the head, tucked into the blanket's top edge — only reads
// correctly when the blanket is also owned to tuck behind. A bit of the bushy tail pokes
// over the shoulder, matching drawSquirrelPeeking()'s/drawPikachuPeeking()'s partial-tail
// treatment.
static void drawEeveePeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 40, by = cy - 6;
    tft.fillTriangle(bx + 13, by + 3, bx + 9,  by,     bx + 19, by - 5,  C_EEVEE);       // bit of tail poking over the shoulder
    tft.fillTriangle(bx + 9,  by,     bx + 19, by - 5,  bx + 22, by - 10, C_EEVEE_LIGHT); // tapered light-beige tip, joined along the shared edge above
    drawEeveeHead(bx, by);
}

// Full-body eevee sitting beside the cat — used when eevee is owned without the blanket,
// since there's no blanket edge to tuck a lone head behind. No belly patch — unlike the
// other stuffies, Eevee's real silhouette has no separate lighter patch on the stomach, so
// the body is a plain, uninterrupted fur color (accentColor goes unused here). Body/feet are
// drawn *before* the head so the head's neck ruff isn't overpainted — the body's corner
// radius (10) is half its width, so its top is a full semicircle that would otherwise cover
// most of the ruff. Drawing it first still leaves no gap, since the head circle's bottom
// (by+10) overlaps the body dome's apex (by+8).
static void drawEeveeFull(int cx, int cy, uint16_t accentColor) {
    int bx = cx - 38, by = cy - 8;
    drawEeveeTail(bx, by, 1);
    tft.fillRoundRect(bx - 10, by + 8, 20, 30, 10, C_EEVEE);  // body
    tft.fillCircle(bx - 6, by + 39, 5, C_EEVEE);              // left foot
    tft.fillCircle(bx + 6, by + 39, 5, C_EEVEE);              // right foot
    drawEeveeHead(bx, by);
}

// Right-arm slot pose (DIY-64) — an exact mirror of drawEeveeFull(), with the tail's `dir`
// flipped (see drawEeveeTail()) so it still pokes toward the cat's body on this side.
static void drawEeveeHeld(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 38, by = cy - 8;
    drawEeveeTail(bx, by, -1);
    tft.fillRoundRect(bx - 10, by + 8, 20, 30, 10, C_EEVEE);  // body
    tft.fillCircle(bx - 6, by + 39, 5, C_EEVEE);              // left foot
    tft.fillCircle(bx + 6, by + 39, 5, C_EEVEE);              // right foot
    drawEeveeHead(bx, by);
}

// Night-only right-arm variant (DIY-64) — see drawTeddyHeldPeeking() for rationale. Tail bit
// mirrored to poke toward the body (leftward) rather than drawEeveePeeking()'s rightward poke,
// keeping the same "over the near shoulder" silhouette on this side.
static void drawEeveeHeldPeeking(int cx, int cy, uint16_t accentColor) {
    int bx = cx + 40, by = cy - 6;
    tft.fillTriangle(bx - 13, by + 3, bx - 9,  by,     bx - 19, by - 5,  C_EEVEE);       // bit of tail poking over the shoulder
    tft.fillTriangle(bx - 9,  by,     bx - 19, by - 5,  bx - 22, by - 10, C_EEVEE_LIGHT); // tapered light-beige tip, joined along the shared edge above
    drawEeveeHead(bx, by);
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

// Fixed layout for the "6-7" meme theme's three digit pairs — position, rotation angle, and
// a distinct color per digit, pre-set rather than randomized per call for the same
// repeated-partial-redraw reason as STARRY_NIGHT_STARS above. Angles are deliberately
// mismatched (not a shared value) so the three pairs don't read as one uniform rotation.
struct SixSeven { int16_t x, y; int16_t angle; uint16_t color6; uint16_t color7; };
static constexpr SixSeven SIX_SEVENS[] = {
    {50,  ANIMAL_Y + 35,  -14, 0x07FF, 0xF81F},  // cyan 6 / magenta 7
    {190, ANIMAL_Y + 85,    9, 0xFFE0, 0x07E0},  // yellow 6 / green 7 — right side, pushed further right
    {75,  ANIMAL_Y + 118, -20, 0xFD20, 0x781F},  // orange 6 / purple 7 — bottom-left, raised so the stuffy doesn't cover it
};
static constexpr int SIX_SEVEN_COUNT = sizeof(SIX_SEVENS) / sizeof(SIX_SEVENS[0]);
static constexpr int SIX_SEVEN_SPRITE_W = 44, SIX_SEVEN_SPRITE_H = 30;

// "6-7" meme room theme (DIY-87): three "67" digit pairs, each digit its own color, each
// pair tilted at its own angle, scattered on a black backdrop. TFT_eSPI's drawString() can't
// rotate text directly, so each pair is drawn at 0 degrees into a small offscreen sprite and
// then stamped onto the zone with pushRotated() — the standard TFT_eSPI technique for angled
// text — pivoting around the sprite's own center so it tilts in place rather than swinging
// around a corner. The sprite is created once and kept alive for the process lifetime rather
// than recreated every call, since drawBackground() runs on every small erasure throughout
// the animal zone and repeated heap alloc/free at that frequency would be wasteful.
static void drawSixSevenBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, TFT_BLACK);

    static TFT_eSprite pairSprite(&tft);
    static bool pairSpriteReady = false;
    if (!pairSpriteReady) {
        pairSprite.createSprite(SIX_SEVEN_SPRITE_W, SIX_SEVEN_SPRITE_H);
        pairSprite.setTextDatum(TL_DATUM);
        pairSprite.setPivot(SIX_SEVEN_SPRITE_W / 2, SIX_SEVEN_SPRITE_H / 2);
        pairSpriteReady = true;
    }

    for (int i = 0; i < SIX_SEVEN_COUNT; i++) {
        const SixSeven& s = SIX_SEVENS[i];
        pairSprite.fillSprite(TFT_BLACK);
        pairSprite.setTextColor(s.color6);
        pairSprite.drawString("6", 0, 0, 4);
        pairSprite.setTextColor(s.color7);
        pairSprite.drawString("7", 21, 0, 4);
        tft.setPivot(s.x, s.y);
        pairSprite.pushRotated(s.angle, TFT_BLACK);
    }

    tft.resetViewport();
}

// Fixed cloud layout for the "Clear Sky" theme — pre-set positions rather than re-randomized
// per call, for the same repeated-partial-redraw reason as STARRY_NIGHT_STARS/SIX_SEVENS
// above. Each cloud is drawn as 3-4 overlapping filled ellipses/circles (TFT_eSPI has no
// native cloud primitive) around a common "puffiness" radius `r`.
struct Cloud { int16_t x, y; uint8_t r; };
static constexpr Cloud SKY_CLOUDS[] = {
    {42,  ANIMAL_Y + 8,  8},    // top-left, small — tucked under the header, clear of the rainbow below it
    {170, ANIMAL_Y + 15, 13},   // top-right, larger
    {60,  ANIMAL_Y + 150, 9},   // bottom-left, small — kept clear of the cat's feet
    {205, ANIMAL_Y + 140, 11},  // bottom-right
};
static constexpr int SKY_CLOUD_COUNT = sizeof(SKY_CLOUDS) / sizeof(SKY_CLOUDS[0]);

// Concentric rainbow bands (outer -> inner), classic ROYGBIV order, drawn via drawArc()'s
// ring-thickness trick: each band's outer radius equals the previous band's inner radius,
// so the bands sit flush with no gaps or overlaps.
struct RainbowBand { uint16_t color; };
static constexpr RainbowBand SKY_RAINBOW_BANDS[] = {
    {TFT_RED}, {0xFD20 /*orange*/}, {TFT_YELLOW}, {TFT_GREEN}, {0x001F /*blue*/}, {0x781F /*violet*/},
};
static constexpr int SKY_RAINBOW_BAND_COUNT = sizeof(SKY_RAINBOW_BANDS) / sizeof(SKY_RAINBOW_BANDS[0]);
// Placed in the top-left corner of the zone — the only patch of open real estate: the badge
// column claims the top-right (BADGE_COL_X=168 onward), and the meds/play + water/treat
// buttons claim the bottom-left/bottom-right corners (see TREAT_X/PLAY_X/etc.), so a bow
// centered lower-right (the original placement) rendered directly on top of the treat button.
// TFT_eSPI's drawArc() angle 0 is at 6 o'clock, sweeping clockwise (90=9 o'clock/left,
// 180=12 o'clock/top, 270=3 o'clock/right) — 90..270 sweeps a half circle through the top,
// i.e. a classic downward-opening arch (∩ shape).
static constexpr int SKY_RAINBOW_CX = 35, SKY_RAINBOW_CY = ANIMAL_Y + 60;  // arc center, top-left of the zone
static constexpr int SKY_RAINBOW_OUTER_R = 32;   // outer radius of the outermost (red) band
static constexpr int SKY_RAINBOW_BAND_THICKNESS = 4;
static constexpr uint32_t SKY_RAINBOW_START_ANGLE = 90, SKY_RAINBOW_END_ANGLE = 270;  // half-circle arch, opening downward

// "Clear Sky" room theme (DIY-90): light blue sky, a scatter of puffy clouds built from
// overlapping filled ellipses/circles, and a small rainbow drawn as concentric drawArc()
// rings. Same setViewport/resetViewport clipping convention as drawStarryNightBackground —
// see that function's comment for why.
static void drawSkyBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, 0x5D9C);  // light blue sky

    for (int i = 0; i < SKY_CLOUD_COUNT; i++) {
        const Cloud& c = SKY_CLOUDS[i];
        tft.fillEllipse(c.x,          c.y,          c.r,           (int)(c.r * 0.6f), TFT_WHITE);
        tft.fillCircle(c.x - c.r,     c.y + 2,      (int)(c.r * 0.65f),                TFT_WHITE);
        tft.fillCircle(c.x + c.r,     c.y + 2,      (int)(c.r * 0.65f),                TFT_WHITE);
        tft.fillCircle(c.x + (c.r/3), c.y - (c.r/2),(int)(c.r * 0.7f),                 TFT_WHITE);
    }

    for (int i = 0; i < SKY_RAINBOW_BAND_COUNT; i++) {
        int outerR = SKY_RAINBOW_OUTER_R - i * SKY_RAINBOW_BAND_THICKNESS;
        int innerR = outerR - SKY_RAINBOW_BAND_THICKNESS;
        tft.drawArc(SKY_RAINBOW_CX, SKY_RAINBOW_CY, outerR, innerR,
                    SKY_RAINBOW_START_ANGLE, SKY_RAINBOW_END_ANGLE,
                    SKY_RAINBOW_BANDS[i].color, 0x5D9C, true);
    }

    tft.resetViewport();
}

// Fixed balloon layout for the theme-week-exclusive (DIY-108) "Birthday" theme — pre-set
// positions/colors rather than re-randomized per call, same repeated-partial-redraw reason as
// SKY_CLOUDS/STARRY_NIGHT_STARS above.
struct Balloon { int16_t x, y; uint16_t color; };
static constexpr Balloon BIRTHDAY_BALLOONS[] = {
    {50,  ANIMAL_Y + 30, C_BOW_MAGENTA},
    {190, ANIMAL_Y + 22, C_BOW_BABY_BLUE},
    {215, ANIMAL_Y + 70, C_BOW_LEMON_YELLOW},
};
static constexpr int BIRTHDAY_BALLOON_COUNT = sizeof(BIRTHDAY_BALLOONS) / sizeof(BIRTHDAY_BALLOONS[0]);

// Fixed confetti scatter, same idea as BIRTHDAY_BALLOONS above — a handful of small colored
// squares rather than a full particle system.
struct Confetti { int16_t x, y; uint16_t color; };
static constexpr Confetti BIRTHDAY_CONFETTI[] = {
    {25, ANIMAL_Y + 100, C_BOW_LEMON_YELLOW}, {70, ANIMAL_Y + 15, C_BOW_APPLE_GREEN},
    {110, ANIMAL_Y + 160, C_BOW_MAGENTA},     {150, ANIMAL_Y + 120, C_BOW_BABY_BLUE},
    {230, ANIMAL_Y + 145, C_BOW_TANGERINE},   {15, ANIMAL_Y + 55, C_BOW_PURPLE},
    {180, ANIMAL_Y + 165, C_BOW_LEMON_YELLOW}, {235, ANIMAL_Y + 10, C_BOW_APPLE_GREEN},
};
static constexpr int BIRTHDAY_CONFETTI_COUNT = sizeof(BIRTHDAY_CONFETTI) / sizeof(BIRTHDAY_CONFETTI[0]);

// "Birthday" room theme (DIY-108) — pastel backdrop, a scatter of confetti squares, and a
// few floating balloons (ellipse + string). Only ever owned/selectable during an active
// theme week (see isStoreRoomTheme() and applyThemeWeekCosmetics()/
// revertThemeWeekCosmetics()) — this is cyd-clock's first "special theme week" visual, not a
// normal store item. Same setViewport/resetViewport clipping convention as
// drawStarryNightBackground() — see that function's comment for why.
static void drawBirthdayBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, 0xFCF3);  // pale party-pink

    for (int i = 0; i < BIRTHDAY_CONFETTI_COUNT; i++) {
        const Confetti& c = BIRTHDAY_CONFETTI[i];
        tft.fillRect(c.x, c.y, 4, 4, c.color);
    }

    for (int i = 0; i < BIRTHDAY_BALLOON_COUNT; i++) {
        const Balloon& b = BIRTHDAY_BALLOONS[i];
        tft.drawLine(b.x, b.y + 12, b.x, b.y + 26, C_DARK);  // string
        tft.fillEllipse(b.x, b.y, 9, 11, b.color);
        tft.fillTriangle(b.x - 2, b.y + 10, b.x + 2, b.y + 10, b.x, b.y + 13, b.color);  // knot
    }

    tft.resetViewport();
}

// Defined in the Theme weeks section below (it needs flashSaleNow()); declared here so
// drawHauntedNightBackground() can decide whether to add the Oct 31 ghost.
static bool isHalloweenGhostShown();

// Fixed star scatter for Haunted Night, same idea as STARRY_NIGHT_STARS but sparser and dimmer
// so the bats and moon stay the focus.
struct HauntedPoint { int16_t x, y; };
static constexpr HauntedPoint HAUNTED_STARS[] = {
    {95, 52}, {150, 48}, {60, 120}, {176, 72}, {20, 160}, {190, 168}, {108, 196},
};
// Bat positions: the first crosses the moon (black on orange, the high-contrast one). The other
// two sit in the only open sky left between the cat's ears and the zone's overlays: one left of
// the left ear (clear of the boredom "Zz" column at x 48–70), one right of the right ear (above
// the points text in the badge column).
static constexpr HauntedPoint HAUNTED_BATS[] = {
    {28, ANIMAL_Y + 34}, {76, ANIMAL_Y + 10}, {162, ANIMAL_Y + 10},
};

/**
 * Draws one bat silhouette (about 22x9 px) centered on (x, y): a round body, two swept wing
 * triangles with a scalloped trailing tip each, and two ear points. Always black, so bats are
 * only placed where they contrast (over the moon or the mid-purple sky).
 *
 * @param x Body center x.
 * @param y Body center y.
 */
static void drawBat(int x, int y) {
    tft.fillCircle(x, y, 2, TFT_BLACK);
    tft.fillTriangle(x - 1, y - 1, x - 9, y - 4, x - 7, y + 3, TFT_BLACK);
    tft.fillTriangle(x + 1, y - 1, x + 9, y - 4, x + 7, y + 3, TFT_BLACK);
    tft.fillTriangle(x - 9, y - 4, x - 5, y + 1, x - 11, y + 1, TFT_BLACK);
    tft.fillTriangle(x + 9, y - 4, x + 5, y + 1, x + 11, y + 1, TFT_BLACK);
    tft.fillTriangle(x - 2, y - 2, x - 1, y - 5, x, y - 2, TFT_BLACK);
    tft.fillTriangle(x + 2, y - 2, x + 1, y - 5, x, y - 2, TFT_BLACK);
}

/**
 * "Haunted Night" room theme (theme-week exclusive, COM-379), only owned while the Halloween
 * theme week is applied. It draws a deep purple sky with a few faint stars, a big orange moon
 * top-left (kept off the right-hand medal/points column), three bats and a dark hill along the
 * floor. A lit jack-o'-lantern sits in the bottom-left corner. On Oct 31 (or in the admin
 * Preview mode) it also draws a small white ghost on the left, between the moon and the Meds
 * button. That ghost is plain backdrop art, not an animation:
 * checkThemeWeekTransition() forces one full backdrop repaint when it appears or disappears.
 * Uses the same setViewport/resetViewport clipping as drawStarryNightBackground(), so it can
 * draw the whole scene on every small erase and only the requested slice reaches the screen.
 *
 * @param x Left edge of the rect to repaint.
 * @param y Top edge of the rect to repaint (already clamped to the animal zone).
 * @param w Width of the rect.
 * @param h Height of the rect.
 */
static void drawHauntedNightBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, C_HAUNTED_SKY);

    for (const HauntedPoint& s : HAUNTED_STARS) tft.drawPixel(s.x, s.y, C_HAUNTED_STAR);

    // Moon, top-left, with three craters. Kept left of x=47 so the boredom "Zz" marks
    // (drawBoredomZzz(), x 48–70) never print over it.
    tft.fillCircle(26, ANIMAL_Y + 32, 20, C_HAUNTED_MOON);
    tft.fillCircle(19, ANIMAL_Y + 27, 3, C_HAUNTED_CRATER);
    tft.fillCircle(33, ANIMAL_Y + 40, 3, C_HAUNTED_CRATER);
    tft.fillCircle(29, ANIMAL_Y + 22, 2, C_HAUNTED_CRATER);

    for (const HauntedPoint& b : HAUNTED_BATS) drawBat(b.x, b.y);

    // Two overlapping hill silhouettes along the floor. Their lower halves fall past the zone
    // and the viewport clips them.
    tft.fillEllipse(60, ANIMAL_Y + 182, 90, 20, C_HAUNTED_HILL);
    tft.fillEllipse(210, ANIMAL_Y + 186, 70, 18, C_HAUNTED_HILL);

    // Jack-o'-lantern, bottom-left on the hill: stem, ridged body, glowing triangle eyes and a
    // wide mouth with two teeth. Deliberately night-only: it sits behind the Play button
    // (PLAY_X/PLAY_Y), which covers it whenever the action buttons are showing. It only
    // appears in the sleep-window peek scene (peekingAsleep), where drawAnimal() skips the
    // buttons. There's no free floor spot that clears the buttons and name label, and the user
    // chose to keep it as a night touch rather than move or drop it (COM-379).
    tft.fillRect(28, ANIMAL_Y + 139, 4, 5, C_PUMPKIN_STEM);
    tft.fillEllipse(30, ANIMAL_Y + 154, 15, 11, C_PUMPKIN);
    tft.drawLine(24, ANIMAL_Y + 145, 23, ANIMAL_Y + 163, C_PUMPKIN_RIDGE);
    tft.drawLine(36, ANIMAL_Y + 145, 37, ANIMAL_Y + 163, C_PUMPKIN_RIDGE);
    tft.fillTriangle(21, ANIMAL_Y + 152, 27, ANIMAL_Y + 152, 24, ANIMAL_Y + 147, C_CANDLE_GLOW);
    tft.fillTriangle(33, ANIMAL_Y + 152, 39, ANIMAL_Y + 152, 36, ANIMAL_Y + 147, C_CANDLE_GLOW);
    tft.fillTriangle(20, ANIMAL_Y + 157, 40, ANIMAL_Y + 157, 30, ANIMAL_Y + 163, C_CANDLE_GLOW);
    tft.fillRect(26, ANIMAL_Y + 157, 3, 2, C_PUMPKIN);
    tft.fillRect(32, ANIMAL_Y + 157, 3, 2, C_PUMPKIN);

    if (isHalloweenGhostShown()) {
        // Ghost: round head, a body block and a three-bump hem, with hollow eyes and an "o" mouth.
        // Left side, in the open sky between the moon (bottom y=ANIMAL_Y+52) and the Meds
        // button (top y=MEDS_Y, shown while sick): spans x 23–45, y ANIMAL_Y+61..+90, clear
        // of the "Zz" column (x>=48), the left sparkle (x>=61) and the whiskers (x>=74).
        // It used to sit on the right, where the always-on Water button covered it.
        const int gx = 34, gy = ANIMAL_Y + 76;
        tft.fillCircle(gx, gy - 4, 11, C_GHOST);
        tft.fillRect(gx - 11, gy - 4, 23, 14, C_GHOST);
        tft.fillCircle(gx - 7, gy + 10, 4, C_GHOST);
        tft.fillCircle(gx, gy + 10, 4, C_GHOST);
        tft.fillCircle(gx + 7, gy + 10, 4, C_GHOST);
        tft.fillEllipse(gx - 4, gy - 5, 2, 3, TFT_BLACK);
        tft.fillEllipse(gx + 4, gy - 5, 2, 3, TFT_BLACK);
        tft.fillCircle(gx, gy + 2, 2, TFT_BLACK);
    }

    tft.resetViewport();
}

// Hogwarts at Night (COM-378) castle towers, left to right. Each is a rect from `top` down to the
// curtain wall at HOGWARTS_WALL_Y, capped by a pointed roof `roofH` tall that overhangs the
// walls by 2px. The two tall towers fill the only floor-level gaps no overlay ever covers, between
// the side buttons and the cat's clear rect (x 56–69 and 171–184). Every roof in the right-hand
// badge column (x>=168) stays below y=112, and the right turret stays right of the right-hand
// "Zz" (x<=210), since both print text over a bgColor box that would show against black.
struct HogwartsTower { uint8_t x, top, w, roofH; };
static constexpr HogwartsTower HOGWARTS_TOWERS[] = {
    {30,  118, 14, 20},  // left turret, peeks over the Meds button
    {57,  124, 12, 24},  // left tall tower
    {84,  118, 72, 26},  // great hall, mostly behind the cat
    {92,  96,  12, 26},  // keep towers either side of the hall roof, behind the cat's head
    {136, 96,  12, 26},
    {172, 130, 12, 16},  // right tall tower
    {214, 128, 14, 16},  // right turret, peeks over the Water button
};
static constexpr int HOGWARTS_WALL_Y = ANIMAL_Y + 120;  // top of the curtain wall; the lake starts at ANIMAL_Y+148
static constexpr int HOGWARTS_LAKE_Y = ANIMAL_Y + ANIMAL_H - 27;  // = the name label strip, flat C_HOGWARTS_SKY

// Lit windows, 3x4 px each. The first HOGWARTS_FLICKER_WINDOWS entries are the ones that may
// flicker: they sit only where nothing is ever drawn over the backdrop while the animation runs
// (the tall towers' gaps, and the turret tops above the Meds/Water buttons), because the flicker
// repaints them straight through zoneFillRect() and would otherwise erase whatever was on top.
struct HogwartsWindow { uint8_t x, y; };
static constexpr HogwartsWindow HOGWARTS_WINDOWS[] = {
    {61, 132}, {61, 146}, {61, 170},     // left tall tower
    {176, 138}, {176, 150}, {176, 170},  // right tall tower
    {35, 124}, {219, 130},               // turret tops
    // Static from here on.
    {96, 104}, {140, 104},
    {90, 126}, {102, 126}, {136, 126}, {148, 126}, {90, 142}, {148, 142},
    {14, 170}, {44, 170}, {200, 170}, {226, 170},
};
static constexpr int HOGWARTS_WINDOW_COUNT = sizeof(HOGWARTS_WINDOWS) / sizeof(HOGWARTS_WINDOWS[0]);
static constexpr int HOGWARTS_FLICKER_WINDOWS = 8;
static constexpr int HOGWARTS_WINDOW_W = 3, HOGWARTS_WINDOW_H = 4;
static constexpr uint8_t HOGWARTS_NO_WINDOW = 0xFF;

// Shooting star path: the head starts at (X0, Y0) and moves DX left and DY down per frame, with a
// tail line TAIL_DX right and TAIL_DY up from it. The whole path stays in the open sky top-left
// (x 6–62, y 42–65): above the left "Zz" marks (y>=57 only from x 48), left of the cat's clear
// rect (x>=70), and above the moon.
static constexpr int HOGWARTS_STAR_X0 = 46, HOGWARTS_STAR_Y0 = ANIMAL_Y + 8;
static constexpr int HOGWARTS_STAR_DX = 5, HOGWARTS_STAR_DY = 2;
static constexpr int HOGWARTS_STAR_TAIL_DX = 10, HOGWARTS_STAR_TAIL_DY = 4;
static constexpr int HOGWARTS_STAR_FRAMES = 9;

// Animation state, read by drawHogwartsNightBackground() so every partial redraw of the zone
// agrees with what updateHogwartsNightAnim() last put on screen.
static uint8_t hogwartsDarkWindow = HOGWARTS_NO_WINDOW;  // flicker window currently dark
static int8_t  hogwartsStarFrame  = -1;                  // shooting-star frame, -1 when none in flight

/**
 * "Hogwarts at Night" room theme (legendary, COM-378): a navy sky with stars and a moon, and a
 * black castle silhouette (curtain wall, towers, pointed roofs) with warm lit windows standing on
 * a lake. The lake is the same flat navy as the sky and the theme's bgColor, because the cat's
 * name label sits on it and erases its glyph cells with bgColor. Uses the same
 * setViewport/resetViewport clipping as drawStarryNightBackground(), so it can draw the whole
 * scene on every small erase and only the requested slice reaches the screen. It also draws the
 * current animation state (a dark window, the shooting star in flight), so the frequent small
 * erases elsewhere in the zone never resurrect or wipe either one.
 *
 * @param x Left edge of the rect to repaint.
 * @param y Top edge of the rect to repaint (already clamped to the animal zone).
 * @param w Width of the rect.
 * @param h Height of the rect.
 */
static void drawHogwartsNightBackground(int x, int y, int w, int h) {
    tft.setViewport(x, y, w, h, false);
    tft.fillRect(x, y, w, h, C_HOGWARTS_SKY);

    // Same fixed star field as Starry Night; the castle covers the lower ones.
    for (const Star& s : STARRY_NIGHT_STARS) {
        if (s.r == 0) tft.drawPixel(s.x, s.y, TFT_WHITE);
        else          tft.fillCircle(s.x, s.y, s.r, TFT_WHITE);
    }

    // Moon, left of the "Zz" column and below the shooting star's path.
    tft.fillCircle(16, ANIMAL_Y + 42, 8, 0xFFDB);
    tft.fillCircle(13, ANIMAL_Y + 39, 2, 0xEF7A);

    tft.fillRect(0, HOGWARTS_WALL_Y, 240, HOGWARTS_LAKE_Y - HOGWARTS_WALL_Y, C_HOGWARTS_CASTLE);
    for (int mx = 0; mx < 240; mx += 8) tft.fillRect(mx, HOGWARTS_WALL_Y - 4, 4, 4, C_HOGWARTS_CASTLE);
    for (const HogwartsTower& t : HOGWARTS_TOWERS) {
        tft.fillRect(t.x, t.top, t.w, HOGWARTS_WALL_Y - t.top, C_HOGWARTS_CASTLE);
        tft.fillTriangle(t.x - 2, t.top, t.x + t.w + 1, t.top, t.x + t.w / 2, t.top - t.roofH, C_HOGWARTS_CASTLE);
    }

    for (int i = 0; i < HOGWARTS_WINDOW_COUNT; i++) {
        if (i == hogwartsDarkWindow) continue;
        tft.fillRect(HOGWARTS_WINDOWS[i].x, HOGWARTS_WINDOWS[i].y, HOGWARTS_WINDOW_W, HOGWARTS_WINDOW_H, C_HOGWARTS_WINDOW);
    }

    // Window reflections on the lake under the two tall towers, outside the name text.
    tft.drawFastHLine(58, HOGWARTS_LAKE_Y + 4, 10, C_HOGWARTS_GLINT);
    tft.drawFastHLine(60, HOGWARTS_LAKE_Y + 9, 6, C_HOGWARTS_GLINT);
    tft.drawFastHLine(173, HOGWARTS_LAKE_Y + 4, 10, C_HOGWARTS_GLINT);
    tft.drawFastHLine(175, HOGWARTS_LAKE_Y + 9, 6, C_HOGWARTS_GLINT);

    if (hogwartsStarFrame >= 0) {
        int hx = HOGWARTS_STAR_X0 - hogwartsStarFrame * HOGWARTS_STAR_DX;
        int hy = HOGWARTS_STAR_Y0 + hogwartsStarFrame * HOGWARTS_STAR_DY;
        tft.drawLine(hx, hy, hx + HOGWARTS_STAR_TAIL_DX, hy - HOGWARTS_STAR_TAIL_DY, C_HOGWARTS_TAIL);
        tft.fillRect(hx, hy, 2, 2, TFT_WHITE);
    }

    tft.resetViewport();
}

/**
 * Advances Hogwarts at Night's two animations, called every awake loop() tick. Every ~6–15 s one
 * of the flicker windows goes dark for 1–3 s, and every 1–3 min a shooting star crosses the
 * top-left sky over HOGWARTS_STAR_FRAMES ticks (about half a second). Each step only updates the
 * state drawHogwartsNightBackground() reads, then repaints the few pixels that changed through
 * zoneFillRect(); both effects stay in spots nothing is drawn over, so nothing else needs a redraw.
 * The state resets while another theme is equipped, and a star in flight is dropped (with one full
 * backdrop repaint) when the sleep-window peek scene starts, which stays static.
 *
 * @param now The loop's millis() reading.
 */
static void updateHogwartsNightAnim(unsigned long now) {
    static unsigned long nextFlickerMs = 0, nextStarMs = 0;
    if (equippedRoomThemeIndex() != ROOM_THEME_IDX_HOGWARTS_NIGHT) {
        hogwartsDarkWindow = HOGWARTS_NO_WINDOW;
        hogwartsStarFrame = -1;
        nextFlickerMs = 0;
        return;
    }
    if (peekingAsleep) {
        if (hogwartsStarFrame >= 0) { hogwartsStarFrame = -1; dirty.animalBg = true; }
        return;
    }
    if (nextFlickerMs == 0) {
        nextFlickerMs = now + random(6000, 15001);
        nextStarMs = now + random(60000, 180001);
    }

    if ((long)(now - nextFlickerMs) >= 0) {
        uint8_t win = hogwartsDarkWindow;
        if (win == HOGWARTS_NO_WINDOW) {
            win = hogwartsDarkWindow = (uint8_t)random(HOGWARTS_FLICKER_WINDOWS);
            nextFlickerMs = now + random(1000, 3001);
        } else {
            hogwartsDarkWindow = HOGWARTS_NO_WINDOW;
            nextFlickerMs = now + random(6000, 15001);
        }
        zoneFillRect(HOGWARTS_WINDOWS[win].x, HOGWARTS_WINDOWS[win].y, HOGWARTS_WINDOW_W, HOGWARTS_WINDOW_H);
    }

    if (hogwartsStarFrame < 0) {
        if ((long)(now - nextStarMs) < 0) return;
        hogwartsStarFrame = 0;
    } else if (++hogwartsStarFrame >= HOGWARTS_STAR_FRAMES) {
        hogwartsStarFrame = -1;
        nextStarMs = now + random(60000, 180001);
    }
    // Repaint the previous frame's streak and the new one in one rect (one frame past the last
    // when the star just ended, which only erases).
    int f = hogwartsStarFrame >= 0 ? hogwartsStarFrame : HOGWARTS_STAR_FRAMES;
    int hx = HOGWARTS_STAR_X0 - f * HOGWARTS_STAR_DX;
    int hy = HOGWARTS_STAR_Y0 + f * HOGWARTS_STAR_DY;
    zoneFillRect(hx, hy - HOGWARTS_STAR_DY - HOGWARTS_STAR_TAIL_DY,
                 HOGWARTS_STAR_DX + HOGWARTS_STAR_TAIL_DX + 2, HOGWARTS_STAR_DY + HOGWARTS_STAR_TAIL_DY + 2);
}

// Perceived luminance of an RGB565 color, used below to pick a contrasting tape color —
// standard ITU-R BT.601 weights on the 5/6/5-bit channels scaled up to 8 bits each.
// Threshold of 128 matches the usual "midpoint" cutoff for light-vs-dark text/UI contrast.
static bool colorReadsDark(uint16_t c) {
    uint8_t r = ((c >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((c >> 5)  & 0x3F) * 255 / 63;
    uint8_t b = (c & 0x1F) * 255 / 31;
    uint16_t luminance = (r * 299 + g * 587 + b * 114) / 1000;
    return luminance < 128;
}

// Mini hockey stick (DIY-110) — the toy catalog's first entry, held in the right arm slot.
// Anchored at bx=cx+38 (same x as drawTeddyHeld()'s right-arm position), by=cy+10 — moved
// down twice now (first cy-8 -> cy+4, then +4 -> +10) per on-device feedback that it kept
// reading too high; the shaft is also longer than the first cut (50px vs. 42px), so the
// blade ends up lower still relative to where it first was. Leans inward (top tilted toward
// the body, away from the paw) rather than standing straight up — also on-device feedback —
// built from two slanted parallelogram bands (each a pair of fillTriangle() calls sharing an
// edge, the same trick drawBirthdayBackground() uses for balloon knots) rather than
// fillRoundRect(), which can only draw axis-aligned rects.
//
// Tape color: both taped regions (the grip wrap at the top and the blade wrap at the
// bottom) share one contrast-against-background computation via zoneBgColor()/
// colorReadsDark() above, rather than a fixed black — computed once and reused for both,
// so they can never drift out of sync the way a second hardcoded color constant did before
// (the blade wrap was left on the fixed C_HOCKEY_BLADE black and didn't flip with the grip
// wrap; DIY-110 follow-up fix). This is a proxy for "what's behind the stick", not a true
// read of the actual pixels there — real per-pixel framebuffer sampling isn't practical
// here: the primary `cyd` board's platformio.ini never defines TFT_MISO at all (no readback
// wiring on that SPI bus), and even on freenove-s3, where MISO is wired, several themes
// (Starry Night, Clear Sky, Birthday) paint patterned art rather than a flat fill, so any
// single sampled pixel wouldn't reliably represent the whole area behind the stick anyway.
// zoneBgColor()'s per-theme representative color is already the same approximation the rest
// of the file uses for cheap contrast decisions (e.g. text glyph background erasure), so
// reusing it here stays consistent rather than reaching for a fragile, board-specific
// readback path for a cosmetic tweak.
static void drawHockeyStickHeld(int cx, int cy) {
    int bx = cx + 38, by = cy + 10;
    int topX = bx - 14, topY = by - 30;  // top of the shaft, leaning toward the body
    int botX = bx,      botY = by + 20;  // bottom of the shaft, at the paw
    int w = 7;
    uint16_t tapeColor = colorReadsDark(zoneBgColor()) ? TFT_WHITE : C_HOCKEY_TAPE;
    // Shaft
    tft.fillTriangle(topX, topY, topX + w, topY, botX, botY, C_HOCKEY_SHAFT);
    tft.fillTriangle(topX + w, topY, botX, botY, botX + w, botY, C_HOCKEY_SHAFT);
    // Grip tape wrap — same slant, just the top ~9px band
    int tapeX = topX + 3, tapeY = topY + 9;
    tft.fillTriangle(topX, topY, topX + w, topY, tapeX, tapeY, tapeColor);
    tft.fillTriangle(topX + w, topY, tapeX, tapeY, tapeX + w, tapeY, tapeColor);
    // Blade tape wrap, at the paw end, angled out to the right
    tft.fillRoundRect(botX - 2, botY - 4, 18, 6, 3, tapeColor);
}

// Magic wand (DIY-111) — slim shaft like the hockey stick, with a four-point star tip.
static void drawMagicWandHeld(int cx, int cy) {
    int bx = cx + 38, by = cy + 22;
    int topX = bx - 14, topY = by - 30;  // top of the shaft, leaning toward the body
    int botX = bx,      botY = by + 20;  // bottom of the shaft, at the paw
    int w = 5;
    // Shaft
    tft.fillTriangle(topX, topY, topX + w, topY, botX, botY, C_WAND_SHAFT);
    tft.fillTriangle(topX + w, topY, botX, botY, botX + w, botY, C_WAND_SHAFT);
    // Star tip — two overlapping triangles centered above the shaft's top end
    int sx = topX + w / 2, sy = topY - 8;
    tft.fillTriangle(sx - 8, sy + 3, sx + 8, sy + 3, sx, sy - 9, C_WAND_TIP);
    tft.fillTriangle(sx - 6, sy - 4, sx + 6, sy - 4, sx, sy + 8, C_WAND_TIP);
}

// Right-arm toy slot (DIY-110) — shares drawRightArmStuffy()'s spot rather than getting its
// own, since on-device feedback moved the toy from its originally-specced left arm to the
// right arm, which a stuffy may already occupy (DIY-64). The two are mutually exclusive by
// construction (equippedRightArmKind, see equippedStuffyRightIndex()/equippedToyIndex()) —
// the user explicitly picks one or the other for the slot in the dressing room, rather than
// this function needing to arbitrate a conflict at draw time. Called from both drawAnimal()
// (day) and drawSleepingCat() (night), same as drawRightArmStuffy(), right after it so the
// "same slot" relationship reads naturally in call order.
static void drawRightArmToy(int cx, int cy) {
    int toyIdx = equippedToyIndex();
    if (toyIdx < 0) return;
    TOYS[toyIdx].drawHeld(cx, cy);
}

// Right-arm slot (DIY-64) — called separately by each scene (day in drawAnimal(), night in
// drawSleepingCat()), rather than unconditionally from inside drawCat(), because which pose
// is correct depends on whether a blanket covers the body: drawHeld() is a full-size mirror
// of the left slot's drawFull(), so if a blanket is equipped its body would poke out past the
// blanket's edge — drawHeldPeeking() (mirroring drawPeeking(), head only) is used instead in
// that case, drawn by the caller on top of the already-painted blanket.
static void drawRightArmStuffy(int cx, int cy, bool hasBlanket) {
    int rightArmIdx = equippedStuffyRightIndex();
    if (rightArmIdx < 0) return;
    // The blanket is a night-only item with nothing to visually match during the day, so the
    // day (and blanket-less night) pose always uses a fixed default accent rather than
    // whatever blanket color happens to be equipped for the unrelated night scene. Only the
    // head-only night-with-blanket pose borrows the actual blanket's trim, since that's the
    // only time it renders on top of the visible blanket it'd otherwise clash with.
    uint16_t accent = hasBlanket ? BLANKET_COLORS[equippedBlanketIndex()].trim : BLANKET_COLORS[0].trim;
    if (hasBlanket) STUFFIES[rightArmIdx].drawHeldPeeking(cx, cy, accent);
    else            STUFFIES[rightArmIdx].drawHeld(cx, cy, accent);
}

// Thirsty signal — a single water drop at the temple, dripping-sweat style (DIY-109: drawn
// as its own pass, called from drawAnimal() *after* drawRightArmStuffy(), rather than from
// inside drawCat() itself. Both the equipped glasses (drawn inside drawCat()) and the
// right-arm stuffy (drawn separately, after drawCat()) can occupy this same temple-ish
// region depending on which are equipped, and no single fixed y-offset dodges every
// combination of the two — drawing the droplet last, on top of both, means it's never
// blocked regardless of what's equipped, without needing to hand-fit it into whatever gap
// happens to be left between them.
// Plain fill, no outline (DIY-109) — same triangle+circle shape as drawWaterBtn()'s droplet,
// matched exactly (including the missing outline) so the two read as the same icon. An
// earlier version of this outlined it in C_DARK for contrast against pale backdrops, but on
// real hardware the outline's own edges (unavoidably jagged at this size) read as stray dark
// lines cutting across the droplet rather than a clean border.
//
// tdy=cy-38 sits in the gap between the tallest held stuffy (snowman's hat, whose brim rect
// stays a full cy-29 all the way across x=cx+33..43, unlike the other stuffies' rounded
// ears/heads which taper down well before reaching this x) and the equipped glasses' temple
// arm (~cy-43..-47 at this x, see drawSunglassesPinkRim()/drawBalloonSunglasses()). That gap
// is only ~1-2px tall at x=cx+40, less than the droplet's own height, so this can only get
// close — the tip grazes 2-3px into the temple arm's line, and the base sits ~1px into the
// snowman's hat. Both are cosmetic (the droplet still draws on top, so it's never blocked),
// and far smaller than the double-digit-pixel overlaps of earlier positions.
static void drawThirstyDroplet(int cx, int cy) {
    int tdx = cx + 40, tdy = cy - 38;
    tft.fillTriangle(tdx, tdy - 6, tdx - 5, tdy + 4, tdx + 5, tdy + 4, C_WATER);
    tft.fillCircle(tdx, tdy + 5, 5, C_WATER);
}

static void drawSleepingCat(int cx, int cy) {
    drawCat(cx, cy, CatStatus::Content, CatBoredom::Entertained,
            CatHealth::Healthy, CatThirst::Hydrated, /*eyeOpen=*/false);
    drawSleepyEyes(cx, cy);

    // Re-draw glasses: drawSleepyEyes()'s fillRect (cx-28..+28, cy-50..-24) repaints the
    // same eye region drawCat() already drew the glasses into, wiping them out — same
    // fix as drawEyes()'s blink redraw, but here we just redraw rather than skip, since
    // the sleep scene isn't on a fast update loop like blinking is.
    int glassesIdx = equippedGlassesIndex();
    if (glassesIdx >= 0 && GLASSES[glassesIdx].draw) {
        GLASSES[glassesIdx].draw(cx, cy);
    }

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

        // The blanket just covered the chest badge drawCat() drew; draw it again on top, so it
        // reads as pinned to the blanket (COM-382).
        int badgeIdx = equippedBadgeIndex();
        if (badgeIdx >= 0) BADGES[badgeIdx].draw(cx, cy);
    }

    if (stuffyIdx >= 0) {
        // Only one stuffy is ever equipped at a time, so switching the dressing-room
        // selection (e.g. teddy → bunny) simply swaps which catalog entry's art draws here.
        const Stuffy& stuffy = STUFFIES[stuffyIdx];
        if (hasBlanket) stuffy.drawPeeking(cx, cy, accentColor);
        else             stuffy.drawFull(cx, cy, accentColor);
    }

    // Right-arm slot (DIY-64) — drawn after the blanket so drawHeldPeeking() (used whenever
    // hasBlanket) always ends up on top of it. See drawRightArmStuffy() for why the pose
    // choice depends on hasBlanket.
    drawRightArmStuffy(cx, cy, hasBlanket);
    drawRightArmToy(cx, cy);  // mutually exclusive with the stuffy above — see its own comment
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
    // Three short horizontal lines on the tummy — manga hunger growl effect. A chest badge
    // (COM-382) sits right where they normally go, and the fur-colour erase below would punch
    // through it, so with a badge equipped they move just below it, packed tighter to stay above
    // the paw toe lines.
    bool badge = equippedBadgeIndex() >= 0;
    int bx = cx - 12, by = badge ? cy + BADGE_BOTTOM_DY + 3 : cy + 18;
    int step = badge ? 5 : 8;
    uint16_t col = show ? C_RUMBLE : catBodyColor();  // erase with body colour to avoid flicker
    tft.drawFastHLine(bx,      by,            14, col);
    tft.drawFastHLine(bx +  2, by + step,     10, col);
    tft.drawFastHLine(bx,      by + 2 * step, 14, col);
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
// awardXp()'s callers (further down) can kick it off on a level-up, mirroring
// isFlashSaleActive()'s forward-declaration for drawSaleFlash() just below.
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

// Forward declaration: true while any flash sale is active, defined below alongside
// isInSleepWindow(). Declared here so drawSaleFlash() can call it directly.
static bool isFlashSaleActive();

// "SALE" banner at the bottom of the column, below the points balance, flashing
// red/yellow while any flash sale is active. Same clear-rect x-start as the
// other two badges (past the head's right edge).
static void drawSaleFlash() {
    zoneFillRect(BADGE_COL_X, ANIMAL_Y + LEVEL_BADGE_H + 20, BADGE_COL_W, 16);
    if (!isFlashSaleActive()) return;
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
        drawCat(CAT_CX, CAT_CY + dy, cat.status, cat.boredom, cat.health, cat.thirst, cat.eyeOpen);
        drawRightArmStuffy(CAT_CX, CAT_CY + dy, /*hasBlanket=*/false);  // no blanket during the day
        drawRightArmToy(CAT_CX, CAT_CY + dy);  // mutually exclusive with the stuffy above
        // Drawn after the stuffy above (DIY-109) so it's never covered by it — see
        // drawThirstyDroplet()'s comment.
        if (cat.thirst == CatThirst::Thirsty) {
            drawThirstyDroplet(CAT_CX, CAT_CY + dy);
        }

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
    tft.drawCentreString(getCatName(equippedCatColorIndex()).c_str(), CX, ANIMAL_Y + ANIMAL_H - 22, 2);

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

// Result of awardXp(): whether this award crossed a level boundary (fireworks-worthy
// on its own, even with zero bonus — not every level crosses a milestone) and the
// milestone bonus, if any, earned alongside it.
struct AwardXpResult {
    bool leveledUp;
    uint32_t bonusPoints;
};

// Awards XP 1:1 alongside points. XP is a separate, monotonically-increasing lifetime
// counter — never spent, never reduced by store purchases. Also grants a one-time bonus
// to the spendable points balance for every MILESTONE_LEVEL_INTERVAL level crossed by
// this single award (a big cheat grant can cross more than one milestone at once) that
// hasn't already paid out a bonus before — tracked via highestMilestoneLevel, so resetting
// totalXp back to 0 (see handleConfigBadgesResetPost()) and re-leveling can't re-farm the
// same bonus. Does not call configMgr.save() itself — callers already save after their
// own mutation. Does not trigger fireworks itself — callers check result.leveledUp and
// call triggerFireworks(result.bonusPoints) so this stays decoupled from the fireworks
// takeover animation.
static AwardXpResult awardXp(uint32_t amount) {
    if (amount == 0) return {false, 0};
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
        return {true, bonusEarned};
    }
    return {false, bonusEarned};
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
            AwardXpResult xpResult = awardXp(pointsAwarded);
            if (xpResult.leveledUp) triggerFireworks(xpResult.bonusPoints);
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

    // Blink (idle only) — eye-only redraw to avoid full-zone flash. Skipped while glasses
    // are equipped: drawEyes()'s partial redraw only repaints the eye rect, not the glasses
    // layered over it, so a blink would erase the glasses until the next full drawCat().
    if (cat.mood == CatMood::Idle && equippedGlassesIndex() < 0) {
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

// ── Flash sales (DIY-79) ─────────────────────────────────────────────────────
// Sale windows used to be a compile-time table (see DIY-54's history on why they're
// absolute start/end instants, not a recurring daily check). DIY-59/DIY-79 move that table
// server-side into cat-buddy-api (compton-apps repo) so a sale can be scheduled without a
// firmware release — the device polls GET /flash-sale/current on
// FLASH_SALE_POLL_INTERVAL_MS and caches the single active-or-upcoming sale it returns.
//
// The API response is `{ itemId, startAt, endAt }` (ISO-8601 UTC strings) — no price. Price
// stays a firmware-side concern because cat-buddy-api's scope (DIY-59) is deliberately just
// "what's on sale and when," not pricing policy. Any store item (cat color, stuffy, blanket,
// room theme, accessory, right-arm slot) can go on sale just by matching its `id` — see every
// flashSalePrice() call site below — at a flat FLASH_SALE_DISCOUNT_PERCENT off that item's own
// listed cost, so there's no per-item price table to keep in sync as new items are added.
//
// Only one sale is ever expected to be active at a time, matching the old table's assumption.
static constexpr uint32_t FLASH_SALE_DISCOUNT_PERCENT = 50;

struct FlashSaleState {
    bool valid = false;      // true once a poll has successfully populated an active sale
    char itemId[32] = {0};
    int64_t start   = 0;     // YYYYMMDDHHMM, same scheme as flashSaleNow() below
    int64_t end     = 0;
};
static FlashSaleState currentFlashSale;
unsigned long lastFlashSalePoll = 0;

// Surfaced on /config/admin/flashsale so a local dev/test setup (see below) doesn't have to guess
// why nothing happened — mirrors lastUpdateCheckFailed/lastUpdateCheckSkipped's role for OTA.
enum class FlashSalePollStatus { NeverPolled, Ok, NoActiveSale, Failed, SkippedNoCaCert, SkippedNoUrl, SkippedUnknownItem };
static FlashSalePollStatus lastFlashSalePollStatus = FlashSalePollStatus::NeverPolled;
static String lastFlashSalePollDetail;  // human-readable extra info: HTTP code, parse error, item id
static int lastFlashSalePollHttpCode = 0;  // 0 = no request was actually made (e.g. skipped)
static String lastFlashSalePollRawBody;    // raw HTTP response body, verbatim, for the /config/admin/flashsale page

#ifdef CAT_BUDDY_API_CA_CERT
#define CAT_BUDDY_HAS_CA_CERT 1
#else
#define CAT_BUDDY_HAS_CA_CERT 0
#endif

// Self-contained (fetches its own local time) so it can be called from the store HTTP
// handlers as well as loop(), not just places that already have nowMinutes in scope.
// Returns -1 if NTP hasn't synced yet.
static int64_t flashSaleNow() {
    time_t epoch = ntpClient.getEpochTime();
    time_t utcCheck = epoch - (time_t)configMgr.config().utcOffsetSeconds;
    if (utcCheck <= 1000000000) return -1;  // not NTP-synced yet — sanity check, matches loop()'s
    struct tm* t = localtime(&epoch);
    return (int64_t)(t->tm_year + 1900) * 100000000LL
         + (int64_t)(t->tm_mon + 1)     * 1000000LL
         + (int64_t)t->tm_mday          * 10000LL
         + (int64_t)t->tm_hour          * 100LL
         + (int64_t)t->tm_min;
}

// Days-since-epoch for a UTC calendar date (Howard Hinnant's civil_from_days, reversed) —
// used instead of timegm(), which this Arduino/ESP32 toolchain doesn't provide.
static time_t utcTmToEpoch(int year, int mon, int day, int hour, int min, int sec) {
    int y = year - (mon <= 2 ? 1 : 0);
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097L + (long)doe - 719468L;
    return (time_t)days * 86400L + (time_t)hour * 3600L + (time_t)min * 60L + (time_t)sec;
}

// Converts a UTC ISO-8601 timestamp from the API ("2026-07-21T18:00:00.000Z" or without
// millis) into the same local YYYYMMDDHHMM int64 scheme flashSaleNow() produces, applying the
// device's configured UTC offset the same way. Returns -1 on parse failure.
static int64_t parseIso8601ToLocalStamp(const char* iso) {
    if (!iso || !iso[0]) return -1;
    int year, mon, day, hour, min, sec;
    if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &year, &mon, &day, &hour, &min, &sec) != 6) return -1;

    time_t utcEpoch = utcTmToEpoch(year, mon, day, hour, min, sec);
    if (utcEpoch <= 0) return -1;

    time_t localEpoch = utcEpoch + (time_t)configMgr.config().utcOffsetSeconds;
    struct tm* lt = localtime(&localEpoch);
    return (int64_t)(lt->tm_year + 1900) * 100000000LL
         + (int64_t)(lt->tm_mon + 1)     * 1000000LL
         + (int64_t)lt->tm_mday          * 10000LL
         + (int64_t)lt->tm_hour          * 100LL
         + (int64_t)lt->tm_min;
}

// Shared with assertStoreIdsUnique() below and fetchFlashSale()'s unknown-item guard — collects
// every purchasable store item's id (every catalog's id column, plus the one non-catalog item,
// "right_arm_slot") into `ids`, which the caller must size to at least `cap` entries. Returns
// the count written. `cap` is checked on every write rather than trusted from the caller — every
// call site below sizes its buffer via STORE_ITEM_ID_COUNT, which is kept in sync by
// construction (it's the same catalog counts summed here), but if a future catalog is added or
// grown without updating it, this halts immediately instead of silently overrunning the
// caller's stack buffer.
static constexpr int STORE_ITEM_ID_COUNT = CAT_COLOR_COUNT + ACCESSORY_COUNT + GLASSES_COUNT + BADGE_COUNT + STUFFY_COUNT + TOY_COUNT + BLANKET_COLOR_COUNT + ROOM_THEME_COUNT + 1;
static int collectStoreItemIds(const char** ids, int cap) {
    int n = 0;
    auto push = [&](const char* id) {
        if (n >= cap) {
            Serial.println("FATAL: collectStoreItemIds() overran its capacity — a catalog was added/grown without updating STORE_ITEM_ID_COUNT");
            Serial.flush();
            abort();
        }
        ids[n++] = id;
    };
    for (int i = 0; i < CAT_COLOR_COUNT; i++) push(CAT_COLORS[i].id);
    for (int i = 0; i < ACCESSORY_COUNT; i++) push(ACCESSORIES[i].id);
    for (int i = 0; i < GLASSES_COUNT; i++) push(GLASSES[i].id);
    for (int i = 0; i < BADGE_COUNT; i++) push(BADGES[i].id);
    for (int i = 0; i < STUFFY_COUNT; i++) push(STUFFIES[i].id);
    for (int i = 0; i < TOY_COUNT; i++) push(TOYS[i].id);
    for (int i = 0; i < BLANKET_COLOR_COUNT; i++) push(BLANKET_COLORS[i].id);
    for (int i = 0; i < ROOM_THEME_COUNT; i++) push(ROOM_THEMES[i].id);
    push("right_arm_slot");
    return n;
}

// True if `id` matches some real, purchasable store item — used by fetchFlashSale() to reject a
// flash sale for an item id that doesn't exist (typo, removed item, catalog drift) rather than
// letting the device show an active "SALE!" flash for an item that can never actually go on
// sale (flashSalePrice() would never match it either, so the sale would silently do nothing
// while still visually announcing itself).
static bool isKnownStoreItemId(const char* id) {
    const char* ids[STORE_ITEM_ID_COUNT];
    int n = collectStoreItemIds(ids, STORE_ITEM_ID_COUNT);
    for (int i = 0; i < n; i++) {
        if (strcmp(ids[i], id) == 0) return true;
    }
    return false;
}

// Polls cat-buddy-api for the current flash sale and refreshes currentFlashSale. A 404 (no
// active sale right now) is expected, not an error, and clears currentFlashSale.valid — same
// "fail open, don't crash" posture as the OTA check elsewhere in this file. A network/parse
// failure leaves currentFlashSale as-is so a single flaky poll doesn't blank out a genuinely
// active sale; it'll retry next interval.
//
// TLS is required for an https:// URL — refuses to send the token rather than fall back to
// WiFiClientSecure::setInsecure() if no CA is pinned (see CAT_BUDDY_API_CA_CERT in Config.h).
// CAT_BUDDY_API_URL/TOKEN are build-time values (see Config.h + scripts/pio.sh) — a local
// apps/cyd-clock/.env (see .env.example, DIY-79) can override them to point a dev build at a
// plain http:// LAN cat-buddy-api instance, which skips TLS entirely; that's acceptable for a
// throwaway local dev token, not something a real prod deployment should ever use.
static void fetchFlashSale() {
    String url = CAT_BUDDY_API_URL;
    if (!url.length()) {
        lastFlashSalePollStatus = FlashSalePollStatus::SkippedNoUrl;
        lastFlashSalePollHttpCode = 0;
        lastFlashSalePollRawBody = "";
        return;
    }
    String token = CAT_BUDDY_API_TOKEN;
    bool isHttps = url.startsWith("https://");

    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    HTTPClient http;
    bool began = false;

    if (isHttps) {
#if CAT_BUDDY_HAS_CA_CERT
        secureClient.setCACert(CAT_BUDDY_API_CA_CERT);
        began = http.begin(secureClient, url);
#else
        lastFlashSalePollStatus = FlashSalePollStatus::SkippedNoCaCert;
        lastFlashSalePollHttpCode = 0;
        lastFlashSalePollRawBody = "";
        static bool warnedNoCaCert = false;
        if (!warnedNoCaCert) {
            Serial.println("flash-sale poll: skipped, https:// URL but no CA pinned (see Config.h, DIY-79) — use a local http:// override to test");
            warnedNoCaCert = true;
        }
        return;
#endif
    } else {
        began = http.begin(plainClient, url);
    }
    if (!began) {
        lastFlashSalePollStatus = FlashSalePollStatus::Failed;
        lastFlashSalePollDetail = "begin() failed — check the URL";
        lastFlashSalePollHttpCode = 0;
        lastFlashSalePollRawBody = "";
        return;
    }

    http.addHeader("api-key", token);
    http.setTimeout(8000);

    int code = http.GET();
    // Captured regardless of outcome (including negative HTTPClient error codes, e.g.
    // connection refused/timeout) so /config/admin/flashsale can show exactly what happened, not
    // just a summary.
    lastFlashSalePollHttpCode = code;
    String body = code > 0 ? http.getString() : http.errorToString(code);
    lastFlashSalePollRawBody = body;
    http.end();

    if (code == 404) {
        currentFlashSale.valid = false;
        lastFlashSalePollStatus = FlashSalePollStatus::NoActiveSale;
        lastFlashSalePollDetail = "";
        return;
    }
    if (code != HTTP_CODE_OK) {
        Serial.printf("flash-sale poll failed: HTTP %d\n", code);
        lastFlashSalePollStatus = FlashSalePollStatus::Failed;
        lastFlashSalePollDetail = "HTTP " + String(code);
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("flash-sale poll: JSON parse failed: %s\n", err.c_str());
        lastFlashSalePollStatus = FlashSalePollStatus::Failed;
        lastFlashSalePollDetail = String("parse error: ") + err.c_str();
        return;
    }

    const char* itemId   = doc["itemId"]  | "";
    const char* startIso = doc["startAt"] | "";
    const char* endIso   = doc["endAt"]   | "";
    int64_t start = parseIso8601ToLocalStamp(startIso);
    int64_t end   = parseIso8601ToLocalStamp(endIso);
    if (!itemId[0] || start < 0 || end < 0) {
        Serial.println("flash-sale poll: malformed response, ignoring");
        lastFlashSalePollStatus = FlashSalePollStatus::Failed;
        lastFlashSalePollDetail = "malformed response (missing/unparseable itemId, startAt, or endAt)";
        return;
    }
    // Safety net (DIY-89 piggyback): the response parsed cleanly but names an item that doesn't
    // exist in any catalog. Unlike the failure paths above (network error, bad JSON), this is a
    // fully-formed, successfully-parsed response, so it's treated as authoritative rather than
    // left alone for a retry — explicitly invalidating currentFlashSale skips the sale outright
    // instead of leaving a stale sale active or showing a "SALE!" flash for an item that can
    // never actually discount (flashSalePrice() would never match it either).
    if (!isKnownStoreItemId(itemId)) {
        Serial.printf("flash-sale poll: unknown item id \"%s\", skipping sale\n", itemId);
        // Not FlashSalePollStatus::Failed — the poll itself succeeded (HTTP 200, valid JSON);
        // it just named an item that doesn't exist. Distinguishing the two keeps
        // /config/admin/flashsale from reporting a genuinely healthy poll as "failed".
        lastFlashSalePollStatus = FlashSalePollStatus::SkippedUnknownItem;
        lastFlashSalePollDetail = String("unknown item id: ") + itemId;
        currentFlashSale.valid = false;
        return;
    }

    strlcpy(currentFlashSale.itemId, itemId, sizeof(currentFlashSale.itemId));
    currentFlashSale.start = start;
    currentFlashSale.end   = end;
    currentFlashSale.valid = true;
    lastFlashSalePollStatus = FlashSalePollStatus::Ok;
    lastFlashSalePollDetail = String(itemId);
}

// True while the last-polled flash sale is both valid and inside its window — all badges and
// on-device "SALE!" indicators should gate on this rather than naming a specific sale.
static bool isFlashSaleActive() {
    if (!currentFlashSale.valid) return false;
    int64_t now = flashSaleNow();
    if (now < 0) return false;
    return now >= currentFlashSale.start && now < currentFlashSale.end;
}

// Sale price for itemId if it's the one currently on sale, else defaultCost. Called from both
// the store-page renderer and the purchase handler so price can't drift between display and
// charge. Flat FLASH_SALE_DISCOUNT_PERCENT off defaultCost — works for any item's own cost
// without a per-item price table (see the comment above FLASH_SALE_DISCOUNT_PERCENT).
static uint32_t flashSalePrice(const char* itemId, uint32_t defaultCost) {
    if (!isFlashSaleActive() || strcmp(currentFlashSale.itemId, itemId) != 0) return defaultCost;
    uint32_t discounted = defaultCost * (100 - FLASH_SALE_DISCOUNT_PERCENT) / 100;
    return discounted > 0 ? discounted : 1;  // never free
}

// flashSalePrice() matches a sale's itemId against every store category independently, so
// two catalogs sharing an id string would both silently go on sale together. Walks
// collectStoreItemIds()'s combined id list and halts at boot if any duplicate is found —
// cheap O(n^2) over a handful of entries, run once at startup.
static void assertStoreIdsUnique() {
    const char* ids[STORE_ITEM_ID_COUNT];
    int n = collectStoreItemIds(ids, STORE_ITEM_ID_COUNT);

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (strcmp(ids[i], ids[j]) == 0) {
                Serial.printf("FATAL: store item id \"%s\" is not unique across catalogs — flash sales match by id alone\n", ids[i]);
                Serial.flush();
                abort();
            }
        }
    }
}

// ── Theme weeks (DIY-108, COM-379) ───────────────────────────────────────────
// Special theme weeks are entirely local/offline. Everything is evaluated against the device's
// own NTP-synced local clock (flashSaleNow()), with no network round trip and no cat-buddy-api
// dependency. This intentionally replaced an earlier design that polled a cat-buddy-api
// endpoint for the range (see DIY-108 history); the user decided against storing theme-week
// schedules server-side at all.
//
// Two themes exist, each hardcoded rather than driven by a generic table:
//   - "birthday" (DIY-108): the admin enters a date range on the device's 7-tap secret admin
//     page (/config/admin/themeweek), persisted as themeWeekBirthdayStartDate/EndDate.
//   - "halloween" (COM-379): fixed to Oct 24–31 inclusive every year, so it needs no setup.
//     The same admin page has an Auto/Preview/Off override (themeWeekHalloweenMode): Preview
//     forces the full Oct 31 look on any date for on-device checks, Off skips it.
// Only one theme is ever applied at a time. When both are active, Birthday wins (it's a range
// someone scheduled on purpose; Halloween is automatic). See desiredThemeWeekKey().
//
// Each theme owns one exclusive entry in ACCESSORIES[], GLASSES[] and ROOM_THEMES[], never
// buyable (see the *_IDX_* constants beside each catalog). A future theme appends its entries
// to each catalog and gives each one a fixed *_IDX_* constant. In GLASSES[] they go past
// GLASSES_STORE_COUNT, so also bump that subtraction. In ACCESSORIES[] and ROOM_THEMES[] the
// entry must be cost 0 instead: store-purchasable there means cost > 0 (isStoreAccessory()/
// isStoreRoomTheme()), because a store item (the Sorting Hat, Hogwarts at Night) already sits
// after the exclusives. The theme also gets its own
// key, isXActive() check and themeWeekItems() branch.
static constexpr const char* THEME_WEEK_BIRTHDAY  = "birthday";
static constexpr const char* THEME_WEEK_HALLOWEEN = "halloween";

// themeWeekHalloweenMode values (ConfigManager), set from the admin page.
static constexpr uint8_t HALLOWEEN_MODE_AUTO    = 0;  // Oct 24–31 from the clock (default)
static constexpr uint8_t HALLOWEEN_MODE_PREVIEW = 1;  // forced on, including the Oct 31 ghost
static constexpr uint8_t HALLOWEEN_MODE_OFF     = 2;  // never applies

// Halloween's fixed window as MMDD, inclusive at both ends, plus the ghost day.
static constexpr int HALLOWEEN_START_MMDD = 1024;
static constexpr int HALLOWEEN_END_MMDD   = 1031;
static constexpr int HALLOWEEN_DAY_MMDD   = 1031;

// Adds one calendar day to a YYYYMMDD date-only int (e.g. 20260831 -> 20260901) — used to turn
// an inclusive admin-picked end date into the exclusive end-of-day boundary
// isBirthdayWeekActive() compares against. Pure calendar-digit arithmetic: utcTmToEpoch() is used
// only as a scratch epoch to get correct month/year rollover (via gmtime()), not as any real
// UTC conversion — same trick parseIso8601ToLocalStamp() uses elsewhere for a different reason.
static int32_t addOneCalendarDay(int32_t yyyymmdd) {
    int y = yyyymmdd / 10000, mo = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
    time_t t = utcTmToEpoch(y, mo, d, 0, 0, 0) + 86400;
    struct tm* g = gmtime(&t);
    return (int32_t)(g->tm_year + 1900) * 10000 + (int32_t)(g->tm_mon + 1) * 100 + (int32_t)g->tm_mday;
}

// True while a birthday range is configured (both dates non-zero) and the device's current
// local date/time falls inside it — active for the whole of both the start and end date,
// inclusive. Expands the stored YYYYMMDD dates into the packed-decimal YYYYMMDDHHMM scheme
// flashSaleNow() produces (start at 00:00, end at 00:00 of the day *after* the picked end
// date, i.e. an exclusive boundary) and compares directly — no timezone conversion at all,
// since both sides are always the device's own local wall clock (the same clock/timezone the
// on-screen time already uses).
static bool isBirthdayWeekActive() {
    int32_t startDate = configMgr.config().themeWeekBirthdayStartDate;
    int32_t endDate   = configMgr.config().themeWeekBirthdayEndDate;
    if (startDate == 0 || endDate == 0) return false;
    int64_t start = (int64_t)startDate * 10000;                    // 00:00 of the start date
    int64_t end   = (int64_t)addOneCalendarDay(endDate) * 10000;   // 00:00 of the day after the end date
    int64_t now = flashSaleNow();
    if (now < 0) return false;
    return now >= start && now < end;
}

/**
 * Today's local month and day as MMDD (e.g. 1031), from the same local clock as the
 * on-screen time.
 *
 * @return MMDD, or -1 while the clock hasn't NTP-synced yet.
 */
static int localTodayMmdd() {
    int64_t now = flashSaleNow();
    if (now < 0) return -1;
    return (int)((now / 10000) % 10000);
}

/**
 * Whether the Halloween theme week should be applied right now, ignoring any Birthday
 * overlap (desiredThemeWeekKey() resolves that). Auto means Oct 24 00:00 up to Nov 1 00:00
 * local time, every year. Preview is always on and Off is always off.
 *
 * @return true while Halloween is active.
 */
static bool isHalloweenWeekActive() {
    uint8_t mode = configMgr.config().themeWeekHalloweenMode;
    if (mode == HALLOWEEN_MODE_OFF) return false;
    if (mode == HALLOWEEN_MODE_PREVIEW) return true;
    int mmdd = localTodayMmdd();
    return mmdd >= HALLOWEEN_START_MMDD && mmdd <= HALLOWEEN_END_MMDD;
}

/**
 * Whether the Haunted Night backdrop should include the Oct 31 ghost: on Halloween day itself
 * in Auto mode, or at any time in Preview mode so the full look can be checked on-device.
 * It's only ever visible while Haunted Night is equipped, which needs the theme applied.
 *
 * @return true when drawHauntedNightBackground() should draw the ghost.
 */
static bool isHalloweenGhostShown() {
    uint8_t mode = configMgr.config().themeWeekHalloweenMode;
    if (mode == HALLOWEEN_MODE_PREVIEW) return true;
    return mode == HALLOWEEN_MODE_AUTO && localTodayMmdd() == HALLOWEEN_DAY_MMDD;
}

/**
 * Picks which theme week should be applied right now. Birthday wins over Halloween when both
 * are active, so a birthday scheduled inside Oct 24–31 shows for its own days. Halloween then
 * takes over for whatever's left of the week.
 *
 * @return THEME_WEEK_BIRTHDAY, THEME_WEEK_HALLOWEEN, or "" for none.
 */
static const char* desiredThemeWeekKey() {
    if (isBirthdayWeekActive()) return THEME_WEEK_BIRTHDAY;
    if (isHalloweenWeekActive()) return THEME_WEEK_HALLOWEEN;
    return "";
}

// One theme week's exclusive catalog entries, as indices into ACCESSORIES[]/GLASSES[]/ROOM_THEMES[].
struct ThemeWeekItems { int accessory, glasses, roomTheme; };

/**
 * Looks up the exclusive cosmetics that belong to a theme-week key.
 *
 * @param key A THEME_WEEK_* key, e.g. the persisted activeThemeWeekKey.
 * @param out Filled with that theme's catalog indices on success.
 * @return false for an unknown key, e.g. one written by a newer firmware's backup.
 */
static bool themeWeekItems(const String& key, ThemeWeekItems& out) {
    if (key == THEME_WEEK_BIRTHDAY) {
        out = {ACCESSORY_IDX_PARTY_HAT, GLASSES_IDX_BALLOON, ROOM_THEME_IDX_BIRTHDAY};
        return true;
    }
    if (key == THEME_WEEK_HALLOWEEN) {
        out = {ACCESSORY_IDX_WITCH_HAT, GLASSES_IDX_CANDY_CORN, ROOM_THEME_IDX_HAUNTED_NIGHT};
        return true;
    }
    return false;
}

/**
 * Grants and force-equips one theme week's exclusive hat, glasses and room theme, and records
 * `key` as activeThemeWeekKey. Called once per transition by checkThemeWeekTransition(), only
 * when nothing is applied: a switch between themes reverts the old one first. It snapshots the
 * room theme the user had equipped beforehand (preThemeWeekRoomTheme), so
 * revertThemeWeekCosmetics() can restore it exactly, whatever the user changes during the week.
 *
 * @param key THEME_WEEK_BIRTHDAY or THEME_WEEK_HALLOWEEN.
 */
static void applyThemeWeekCosmetics(const char* key) {
    ThemeWeekItems items;
    if (!themeWeekItems(key, items)) return;

    configMgr.config().preThemeWeekRoomTheme = configMgr.config().equippedRoomTheme;

    configMgr.config().ownedAccessories |= (1 << items.accessory);
    configMgr.config().equippedAccessory = items.accessory;
    configMgr.config().ownedGlasses |= (1 << items.glasses);
    configMgr.config().equippedGlasses = items.glasses;
    configMgr.config().ownedRoomThemes |= (1 << items.roomTheme);
    configMgr.config().equippedRoomTheme = items.roomTheme;

    configMgr.config().activeThemeWeekKey = key;
    configMgr.save();
    dirty.animal = true;
    dirty.animalBg = true;  // the newly-equipped room theme changes the backdrop
}

/**
 * Force-removes the currently applied theme week's exclusive cosmetics and restores the
 * pre-theme-week room theme. Called once when a theme week ends, or just before switching to
 * another one. Only clears the owned bits: equippedAccessory/equippedGlasses are left as-is,
 * since equippedAccessoryIndex()/equippedGlassesIndex() already fall back gracefully once the
 * owned bit clears (see their "owned bit not set" branch). The room theme is the one exception
 * that needs an explicit restore, since its fallback would pick "lowest owned theme" rather
 * than "what the user had before". An unknown key (from a newer firmware's backup) still
 * restores the room theme and clears the key, so the device can't get stuck in that state.
 */
static void revertThemeWeekCosmetics() {
    ThemeWeekItems items;
    if (themeWeekItems(configMgr.config().activeThemeWeekKey, items)) {
        configMgr.config().ownedAccessories &= ~(1 << items.accessory);
        configMgr.config().ownedGlasses &= ~(1 << items.glasses);
        configMgr.config().ownedRoomThemes &= ~(1 << items.roomTheme);
    }

    configMgr.config().equippedRoomTheme = configMgr.config().preThemeWeekRoomTheme;
    configMgr.config().preThemeWeekRoomTheme = EQUIP_NONE;
    configMgr.config().activeThemeWeekKey = "";
    configMgr.save();
    dirty.animal = true;
    dirty.animalBg = true;
}

// Compares desiredThemeWeekKey() against activeThemeWeekKey and fires revert/apply on a
// transition. A switch (Birthday ending mid-Halloween, or a birthday scheduled inside it) is a
// revert followed by an apply: the revert restores the original room theme first, so the
// apply re-snapshots that original and not the outgoing theme's backdrop. Pure local-state
// comparison (no network), so it's cheap enough to call unconditionally from every loop()
// tick (see that call site). The admin page's handlers call it directly too, so a schedule or
// mode change takes effect on the very same request rather than waiting for the next tick.
//
// It also repaints the backdrop once when the Halloween ghost appears or disappears (local
// midnight going into and out of Oct 31, or a Preview toggle), since that change otherwise
// wouldn't redraw anything.
//
// Bails out entirely while the clock hasn't NTP-synced yet (flashSaleNow() < 0) rather than
// letting isBirthdayWeekActive()/isHalloweenWeekActive() report that as "inactive" — a device
// rebooting mid-theme-week would otherwise see a false "not active" on the first few ticks after boot (before sync),
// fire a premature revertThemeWeekCosmetics(), then re-apply moments later once the clock
// catches up. "Clock unknown" must mean "leave whatever's currently applied alone," not
// "treat the theme as over."
static void checkThemeWeekTransition() {
    if (flashSaleNow() < 0) return;
    const char* desired = desiredThemeWeekKey();
    const String& applied = configMgr.config().activeThemeWeekKey;
    if (applied != desired) {
        if (applied.length() > 0) revertThemeWeekCosmetics();
        if (desired[0] != '\0') applyThemeWeekCosmetics(desired);
    }

    static bool ghostShown = false;
    bool ghost = isHalloweenGhostShown();
    if (ghost != ghostShown) {
        ghostShown = ghost;
        if (equippedRoomThemeIndex() == ROOM_THEME_IDX_HAUNTED_NIGHT) dirty.animalBg = true;
    }
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
input,select{display:block;width:100%;padding:8px;margin-bottom:14px;background:#1e1e1e;color:#ddd;border:1px solid #333;border-radius:5px}
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
.pick.disabled{color:#666}
.pick.disabled input{cursor:not-allowed}
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
<h2 id="ccpTitle">Cat Control Panel</h2>
<a class="nav" href="/config/cat">Cat</a>
<a class="nav" href="/config/city">City (weather &amp; timezone)</a>
<a class="nav" href="/config/store">Store</a>
<a class="nav" href="/config/dress">Dressing Room</a>
<a class="nav" href="/config/badges">Badges</a>
<a class="nav" href="/config/backup">Backup</a>
<a class="nav" href="/config/update">Firmware Update</a>
<a class="nav" id="adminNav" href="/config/admin" style="display:none">Admin</a>
<script>
// Hidden entry point: tap the "Cat Control Panel" heading 7 times in a row
// (no other tap in between) to reveal the Admin nav link. Same idiom as the
// Store page's cheat-code easter egg — resets on every page load.
(function(){
    var taps = 0;
    var title = document.getElementById('ccpTitle');
    var admin = document.getElementById('adminNav');
    document.addEventListener('click', function(e){
        if (e.target === title) {
            taps++;
            if (taps >= 7) { admin.style.display = 'block'; }
        } else {
            taps = 0;
        }
    });
})();
</script>
</body></html>
)html";

static const char CONFIG_ADMIN_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel &middot; Admin</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config">&larr; Configuration</a>
<h2>Admin</h2>
<a class="nav" href="/config/admin/flashsale">Flash Sale API</a>
<a class="nav" href="/config/admin/themeweek">Theme Week</a>
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

static const char CONFIG_FLASHSALE_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel · Flash Sale API</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config/admin">&larr; Admin</a>
<h2>Flash Sale API</h2>
%%MSG%%

<h3>Status</h3>
<p>%%POLL_STATUS%%</p>
<p>Last HTTP response: <strong>%%POLL_HTTP_CODE%%</strong></p>
<pre style="background:#1e1e1e;border:1px solid #333;border-radius:5px;padding:10px;white-space:pre-wrap;word-break:break-word;font-size:.82rem;color:#ddd">%%POLL_RAW_BODY%%</pre>

<form method="POST" action="/config/admin/flashsale/check" style="margin-top:12px">
<button type="submit" style="width:100%">Poll now</button>
</form>

<p style="margin-top:20px;color:#888;font-size:.82rem">Endpoint/token are build-time values — to test against a local dev server, set them in a gitignored <code>apps/cyd-clock/.env</code> (see <code>.env.example</code>) and rebuild, rather than editing them here.</p>
</body></html>
)html";

static const char CONFIG_THEMEWEEK_HTML[] PROGMEM = R"html(<!DOCTYPE html>
<html><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Cat Control Panel &middot; Theme Week</title>
<style>%%STYLE%%</style>
</head><body>
<a class="back" href="/config/admin">&larr; Admin</a>
<h2>Theme Week</h2>
%%MSG%%
<p class="dim">Seasonal cosmetics for the cat. While a theme week is on, its hat, glasses and room theme are added and equipped, then removed again when it ends (the previous room theme comes back). Everything is stored on this device and checked against its own clock/timezone, the same one the clock screen uses. Nothing is sent anywhere. If a birthday overlaps Halloween, the birthday shows on its days.</p>

<h3>Status</h3>
<p>%%STATUS%%</p>
<p>%%HALLOWEEN_STATUS%%</p>

<h3>Birthday</h3>
<form method="POST" action="/config/admin/themeweek/set">
<label>Start date<input type="date" name="start" value="%%START_VALUE%%" required></label>
<label>End date<input type="date" name="end" value="%%END_VALUE%%" required></label>
<button type="submit" style="width:100%">Schedule</button>
</form>
<p class="dim" style="font-size:.82rem">Dates are calendar days, not a specific time — the theme is active from the start of the start date through the end of the end date. Scheduling replaces any range set previously.</p>

<form method="POST" action="/config/admin/themeweek/clear" style="margin-top:12px">
<button type="submit" style="width:100%">Clear birthday</button>
</form>

<h3>Halloween</h3>
<form method="POST" action="/config/admin/themeweek/halloween">
<label>Mode<select name="mode">
<option value="auto"%%HW_AUTO%%>Auto (Oct 24 &ndash; Oct 31)</option>
<option value="preview"%%HW_PREVIEW%%>Preview now (includes the Oct 31 ghost)</option>
<option value="off"%%HW_OFF%%>Off</option>
</select></label>
<button type="submit" style="width:100%">Save</button>
</form>
<p class="dim" style="font-size:.82rem">Preview stays on until you switch back to Auto.</p>
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

<h3>Stuffies</h3>
%%STUFFY_ITEMS%%

<h3>Blankets (night only)</h3>
%%BLANKET_ITEMS%%

<h3>Room Themes</h3>
%%ROOM_THEME_ITEMS%%

<h3>Accessories - Head</h3>
%%ACCESSORY_ITEMS%%

<h3>Glasses</h3>
%%GLASSES_ITEMS%%

<h3>Badges - Chest</h3>
%%BADGE_ITEMS%%

<h3>Toys (for the Right Arm slot)</h3>
%%TOY_ITEMS%%

<h3>Right Arm Slot (day &amp; night — holds either a Buddy or a Toy)</h3>
%%RIGHT_ARM_SLOT_ITEM%%

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

<h3>Stuffies</h3>
%%STUFFY_OPTIONS%%

<h3>Right Arm Slot (day &amp; night — pick a Buddy or a Toy)</h3>
%%RIGHT_ARM_OPTIONS%%

<h3>Blankets (night only)</h3>
%%BLANKET_OPTIONS%%

<h3>Room Themes</h3>
%%ROOM_THEME_OPTIONS%%

<h3>Accessories - Head</h3>
%%ACCESSORY_OPTIONS%%

<h3>Glasses</h3>
%%GLASSES_OPTIONS%%

<h3>Badges - Chest</h3>
%%BADGE_OPTIONS%%

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

// Every config/store page renders live device state (points, owned items, flash-sale
// status, ...) fresh per request — but without an explicit no-store, a browser is free to
// serve a stale cached copy indefinitely instead of re-fetching (e.g. a tab left open
// during the black cat sale would keep showing the SALE badge long after it actually ended).
static void sendHtmlPage(const String& page) {
    wm.server->sendHeader("Cache-Control", "no-store");
    wm.server->send(200, "text/html", page);
}

// A config page template with the shared stylesheet substituted for its %%STYLE%% slot.
static String loadPage(const char* html) {
    String page = String(FPSTR(html));
    page.replace("%%STYLE%%", String(FPSTR(CONFIG_STYLE)));
    return page;
}

static void handleConfigHome() {
    if (!configMgr.config().setupComplete) {
        wm.server->sendHeader("Location", "/setup");
        wm.server->send(302, "text/plain", "");
        return;
    }
    String page = loadPage(CONFIG_HOME_HTML);
    sendHtmlPage(page);
}

static void handleConfigAdminGet() {
    String page = loadPage(CONFIG_ADMIN_HTML);
    sendHtmlPage(page);
}

// First-run setup wizard: reached via the on-device "complete setup at <ip>" screen (see
// drawSetupPrompt()) once Wi-Fi is connected but configMgr.config().setupComplete is still
// false. Only offers the free-tier solid colors (STORE_COST_CAT_COLOR_SOLID) — tabby/calico
// stay store-only purchases, same as the "maybe" colors DIY-48's card described.
static void handleSetupGet() {
    String page = loadPage(CONFIG_SETUP_HTML);

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
    if (!sanitizeCatName(name)) {
        wm.server->sendHeader("Location", "/setup?err=name");
        wm.server->send(302, "text/plain", "");
        return;
    }
    // No fallback exists yet at this point in the flow, so an empty submission still
    // needs a concrete default here, unlike the dressing room's rename fields.
    if (name.length() == 0) name = "Biscuit";

    float lat = wm.server->arg("lat").toFloat();
    float lon = wm.server->arg("lon").toFloat();
    int   utc = wm.server->arg("utc").toInt();
    if (lat < -90.0f || lat > 90.0f || lon < -180.0f || lon > 180.0f || utc < -50400 || utc > 50400) {
        wm.server->sendHeader("Location", "/setup?err=city");
        wm.server->send(302, "text/plain", "");
        return;
    }

    configMgr.config().points  = 70;
    int nameIdx = -1;  // -1 = white
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
            nameIdx = idx;
        } else {
            configMgr.config().equippedCatColor = EQUIP_NONE;
        }
    }
    // "The first name given" — becomes the fallback for every other color owned now or
    // bought later, until each is individually renamed (see getCatName()/DIY-56).
    configMgr.config().catNameDefault = name;
    setCatName(nameIdx, name);
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
    String page = loadPage(ROOT_MENU_HTML);
    sendHtmlPage(page);
}

static void handleConfigCatGet() {
    String page = loadPage(CONFIG_CAT_HTML);
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
    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Settings saved.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

static void handleConfigCityGet() {
    String page = loadPage(CONFIG_CITY_HTML);
    page.replace("%%LAT%%", String(configMgr.config().latitude,  4));
    page.replace("%%LON%%", String(configMgr.config().longitude, 4));
    page.replace("%%UTC%%", String(configMgr.config().utcOffsetSeconds));
    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Settings saved.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

// The eight item catalogs, as one switchable category (COM-387). Each catalog has its own
// struct type, so the store and dressing-room handlers used to repeat the same row/radio/lookup
// code once per catalog; they now loop over these categories and go through the accessors
// below instead. Enum order is the store page's section order and the purchase lookup's
// search order. Catalogs are still walked by index with the same owned-bitmask checks, so
// the append-only catalog rule is untouched.
enum ItemCategory : uint8_t {
    ITEM_STUFFY, ITEM_BLANKET, ITEM_ROOM_THEME, ITEM_CAT_COLOR,
    ITEM_ACCESSORY, ITEM_GLASSES, ITEM_BADGE, ITEM_TOY, ITEM_CATEGORY_COUNT
};

// One catalog entry's web-facing fields. `webColor` is nullptr for catalogs whose labels are
// never colored (stuffies, toys); a room theme without its own webColor reports "#fff".
struct CatalogEntry {
    const char* id;
    const char* label;
    const char* webColor;
    uint32_t cost;
};

static constexpr int ITEM_CATALOG_COUNTS[ITEM_CATEGORY_COUNT] = {
    STUFFY_COUNT, BLANKET_COLOR_COUNT, ROOM_THEME_COUNT, CAT_COLOR_COUNT,
    ACCESSORY_COUNT, GLASSES_COUNT, BADGE_COUNT, TOY_COUNT,
};

static CatalogEntry catalogEntry(ItemCategory cat, int i) {
    switch (cat) {
        case ITEM_STUFFY:     return {STUFFIES[i].id, STUFFIES[i].label, nullptr, STUFFIES[i].cost};
        case ITEM_BLANKET:    return {BLANKET_COLORS[i].id, BLANKET_COLORS[i].label, BLANKET_COLORS[i].webColor, STORE_COST_BLANKET};
        case ITEM_ROOM_THEME: return {ROOM_THEMES[i].id, ROOM_THEMES[i].label,
                                      ROOM_THEMES[i].webColor ? ROOM_THEMES[i].webColor : "#fff", ROOM_THEMES[i].cost};
        case ITEM_CAT_COLOR:  return {CAT_COLORS[i].id, CAT_COLORS[i].label, CAT_COLORS[i].webColor, CAT_COLORS[i].cost};
        case ITEM_ACCESSORY:  return {ACCESSORIES[i].id, ACCESSORIES[i].label, ACCESSORIES[i].webColor, ACCESSORIES[i].cost};
        case ITEM_GLASSES:    return {GLASSES[i].id, GLASSES[i].label, GLASSES[i].webColor, GLASSES[i].cost};
        case ITEM_BADGE:      return {BADGES[i].id, BADGES[i].label, BADGES[i].webColor, BADGES[i].cost};
        default:              return {TOYS[i].id, TOYS[i].label, nullptr, TOYS[i].cost};
    }
}

// Whether entry `i` is sold in the store. The theme-week exclusives (DIY-108) are skipped:
// cost-0 entries in ACCESSORIES[]/ROOM_THEMES[], and the tail of GLASSES[] past
// GLASSES_STORE_COUNT. They must never show up or be purchasable, whatever item id is posted.
static bool isStoreItem(ItemCategory cat, int i) {
    switch (cat) {
        case ITEM_ROOM_THEME: return isStoreRoomTheme(i);
        case ITEM_ACCESSORY:  return isStoreAccessory(i);
        case ITEM_GLASSES:    return i < GLASSES_STORE_COUNT;
        case ITEM_BADGE:      return i < BADGE_STORE_COUNT;
        default:              return true;
    }
}

// The category's owned bitmask from ConfigManager, widened to 32 bits (the fields are a mix
// of uint8_t and uint16_t).
static uint32_t ownedItems(ItemCategory cat) {
    const AppConfig& c = configMgr.config();
    switch (cat) {
        case ITEM_STUFFY:     return c.ownedStuffies;
        case ITEM_BLANKET:    return c.ownedBlanketColors;
        case ITEM_ROOM_THEME: return c.ownedRoomThemes;
        case ITEM_CAT_COLOR:  return c.ownedCatColors;
        case ITEM_ACCESSORY:  return c.ownedAccessories;
        case ITEM_GLASSES:    return c.ownedGlasses;
        case ITEM_BADGE:      return c.ownedBadges;
        default:              return c.ownedToys;
    }
}

// Sets or clears one bit of the category's owned bitmask.
static void setItemOwned(ItemCategory cat, int i, bool owned) {
    AppConfig& c = configMgr.config();
    uint32_t mask = ownedItems(cat);
    mask = owned ? (mask | (1u << i)) : (mask & ~(1u << i));
    switch (cat) {
        case ITEM_STUFFY:     c.ownedStuffies      = mask; break;
        case ITEM_BLANKET:    c.ownedBlanketColors = mask; break;
        case ITEM_ROOM_THEME: c.ownedRoomThemes    = mask; break;
        case ITEM_CAT_COLOR:  c.ownedCatColors     = mask; break;
        case ITEM_ACCESSORY:  c.ownedAccessories   = mask; break;
        case ITEM_GLASSES:    c.ownedGlasses       = mask; break;
        case ITEM_BADGE:      c.ownedBadges        = mask; break;
        default:              c.ownedToys          = mask; break;
    }
}

// The category's equipped index in ConfigManager. Toys have no single equipped field of their
// own here; their slot is the shared right arm (equippedToy plus equippedRightArmKind).
static uint8_t* equippedItemField(ItemCategory cat) {
    AppConfig& c = configMgr.config();
    switch (cat) {
        case ITEM_STUFFY:     return &c.equippedStuffy;
        case ITEM_BLANKET:    return &c.equippedBlanketColor;
        case ITEM_ROOM_THEME: return &c.equippedRoomTheme;
        case ITEM_CAT_COLOR:  return &c.equippedCatColor;
        case ITEM_ACCESSORY:  return &c.equippedAccessory;
        case ITEM_GLASSES:    return &c.equippedGlasses;
        case ITEM_BADGE:      return &c.equippedBadge;
        default:              return &c.equippedToy;
    }
}

// Index of the entry whose id is `id`, or -1. `storeOnly` restricts the search to
// isStoreItem() entries, for purchases.
static int findCatalogIndex(ItemCategory cat, const String& id, bool storeOnly) {
    for (int i = 0; i < ITEM_CATALOG_COUNTS[cat]; i++) {
        if (storeOnly && !isStoreItem(cat, i)) continue;
        if (id == catalogEntry(cat, i).id) return i;
    }
    return -1;
}

/**
 * The tags shown after a store item's label: a gold "LEGENDARY" marker for anything priced at
 * the legendary tier (COM-382), and the red "SALE" tag while a flash sale has cut its price.
 * Shared by every store section so the two tags always look the same everywhere.
 *
 * @param baseCost The item's catalog price, before any flash sale.
 * @param cost The price actually charged, from flashSalePrice().
 */
static void appendStoreItemTags(String& html, uint32_t baseCost, uint32_t cost) {
    if (baseCost >= STORE_COST_LEGENDARY) html += " <span style='color:#ffcc33;font-weight:bold'>\xE2\x98\x85 LEGENDARY</span>";
    if (cost != baseCost) html += " <span style='color:#ff4444;font-weight:bold'>\xF0\x9F\x94\xA5 SALE</span>";
}

/**
 * Appends one store row: the label, its tags, then either the owned marker or a buy form that
 * posts `item` to /save-config/store.
 *
 * @param webColor CSS color for the label, or nullptr for an uncolored label.
 * @param labelPrefix Text before the label (e.g. "Blanket - "), or "".
 * @param ownedText The owned marker's text, or nullptr when the item can still be bought.
 * @param buyText The buy button's text before the price ("Buy for " / "Buy 2nd for ").
 */
static void appendStoreItemRow(String& html, const char* item, const char* webColor,
                               const char* labelPrefix, const char* label, uint32_t baseCost,
                               const char* ownedText, const char* buyText, uint32_t points) {
    uint32_t cost = flashSalePrice(item, baseCost);
    html += "<div class='item'><span";
    if (webColor) { html += " style='color:"; html += webColor; html += "'"; }
    html += ">";
    html += labelPrefix;
    html += label;
    html += "</span>";
    appendStoreItemTags(html, baseCost, cost);
    if (ownedText) {
        html += "<span class='owned'>";
        html += ownedText;
        html += "</span>";
    } else {
        html += "<form method='POST' action='/save-config/store'><input type='hidden' name='item' value='";
        html += item;
        html += "'><button type='submit'";
        if (points < cost) html += " disabled";
        html += ">";
        html += buyText;
        html += String(cost);
        html += "</button></form>";
    }
    html += "</div>\n";
}

// Store row for catalog entry `i` of `cat`. Stuffies can be bought twice (DIY-106), so the
// same item id posts a second purchase once the first copy is owned, labeled "Buy 2nd for N"
// so it's clear the button re-buys the same item; only owning both copies shows as owned.
static void appendStoreRow(String& html, ItemCategory cat, int i, uint32_t points) {
    static const char* const LABEL_PREFIXES[ITEM_CATEGORY_COUNT] = {"", "Blanket - ", "", "Cat - ", "", "", "", ""};
    CatalogEntry e = catalogEntry(cat, i);
    bool owned = ownedItems(cat) & (1u << i);
    const char* ownedText = owned ? "Owned" : nullptr;
    const char* buyText = "Buy for ";
    if (cat == ITEM_STUFFY) {
        bool ownedSecond = configMgr.config().ownedStuffiesSecond & (1u << i);
        ownedText = ownedSecond ? "Owned (x2)" : nullptr;
        if (owned) buyText = "Buy 2nd for ";
    }
    appendStoreItemRow(html, e.id, e.webColor, LABEL_PREFIXES[cat], e.label, e.cost, ownedText, buyText, points);
}

// Each category's slot in CONFIG_STORE_HTML, in ItemCategory order.
static const char* const STORE_ITEM_PLACEHOLDERS[ITEM_CATEGORY_COUNT] = {
    "%%STUFFY_ITEMS%%", "%%BLANKET_ITEMS%%", "%%ROOM_THEME_ITEMS%%", "%%CAT_COLOR_ITEMS%%",
    "%%ACCESSORY_ITEMS%%", "%%GLASSES_ITEMS%%", "%%BADGE_ITEMS%%", "%%TOY_ITEMS%%",
};

static void handleConfigStoreGet() {
    // Clear the on-device points flash by stamping the current catalog sizes as "seen" —
    // any store visit acknowledges every item added up to this point.
    if (hasNewStoreItems()) {
        configMgr.config().seenStuffyCount       = (uint8_t)STUFFY_COUNT;
        configMgr.config().seenBlanketColorCount = (uint8_t)BLANKET_COLOR_COUNT;
        configMgr.config().seenRoomThemeCount    = (uint8_t)ROOM_THEME_STORE_COUNT;
        configMgr.config().seenCatColorCount     = (uint8_t)CAT_COLOR_COUNT;
        configMgr.config().seenAccessoryCount    = (uint8_t)ACCESSORY_STORE_COUNT;
        configMgr.config().seenGlassesCount      = (uint8_t)GLASSES_STORE_COUNT;
        configMgr.config().seenBadgeCount        = (uint8_t)BADGE_STORE_COUNT;
        configMgr.config().seenToyCount          = (uint8_t)TOY_COUNT;
        configMgr.config().seenRightArmSlot      = true;
        configMgr.save();
    }
    String page = loadPage(CONFIG_STORE_HTML);
    uint32_t points = configMgr.config().points;
    page.replace("%%POINTS%%", String(points));
    for (int c = 0; c < ITEM_CATEGORY_COUNT; c++) {
        ItemCategory cat = (ItemCategory)c;
        // White isn't a CAT_COLORS[] entry (it's the always-available default, equipped via
        // EQUIP_NONE — see the comment above that catalog) but the wizard and Dress page both
        // list it as an explicit choice, so show it here too rather than have it look missing.
        String rows = cat == ITEM_CAT_COLOR
            ? "<div class='item'><span>Cat - White</span><span class='owned'>Owned</span></div>\n" : "";
        for (int i = 0; i < ITEM_CATALOG_COUNTS[cat]; i++) {
            if (isStoreItem(cat, i)) appendStoreRow(rows, cat, i, points);
        }
        page.replace(STORE_ITEM_PLACEHOLDERS[cat], rows);
    }
    String rightArmSlotItem;
    appendStoreItemRow(rightArmSlotItem, "right_arm_slot", nullptr, "", "Right Arm Slot", STORE_COST_RIGHT_ARM_SLOT,
                       configMgr.config().rightArmSlotUnlocked ? "Owned" : nullptr, "Buy for ", points);
    page.replace("%%RIGHT_ARM_SLOT_ITEM%%", rightArmSlotItem);
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
    String page = loadPage(CONFIG_BADGES_HTML);
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
    uint32_t prevXp = configMgr.config().totalXp;
    configMgr.config().totalXp = 0;
    if (!configMgr.save()) {
        configMgr.config().totalXp = prevXp;  // roll back in-memory state since persistence failed
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
        AwardXpResult xpResult = awardXp((uint32_t)amount);
        if (xpResult.leveledUp) triggerFireworks(xpResult.bonusPoints);
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
    AppConfig prevConfig = configMgr.config();
    configMgr.resetToDefaults();
    if (!configMgr.save()) {
        configMgr.config() = prevConfig;  // roll back in-memory state since persistence failed
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
    String page = loadPage(CONFIG_BACKUP_HTML);
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
    // importBackupJson() merges into the live config, so keep a copy to roll back to if the
    // restore can't be persisted (same idea as the store purchase handler's rollback).
    AppConfig prevConfig = configMgr.config();
    if (!configMgr.importBackupJson(json)) {
        wm.server->sendHeader("Location", "/config/backup?err=parse");
        wm.server->send(302, "text/plain", "");
        return;
    }
    if (!configMgr.save()) {
        configMgr.config() = prevConfig;  // roll back in-memory state since persistence failed
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
    String page = loadPage(CONFIG_UPDATE_HTML);
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

// Human-readable summary of lastFlashSalePollStatus/currentFlashSale for the /config/admin/flashsale
// page — same "surface exactly why nothing happened" idea as the update page's skipped/failed
// banners above.
static String flashSalePollStatusText() {
    switch (lastFlashSalePollStatus) {
        case FlashSalePollStatus::NeverPolled:
            return "Never polled yet (polls automatically every " + String(FLASH_SALE_POLL_INTERVAL_MS / 60000UL) + " min, or use \"Poll now\" below).";
        case FlashSalePollStatus::SkippedNoUrl:
            return "Skipped — no API URL configured (set one below, or rebuild with CAT_BUDDY_API_URL set).";
        case FlashSalePollStatus::SkippedNoCaCert:
            return "Skipped — the configured URL is https:// but no CA is pinned in this firmware build (CAT_BUDDY_API_CA_CERT). Use a http:// URL for local testing instead.";
        case FlashSalePollStatus::Failed:
            return "Last poll failed: " + htmlEscape(lastFlashSalePollDetail);
        case FlashSalePollStatus::SkippedUnknownItem:
            return "Last poll succeeded, but the sale was skipped — " + htmlEscape(lastFlashSalePollDetail) + ".";
        case FlashSalePollStatus::NoActiveSale:
            return "Last poll succeeded — no active sale right now.";
        case FlashSalePollStatus::Ok: {
            String s = "Last poll succeeded — <strong>" + htmlEscape(lastFlashSalePollDetail) + "</strong> on sale";
            s += isFlashSaleActive() ? " (currently active)." : " (window not active right now).";
            return s;
        }
    }
    return "";
}

static void handleConfigFlashSaleGet() {
    String page = loadPage(CONFIG_FLASHSALE_HTML);
    page.replace("%%POLL_STATUS%%", flashSalePollStatusText());
    page.replace("%%POLL_HTTP_CODE%%", lastFlashSalePollHttpCode == 0 ? "(no request made)" : String(lastFlashSalePollHttpCode));
    page.replace("%%POLL_RAW_BODY%%", lastFlashSalePollRawBody.length() ? htmlEscape(lastFlashSalePollRawBody) : "(empty)");
    String msg = "";
    if (wm.server->hasArg("checked")) msg = "<div class='banner ok'>Polled — see status below.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

static void handleConfigFlashSaleCheckPost() {
    fetchFlashSale();
    wm.server->sendHeader("Location", "/config/admin/flashsale?checked=1");
    wm.server->send(302, "text/plain", "");
}

// Formats a YYYYMMDD date-only int as "YYYY-MM-DD", for pre-filling an <input type=date>. 0
// (unset) formats as "".
static String dateToInputValue(int32_t yyyymmdd) {
    if (yyyymmdd <= 0) return "";
    int y = yyyymmdd / 10000, mo = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, mo, d);
    return String(buf);
}

// Parses an <input type=date> value ("YYYY-MM-DD") into a YYYYMMDD int. Returns 0 on parse
// failure.
static int32_t parseDateInput(const String& s) {
    int y, mo, d;
    if (sscanf(s.c_str(), "%d-%d-%d", &y, &mo, &d) != 3) return 0;
    return (int32_t)y * 10000 + (int32_t)mo * 100 + (int32_t)d;
}

static constexpr const char* MONTH_ABBREV[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
};

// Formats a YYYYMMDD date-only int as "Aug 24, 2026", for the human-readable status line —
// e.g. via formatDateRange() below.
static String formatDate(int32_t yyyymmdd) {
    int y = yyyymmdd / 10000, mo = (yyyymmdd / 100) % 100, d = yyyymmdd % 100;
    if (mo < 1 || mo > 12) return String(yyyymmdd);  // malformed, fall back to raw digits
    return String(MONTH_ABBREV[mo - 1]) + " " + String(d) + ", " + String(y);
}

// Formats a start/end YYYYMMDD pair as "Aug 24 – Aug 31, 2026" (year shown once, at the end)
// when both dates share a year, or "Dec 28, 2026 – Jan 3, 2027" (full date on both sides)
// when a range spans a year boundary.
static String formatDateRange(int32_t startDate, int32_t endDate) {
    int startYear = startDate / 10000, endYear = endDate / 10000;
    if (startYear == endYear) {
        int mo = (startDate / 100) % 100, d = startDate % 100;
        String startPart = (mo >= 1 && mo <= 12) ? (String(MONTH_ABBREV[mo - 1]) + " " + String(d)) : String(startDate);
        return startPart + " \xE2\x80\x93 " + formatDate(endDate);  // "\xE2\x80\x93" = en dash (UTF-8)
    }
    return formatDate(startDate) + " \xE2\x80\x93 " + formatDate(endDate);
}

// Human-readable summary of the locally-stored birthday range for the
// /config/admin/themeweek page.
static String themeWeekStatusText() {
    int32_t startDate = configMgr.config().themeWeekBirthdayStartDate;
    int32_t endDate   = configMgr.config().themeWeekBirthdayEndDate;
    if (startDate == 0 || endDate == 0) return "No birthday scheduled.";

    String s = "Birthday: <strong>" + formatDateRange(startDate, endDate) + "</strong>";
    if (isBirthdayWeekActive()) {
        s += " (currently active).";
    } else {
        int64_t now = flashSaleNow();
        int64_t end = (int64_t)addOneCalendarDay(endDate) * 10000;
        s += (now >= 0 && now >= end) ? " (already ended)." : " (not active yet).";
    }
    return s;
}

/**
 * One-line summary of the Halloween theme week for the /config/admin/themeweek page: the
 * override mode, and in Auto mode whether it's active now or held off by an overlapping
 * birthday.
 *
 * @return HTML-safe status text.
 */
static String halloweenStatusText() {
    uint8_t mode = configMgr.config().themeWeekHalloweenMode;
    if (mode == HALLOWEEN_MODE_OFF) return "Halloween: <strong>off</strong> (won't apply).";
    if (mode == HALLOWEEN_MODE_PREVIEW) {
        return String("Halloween: <strong>preview</strong> (forced on, with the Oct 31 ghost)") +
               (isBirthdayWeekActive() ? ", waiting for the birthday to end." : ".");
    }
    String s = "Halloween: <strong>Oct 24 \xE2\x80\x93 Oct 31</strong>, automatic";
    if (!isHalloweenWeekActive())      s += " (not active now).";
    else if (isBirthdayWeekActive())   s += " (birthday is showing instead).";
    else                               s += " (currently active).";
    return s;
}

static void handleConfigThemeWeekGet() {
    String page = loadPage(CONFIG_THEMEWEEK_HTML);
    page.replace("%%STATUS%%", themeWeekStatusText());
    page.replace("%%HALLOWEEN_STATUS%%", halloweenStatusText());
    uint8_t mode = configMgr.config().themeWeekHalloweenMode;
    page.replace("%%HW_AUTO%%",    mode == HALLOWEEN_MODE_AUTO    ? " selected" : "");
    page.replace("%%HW_PREVIEW%%", mode == HALLOWEEN_MODE_PREVIEW ? " selected" : "");
    page.replace("%%HW_OFF%%",     mode == HALLOWEEN_MODE_OFF     ? " selected" : "");
    page.replace("%%START_VALUE%%", dateToInputValue(configMgr.config().themeWeekBirthdayStartDate));
    page.replace("%%END_VALUE%%", dateToInputValue(configMgr.config().themeWeekBirthdayEndDate));
    String msg = "";
    if (wm.server->hasArg("scheduled")) msg = "<div class='banner ok'>Scheduled — see status below.</div>";
    else if (wm.server->hasArg("cleared")) msg = "<div class='banner ok'>Cleared.</div>";
    else if (wm.server->hasArg("halloween")) msg = "<div class='banner ok'>Halloween mode saved.</div>";
    else if (wm.server->hasArg("err")) msg = "<div class='banner err'>End date must be on or after the start date.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

static void handleConfigThemeWeekSetPost() {
    int32_t start = parseDateInput(wm.server->arg("start"));
    int32_t end   = parseDateInput(wm.server->arg("end"));
    if (start == 0 || end == 0 || end < start) {
        wm.server->sendHeader("Location", "/config/admin/themeweek?err=1");
        wm.server->send(302, "text/plain", "");
        return;
    }
    configMgr.config().themeWeekBirthdayStartDate = start;
    configMgr.config().themeWeekBirthdayEndDate   = end;
    configMgr.save();
    checkThemeWeekTransition();  // apply immediately if the new range is already active
    wm.server->sendHeader("Location", "/config/admin/themeweek?scheduled=1");
    wm.server->send(302, "text/plain", "");
}

static void handleConfigThemeWeekClearPost() {
    configMgr.config().themeWeekBirthdayStartDate = 0;
    configMgr.config().themeWeekBirthdayEndDate   = 0;
    configMgr.save();
    checkThemeWeekTransition();  // revert immediately if it was currently applied
    wm.server->sendHeader("Location", "/config/admin/themeweek?cleared=1");
    wm.server->send(302, "text/plain", "");
}

/**
 * POST /config/admin/themeweek/halloween: saves the Halloween override (`mode` = auto,
 * preview or off) and runs checkThemeWeekTransition() straight away, so Preview applies and
 * Off reverts on this request. Redirects back to the theme-week page, or answers 400 for an
 * unknown mode.
 */
static void handleConfigThemeWeekHalloweenPost() {
    String m = wm.server->arg("mode");
    uint8_t mode;
    if (m == "auto")         mode = HALLOWEEN_MODE_AUTO;
    else if (m == "preview") mode = HALLOWEEN_MODE_PREVIEW;
    else if (m == "off")     mode = HALLOWEEN_MODE_OFF;
    else {
        wm.server->send(400, "text/plain", "Invalid mode");
        return;
    }
    configMgr.config().themeWeekHalloweenMode = mode;
    configMgr.save();
    checkThemeWeekTransition();
    wm.server->sendHeader("Location", "/config/admin/themeweek?halloween=1");
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
    if (hunger < 1 || hunger > 1440 || boredom < 1 || boredom > 1440
        || sickCooldown < 1 || sickCooldown > 168
        || forceSickMinutes < 0 || forceSickMinutes > 1440
        || thirstForceMinutes < 1 || thirstForceMinutes > 1440
        || forceThirstMinutes < 0 || forceThirstMinutes > 1440
        || !sleepBedOk || !sleepWakeOk) {
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
    ItemCategory cat = ITEM_CATEGORY_COUNT;  // ITEM_CATEGORY_COUNT = the right arm slot, not a catalog item
    int idx = -1;
    // Store-only lookup (isStoreItem()): the theme-week-exclusive entries (DIY-108, in
    // ROOM_THEMES[]/ACCESSORIES[]/GLASSES[]) must never be purchasable here, no matter what
    // item id is posted.
    for (int c = 0; c < ITEM_CATEGORY_COUNT && idx < 0; c++) {
        idx = findCatalogIndex((ItemCategory)c, item, true);
        if (idx >= 0) cat = (ItemCategory)c;
    }
    if (idx >= 0) {
        CatalogEntry e = catalogEntry(cat, idx);
        cost = flashSalePrice(e.id, e.cost);
        // Stuffies can be bought twice (DIY-106) — only maxed at 2 copies counts as
        // "already owned" here; owning just the 1st copy still allows this same item id
        // to be bought again for the 2nd, handled in the purchase branch below.
        alreadyOwned = (cat == ITEM_STUFFY ? configMgr.config().ownedStuffiesSecond : ownedItems(cat)) & (1u << idx);
    } else if (item == "right_arm_slot") {
        cost = flashSalePrice("right_arm_slot", STORE_COST_RIGHT_ARM_SLOT);
        alreadyOwned = configMgr.config().rightArmSlotUnlocked;
    } else {
        wm.server->send(400, "text/plain", "Unknown item");
        return;
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
    // 1st copy already owned — this purchase is for the 2nd copy (DIY-106). Doesn't touch
    // equippedStuffy/equippedStuffyRight; the user picks which arm gets it via the dressing
    // room, same as unlocking the right arm slot doesn't auto-equip either.
    bool secondStuffy = cat == ITEM_STUFFY && (configMgr.config().ownedStuffies & (1u << idx));
    // Snapshot the slot an auto-equip below may overwrite, so a failed save restores it too
    // rather than leaving it pointing at an item that's no longer owned.
    uint8_t* equipped = idx >= 0 ? equippedItemField(cat) : nullptr;
    uint8_t prevEquipped = equipped ? *equipped : EQUIP_NONE;
    if (idx < 0) {
        configMgr.config().rightArmSlotUnlocked = true;  // starts empty — see equippedStuffyRightIndex()
    } else if (secondStuffy) {
        configMgr.config().ownedStuffiesSecond |= (1u << idx);
    } else {
        setItemOwned(cat, idx, true);
        // A newly bought item becomes equipped, except a toy — a toy shares the right-arm slot
        // with a stuffy (DIY-110), so buying one shouldn't silently switch the slot away from
        // whatever's already equipped there. Same reasoning as why buying a stuffy never
        // auto-equips it to the right arm either.
        if (cat != ITEM_TOY) *equippedItemField(cat) = idx;
    }
    if (!configMgr.save()) {
        // Roll back in-memory state since persistence failed.
        configMgr.config().points += cost;
        if (idx < 0) configMgr.config().rightArmSlotUnlocked = false;
        else if (secondStuffy) configMgr.config().ownedStuffiesSecond &= ~(1u << idx);
        else setItemOwned(cat, idx, false);
        if (equipped) *equipped = prevEquipped;
        wm.server->sendHeader("Location", "/config/store?err=save");
        wm.server->send(302, "text/plain", "");
        return;
    }
    dirty.animal = true;
    dirty.animalBg = true;  // a newly bought room theme auto-equips, changing the backdrop
    wm.server->sendHeader("Location", "/config/store?saved=1");
    wm.server->send(302, "text/plain", "");
}

/**
 * Appends one dressing-room radio option.
 *
 * @param valuePrefix Prepended to `value` in the posted value ("stuffy:" / "toy:" for the
 *     combined right-arm field), or "".
 * @param webColor CSS color for the label, or nullptr for an uncolored label.
 * @param disabled Greys out the option and disables its radio.
 * @param note Raw text after the label, inside the option (e.g. " (also on left arm)"), or "".
 */
static void appendPickRadio(String& html, const char* field, const char* valuePrefix, const char* value,
                            const char* label, const char* webColor, bool checked,
                            bool disabled = false, const char* note = "") {
    html += "<label class='pick";
    if (disabled) html += " disabled";
    html += "'><input type='radio' name='";
    html += field;
    html += "' value='";
    html += valuePrefix;
    html += value;
    html += "'";
    if (checked) html += " checked";
    if (disabled) html += " disabled";
    html += "> ";
    if (webColor) { html += "<span style='color:"; html += webColor; html += "'>"; }
    html += label;
    if (webColor) html += "</span>";
    html += note;
    html += "</label>";
}

static const char NOT_OWNED_HTML[] = "<p style='color:#888'>Not owned yet — visit the Store.</p>";

// A "None" option plus one option per owned entry of `cat`, or the not-owned notice if none
// are owned. Used by every single-slot picker on the dressing room page.
static String buildPickGroup(const char* field, ItemCategory cat, int equippedIdx) {
    uint32_t owned = ownedItems(cat);
    if (owned == 0) return NOT_OWNED_HTML;
    String html;
    appendPickRadio(html, field, "", "none", "None", nullptr, equippedIdx < 0);
    for (int i = 0; i < ITEM_CATALOG_COUNTS[cat]; i++) {
        if (!(owned & (1u << i))) continue;
        CatalogEntry e = catalogEntry(cat, i);
        appendPickRadio(html, field, "", e.id, e.label, e.webColor, i == equippedIdx);
    }
    return html;
}

// One option per owned stuffy, shared by the left ("stuffy") and right-arm ("rightArm",
// values prefixed "stuffy:") slot pickers. `otherArmIdx` is the *other* slot's
// currently-equipped stuffy (< 0 if none): matching it greys out and disables that option
// here, so the user can't select a stuffy that's already on the other arm in the first place
// — see the same-stuffy-on-both-arms guard in handleConfigDressPost() below, which this keeps
// the user from ever needing to hit. Owning a 2nd copy of that stuffy (DIY-106) lifts the
// disable, since a 2nd copy means there's genuinely one for each arm — `otherArmNote` (e.g.
// " (also on right arm)") still shows so it's clear that arm is also wearing it.
static void appendStuffyRadios(String& html, const char* field, const char* valuePrefix,
                               int equippedIdx, int otherArmIdx, const char* otherArmNote) {
    uint16_t ownedStuffies = configMgr.config().ownedStuffies;
    uint16_t ownedStuffiesSecond = configMgr.config().ownedStuffiesSecond;
    for (int i = 0; i < STUFFY_COUNT; i++) {
        if (!(ownedStuffies & (1 << i))) continue;
        bool onOtherArm = (i == otherArmIdx);
        bool conflicts = onOtherArm && !(ownedStuffiesSecond & (1 << i));
        appendPickRadio(html, field, valuePrefix, STUFFIES[i].id, STUFFIES[i].label, nullptr,
                        i == equippedIdx, conflicts, onOtherArm ? otherArmNote : "");
    }
}

// Combined right-arm slot picker (DIY-110 restructure) — a single radio group covering both
// sub-categories that can occupy the shared right-arm slot (equippedRightArmKind), so
// picking one is an explicit either/or choice rather than the earlier priority-based
// implicit hide. Values are prefixed ("stuffy:<id>" / "toy:<id>") so
// handleConfigDressPost() can tell which sub-catalog a selection belongs to without having
// to search both by id — store item ids are already globally unique (assertStoreIdsUnique())
// but the prefix keeps the parsing explicit rather than relying on that. The stuffy half
// shares appendStuffyRadios()'s left/right conflict logic (a stuffy already on the left
// arm is disabled here unless a 2nd copy is owned, DIY-106); toys have no such conflict since
// they have no left-arm slot to clash with.
static String buildRightArmRadioOptions(int equippedLeftStuffyIdx) {
    uint16_t ownedToys = configMgr.config().ownedToys;
    uint8_t kind = configMgr.config().equippedRightArmKind;
    // Both resolvers are already -1 unless the slot's kind matches.
    int equippedToyIdx = equippedToyIndex();

    String options;
    appendPickRadio(options, "rightArm", "", "none", "None", nullptr,
                    kind != RIGHT_ARM_KIND_STUFFY && kind != RIGHT_ARM_KIND_TOY);
    appendStuffyRadios(options, "rightArm", "stuffy:", equippedStuffyRightIndex(), equippedLeftStuffyIdx,
                       " (also on left arm)");
    for (int i = 0; i < TOY_COUNT; i++) {
        if (!(ownedToys & (1 << i))) continue;
        appendPickRadio(options, "rightArm", "toy:", TOYS[i].id, TOYS[i].label, nullptr, i == equippedToyIdx);
    }
    return options;
}

static void handleConfigDressGet() {
    String page = loadPage(CONFIG_DRESS_HTML);

    page.replace("%%BLANKET_OPTIONS%%", buildPickGroup("blanketColor", ITEM_BLANKET, equippedBlanketIndex()));

    int equippedStuffyIdx = equippedStuffyIndex();
    String stuffyOptions = NOT_OWNED_HTML;
    if (configMgr.config().ownedStuffies != 0) {
        stuffyOptions = "";
        appendPickRadio(stuffyOptions, "stuffy", "", "none", "None", nullptr, equippedStuffyIdx < 0);
        appendStuffyRadios(stuffyOptions, "stuffy", "", equippedStuffyIdx, equippedStuffyRightIndex(),
                           " (also on right arm)");
    }
    page.replace("%%STUFFY_OPTIONS%%", stuffyOptions);

    String rightArmOptions;
    if (!configMgr.config().rightArmSlotUnlocked) {
        rightArmOptions = "<p style='color:#888'>Not unlocked yet — visit the Store.</p>";
    } else if (configMgr.config().ownedStuffies == 0 && configMgr.config().ownedToys == 0) {
        rightArmOptions = NOT_OWNED_HTML;
    } else {
        rightArmOptions = buildRightArmRadioOptions(equippedStuffyIdx);
    }
    page.replace("%%RIGHT_ARM_OPTIONS%%", rightArmOptions);

    page.replace("%%ROOM_THEME_OPTIONS%%", buildPickGroup("roomTheme", ITEM_ROOM_THEME, equippedRoomThemeIndex()));

    // Cat color has no "not owned" state (white is always available), and each color carries
    // a rename field next to its radio.
    uint8_t ownedCatColors = configMgr.config().ownedCatColors;
    int equippedCatColorIdx = equippedCatColorIndex();
    String catColorOptions;
    appendPickRadio(catColorOptions, "catColor", "", "none", "White", nullptr, equippedCatColorIdx < 0);
    catColorOptions += "<input name='name_white' maxlength='16' value='" + htmlEscape(getCatName(-1)) + "'>";
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (!(ownedCatColors & (1 << i))) continue;
        appendPickRadio(catColorOptions, "catColor", "", CAT_COLORS[i].id, CAT_COLORS[i].label,
                        CAT_COLORS[i].webColor, i == equippedCatColorIdx);
        catColorOptions += "<input name='name_" + String(CAT_COLORS[i].id) + "' maxlength='16' value='"
                          + htmlEscape(getCatName(i)) + "'>";
    }
    page.replace("%%CAT_COLOR_OPTIONS%%", catColorOptions);

    page.replace("%%ACCESSORY_OPTIONS%%", buildPickGroup("accessory", ITEM_ACCESSORY, equippedAccessoryIndex()));
    page.replace("%%GLASSES_OPTIONS%%", buildPickGroup("glasses", ITEM_GLASSES, equippedGlassesIndex()));
    page.replace("%%BADGE_OPTIONS%%", buildPickGroup("badge", ITEM_BADGE, equippedBadgeIndex()));

    String msg = "";
    if (wm.server->hasArg("saved"))
        msg = "<div class='banner ok'>Saved.</div>";
    page.replace("%%MSG%%", msg);
    sendHtmlPage(page);
}

// Applies one single-slot equip field from the dressing room form: "none" unequips, a catalog
// id equips that entry if it's owned, and an absent/empty field leaves the slot alone. Sends a
// 400 and returns false for an unknown or unowned id, before touching the slot.
static bool applyEquipField(const char* field, ItemCategory cat, bool& changed) {
    String id = wm.server->arg(field);
    if (id.length() == 0) return true;
    int idx = EQUIP_NONE;
    if (id != "none") {
        idx = findCatalogIndex(cat, id, false);
        if (idx < 0 || !(ownedItems(cat) & (1u << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return false;
        }
    }
    *equippedItemField(cat) = idx;
    changed = true;
    return true;
}

static void handleConfigDressPost() {
    bool changed = false;

    // A dressing-room page loaded before the right-arm restructure still posts the old
    // separate stuffyRight/toy field names, which no template generated after this update
    // ever emits (CONFIG_DRESS_HTML only has the combined "rightArm" field now) — so either
    // one present here can only mean a stale cached page. Checked first, before anything else
    // in this function mutates configMgr.config() (e.g. blanketColor just below writes
    // directly rather than through a local first) — a check placed after any such mutation
    // would let a rejected submission still leave that earlier write live in memory even
    // though the 400 skips configMgr.save(), the same in-memory/disk divergence the
    // stuffy/right-arm locals below are already careful to avoid (see DIY-56 review).
    if (wm.server->arg("rightArm").length() == 0 &&
        (wm.server->hasArg("stuffyRight") || wm.server->hasArg("toy"))) {
        wm.server->send(400, "text/plain", "Stale page — please reload the Dressing Room and try again");
        return;
    }

    // Validate every submitted name field up front, before any equip mutation below touches
    // configMgr.config() in memory, so a rejected name 400s atomically instead of leaving
    // in-memory equip state diverged from disk (see DIY-56 review).
    bool hasWhiteName = wm.server->hasArg("name_white");
    String whiteName = wm.server->arg("name_white");
    if (hasWhiteName && !sanitizeCatName(whiteName)) {
        wm.server->send(400, "text/plain", "Invalid name");
        return;
    }
    String colorNames[CAT_COLOR_COUNT];
    bool hasColorName[CAT_COLOR_COUNT] = {};
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (!(configMgr.config().ownedCatColors & (1 << i))) continue;
        String argName = "name_" + String(CAT_COLORS[i].id);
        if (!wm.server->hasArg(argName)) continue;
        colorNames[i] = wm.server->arg(argName);
        if (!sanitizeCatName(colorNames[i])) {
            wm.server->send(400, "text/plain", "Invalid name");
            return;
        }
        hasColorName[i] = true;
    }

    if (!applyEquipField("blanketColor", ITEM_BLANKET, changed)) return;

    // A stuffy is one physical toy — the left (drawPeeking/drawFull) and right-arm
    // (drawHeld/drawHeldPeeking, DIY-64) slots are separate equip choices but must name
    // different stuffies. Both slots are resolved into local variables first — without
    // touching configMgr.config() — so the conflict check below can reject before anything
    // is mutated. Committing straight into configMgr.config() as each slot resolved (as this
    // used to) meant a rejected request could still leave the in-memory config (which
    // rendering reads live) holding the same stuffy on both arms, since the 400 return skips
    // configMgr.save() but not the earlier writes.
    int newStuffy = configMgr.config().equippedStuffy;
    bool stuffyChanged = false;
    String stuffyId = wm.server->arg("stuffy");
    if (stuffyId == "none") {
        newStuffy = EQUIP_NONE;
        stuffyChanged = true;
    } else if (stuffyId.length() > 0) {
        int idx = findCatalogIndex(ITEM_STUFFY, stuffyId, false);
        if (idx < 0 || !(configMgr.config().ownedStuffies & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        newStuffy = idx;
        stuffyChanged = true;
    }

    // The right-arm slot (DIY-64) holds exactly one of a stuffy or a toy (DIY-110 restructure)
    // — a single combined field ("rightArm", values "none" / "stuffy:<id>" / "toy:<id>")
    // rather than the separate stuffyRight/toy fields this replaced, so the two are a real
    // either/or choice instead of two independently-settable fields that then needed a
    // priority rule to reconcile at draw time. Resolved into locals first, same reasoning as
    // newStuffy/stuffyChanged above — the same-stuffy-on-both-arms conflict check below must
    // see the fully-resolved kind before anything is mutated.
    int newStuffyRight = configMgr.config().equippedStuffyRight;
    int newToy = configMgr.config().equippedToy;
    int newRightArmKind = configMgr.config().equippedRightArmKind;
    bool rightArmChanged = false;
    String rightArmId = wm.server->arg("rightArm");
    if (rightArmId == "none") {
        newRightArmKind = RIGHT_ARM_KIND_NONE;
        rightArmChanged = true;
    } else if (rightArmId.startsWith("stuffy:")) {
        if (!configMgr.config().rightArmSlotUnlocked) {
            wm.server->send(400, "text/plain", "Right arm slot not unlocked");
            return;
        }
        int idx = findCatalogIndex(ITEM_STUFFY, rightArmId.substring(7), false);
        if (idx < 0 || !(configMgr.config().ownedStuffies & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        newStuffyRight = idx;
        newRightArmKind = RIGHT_ARM_KIND_STUFFY;
        rightArmChanged = true;
    } else if (rightArmId.startsWith("toy:")) {
        if (!configMgr.config().rightArmSlotUnlocked) {
            wm.server->send(400, "text/plain", "Right arm slot not unlocked");
            return;
        }
        int idx = findCatalogIndex(ITEM_TOY, rightArmId.substring(4), false);
        if (idx < 0 || !(configMgr.config().ownedToys & (1 << idx))) {
            wm.server->send(400, "text/plain", "Invalid selection");
            return;
        }
        newToy = idx;
        newRightArmKind = RIGHT_ARM_KIND_TOY;
        rightArmChanged = true;
    } else if (rightArmId.length() > 0) {
        wm.server->send(400, "text/plain", "Invalid selection");
        return;
    }

    // Only enforced when this request actually touched a stuffy slot and the right arm's
    // resolved kind is a stuffy — a config from before this guard existed could already have
    // the same stuffy on both arms, and an unrelated field change (e.g. blanket color)
    // shouldn't 400 out just because that pre-existing state happens to conflict. Owning a
    // 2nd copy of the stuffy (DIY-106) lifts the conflict — there's genuinely one for each arm
    // in that case. The kind check matters now too: a stale equippedStuffyRight index that
    // happens to equal newStuffy must never trip this once the slot actually holds a toy or
    // nothing.
    if ((stuffyChanged || rightArmChanged) &&
        newRightArmKind == RIGHT_ARM_KIND_STUFFY &&
        newStuffy != EQUIP_NONE && newStuffy == newStuffyRight &&
        !(configMgr.config().ownedStuffiesSecond & (1 << newStuffy))) {
        wm.server->send(400, "text/plain", "Can't equip the same stuffy on both arms");
        return;
    }

    if (stuffyChanged) { configMgr.config().equippedStuffy = newStuffy; changed = true; }
    if (rightArmChanged) {
        configMgr.config().equippedRightArmKind = newRightArmKind;
        if (newRightArmKind == RIGHT_ARM_KIND_STUFFY) configMgr.config().equippedStuffyRight = newStuffyRight;
        else if (newRightArmKind == RIGHT_ARM_KIND_TOY) configMgr.config().equippedToy = newToy;
        changed = true;
    }

    if (!applyEquipField("roomTheme", ITEM_ROOM_THEME, changed)) return;

    if (!applyEquipField("catColor", ITEM_CAT_COLOR, changed)) return;  // "none" falls back to white, catBodyColor()

    if (!applyEquipField("accessory", ITEM_ACCESSORY, changed)) return;

    if (!applyEquipField("glasses", ITEM_GLASSES, changed)) return;

    if (!applyEquipField("badge", ITEM_BADGE, changed)) return;

    // Rename any owned cat (including white) — one optional field per color, submitted
    // alongside its radio button. Absent fields (e.g. a plain equip-only submission from
    // an older cached page) are left untouched rather than clobbered with an empty name.
    // Values were already validated above, before any mutation in this function ran.
    if (hasWhiteName) {
        setCatName(-1, whiteName);
        changed = true;
    }
    for (int i = 0; i < CAT_COLOR_COUNT; i++) {
        if (!hasColorName[i]) continue;
        setCatName(i, colorNames[i]);
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
    // autoConnect()'s blocking startConfigPortal() when there are no saved credentials.
    // getConfigPortalActive() is true only during that AP-mode phase (no WiFi yet) — skip
    // registration there so WiFiManager's own stock pages (SSID/password entry) run
    // untouched at 192.168.4.1, exactly as drawWifiPortal() tells the user to expect. Once
    // WiFi connects, autoConnect() returns and the app's explicit wm.startWebPortal() call
    // fires this same callback again with getConfigPortalActive() now false — that's when
    // "/" (and everything else, including the /setup wizard drawSetupPrompt() points users
    // at via their real LAN IP) actually gets wired up. WM_WebServer matches handlers in
    // registration order and stops at the first match, so "/" being registered first here
    // wins over WiFiManager's own root handler once we do register it (see
    // handleRootPage()'s comment for why that's unconditional from that point on).
    wm.setWebServerCallback([]() {
        if (wm.getConfigPortalActive()) {
            // Still choosing WiFi — send "/" straight to WiFiManager's own network-scan/
            // credential-entry page instead of falling through to its stock root menu
            // (which links to "custom"/"info"/"exit", none of which make sense before
            // WiFi is even configured). Registered here, before WiFiManager wires up its
            // own "/" handler, so it wins per the first-match-wins ordering noted above.
            wm.server->on("/", HTTP_GET, []() {
                wm.server->sendHeader("Location", "/wifi");
                wm.server->send(302, "text/plain", "");
            });
            return;
        }
        wm.server->on("/",                     HTTP_GET,  handleRootPage);
        wm.server->on("/config",               HTTP_GET,  handleConfigHome);
        wm.server->on("/config/admin",         HTTP_GET,  handleConfigAdminGet);
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
        wm.server->on("/config/admin/flashsale",       HTTP_GET,  handleConfigFlashSaleGet);
        wm.server->on("/config/admin/flashsale/check", HTTP_POST, handleConfigFlashSaleCheckPost);
        wm.server->on("/config/admin/themeweek",       HTTP_GET,  handleConfigThemeWeekGet);
        wm.server->on("/config/admin/themeweek/set",   HTTP_POST, handleConfigThemeWeekSetPost);
        wm.server->on("/config/admin/themeweek/clear", HTTP_POST, handleConfigThemeWeekClearPost);
        wm.server->on("/config/admin/themeweek/halloween", HTTP_POST, handleConfigThemeWeekHalloweenPost);
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

    assertStoreIdsUnique();

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
    lastWeatherFetch  = millis();
    lastUpdateCheck   = millis();
    lastFlashSalePoll = millis();

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

    // Firmware update check — the network check itself runs unconditionally, ahead of
    // the sleep-window and setup-prompt early returns below, so neither an overnight
    // sleep window nor an in-progress first-run setup can starve it for hours at a
    // time. The flash+reboot step is heavier (lights the screen full-brightness,
    // reboots the device) so it's deferred via otaUpdatePending until control reaches
    // past both of those early returns below — i.e. only once the device is awake and
    // past first-run setup.
    if (now - lastUpdateCheck > UPDATE_CHECK_INTERVAL_MS) {
        lastUpdateCheck = now;
        OtaCheckResult result = performUpdateCheckOnly();
        if (!lastUpdateCheckFailed && !lastUpdateCheckSkipped && result.updateAvailable) {
            pendingOtaUpdate  = result;
            otaUpdatePending  = true;
        }
    }

    // Flash-sale poll (DIY-79) — same placement/reasoning as the OTA check above: runs
    // unconditionally ahead of the sleep-window and setup-prompt early returns so an overnight
    // sleep window can't starve it, and it's a lightweight JSON GET (no flash/reboot) so there's
    // no need to defer it like the OTA apply step is.
    if (now - lastFlashSalePoll > FLASH_SALE_POLL_INTERVAL_MS) {
        lastFlashSalePoll = now;
        fetchFlashSale();
    }

    // Theme-week transition check (DIY-108, COM-379) — entirely local (no network involved),
    // so it just runs every tick rather than being gated behind a poll interval like the
    // flash-sale check above. The admin page's Set/Clear/Halloween-mode handlers also call this
    // directly so a change takes effect on the same request rather than waiting for the next tick.
    checkThemeWeekTransition();

    // First-run setup: hold on the "complete setup at <ip>" screen until the wizard (cat
    // color + name) has been finished. Checked ahead of the sleep window below so a fresh
    // device configured at night doesn't have its setup prompt preempted by the sleep screen.
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

    // Apply any update found by the check above — only reached once we're awake and
    // past first-run setup (both early-return above this point otherwise), so a
    // sleeping or mid-setup device is never abruptly lit up full-brightness and
    // rebooted by an overnight or mid-wizard update.
    if (otaUpdatePending) {
        otaUpdatePending = false;
        applyFoundUpdate(pendingOtaUpdate);  // reboots on success; only returns on failure
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

    // Sale flash — while any flash sale is active, toggle the "SALE!" banner's color every
    // 500ms; repaint once more with the flash off the moment the sale window ends so it
    // doesn't linger.
    {
        static unsigned long lastSaleFlash = 0;
        static bool wasSaleActive = false;
        bool saleActive = isFlashSaleActive();
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

    // Hogwarts at Night's window flicker and shooting star (COM-378); no-op under any other theme.
    updateHogwartsNightAnim(now);

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
