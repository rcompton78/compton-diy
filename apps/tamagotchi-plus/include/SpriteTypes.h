#pragma once

#include <stdint.h>
#include <string.h>

// Data types for the sprite assets that tools/sprites/convert.py generates into
// include/generated/sprite_assets.h from the committed Aseprite exports.
//
// Pixels are palette indices, 4 bits each (two per byte, high nibble = left pixel, rows
// padded to a whole byte). Index 0 is always transparent; 1..15 look up the active
// Palette, so swapping palettes recolours every sprite with no extra pixel data.

struct Palette {
    const char* name;
    uint16_t    colors[16];  // RGB565; [0] is unused (transparent)
};

struct SpriteFrame {
    const uint8_t* pixels;      // 4bpp, stride (w + 1) / 2
    uint8_t        x, y;        // offset of this (possibly trimmed) frame inside the sheet's w×h cell
    uint8_t        w, h;
    uint16_t       durationMs;  // authored in Aseprite
};

enum class TagDirection : uint8_t { Forward = 0, Reverse = 1, PingPong = 2 };

// An Aseprite tag: a named frame range, e.g. "idle" = frames 0..3.
struct SpriteTag {
    const char*  name;
    uint8_t      from, to;  // inclusive
    TagDirection direction;
};

struct SpriteSheet {
    const char*        name;
    uint8_t            w, h;  // cell size (Aseprite's sourceSize)
    uint8_t            frameCount;
    const SpriteFrame* frames;
    uint8_t            tagCount;
    const SpriteTag*   tags;

    const SpriteTag* findTag(const char* tagName) const {
        for (uint8_t i = 0; i < tagCount; i++) {
            if (strcmp(tags[i].name, tagName) == 0) return &tags[i];
        }
        return nullptr;
    }
};

// Palette index of pixel (x, y) within a frame.
inline uint8_t framePixel(const SpriteFrame& f, int x, int y) {
    uint8_t b = f.pixels[y * ((f.w + 1) / 2) + (x >> 1)];
    return (x & 1) ? (b & 0x0F) : (b >> 4);
}
