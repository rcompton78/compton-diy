#pragma once

#include <Arduino.h>

// Battery level + USB-host status for the Waveshare ESP32-S3-Touch-LCD-1.69 (COM-298),
// using only what the unmodified board already wires up.
//
// Battery: BAT_ADC (GPIO1) reads B+ through a 200K/100K divider. Readings are averaged,
// smoothed (the Wi-Fi TX current sags the cell for a few ms at a time) and mapped to a
// percentage along a LiPo discharge curve rather than linearly, since a LiPo sits near
// 3.7-3.9V for most of its capacity and then falls off a cliff.
//
// The curve is for a *resting* cell, but B+ is always read under load or while charging:
//  - On battery, the running load sags B+ ~0.15V (BAT_LOAD_SAG_V), which on the steep
//    part of the curve is worth ~20 points, so it's added back before the lookup.
//  - While charging, the charger pushes B+ up towards its 4.2V charge voltage, so it reads
//    high (~100% once it gets there) no matter how full the cell really is.
// So the shown % moves at most one point per PCT_STEP_MS towards the target. Real charge
// or discharge is far slower than that, but plugging in/out no longer makes it jump.
//
// USB: the S3's USB-Serial/JTAG controller has no VBUS sense, so "USB" here means a USB
// *host* is sending start-of-frame packets (HWCDC::isPlugged()). A plain wall charger or
// power bank powers the board without being detected. The ETA6098 charger's STAT pin is
// unrouted, so there's no "charging" signal either (see the README).
class Battery {
public:
    // Configures the ADC pin and takes the first reading, so percent() is valid right away.
    void begin();

    // Samples the ADC every SAMPLE_MS; cheap to call every loop.
    void update(unsigned long now);

    int   percent() const { return _shownPct; }   // 0..100, rate-limited
    float targetPercent() const { return _targetPct; }  // unlimited, from the latest reading
    float volts() const { return _volts; }        // smoothed B+ voltage
    bool  usbConnected() const;                   // a USB host is connected (not VBUS)

    // Exposed for testing/tuning: resting LiPo voltage → 0..100 %.
    static float voltsToPercent(float v);

private:
    static constexpr unsigned long SAMPLE_MS   = 2000;
    static constexpr unsigned long PCT_STEP_MS = 15000;  // ≤4 points/min on screen

    float         _volts      = 0.0f;
    float         _targetPct  = 0.0f;
    int           _shownPct   = -1;   // -1 until the first sample
    bool          _seeded     = false;
    unsigned long _lastSample = 0;
    unsigned long _lastStep   = 0;

    float readVolts() const;
};
