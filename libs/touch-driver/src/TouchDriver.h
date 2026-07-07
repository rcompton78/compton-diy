#pragma once

struct TouchPoint { int x, y; };

// Common interface for touch controllers so boards can swap hardware
// (SPI/resistive, I2C/capacitive, ...) at compile time without touching
// app-level touch handling.
class TouchDriver {
public:
    virtual ~TouchDriver() = default;
    virtual void begin() = 0;
    // Returns true if a touch was detected, filling out with screen-space coordinates.
    virtual bool read(TouchPoint& out) = 0;
};
