#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>

#include "SpriteTypes.h"

// Layered, palette-indexed renderer for chunky pixel art (COM-296).
//
// The scene is drawn at a low logical resolution (60×70) into an 8-bit canvas of palette
// indices: background → pet → effects, all in index space. present() then composes each
// frame into RGB565 horizontal strips, scaling every logical pixel to a crisp 4×4 block,
// overlaying a full-resolution UI layer (text etc.), and streams the strips to the panel
// over SPI DMA. It double-buffers: while one strip is being sent, the next is composed
// into the other buffer. No partial frame is ever visible as "cleared then redrawn", so
// there's no flicker.
//
// Why strips rather than one full-screen RGB565 framebuffer in PSRAM: a 240×280 frame is
// 134KB, too much for internal RAM next to Wi-Fi, and on this Arduino core (IDF 4.4) SPI
// DMA can't read PSRAM directly (the driver would bounce-copy it through internal RAM).
// Two 240×20 strips are 19KB of DMA-capable internal RAM and give the same visible
// result. Everything that *is* full-screen (the logical canvas and the UI layer) stays in
// index form: 4KB and 33KB (PSRAM) respectively.
class FrameRenderer {
public:
    static constexpr int SCALE     = 4;
    static constexpr int LOGICAL_W = 60;   // × SCALE = 240
    static constexpr int LOGICAL_H = 70;   // × SCALE = 280
    static constexpr int STRIP_H   = 20;   // physical rows per DMA transfer (divides 280, multiple of SCALE)

    struct Stats {
        uint32_t frames      = 0;
        uint32_t composeUs   = 0;  // time spent building strips (CPU)
        uint32_t totalUs     = 0;  // whole present(), including waiting on the last DMA
        uint32_t maxTotalUs  = 0;
    };

    explicit FrameRenderer(TFT_eSPI& tft);

    // Allocates the DMA strips + UI layer and starts the SPI DMA engine. Call after tft.init().
    bool begin();

    // ── Logical canvas (palette indices) ──
    void clear(uint8_t index);
    void fillRect(int x, int y, int w, int h, uint8_t index);
    // Draws one frame of a sheet with its cell's top-left at (x, y). Index 0 = transparent.
    void drawSprite(const SpriteSheet& sheet, uint8_t frame, int x, int y);

    // Palette used for the canvas (the pet/scene). Swapping it recolours the whole scene
    // with no change to pixel data. The UI layer always uses the ui palette.
    void setScenePalette(const Palette& p);
    void setUiPalette(const Palette& p);

    // ── UI layer: full-resolution 4bpp sprite on top of the scene ──
    // Index 0 is see-through; 1..15 use the ui palette. Draw with the normal TFT_eSPI text/
    // shape calls (colours are palette indices), then call uiChanged().
    TFT_eSprite& ui() { return _ui; }
    void uiChanged();

    // Composes and pushes the whole frame. Blocks until the last strip has been sent, so
    // it's safe to draw straight to the tft afterwards (e.g. the OTA progress screen).
    void present();

    const Stats& stats() const { return _stats; }
    void resetStats() { _stats = Stats(); }

private:
    void composeStrip(int strip, uint16_t* out);

    TFT_eSPI&   _tft;
    TFT_eSprite _ui;
    uint8_t     _canvas[LOGICAL_W * LOGICAL_H];
    uint16_t*   _strips[2] = {nullptr, nullptr};
    uint16_t    _sceneLut[16];   // RGB565, byte-swapped for the panel (DMA sends memory order)
    uint16_t    _uiLut[16];
    uint32_t    _uiRows[(LOGICAL_H * SCALE + 31) / 32];  // bitset: physical rows with any UI pixel
    Stats       _stats;
};
