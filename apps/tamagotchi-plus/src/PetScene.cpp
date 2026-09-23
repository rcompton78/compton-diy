#include "PetScene.h"

#include <algorithm>
#include <math.h>
#include <esp_system.h>

#include "Config.h"
#include "PaletteIndex.h"
#include "generated/sprite_assets.h"

// ── Layout (logical pixels; ×4 on screen) ────────────────────────────────────────────
static constexpr int GROUND_Y = 60;  // first row of the ground strip
static const int     EGG_X    = (FrameRenderer::LOGICAL_W - assets::EGG.w) / 2;
static const int     EGG_Y    = GROUND_Y - assets::EGG.h + 2;  // sprite has ~2px empty below

// Floating heart: rises HEART_RISE px over HEART_LIFE_MS, blinking out over the last part.
static constexpr uint32_t HEART_LIFE_MS  = 1200;
static constexpr uint32_t HEART_BLINK_MS = 350;
static constexpr int      HEART_RISE     = 22;

// Rocking. A whole-sprite horizontal nudge rather than extra sprite frames: six stages ×
// a few wobble frames each would have cost more flash than the crack stages themselves,
// and this way any stage rocks for free.
static constexpr uint32_t ROCK_STEP_MS = 60;
static const int ROCK_TAP[]  = {1, 2, 1, -1, -2, -1, 1, 0};  // tap: a proper shove
static const int ROCK_IDLE[] = {1, 1, 0, -1, -1, 0};         // on its own: a gentle lean
static constexpr uint32_t IDLE_ROCK_MIN_MS = 4000;
static constexpr uint32_t IDLE_ROCK_MAX_MS = 9000;

// Hatch: shake for anticipation, then the lid leaves on a ballistic arc.
static constexpr uint32_t HATCH_SHAKE_MS = 420;
static const int ROCK_SHAKE[] = {1, -1, 1, -1, 2, -2, 1, 0};
static constexpr float LID_VX   =  12.0f;   // logical px / s
static constexpr float LID_VY0  = -43.0f;
static constexpr float LID_GRAV =  82.0f;   // px / s²
static constexpr uint8_t LID_REST_FRAME = 2;  // the fully-tilted frame it comes to rest on

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

void PetScene::begin(uint32_t now, bool hatched) {
    _r.setScenePalette(assets::PALETTE_NORMAL);
    if (hatched) {
        _state = State::Baby;
        _lidReleased = true;
        // Rest the lid where the arc would have left it, so a reboot after hatching looks
        // the same as the moment the hatch ended.
        lidRestPose(_lidRestX, _lidRestY);
        const SpriteTag* open = assets::EGG.findTag("open");
        if (open) _egg.play(assets::EGG, *open, true, now);
    } else {
        _state = State::Egg;
        playStage(0, now);
    }
    scheduleIdleRock(now);
    _dirty = true;
}

void PetScene::playStage(uint8_t stage, uint32_t now) {
    _stage = std::min<uint8_t>(stage, STAGE_COUNT - 1);
    char name[8];
    snprintf(name, sizeof(name), "stage%u", (unsigned)_stage);
    const SpriteTag* tag = assets::EGG.findTag(name);
    if (!tag && assets::EGG.tagCount > 0) tag = &assets::EGG.tags[0];
    if (tag) _egg.play(assets::EGG, *tag, true, now);
    _dirty = true;
}

void PetScene::setEggProgress(float progress, uint32_t now) {
    if (_state != State::Egg) return;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    uint8_t want = (uint8_t)(progress * STAGE_COUNT);
    if (want >= STAGE_COUNT) want = STAGE_COUNT - 1;
    if (want != _stage) playStage(want, now);
}

void PetScene::startHatch(uint32_t now) {
    if (_state != State::Egg) return;
    _state       = State::Hatching;
    _hatchStart  = now;
    _rockStart   = 0;
    _lidReleased = false;
    playStage(STAGE_COUNT - 1, now);   // still whole while it shakes
    _dirty = true;
}

void PetScene::resetToEgg(uint32_t now) {
    _state = State::Egg;
    _lidReleased = false;
    for (auto& h : _hearts) h.active = false;
    _rockStart = 0;
    _lastRock  = 0;
    playStage(0, now);
    scheduleIdleRock(now);
}

void PetScene::scheduleIdleRock(uint32_t now) {
    _nextIdleRock = now + IDLE_ROCK_MIN_MS + (esp_random() % (IDLE_ROCK_MAX_MS - IDLE_ROCK_MIN_MS));
}

void PetScene::startRock(uint32_t now, bool strong) {
    _rockStart  = now ? now : 1;  // 0 means "not rocking"
    _rockStrong = strong;
}

