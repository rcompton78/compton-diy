#include "PetScene.h"

#include <algorithm>

#include "PaletteIndex.h"
#include "generated/sprite_assets.h"

// ── Layout (logical pixels; ×4 on screen) ────────────────────────────────────────────
static constexpr int GROUND_Y = 60;  // first row of the ground strip
static const int     PET_X    = (FrameRenderer::LOGICAL_W - assets::PET.w) / 2;
static const int     PET_Y    = GROUND_Y - assets::PET.h + 2;  // sprite has ~2px empty below its feet

// Floating heart: rises HEART_RISE px over HEART_LIFE_MS, blinking out over the last part.
static constexpr uint32_t HEART_LIFE_MS  = 1200;
static constexpr uint32_t HEART_BLINK_MS = 350;
static constexpr int      HEART_RISE     = 22;

// ── AnimPlayer ───────────────────────────────────────────────────────────────────────

void AnimPlayer::play(const SpriteSheet& sheet, const SpriteTag& tag, bool loop, uint32_t now) {
    _sheet     = &sheet;
    _tag       = &tag;
    _loop      = loop;
    _finished  = false;
    _step      = 0;
    _frame     = frameAt(0);
    _stepStart = now;
}

uint16_t AnimPlayer::stepCount() const {
    uint16_t n = _tag->to - _tag->from + 1;
    return (_tag->direction == TagDirection::PingPong && n > 1) ? 2 * n - 2 : n;
}

uint8_t AnimPlayer::frameAt(uint16_t step) const {
    uint16_t n = _tag->to - _tag->from + 1;
    switch (_tag->direction) {
        case TagDirection::Reverse:  return _tag->to - step;
        case TagDirection::PingPong: return step < n ? _tag->from + step : _tag->to - (step - n + 1);
        default:                     return _tag->from + step;
    }
}

bool AnimPlayer::update(uint32_t now) {
    if (!_tag || _finished) return false;
    bool changed = false;
    // Catch up on every elapsed frame (the loop may have been blocked, e.g. by an OTA check),
    // keeping the authored timing rather than drifting by one frame per stall.
    while (now - _stepStart >= std::max<uint32_t>(_sheet->frames[_frame].durationMs, 1)) {
        _stepStart += std::max<uint32_t>(_sheet->frames[_frame].durationMs, 1);
        if (_step + 1 >= stepCount()) {
            if (!_loop) { _finished = true; return changed; }
            _step = 0;
        } else {
            _step++;
        }
        uint8_t next = frameAt(_step);
        changed |= next != _frame;
        _frame = next;
    }
    return changed;
}

// ── PetScene ─────────────────────────────────────────────────────────────────────────

void PetScene::begin(uint32_t now) {
    _r.setScenePalette(assets::PALETTE_NORMAL);
    enter(State::Idle, now);
}

void PetScene::enter(State s, uint32_t now) {
    _state = s;
    const char* tagName = s == State::Happy ? "happy" : "idle";
    const SpriteTag* tag = assets::PET.findTag(tagName);
    if (!tag && assets::PET.tagCount > 0) tag = &assets::PET.tags[0];
    if (tag) _pet.play(assets::PET, *tag, s == State::Idle, now);  // untagged sheet: frame 0 stays up
    _dirty = true;
}

void PetScene::onTap(uint32_t now) {
    enter(State::Happy, now);  // re-tapping mid-animation restarts it

    for (auto& h : _hearts) {
        if (h.active) continue;
        h.active = true;
        h.start  = now;
        h.x = PET_X + (assets::PET.w - assets::HEART.w) / 2 + (int)(now % 9) - 4;  // a little jitter
        h.y = PET_Y + 2;
        break;
    }
}

void PetScene::toggleSick() {
    _sick = !_sick;
    _r.setScenePalette(_sick ? assets::PALETTE_SICK : assets::PALETTE_NORMAL);
    _dirty = true;
}

bool PetScene::heartVisible(const Heart& h, uint32_t now) const {
    uint32_t age = now - h.start;
    if (age < HEART_LIFE_MS - HEART_BLINK_MS) return true;
    return ((age / 70) & 1) == 0;  // blink out (no alpha blending on-device)
}

int PetScene::heartY(const Heart& h, uint32_t now) const {
    return h.y - (int)((now - h.start) * HEART_RISE / HEART_LIFE_MS);
}

bool PetScene::update(uint32_t now) {
    bool dirty = _pet.update(now) || _dirty;
    _dirty = false;

    if (_state == State::Happy && _pet.finished()) {
        enter(State::Idle, now);
        dirty = true;
    }

    // Effects move far more often than the pet animates, so only redraw when a heart
    // has actually moved/blinked by a whole logical pixel.
    uint32_t sig = 0;
    for (auto& h : _hearts) {
        if (h.active && now - h.start >= HEART_LIFE_MS) { h.active = false; }
        if (h.active) sig = sig * 131 + (uint32_t)(heartY(h, now) + 64) * 2 + heartVisible(h, now) + 1;
    }
    if (sig != _heartSig) { _heartSig = sig; dirty = true; }
    return dirty;
}

void PetScene::draw(uint32_t now) {
    // Background layer. Not navy: that's the sprites' outline colour, which would vanish.
    _r.clear(PAL_LAVENDER);
    _r.fillRect(0, GROUND_Y, FrameRenderer::LOGICAL_W, 2, PAL_GREEN);
    _r.fillRect(0, GROUND_Y + 2, FrameRenderer::LOGICAL_W, FrameRenderer::LOGICAL_H - GROUND_Y - 2, PAL_BROWN);
    for (int x = 1; x < FrameRenderer::LOGICAL_W; x += 6) _r.fillRect(x, GROUND_Y + 4 + (x % 4), 1, 1, PAL_DARK_GREY);

    // Pet layer
    _r.drawSprite(assets::PET, _pet.frame(), PET_X, PET_Y);

    // Effect layer
    for (auto& h : _hearts) {
        if (h.active && heartVisible(h, now)) _r.drawSprite(assets::HEART, 0, h.x, heartY(h, now));
    }
}
