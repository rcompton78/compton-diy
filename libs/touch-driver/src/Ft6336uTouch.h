#pragma once
#if defined(BOARD_FREENOVE_S3)
#include <FT6336U.h>

#include "TouchDriver.h"

// FT6336U I2C capacitive touch. Reports native panel coordinates, so unlike
// resistive controllers this needs no min/max calibration — only axis/rotation handling.
class Ft6336uTouch : public TouchDriver {
public:
    Ft6336uTouch(int8_t sdaPin, int8_t sclPin, uint8_t rstPin, uint8_t intPin,
                 int screenWidth, int screenHeight,
                 bool swapXy = false, bool invertX = false, bool invertY = false);

    void begin() override;
    bool read(TouchPoint& out) override;

private:
    int _screenWidth, _screenHeight;
    bool _swapXy, _invertX, _invertY;
    FT6336U _touch;
};
#endif  // BOARD_FREENOVE_S3
