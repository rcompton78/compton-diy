#include "StopwatchWidget.h"

void StopwatchWidget::start() {
    _elapsedMs = 0;
    _startMs   = millis();
    _running   = true;
}

void StopwatchWidget::pause() {
    if (!_running) return;
    _elapsedMs += millis() - _startMs;
    _running = false;
}

void StopwatchWidget::resume() {
    if (_running) return;
    _startMs = millis();
    _running = true;
}

void StopwatchWidget::reset() {
    _running   = false;
    _elapsedMs = 0;
}

uint32_t StopwatchWidget::elapsedSeconds() const {
    uint32_t elapsed = _elapsedMs;
    if (_running) elapsed += millis() - _startMs;
    return elapsed / 1000UL;
}
