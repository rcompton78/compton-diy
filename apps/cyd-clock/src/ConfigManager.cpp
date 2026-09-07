#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../include/Config.h"

bool ConfigManager::begin() {
    return LittleFS.begin(true);
}

// Sanity check for a packed YYYYMMDD date int (0 = unset, always valid) — used below to
// reject malformed persisted/imported theme-week dates before they can reach
// isThemeWeekActive()'s window math (main.cpp), which has no validation of its own.
// Calendar-aware (rejects e.g. Feb 30, and Feb 29 on a non-leap year), not just range-checked.
static bool isPlausibleDateOrZero(int32_t d) {
    if (d == 0) return true;
    int y = d / 10000, mo = (d / 100) % 100, day = d % 100;
    if (y < 2000 || y > 2100 || mo < 1 || mo > 12 || day < 1) return false;
    static constexpr int DAYS_IN_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leapYear = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int maxDay = (mo == 2 && leapYear) ? 29 : DAYS_IN_MONTH[mo - 1];
    return day <= maxDay;
}

void ConfigManager::fromJson(JsonDocument& doc) {
    _config.latitude         = doc["lat"]    | _config.latitude;
    _config.longitude        = doc["lon"]    | _config.longitude;
    _config.timezone         = doc["tz"] | _config.timezone;
    _config.utcOffsetSeconds = doc["utc"]    | _config.utcOffsetSeconds;
    {
        int h = doc["hunger"] | _config.hungerMinutes;
        if (h >= 1 && h <= 1440) _config.hungerMinutes = h;
    }
    _config.lastTreatEpoch   = doc["treat"]  | _config.lastTreatEpoch;
    {
        int b = doc["boredom"] | _config.boredomMinutes;
        if (b >= 1 && b <= 1440) _config.boredomMinutes = b;
    }
    _config.lastPlayEpoch    = doc["play"]   | _config.lastPlayEpoch;
    {
        int b = doc["sleepBed"] | _config.sleepBedMinutes;
        if (b >= 0 && b <= 1439) _config.sleepBedMinutes = b;
    }
    {
        int w = doc["sleepWake"] | _config.sleepWakeMinutes;
        if (w >= 0 && w <= 1439) _config.sleepWakeMinutes = w;
    }
    {
        int s = doc["sickCooldown"] | _config.sickCooldownHours;
        if (s >= 1 && s <= 168) _config.sickCooldownHours = s;
    }
    _config.lastMedsEpoch = doc["meds"] | _config.lastMedsEpoch;
    {
        int t = doc["thirstForceMinutes"] | _config.thirstForceMinutes;
        if (t >= 1 && t <= 1440) _config.thirstForceMinutes = t;
    }
    _config.lastWaterEpoch = doc["water"] | _config.lastWaterEpoch;
    _config.points = doc["points"] | _config.points;
    {
        // Distinguish "blanketColors absent" (preserve current ownership — e.g. a partial
        // backup import) from "blanketColors present as 0" (an owner-cleared/reset state).
        // .is<uint16_t>(), not uint8_t: once 9+ blanket colors are owned the bitmask value
        // exceeds 255, and an is<uint8_t>() check would silently fail and fall through to the
        // legacy/default branches below, losing real ownership data (same bug fixed for
        // ownedStuffies in DIY-97).
        uint16_t colors;
        if (doc["blanketColors"].is<uint16_t>()) colors = doc["blanketColors"];
        else if (doc["blanket"] | false) colors = 1;  // migrate legacy single-blanket flag
        else colors = _config.ownedBlanketColors;
        _config.ownedBlanketColors = colors;
    }
    _config.equippedBlanketColor = doc["blanketEquipped"] | _config.equippedBlanketColor;
    {
        // .is<uint16_t>(), not uint8_t: once 9+ stuffies are owned the bitmask value exceeds
        // 255, and an is<uint8_t>() check would silently fail and fall through to the
        // legacy/default branches below, losing real ownership data (DIY-97).
        uint16_t stuffies;
        if (doc["stuffies"].is<uint16_t>()) stuffies = doc["stuffies"];
        else if (doc["teddy"] | false) stuffies = 1;  // migrate legacy single-teddy flag
        else stuffies = _config.ownedStuffies;
        _config.ownedStuffies = stuffies;
    }
    _config.equippedStuffy = doc["stuffyEquipped"] | _config.equippedStuffy;
    if (doc["stuffiesSecond"].is<uint16_t>()) _config.ownedStuffiesSecond = doc["stuffiesSecond"];
    _config.seenStuffyCount       = doc["seenStuffies"] | _config.seenStuffyCount;
    _config.rightArmSlotUnlocked = doc["rightArmSlot"]       | _config.rightArmSlotUnlocked;
    _config.equippedStuffyRight  = doc["stuffyRightEquipped"] | _config.equippedStuffyRight;
    _config.seenRightArmSlot     = doc["seenRightArmSlot"]    | _config.seenRightArmSlot;
    if (doc["toys"].is<uint16_t>()) _config.ownedToys = doc["toys"];
    _config.equippedToy      = doc["toyEquipped"] | _config.equippedToy;
    _config.seenToyCount     = doc["seenToys"]    | _config.seenToyCount;
    if (doc["rightArmKind"].is<uint8_t>()) {
        _config.equippedRightArmKind = doc["rightArmKind"];
    } else if (_config.equippedStuffyRight < 16 &&
               (_config.ownedStuffies & (1 << _config.equippedStuffyRight))) {
        // Migrating a config that predates this field (DIY-110): a still-owned, still-valid
        // right-arm stuffy index means the right arm was showing a stuffy under the old
        // kind-less model, so preserve that on upgrade rather than defaulting to "nothing
        // equipped" — same reasoning as this file's other legacy migrations (e.g.
        // setupComplete, the old single-teddy/blanket flags above). `< 16`, not a catalog
        // count constant, since STUFFY_COUNT/TOY_COUNT are main.cpp-only — ownedStuffies is
        // itself only 16 bits wide, so this is the widest range a shift below can safely
        // check without relying on those constants or risking undefined behavior from
        // shifting by an out-of-range amount.
        _config.equippedRightArmKind = 1;  // RIGHT_ARM_KIND_STUFFY (main.cpp)
    } else if (_config.equippedToy < 16 &&
               (_config.ownedToys & (1 << _config.equippedToy))) {
        _config.equippedRightArmKind = 2;  // RIGHT_ARM_KIND_TOY (main.cpp) — toys are new in
                                            // this same update, so this branch shouldn't
                                            // currently be reachable from a real predating
                                            // config, but mirrors the stuffy branch above for
                                            // symmetry/robustness.
    }
    _config.seenBlanketColorCount = doc["seenBlankets"]  | _config.seenBlanketColorCount;
    _config.ownedRoomThemes    = doc["roomThemes"]       | _config.ownedRoomThemes;
    _config.equippedRoomTheme  = doc["roomThemeEquipped"] | _config.equippedRoomTheme;
    _config.seenRoomThemeCount = doc["seenRoomThemes"]    | _config.seenRoomThemeCount;
    _config.ownedCatColors    = doc["catColors"]         | _config.ownedCatColors;
    _config.equippedCatColor  = doc["catColorEquipped"]  | _config.equippedCatColor;
    _config.seenCatColorCount = doc["seenCatColors"]     | _config.seenCatColorCount;
    _config.ownedAccessories   = doc["accessories"]        | _config.ownedAccessories;
    _config.equippedAccessory  = doc["accessoryEquipped"]  | _config.equippedAccessory;
    _config.seenAccessoryCount = doc["seenAccessories"]    | _config.seenAccessoryCount;
    _config.ownedGlasses       = doc["glasses"]            | _config.ownedGlasses;
    _config.equippedGlasses    = doc["glassesEquipped"]    | _config.equippedGlasses;
    _config.seenGlassesCount   = doc["seenGlasses"]        | _config.seenGlassesCount;
    if (doc["catNames"].is<JsonArrayConst>()) {
        JsonArrayConst arr = doc["catNames"];
        int i = 0;
        for (JsonVariantConst v : arr) {
            if (i >= CAT_NAME_SLOTS) break;
            String n = v.as<String>();
            if (n.length() >= 1 && n.length() <= 16) _config.catNames[i] = n;
            i++;
        }
        {
            String wn = doc["catNameWhite"] | _config.catNameWhite;
            if (wn.length() >= 1 && wn.length() <= 16) _config.catNameWhite = wn;
        }
        {
            String dn = doc["catNameDefault"] | _config.catNameDefault;
            if (dn.length() >= 1 && dn.length() <= 16) _config.catNameDefault = dn;
        }
    } else {
        // Legacy migration: this config predates per-color names — carry the single
        // legacy name forward as the fallback default, and seed every already-owned
        // color (plus white) with it, so nothing already named gets lost on upgrade.
        // ownedCatColors above is already the freshly-loaded bitmask, so this reflects
        // what the device actually owns right now, not a stale in-memory value.
        String legacy = doc["name"] | _config.catNameDefault;
        if (legacy.length() >= 1 && legacy.length() <= 16) _config.catNameDefault = legacy;
        _config.catNameWhite = _config.catNameDefault;
        for (int i = 0; i < CAT_NAME_SLOTS; i++) {
            if (_config.ownedCatColors & (1 << i)) _config.catNames[i] = _config.catNameDefault;
        }
    }
    _config.totalXp   = doc["xp"] | _config.totalXp;
    _config.highestMilestoneLevel = doc["milestoneLevel"] | _config.highestMilestoneLevel;
    // Migration default is `true`, not the struct's `false`: a config file that predates this
    // field means the device was already set up before the wizard existed, so it shouldn't be
    // forced back through onboarding. Only a device with no config file at all (fresh install /
    // factory reset) keeps the struct default of `false` and sees the wizard, since fromJson()
    // never runs for it.
    _config.setupComplete = doc["setupComplete"] | true;
    _config.autoUpdateEnabled     = doc["autoUpdate"]   | _config.autoUpdateEnabled;
    _config.lastUpdateCheckVersion = doc["lastCheckVer"] | _config.lastUpdateCheckVersion;
    _config.lastUpdateCheckEpoch   = doc["lastCheckAt"]  | _config.lastUpdateCheckEpoch;
    {
        int32_t start = doc["themeWeekBirthdayStartDate"] | _config.themeWeekBirthdayStartDate;
        int32_t end   = doc["themeWeekBirthdayEndDate"]   | _config.themeWeekBirthdayEndDate;
        // Reject the whole pair on any malformed/inverted/partial range, rather than one
        // field — a start with no matching end (or vice versa) is just as meaningless to
        // isThemeWeekActive() as either being individually invalid, so only a fully-cleared
        // (0, 0) pair or a fully-set pair with end >= start is accepted; anything else leaves
        // the previously-persisted values untouched rather than silently disabling a
        // possibly-active range with a broken half-pair.
        if (isPlausibleDateOrZero(start) && isPlausibleDateOrZero(end) &&
            ((start == 0 && end == 0) || (start != 0 && end != 0 && end >= start))) {
            _config.themeWeekBirthdayStartDate = start;
            _config.themeWeekBirthdayEndDate   = end;
        }
    }
    _config.activeThemeWeekKey     = doc["themeWeekActive"] | _config.activeThemeWeekKey;
    _config.preThemeWeekRoomTheme  = doc["themeWeekPrevRoomTheme"] | _config.preThemeWeekRoomTheme;
}

