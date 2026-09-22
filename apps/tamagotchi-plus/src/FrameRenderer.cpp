#include "FrameRenderer.h"

#include <esp_heap_caps.h>
#include <soc/spi_reg.h>

static constexpr int PHYS_W = FrameRenderer::LOGICAL_W * FrameRenderer::SCALE;
static constexpr int PHYS_H = FrameRenderer::LOGICAL_H * FrameRenderer::SCALE;
static_assert(PHYS_H % FrameRenderer::STRIP_H == 0, "strips must tile the screen");
static_assert(FrameRenderer::STRIP_H % FrameRenderer::SCALE == 0, "strips must hold whole logical rows");

static inline uint16_t swap16(uint16_t c) { return (c << 8) | (c >> 8); }

// Waits for the in-flight DMA strip, then takes the SPI peripheral back out of DMA mode.
//
// TFT_eSPI 2.5.43 is meant to do the second part itself (its dma_end_callback clears
// SPI_DMA_CONF_REG(spi_host)), but on the ESP32-S3 with this Arduino core it clears the
// wrong peripheral: spi_host is SPI3_HOST (== 2), and the core's own REG_SPI_BASE(2),
// which wins over TFT_eSPI's #ifndef-guarded copy, is GPSPI2, while the display bus is
// GPSPI3. Left in DMA-TX mode, GPSPI3 then sends TFT_eSPI's direct register writes (the
// CASET/RASET/RAMWR before each strip, and any plain tft.draw*() afterwards) from the
// empty DMA FIFO instead of the command buffer. The panel never sees a valid write, so it
// just keeps showing whatever was on it before the first DMA transfer. SPI_PORT is the
// exact port TFT_eSPI's direct writes use, so this clears the right one.
static void finishDma(TFT_eSPI& tft) {
    tft.dmaWait();
    CLEAR_PERI_REG_MASK(SPI_DMA_CONF_REG(SPI_PORT), SPI_DMA_TX_ENA | SPI_DMA_RX_ENA);
}

FrameRenderer::FrameRenderer(TFT_eSPI& tft) : _tft(tft), _ui(&tft) {
    memset(_canvas, 0, sizeof(_canvas));
    memset(_sceneLut, 0, sizeof(_sceneLut));
    memset(_uiLut, 0, sizeof(_uiLut));
    memset(_uiRows, 0, sizeof(_uiRows));
}

bool FrameRenderer::begin() {
    for (auto& s : _strips) {
        s = (uint16_t*)heap_caps_malloc(PHYS_W * STRIP_H * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s) return false;
    }
    // 4bpp full-screen UI layer; TFT_eSprite puts it in PSRAM when available.
    _ui.setColorDepth(4);
    if (!_ui.createSprite(PHYS_W, PHYS_H)) return false;
    _ui.fillSprite(0);
    return _tft.initDMA();
}

void FrameRenderer::clear(uint8_t index) { memset(_canvas, index, sizeof(_canvas)); }

void FrameRenderer::fillRect(int x, int y, int w, int h, uint8_t index) {
    int x0 = max(x, 0), y0 = max(y, 0);
    int x1 = min(x + w, LOGICAL_W), y1 = min(y + h, LOGICAL_H);
    for (int yy = y0; yy < y1; yy++) {
        if (x1 > x0) memset(&_canvas[yy * LOGICAL_W + x0], index, x1 - x0);
    }
}

void FrameRenderer::drawSprite(const SpriteSheet& sheet, uint8_t frame, int x, int y) {
    if (frame >= sheet.frameCount) return;
    const SpriteFrame& f = sheet.frames[frame];
    for (int fy = 0; fy < f.h; fy++) {
        int cy = y + f.y + fy;
        if (cy < 0 || cy >= LOGICAL_H) continue;
        uint8_t* row = &_canvas[cy * LOGICAL_W];
        for (int fx = 0; fx < f.w; fx++) {
            int cx = x + f.x + fx;
            if (cx < 0 || cx >= LOGICAL_W) continue;
            uint8_t idx = framePixel(f, fx, fy);
            if (idx) row[cx] = idx;
        }
    }
}

