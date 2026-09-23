#pragma once

#include <Arduino.h>

#include "FrameRenderer.h"
#include "SpriteTypes.h"

// Plays one tagged animation from a sheet using the frame durations authored in Aseprite.
class AnimPlayer {
public:
    // loop = false plays the tag once and then holds its last frame (finished() → true).
    void play(const SpriteSheet& sheet, const SpriteTag& tag, bool loop, uint32_t now);
    // Advances by the elapsed time. Returns true if the visible frame changed.
    bool update(uint32_t now);

    uint8_t frame() const { return _frame; }
    bool finished() const { return _finished; }

private:
    uint8_t frameAt(uint16_t step) const;
    uint16_t stepCount() const;

    const SpriteSheet* _sheet = nullptr;
    const SpriteTag*   _tag   = nullptr;
    bool     _loop     = true;
    bool     _finished = false;
    uint16_t _step     = 0;
    uint8_t  _frame    = 0;
    uint32_t _stepStart = 0;
};

// The egg's lifecycle (COM-299).
//
//   Egg      the shell, showing one of six crack stages picked from the hatch progress
//            main feeds in. Rocks on tap, and on its own every few seconds.
//   Hatching shakes, then the top breaks off and tumbles onto the grass.
//   Baby     the opened shell with the creature blinking inside, lid resting beside it.
//            The creature itself — and its rise out of the shell — is COM-300.
//
// The hatch timer lives in main.cpp, which owns its persistence; this class only renders
// the progress it is given.
class PetScene {
public:
    enum class State : uint8_t { Egg, Hatching, Baby };

    static constexpr uint8_t STAGE_COUNT = 6;

    explicit PetScene(FrameRenderer& renderer) : _r(renderer) {}

    // hatched = start straight in Baby (the egg already hatched on an earlier boot).
    void begin(uint32_t now, bool hatched);

    // 0..1 through the hatch window; picks the crack stage. Ignored unless State::Egg.
    void setEggProgress(float progress, uint32_t now);
    void startHatch(uint32_t now);
    void resetToEgg(uint32_t now);
    void onTap(uint32_t now);

    State   state() const { return _state; }
    uint8_t stage() const { return _stage; }
    // True whenever the egg is past hatching — which includes a boot that began already
    // hatched, where no animation ran. It is a state test, not a just-finished edge, so a
    // caller wanting the moment of hatching must track the transition itself (main.cpp
    // does, via its own persisted eggHatched flag).
    bool hatchDone() const { return _state == State::Baby; }

    // Advances animations/effects. Returns true if the scene needs redrawing.
    bool update(uint32_t now);
    // Draws background → egg/shell → effects into the renderer's logical canvas.
    void draw(uint32_t now);

private:
    struct Heart {
        bool     active = false;
        int      x = 0, y = 0;
        uint32_t start = 0;
    };

    void playStage(uint8_t stage, uint32_t now);
    void startRock(uint32_t now, bool strong);
    int  rockOffset(uint32_t now) const;
    void scheduleIdleRock(uint32_t now);
    // Lid position during/after the hatch, in logical px relative to the egg sprite.
    void lidOffset(uint32_t now, int& dx, int& dy, uint8_t& frame) const;
    // Where the arc leaves the lid once it reaches the ground. Solved from the same
    // constants the arc uses, so the landing and a post-reboot restore cannot drift apart.
    void lidRestPose(int& dx, int& dy) const;
    bool heartVisible(const Heart& h, uint32_t now) const;
    int  heartY(const Heart& h, uint32_t now) const;

    FrameRenderer& _r;
    AnimPlayer     _egg;          // the shell: stage0..stage5, or "open" once hatched
    State          _state = State::Egg;
    uint8_t        _stage = 0;
    bool           _dirty = true;

    uint32_t _rockStart   = 0;    // 0 = not rocking
    bool     _rockStrong  = false;
    int      _lastRock    = 0;    // last drawn offset, to detect movement
    uint32_t _nextIdleRock = 0;

    uint32_t _hatchStart  = 0;
    bool     _lidReleased = false;  // shake over, lid is in flight
    int      _lidRestX    = 0;
    int      _lidRestY    = 0;
    uint32_t _lastLidSig  = 0;

    Heart    _hearts[3];
    uint32_t _heartSig = 0;       // last drawn heart layout, to detect when effects moved
};
