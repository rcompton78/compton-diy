#pragma once
#if !defined(BOARD_FREENOVE_S3)
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "TouchDriver.h"

// XPT2046 resistive touch over SPI. Raw controller readings need per-panel
// calibration (xMin/xMax/yMin/yMax) to map onto screen pixel coordinates.
class Xpt2046Touch : public TouchDriver {
public:
    Xpt2046Touch(uint8_t csPin, uint8_t irqPin, uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin,
                 int screenWidth, int screenHeight,
                 int xMin, int xMax, int yMin, int yMax,
                 int pressureThreshold = 200);

    void begin() override;
    bool read(TouchPoint& out) override;

private:
    uint8_t _clkPin, _misoPin, _mosiPin, _csPin;
    int _screenWidth, _screenHeight;
    int _xMin, _xMax, _yMin, _yMax;
    int _pressureThreshold;
    SPIClass _spi;
    XPT2046_Touchscreen _touch;
};
#endif  // !BOARD_FREENOVE_S3
