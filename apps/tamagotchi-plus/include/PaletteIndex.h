#pragma once

#include <stdint.h>

// Named indices into the locked palette (assets/palette.hex, line N = index N; index 0 is
// transparent). Used for the procedural background layer and the UI layer. Keep in sync
// if palette.hex is ever reordered — sprites reference the same slots.
enum PaletteIndex : uint8_t {
    PAL_TRANSPARENT = 0,
    PAL_NAVY        = 1,   // 1d2b53
    PAL_PLUM        = 2,   // 7e2553
    PAL_GREEN       = 3,   // 008751
    PAL_BROWN       = 4,   // ab5236
    PAL_DARK_GREY   = 5,   // 5f574f
    PAL_LIGHT_GREY  = 6,   // c2c3c7
    PAL_WHITE       = 7,   // fff1e8
    PAL_RED         = 8,   // ff004d
    PAL_ORANGE      = 9,   // ffa300
    PAL_YELLOW      = 10,  // ffec27
    PAL_LIME        = 11,  // 00e436
    PAL_BLUE        = 12,  // 29adff
    PAL_LAVENDER    = 13,  // 83769c
    PAL_PINK        = 14,  // ff77a8
    PAL_PEACH       = 15,  // ffccaa
};
