#pragma once
#if defined(BOARD_WAVESHARE_S3_169)
#include <Arduino.h>
#include <Wire.h>

#include "TouchDriver.h"

// CST816T/CST816D I2C capacitive touch (single point). Talks to the chip's registers
// directly over Wire rather than pulling in a third-party library — the protocol is just
// a 6-byte read. Reports native panel coordinates, so like Ft6336uTouch it needs no
// calibration, only axis/rotation handling.
//
// Uses the default Wire instance, so other devices on the same bus (e.g. the Waveshare
// 1.69"'s QMI8658 IMU and PCF85063 RTC) can share it after begin().
class Cst816Touch : public TouchDriver {
public:
    Cst816Touch(int8_t sdaPin, int8_t sclPin, int8_t rstPin,
                int screenWidth, int screenHeight,
                bool swapXy = false, bool invertX = false, bool invertY = false);

    void begin() override;
    bool read(TouchPoint& out) override;

private:
    int8_t _sdaPin, _sclPin, _rstPin;
    int _screenWidth, _screenHeight;
    bool _swapXy, _invertX, _invertY;
};
#endif  // BOARD_WAVESHARE_S3_169
