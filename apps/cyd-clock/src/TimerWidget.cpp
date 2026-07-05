#include "TimerWidget.h"

void TimerWidget::start(uint32_t durationSeconds) {
    _durationMs = durationSeconds * 1000UL;
    _elapsedMs  = 0;
    _startMs    = millis();
    _running    = true;
    _finished   = false;
}

void TimerWidget::pause() {
    if (!_running) return;
    _elapsedMs += millis() - _startMs;
    _running = false;
}

void TimerWidget::resume() {
    if (_running || _finished) return;
    _startMs = millis();
    _running = true;
}

void TimerWidget::reset() {
    _running   = false;
    _finished  = false;
    _elapsedMs = 0;
}

void TimerWidget::tick() {
    if (!_running) return;
    uint32_t total = _elapsedMs + (millis() - _startMs);
    if (total >= _durationMs) {
        _elapsedMs = _durationMs;
        _running   = false;
        _finished  = true;
    }
}

uint32_t TimerWidget::remaining() const {
    uint32_t elapsed = _elapsedMs;
    if (_running) elapsed += millis() - _startMs;
    if (elapsed >= _durationMs) return 0;
    return (_durationMs - elapsed) / 1000UL;
}
