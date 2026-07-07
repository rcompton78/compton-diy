#include "Xpt2046Touch.h"

#if !defined(BOARD_FREENOVE_S3)

Xpt2046Touch::Xpt2046Touch(uint8_t csPin, uint8_t irqPin, uint8_t clkPin, uint8_t misoPin, uint8_t mosiPin,
                           int screenWidth, int screenHeight,
                           int xMin, int xMax, int yMin, int yMax,
                           int pressureThreshold)
    : _clkPin(clkPin), _misoPin(misoPin), _mosiPin(mosiPin), _csPin(csPin),
      _screenWidth(screenWidth), _screenHeight(screenHeight),
      _xMin(xMin), _xMax(xMax), _yMin(yMin), _yMax(yMax),
      _pressureThreshold(pressureThreshold),
      _spi(HSPI), _touch(csPin, irqPin) {}

void Xpt2046Touch::begin() {
    _spi.begin(_clkPin, _misoPin, _mosiPin, _csPin);
    _touch.begin(_spi);
    _touch.setRotation(0);
}

bool Xpt2046Touch::read(TouchPoint& out) {
    if (!_touch.tirqTouched() || !_touch.touched()) return false;
    TS_Point p = _touch.getPoint();
    if (p.z < _pressureThreshold) return false;
    out.x = constrain(map(p.x, _xMin, _xMax, 0, _screenWidth - 1), 0, _screenWidth - 1);
    out.y = constrain(map(p.y, _yMin, _yMax, 0, _screenHeight - 1), 0, _screenHeight - 1);
    return true;
}

#endif  // !BOARD_FREENOVE_S3