int PetScene::rockOffset(uint32_t now) const {
    if (_state == State::Hatching && now - _hatchStart < HATCH_SHAKE_MS) {
        size_t n = sizeof(ROCK_SHAKE) / sizeof(ROCK_SHAKE[0]);
        size_t i = (now - _hatchStart) / (HATCH_SHAKE_MS / n);
        return i < n ? ROCK_SHAKE[i] : 0;
    }
    if (!_rockStart || _state != State::Egg) return 0;
    const int* pat = _rockStrong ? ROCK_TAP : ROCK_IDLE;
    size_t n = _rockStrong ? sizeof(ROCK_TAP) / sizeof(ROCK_TAP[0])
                           : sizeof(ROCK_IDLE) / sizeof(ROCK_IDLE[0]);
    size_t i = (now - _rockStart) / ROCK_STEP_MS;
    return i < n ? pat[i] : 0;
}

void PetScene::lidRestPose(int& dx, int& dy) const {
    const SpriteFrame& lf = assets::EGGSHELL.frames[LID_REST_FRAME];
    float targetDy = (float)(GROUND_Y + 1 - EGG_Y - (lf.y + lf.h - 1));
    // dy(t) = LID_VY0·t + ½·LID_GRAV·t², taking the descending root.
    float disc = LID_VY0 * LID_VY0 + 2.0f * LID_GRAV * targetDy;
    float t    = disc > 0.0f ? (-LID_VY0 + sqrtf(disc)) / LID_GRAV : 0.0f;
    dx = (int)lroundf(LID_VX * t);
    dy = (int)lroundf(targetDy);
}

void PetScene::lidOffset(uint32_t now, int& dx, int& dy, uint8_t& frame) const {
    if (_state == State::Baby) {
        dx = _lidRestX; dy = _lidRestY; frame = LID_REST_FRAME;
        return;
    }
    // Signed: now - _hatchStart is unsigned, so subtracting HATCH_SHAKE_MS before the
    // shake ends would underflow to ~4.29e9 and sail straight past a `t < 0` guard.
    int32_t sinceRelease = (int32_t)(now - _hatchStart) - (int32_t)HATCH_SHAKE_MS;
    float t = sinceRelease > 0 ? (float)sinceRelease / 1000.0f : 0.0f;
    dx = (int)lroundf(LID_VX * t);
    dy = (int)lroundf(LID_VY0 * t + 0.5f * LID_GRAV * t * t);
    frame = t < 0.42f ? 0 : (t < 0.84f ? 1 : 2);
}

void PetScene::onTap(uint32_t now) {
    if (_state == State::Egg) {
        startRock(now, true);            // a shove: something in there notices
        scheduleIdleRock(now);
        return;
    }
    if (_state != State::Baby) return;
    for (auto& h : _hearts) {
        if (h.active) continue;
        h.active = true;
        h.start  = now;
        h.x = EGG_X + (assets::EGG.w - assets::HEART.w) / 2 + (int)(now % 9) - 4;
        h.y = EGG_Y + 6;
        break;
    }
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
    bool dirty = _egg.update(now) || _dirty;
    _dirty = false;

    // Rocking: only redraw when the offset actually changed by a whole logical pixel.
    int rock = rockOffset(now);
    if (rock != _lastRock) { _lastRock = rock; dirty = true; }
    if (_rockStart && !rock && now - _rockStart > 1000) _rockStart = 0;
    if (_state == State::Egg && (int32_t)(now - _nextIdleRock) >= 0) {
        startRock(now, false);
        scheduleIdleRock(now);
        dirty = true;
    }

    if (_state == State::Hatching && !_lidReleased && now - _hatchStart >= HATCH_SHAKE_MS) {
        _lidReleased = true;
        const SpriteTag* open = assets::EGG.findTag("open");
        if (open) _egg.play(assets::EGG, *open, true, now);
        dirty = true;
    }

    if (_state == State::Hatching && _lidReleased) {
        int dx, dy; uint8_t f;
        lidOffset(now, dx, dy, f);
        const SpriteFrame& lf = assets::EGGSHELL.frames[f];
        int bottom = EGG_Y + dy + lf.y + lf.h - 1;
        if (bottom >= GROUND_Y + 1) {
            lidRestPose(_lidRestX, _lidRestY);
            _state = State::Baby;   // hatched: main persists this
        }
        uint32_t sig = (uint32_t)(dx + 128) * 4096 + (uint32_t)(dy + 128) * 8 + f;
        if (sig != _lastLidSig) { _lastLidSig = sig; dirty = true; }
    }

    // Effects move far more often than the shell animates, so only redraw when a heart
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

    // Shell layer. During the shake the whole egg is still intact, so it keeps rocking;
    // once the lid is away the base stays put and only the lid moves.
    int rock = rockOffset(now);
    _r.drawSprite(assets::EGG, _egg.frame(), EGG_X + rock, EGG_Y);

    if ((_state == State::Hatching && _lidReleased) || _state == State::Baby) {
        int dx, dy; uint8_t f;
        lidOffset(now, dx, dy, f);
        _r.drawSprite(assets::EGGSHELL, f, EGG_X + dx, EGG_Y + dy);
    }

    // Effect layer
    for (auto& h : _hearts) {
        if (h.active && heartVisible(h, now)) _r.drawSprite(assets::HEART, 0, h.x, heartY(h, now));
    }
}
