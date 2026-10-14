#pragma once

#include <cstdint>

// Slideshow double-tap navigation: a double tap on the right half of the
// screen shows the next photo, one on the left half the previous photo. Both
// taps must land on the same half; a tap on the other half restarts the
// double-tap window instead. Sides are in LVGL's rotated (logical)
// coordinates, so "left" is the viewer's left in every screen rotation.
static constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 400;

enum TapSide : int {
  TAP_SIDE_LEFT = 0,
  TAP_SIDE_RIGHT = 1,
};

enum TapAction : int {
  TAP_ACTION_NONE = 0,
  TAP_ACTION_NEXT = 1,
  TAP_ACTION_PREVIOUS = 2,
};

inline TapSide tap_side_for_x(int x, int layout_width) {
  return x < layout_width / 2 ? TAP_SIDE_LEFT : TAP_SIDE_RIGHT;
}

// last_tap_ms == 0 means no first tap is pending.
inline TapAction register_short_tap(uint32_t now_ms, TapSide side,
                                    uint32_t &last_tap_ms, int &last_tap_side) {
  if (last_tap_ms != 0 && (now_ms - last_tap_ms) < DOUBLE_TAP_WINDOW_MS &&
      last_tap_side == side) {
    last_tap_ms = 0;
    return side == TAP_SIDE_LEFT ? TAP_ACTION_PREVIOUS : TAP_ACTION_NEXT;
  }
  last_tap_ms = now_ms;
  last_tap_side = side;
  return TAP_ACTION_NONE;
}
