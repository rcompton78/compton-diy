#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

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
    uint8_t ownedRoomThemes    = 0;  // Store purchase: bitmask, bit N = owns room theme N
    uint8_t equippedRoomTheme  = 0;  // Dressing room: index of the room theme to display
    uint8_t seenRoomThemeCount = 0;  // Highest ROOM_THEME_COUNT the store page has shown the user
    uint8_t ownedCatColors     = 0;  // Store purchase: bitmask, bit N = owns cat color N (white is free, not in this catalog)
    uint8_t equippedCatColor   = 0;  // Dressing room: index of the cat color to display
    uint8_t seenCatColorCount  = 0;  // Highest CAT_COLOR_COUNT the store page has shown the user
    uint32_t totalXp   = 0;          // Lifetime XP; only ever increases, separate from spendable `points`
    uint32_t highestMilestoneLevel = 0;  // Highest level whose milestone bonus points have already
                                          // been paid out; not reset by the badges-page XP reset, so a
                                          // user can't re-farm the same bonus by resetting and re-leveling
    bool setupComplete = false;      // First-run wizard (cat color + name) finished; see ConfigManager::fromJson()
                                      // for why this defaults to false here but migrates existing devices to true
    bool autoUpdateEnabled = true;         // Whether periodic firmware update checks run at all
    String lastUpdateCheckVersion;         // Latest release tag seen at the last check; empty = never checked
    uint32_t lastUpdateCheckEpoch = 0;     // Unix epoch of last check; 0 = never checked
};

class ConfigManager {
public:
    bool begin();
    bool load();
    bool save();
    AppConfig& config() { return _config; }

    // Resets every field to AppConfig's defaults. Only mutates in-memory state — the
    // caller must still call save() to persist it, same as every other config mutation.
    void resetToDefaults() { _config = AppConfig(); }

    // Serializes the entire config as JSON, using the same key names save() writes to
    // disk — for the web UI's backup export/import. Covers everything needed to restore
    // full device state: cat name/schedule, city/timezone, and all gamification state.
    String exportBackupJson() const;
    // Restores the full config from a previously exported JSON blob, with the same
    // validation/bounds-checking load() applies. Returns false only if the payload isn't
    // valid JSON; on success this only mutates in-memory state — the caller must still
    // call save() to persist it, same as every other config mutation.
    bool importBackupJson(const String& json);

private:
    // Shared by save()/exportBackupJson() and load()/importBackupJson() so the on-disk
    // format and the backup format can never drift apart.
    void toJson(JsonDocument& doc) const;
    void fromJson(JsonDocument& doc);

    AppConfig _config;
};