void FrameRenderer::setScenePalette(const Palette& p) {
    for (int i = 0; i < 16; i++) _sceneLut[i] = swap16(p.colors[i]);
}

void FrameRenderer::setUiPalette(const Palette& p) {
    for (int i = 0; i < 16; i++) _uiLut[i] = swap16(p.colors[i]);
}

void FrameRenderer::uiChanged() {
    // Record which physical rows hold any UI pixel, so present() can memcpy the scene
    // straight through for the (majority of) rows with nothing on top.
    memset(_uiRows, 0, sizeof(_uiRows));
    const uint8_t* px = (const uint8_t*)_ui.getPointer();
    if (!px) return;  // begin() failed to allocate the UI layer: nothing to overlay
    const int stride = PHYS_W / 2;
    for (int y = 0; y < PHYS_H; y++) {
        const uint32_t* row = (const uint32_t*)(px + y * stride);  // stride is a multiple of 4
        for (int i = 0; i < stride / 4; i++) {
            if (row[i]) { _uiRows[y >> 5] |= 1u << (y & 31); break; }
        }
    }
}

void FrameRenderer::composeStrip(int strip, uint16_t* out) {
    static uint16_t sceneRow[PHYS_W];
    const uint8_t* ui = (const uint8_t*)_ui.getPointer();

    for (int ly = 0; ly < STRIP_H / SCALE; ly++) {
        int logicalY = strip * (STRIP_H / SCALE) + ly;
        const uint8_t* src = &_canvas[logicalY * LOGICAL_W];
        uint16_t* d = sceneRow;
        for (int lx = 0; lx < LOGICAL_W; lx++) {
            uint16_t c = _sceneLut[src[lx]];
            d[0] = c; d[1] = c; d[2] = c; d[3] = c;  // SCALE == 4
            d += SCALE;
        }

        for (int sy = 0; sy < SCALE; sy++) {
            int py = logicalY * SCALE + sy;
            uint16_t* dst = out + (ly * SCALE + sy) * PHYS_W;
            if (!(_uiRows[py >> 5] & (1u << (py & 31)))) {
                memcpy(dst, sceneRow, sizeof(sceneRow));
                continue;
            }
            const uint8_t* uiRow = ui + py * (PHYS_W / 2);
            for (int x = 0; x < PHYS_W; x += 2) {
                uint8_t b  = uiRow[x >> 1];
                uint8_t hi = b >> 4, lo = b & 0x0F;
                dst[x]     = hi ? _uiLut[hi] : sceneRow[x];
                dst[x + 1] = lo ? _uiLut[lo] : sceneRow[x + 1];
            }
        }
    }
}

void FrameRenderer::present() {
    uint32_t t0 = micros();
    uint32_t composeUs = 0;

    _tft.startWrite();
    for (int s = 0; s < PHYS_H / STRIP_H; s++) {
        uint16_t* buf = _strips[s & 1];
        uint32_t c0 = micros();
        composeStrip(s, buf);  // the other buffer may still be in flight — this one isn't
        composeUs += micros() - c0;
        // Previous strip must be fully sent (and DMA mode cleared) before pushImageDMA's
        // direct-register setAddrWindow; it then queues this strip and returns right away.
        finishDma(_tft);
        _tft.pushImageDMA(0, s * STRIP_H, PHYS_W, STRIP_H, (const uint16_t*)buf);
    }
    finishDma(_tft);
    _tft.endWrite();

    uint32_t total = micros() - t0;
    _stats.frames++;
    _stats.composeUs += composeUs;
    _stats.totalUs += total;
    if (total > _stats.maxTotalUs) _stats.maxTotalUs = total;
}
