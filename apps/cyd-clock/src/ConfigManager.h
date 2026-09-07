#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

// Capacity for per-cat-color names, index-aligned with CAT_COLORS[] in main.cpp.
// Must stay >= CAT_COLOR_COUNT (static_assert'd in main.cpp once that catalog is defined).
static constexpr int CAT_NAME_SLOTS = 8;

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
    String catNames[CAT_NAME_SLOTS];   // per-owned-color name, index-aligned with CAT_COLORS[];
                                        // empty = not yet named, falls back to catNameDefault
    String catNameWhite;                // name for the free default white cat; same fallback
    String catNameDefault = "Biscuit";  // fallback for any color (white or owned) with no name
                                         // of its own yet; seeded from the single legacy name on
                                         // migration (see ConfigManager::fromJson()), "Biscuit"
                                         // for brand-new devices. Max 16 characters, like every
                                         // other cat name.
    int sickCooldownHours = 4;    // min hours between sick events
    uint32_t lastMedsEpoch = 0;   // Unix epoch of last meds; 0 = never medicated
    int thirstForceMinutes = 120;   // force thirsty after this many minutes without water, absent an earlier random trigger
    uint32_t lastWaterEpoch = 0;    // Unix epoch of last water; 0 = never watered
    uint32_t points = 0;            // Gamification points earned from timely care actions
    uint16_t ownedBlanketColors = 0;  // Store purchase: bitmask, bit N = owns blanket color N (widened
                                      // to uint16_t in DIY-107 — bow/blanket color parity brought the
                                      // catalog past 8 entries, same reasoning as ownedStuffies' DIY-97
                                      // widening)
    uint8_t equippedBlanketColor = 0; // Dressing room: index of the blanket color to display
    uint16_t ownedStuffies = 0;      // Store purchase: bitmask, bit N = owns stuffy N (widened to
                                      // uint16_t in DIY-97 — the snowman (DIY-89) brought the catalog
                                      // to 6 entries, uint8_t only had headroom to 8, same reasoning
                                      // as ownedRoomThemes' DIY-90 widening)
    uint8_t equippedStuffy = 0;      // Dressing room: index of the stuffy to display (left arm / night scene)
    uint16_t ownedStuffiesSecond = 0; // Store purchase: bitmask, bit N = owns a 2nd copy of stuffy N (DIY-106).
                                      // Kept as its own bitmask rather than widening ownedStuffies to 2-bit
                                      // counts, so old on-disk configs just default this to 0 with no
                                      // migration needed. Owning a 2nd copy is what allows the same stuffy
                                      // to be equipped on both arms at once — see buildStuffyRadioOptions().
    uint8_t seenStuffyCount       = 0;  // Highest STUFFY_COUNT the store page has shown the user
    bool    rightArmSlotUnlocked = false;  // Store purchase: one-time unlock, independent of ownedStuffies —
                                            // any already-owned stuffy can be equipped here too
    uint8_t equippedStuffyRight  = 0xFF;   // Dressing room: index of the stuffy on the right arm (day + night).
                                            // 0xFF is EQUIP_NONE (main.cpp) — defaults here to "nothing equipped"
                                            // rather than 0, since unlocking the slot shouldn't auto-equip
                                            // whatever's cheapest/lowest-owned; the user picks explicitly
    bool    seenRightArmSlot     = false;  // Mirrors seenStuffyCount's "has the store page shown this yet"
                                            // role, but as a bool since this is a single item, not a catalog
    uint16_t ownedToys        = 0;  // Store purchase: bitmask, bit N = owns toy N (DIY-110)
    uint8_t equippedToy       = 0xFF;  // Dressing room: index of the toy on the right arm slot, only
                                        // meaningful when equippedRightArmKind == RIGHT_ARM_KIND_TOY
                                        // (main.cpp) — see equippedToyIndex(). 0xFF is EQUIP_NONE
                                        // (main.cpp), same reasoning as equippedStuffyRight.
    uint8_t equippedRightArmKind = 0;  // Which of equippedStuffyRight/equippedToy (if either) is actually
                                        // showing on the shared right-arm slot: 0 = RIGHT_ARM_KIND_NONE,
                                        // 1 = RIGHT_ARM_KIND_STUFFY, 2 = RIGHT_ARM_KIND_TOY (main.cpp).
                                        // The slot holds exactly one of the two, mutually exclusive by
                                        // construction rather than by an implicit priority rule (DIY-110).
    uint8_t seenToyCount      = 0;  // Highest TOY_COUNT the store page has shown the user
    uint8_t seenBlanketColorCount = 0;  // Highest BLANKET_COLOR_COUNT the store page has shown the user
    uint16_t ownedRoomThemes   = 0;  // Store purchase: bitmask, bit N = owns room theme N (widened to
                                      // uint16_t in DIY-90 — 9th theme needs bit 8, uint8_t was full)
    uint8_t equippedRoomTheme  = 0;  // Dressing room: index of the room theme to display
    uint8_t seenRoomThemeCount = 0;  // Highest ROOM_THEME_COUNT the store page has shown the user
    uint8_t ownedCatColors     = 0;  // Store purchase: bitmask, bit N = owns cat color N (white is free, not in this catalog)
    uint8_t equippedCatColor   = 0;  // Dressing room: index of the cat color to display
    uint8_t seenCatColorCount  = 0;  // Highest CAT_COLOR_COUNT the store page has shown the user
    uint16_t ownedAccessories  = 0;  // Store purchase: bitmask, bit N = owns accessory N (widened to
                                      // uint16_t in DIY-107 — bow/blanket color parity brought the
                                      // catalog past 8 entries, same reasoning as ownedStuffies' DIY-97
                                      // widening)
    uint8_t equippedAccessory  = 0;  // Dressing room: index of the accessory to display
    uint8_t seenAccessoryCount = 0;  // Highest ACCESSORY_COUNT the store page has shown the user
    uint8_t ownedGlasses       = 0;  // Store purchase: bitmask, bit N = owns glasses N
    uint8_t equippedGlasses    = 0;  // Dressing room: index of the glasses to display
    uint8_t seenGlassesCount   = 0;  // Highest GLASSES_COUNT the store page has shown the user
    uint32_t totalXp   = 0;          // Lifetime XP; only ever increases, separate from spendable `points`
    uint32_t highestMilestoneLevel = 0;  // Highest level whose milestone bonus points have already
                                          // been paid out; not reset by the badges-page XP reset, so a
                                          // user can't re-farm the same bonus by resetting and re-leveling
    bool setupComplete = false;      // First-run wizard (cat color + name) finished; see ConfigManager::fromJson()
                                      // for why this defaults to false here but migrates existing devices to true
    bool autoUpdateEnabled = true;         // Whether periodic firmware update checks run at all
    String lastUpdateCheckVersion;         // Latest release tag seen at the last check; empty = never checked
    uint32_t lastUpdateCheckEpoch = 0;     // Unix epoch of last check; 0 = never checked

    // Special theme weeks (DIY-108) — entirely local/offline, set via the device's own 7-tap
    // secret admin page (/config/admin/themeweek), not any server. Only one theme
    // ("birthday") is supported for now, so its date range gets its own two fields rather
    // than a generic table — see main.cpp's Theme weeks section for the full mechanism.
    // Calendar dates only (no time-of-day — the admin page is a plain date picker), packed as
    // YYYYMMDD; 0 = unset. The theme is active for the whole of both the start and end date,
    // inclusive, evaluated against the device's own already-configured local clock/timezone
    // (same one the on-screen clock uses) — see isThemeWeekActive()/addOneCalendarDay() in
    // main.cpp for how the inclusive end date becomes an exclusive end-of-day boundary.
    int32_t themeWeekBirthdayStartDate = 0;
    int32_t themeWeekBirthdayEndDate   = 0;
    // Tracks what this device has already applied locally, so applyThemeWeekCosmetics()/
    // revertThemeWeekCosmetics() in main.cpp only fire once per transition rather than on
    // every loop() tick. Empty = no theme week's cosmetics are currently applied.
    String activeThemeWeekKey;
    // Snapshot of equippedRoomTheme taken the moment a theme week's cosmetics were applied,
    // so revertThemeWeekCosmetics() can put back exactly what the user had equipped before,
    // regardless of what they changed it to during the theme week — 0xFF (EQUIP_NONE in
    // main.cpp) means "no theme was equipped before".
    uint8_t preThemeWeekRoomTheme = 0xFF;
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
