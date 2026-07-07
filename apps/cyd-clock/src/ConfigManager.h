#pragma once
#include <Arduino.h>

struct AppConfig {
    float latitude = 53.5461;   // Default: Edmonton, AB
    float longitude = -113.4938;
    String timezone = "MST7MDT";
    int utcOffsetSeconds = -25200;
    int hungerMinutes = 60;
    uint32_t lastTreatEpoch = 0;  // Unix epoch of last treat; 0 = never fed
};

class ConfigManager {
public:
    bool begin();
    bool load();
    bool save();
    AppConfig& config() { return _config; }

private:
    AppConfig _config;
};
