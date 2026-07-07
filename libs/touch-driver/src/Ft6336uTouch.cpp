#include "Ft6336uTouch.h"

#if defined(BOARD_FREENOVE_S3)

#include <algorithm>

Ft6336uTouch::Ft6336uTouch(int8_t sdaPin, int8_t sclPin, uint8_t rstPin, uint8_t intPin,
                           int screenWidth, int screenHeight,
                           bool swapXy, bool invertX, bool invertY)
    : _screenWidth(screenWidth), _screenHeight(screenHeight),
      _swapXy(swapXy), _invertX(invertX), _invertY(invertY),
      _touch(sdaPin, sclPin, rstPin, intPin) {}

void Ft6336uTouch::begin() {
    _touch.begin();
}

bool Ft6336uTouch::read(TouchPoint& out) {
    if (_touch.read_td_status() == 0) return false;

    int x = _touch.read_touch1_x();
    int y = _touch.read_touch1_y();
    if (_swapXy) std::swap(x, y);
    if (_invertX) x = _screenWidth - 1 - x;
    if (_invertY) y = _screenHeight - 1 - y;

    out.x = constrain(x, 0, _screenWidth - 1);
    out.y = constrain(y, 0, _screenHeight - 1);
    return true;
}

#endif  // BOARD_FREENOVE_S3
