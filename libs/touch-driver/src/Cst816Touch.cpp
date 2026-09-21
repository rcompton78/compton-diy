#include "Cst816Touch.h"

#if defined(BOARD_WAVESHARE_S3_169)

#include <algorithm>

static constexpr uint8_t CST816_ADDR          = 0x15;
static constexpr uint8_t REG_GESTURE_ID       = 0x01;  // start of the gesture/finger/X/Y block
static constexpr uint8_t REG_DIS_AUTO_SLEEP   = 0xFE;

Cst816Touch::Cst816Touch(int8_t sdaPin, int8_t sclPin, int8_t rstPin,
                         int screenWidth, int screenHeight,
                         bool swapXy, bool invertX, bool invertY)
    : _sdaPin(sdaPin), _sclPin(sclPin), _rstPin(rstPin),
      _screenWidth(screenWidth), _screenHeight(screenHeight),
      _swapXy(swapXy), _invertX(invertX), _invertY(invertY) {}

void Cst816Touch::begin() {
    Wire.begin(_sdaPin, _sclPin);

    if (_rstPin >= 0) {
        pinMode(_rstPin, OUTPUT);
        digitalWrite(_rstPin, LOW);
        delay(10);
        digitalWrite(_rstPin, HIGH);
        delay(50);
    }

    // The chip drops off the bus a few seconds after the last touch to save power and only
    // wakes on the next touch, so polled reads would NACK in between. Keep it awake so
    // read() can poll without depending on the INT line.
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(REG_DIS_AUTO_SLEEP);
    Wire.write(0x01);
    Wire.endTransmission();
}

bool Cst816Touch::read(TouchPoint& out) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(REG_GESTURE_ID);
    if (Wire.endTransmission(false) != 0) return false;

    // gesture, finger count, X high (low nibble), X low, Y high (low nibble), Y low
    uint8_t buf[6];
    if (Wire.requestFrom(CST816_ADDR, (uint8_t)sizeof(buf)) != sizeof(buf)) return false;
    for (uint8_t& b : buf) b = Wire.read();

    if (buf[1] == 0) return false;

    int x = ((buf[2] & 0x0F) << 8) | buf[3];
    int y = ((buf[4] & 0x0F) << 8) | buf[5];
    if (_swapXy) std::swap(x, y);
    if (_invertX) x = _screenWidth - 1 - x;
    if (_invertY) y = _screenHeight - 1 - y;

    out.x = constrain(x, 0, _screenWidth - 1);
    out.y = constrain(y, 0, _screenHeight - 1);
    return true;
}

#endif  // BOARD_WAVESHARE_S3_169