void ConfigManager::toJson(JsonDocument& doc) const {
    doc["lat"]    = _config.latitude;
    doc["lon"]    = _config.longitude;
    doc["tz"]     = _config.timezone;
    doc["utc"]    = _config.utcOffsetSeconds;
    doc["hunger"] = _config.hungerMinutes;
    doc["treat"]  = _config.lastTreatEpoch;
    doc["boredom"] = _config.boredomMinutes;
    doc["play"]    = _config.lastPlayEpoch;
    doc["sleepBed"]  = _config.sleepBedMinutes;
    doc["sleepWake"] = _config.sleepWakeMinutes;
    doc["sickCooldown"] = _config.sickCooldownHours;
    doc["meds"]          = _config.lastMedsEpoch;
    doc["thirstForceMinutes"] = _config.thirstForceMinutes;
    doc["water"]           = _config.lastWaterEpoch;
    doc["points"]          = _config.points;
    doc["blanketColors"]   = _config.ownedBlanketColors;
    doc["blanketEquipped"] = _config.equippedBlanketColor;
    doc["stuffies"]        = _config.ownedStuffies;
    doc["stuffyEquipped"]  = _config.equippedStuffy;
    doc["stuffiesSecond"]  = _config.ownedStuffiesSecond;
    doc["seenStuffies"]    = _config.seenStuffyCount;
    doc["rightArmSlot"]        = _config.rightArmSlotUnlocked;
    doc["stuffyRightEquipped"] = _config.equippedStuffyRight;
    doc["seenRightArmSlot"]    = _config.seenRightArmSlot;
    doc["toys"]            = _config.ownedToys;
    doc["toyEquipped"]     = _config.equippedToy;
    doc["seenToys"]        = _config.seenToyCount;
    doc["rightArmKind"]    = _config.equippedRightArmKind;
    doc["seenBlankets"]    = _config.seenBlanketColorCount;
    doc["roomThemes"]        = _config.ownedRoomThemes;
    doc["roomThemeEquipped"] = _config.equippedRoomTheme;
    doc["seenRoomThemes"]    = _config.seenRoomThemeCount;
    doc["catColors"]        = _config.ownedCatColors;
    doc["catColorEquipped"] = _config.equippedCatColor;
    doc["seenCatColors"]    = _config.seenCatColorCount;
    doc["accessories"]        = _config.ownedAccessories;
    doc["accessoryEquipped"]  = _config.equippedAccessory;
    doc["seenAccessories"]    = _config.seenAccessoryCount;
    doc["glasses"]            = _config.ownedGlasses;
    doc["glassesEquipped"]    = _config.equippedGlasses;
    doc["seenGlasses"]        = _config.seenGlassesCount;
    {
        JsonArray arr = doc["catNames"].to<JsonArray>();
        for (int i = 0; i < CAT_NAME_SLOTS; i++) arr.add(_config.catNames[i]);
    }
    doc["catNameWhite"]   = _config.catNameWhite;
    doc["catNameDefault"] = _config.catNameDefault;
    doc["xp"] = _config.totalXp;
    doc["milestoneLevel"] = _config.highestMilestoneLevel;
    doc["setupComplete"]    = _config.setupComplete;
    doc["autoUpdate"]   = _config.autoUpdateEnabled;
    doc["lastCheckVer"] = _config.lastUpdateCheckVersion;
    doc["lastCheckAt"]  = _config.lastUpdateCheckEpoch;
    doc["themeWeekBirthdayStartDate"] = _config.themeWeekBirthdayStartDate;
    doc["themeWeekBirthdayEndDate"]   = _config.themeWeekBirthdayEndDate;
    doc["themeWeekActive"]         = _config.activeThemeWeekKey;
    doc["themeWeekPrevRoomTheme"]  = _config.preThemeWeekRoomTheme;
}

bool ConfigManager::load() {
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }
    fromJson(doc);

    f.close();
    return true;
}

bool ConfigManager::save() {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;

    JsonDocument doc;
    toJson(doc);

    serializeJson(doc, f);
    f.close();
    return true;
}

String ConfigManager::exportBackupJson() const {
    JsonDocument doc;
    toJson(doc);
    String out;
    serializeJson(doc, out);
    return out;
}

bool ConfigManager::importBackupJson(const String& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    fromJson(doc);
    return true;
}
