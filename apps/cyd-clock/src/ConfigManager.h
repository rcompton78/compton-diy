#pragma once
#include <Arduino.h>

struct AppConfig {
    float latitude = 53.5461;   // Default: Edmonton, AB
    float longitude = -113.4938;
    String timezone = "MST7MDT";
    int utcOffsetSeconds = -25200;
    int hungerMinutes = 60;
    uint32_t lastTreatEpoch = 0;  // Unix epoch of last treat; 0 = never fed
    int boredomMinutes = 90;
    uint32_t lastPlayEpoch = 0;  // Unix epoch of last play; 0 = never played
    int sleepBedMinutes = 1320;   // 22:00, minutes since midnight
    int sleepWakeMinutes = 420;   // 07:00, minutes since midnight
    String catName = "Biscuit";   // Max 16 characters
    int sickCooldownHours = 4;    // min hours between sick events
    uint32_t lastMedsEpoch = 0;   // Unix epoch of last meds; 0 = never medicated
    int thirstCooldownHours = 4;    // min hours between thirsty events
    uint32_t lastWaterEpoch = 0;    // Unix epoch of last water; 0 = never watered
    uint32_t points = 0;            // Gamification points earned from timely care actions
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
