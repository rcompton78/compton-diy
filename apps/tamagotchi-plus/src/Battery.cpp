#include "Battery.h"

#include "Config.h"

static constexpr int   ADC_SAMPLES  = 16;
static constexpr float EMA_ALPHA    = 0.2f;   // per 2s sample → ~10s to settle after a step
static constexpr float PCT_DEADBAND = 0.75f;  // shown % only moves once it's clearly changed

// Single-cell LiPo, resting (open-circuit) voltage → state of charge. Typical curve for a
// small pouch cell; interpolated linearly between points.
struct CurvePoint { float v; float pct; };
static constexpr CurvePoint LIPO_CURVE[] = {
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80}, {3.98f, 70},
    {3.95f, 60},  {3.91f, 50}, {3.87f, 40}, {3.85f, 30}, {3.84f, 20}, {3.82f, 15},
    {3.80f, 10},  {3.79f, 5},  {3.70f, 2},  {3.00f, 0},
};
static constexpr int LIPO_CURVE_N = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

float Battery::voltsToPercent(float v) {
    if (v >= LIPO_CURVE[0].v) return 100.0f;
    if (v <= LIPO_CURVE[LIPO_CURVE_N - 1].v) return 0.0f;
    for (int i = 1; i < LIPO_CURVE_N; i++) {
        const CurvePoint& hi = LIPO_CURVE[i - 1];
        const CurvePoint& lo = LIPO_CURVE[i];
        if (v >= lo.v) return lo.pct + (v - lo.v) * (hi.pct - lo.pct) / (hi.v - lo.v);
    }
    return 0.0f;
}

void Battery::begin() {
    pinMode(BAT_ADC_PIN, INPUT);
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);  // ~0-3.1V at the pin: B+/3 fits
    update(millis());
}

float Battery::readVolts() const {
    uint32_t sum = 0;
    for (int i = 0; i < ADC_SAMPLES; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);  // eFuse-calibrated
    return (sum / (float)ADC_SAMPLES) * BAT_DIVIDER_RATIO / 1000.0f;
}

void Battery::update(unsigned long now) {
    if (_seeded && now - _lastSample < SAMPLE_MS) return;
    _lastSample = now;

    float v = readVolts();
    _volts  = _seeded ? _volts + EMA_ALPHA * (v - _volts) : v;
    _seeded = true;

    // Only a USB host is detectable (a wall charger isn't), so the sag compensation also
    // applies on a wall charger. That just pushes a ~4.2V charge reading into the 100% clamp.
    _targetPct = voltsToPercent(_volts + (usbConnected() ? 0.0f : BAT_LOAD_SAG_V));

    if (_shownPct < 0) {
        _shownPct = (int)lroundf(_targetPct);  // boot: show the real value straight away
        _lastStep = now;
    } else if (fabsf(_targetPct - _shownPct) >= PCT_DEADBAND && now - _lastStep >= PCT_STEP_MS) {
        _shownPct += _targetPct > _shownPct ? 1 : -1;
        _lastStep = now;
    }
}

bool Battery::usbConnected() const {
    // SOF-based (see Battery.h): true only while a USB host is actually talking to us.
    return HWCDC::isPlugged();
}
