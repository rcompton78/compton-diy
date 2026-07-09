#pragma once
#include <Arduino.h>

class StopwatchWidget {
public:
    void start();
    void pause();
    void resume();
    void reset();

    bool     isRunning() const { return _running; }
    uint32_t elapsedSeconds() const;

private:
    uint32_t _startMs   = 0;
    uint32_t _elapsedMs = 0;
    bool     _running   = false;
};
