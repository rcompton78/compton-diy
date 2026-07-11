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
    int thirstForceMinutes = 120;   // force thirsty after this many minutes without water, absent an earlier random trigger
    uint32_t lastWaterEpoch = 0;    // Unix epoch of last water; 0 = never watered
    uint32_t points = 0;            // Gamification points earned from timely care actions
    uint8_t ownedBlanketColors  = 0;  // Store purchase: bitmask, bit N = owns blanket color N
    uint8_t equippedBlanketColor = 0; // Dressing room: index of the blanket color to display
    uint8_t ownedStuffies  = 0;      // Store purchase: bitmask, bit N = owns stuffy N
    uint8_t equippedStuffy = 0;      // Dressing room: index of the stuffy to display
    uint8_t seenStuffyCount       = 0;  // Highest STUFFY_COUNT the store page has shown the user
    uint8_t seenBlanketColorCount = 0;  // Highest BLANKET_COLOR_COUNT the store page has shown the user
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
