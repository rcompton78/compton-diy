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

// Minimal pet state machine for the COM-296 graphics spike: idle loops until a tap, which
// plays "happy" once (plus a floating heart) and then returns to idle. Also owns the
// scene palette swap (normal ↔ sick) used to demo recolouring without extra pixel data.
class PetScene {
public:
    enum class State : uint8_t { Idle, Happy };

    explicit PetScene(FrameRenderer& renderer) : _r(renderer) {}

    void begin(uint32_t now);
    void onTap(uint32_t now);
    void toggleSick();
    bool isSick() const { return _sick; }
    State state() const { return _state; }

    // Advances animations/effects. Returns true if the scene needs redrawing.
    bool update(uint32_t now);
    // Draws background → pet → effects into the renderer's logical canvas.
    void draw(uint32_t now);

private:
    struct Heart {
        bool     active = false;
        int      x = 0, y = 0;
        uint32_t start = 0;
    };

    void enter(State s, uint32_t now);
    bool heartVisible(const Heart& h, uint32_t now) const;
    int  heartY(const Heart& h, uint32_t now) const;

    FrameRenderer& _r;
    AnimPlayer     _pet;
    State          _state = State::Idle;
    bool           _sick  = false;
    bool           _dirty = true;
    Heart          _hearts[3];
    uint32_t       _heartSig = 0;  // last drawn heart layout, to detect when effects moved
};
