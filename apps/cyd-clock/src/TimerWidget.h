#pragma once
#include <Arduino.h>

class TimerWidget {
public:
    void start(uint32_t durationSeconds);
    // If finished or idle, starts a fresh countdown; otherwise extends the running/paused duration.
    void addTime(uint32_t seconds);
    void pause();
    void resume();
    void reset();

    void tick();

    bool     isRunning()  const { return _running; }
    bool     isFinished() const { return _finished; }
    uint32_t remaining()  const;

private:
    uint32_t _durationMs  = 0;
    uint32_t _startMs     = 0;
    uint32_t _pausedMs    = 0;
    uint32_t _elapsedMs   = 0;
    bool     _running     = false;
    bool     _finished    = false;
};
