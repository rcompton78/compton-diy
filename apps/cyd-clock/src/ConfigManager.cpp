#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../include/Config.h"

bool ConfigManager::begin() {
    return LittleFS.begin(true);
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
        String n = doc["name"] | _config.catName;
        if (n.length() >= 1 && n.length() <= 16) _config.catName = n;
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
        uint8_t colors;
        if (doc["blanketColors"].is<uint8_t>()) colors = doc["blanketColors"];
        else if (doc["blanket"] | false) colors = 1;  // migrate legacy single-blanket flag
        else colors = _config.ownedBlanketColors;
        _config.ownedBlanketColors = colors;
    }
    _config.equippedBlanketColor = doc["blanketEquipped"] | _config.equippedBlanketColor;
    {
        uint8_t stuffies;
        if (doc["stuffies"].is<uint8_t>()) stuffies = doc["stuffies"];
        else if (doc["teddy"] | false) stuffies = 1;  // migrate legacy single-teddy flag
        else stuffies = _config.ownedStuffies;
        _config.ownedStuffies = stuffies;
    }
    _config.equippedStuffy = doc["stuffyEquipped"] | _config.equippedStuffy;
    _config.seenStuffyCount       = doc["seenStuffies"] | _config.seenStuffyCount;
    _config.seenBlanketColorCount = doc["seenBlankets"]  | _config.seenBlanketColorCount;
    _config.ownedRoomThemes    = doc["roomThemes"]       | _config.ownedRoomThemes;
    _config.equippedRoomTheme  = doc["roomThemeEquipped"] | _config.equippedRoomTheme;
    _config.seenRoomThemeCount = doc["seenRoomThemes"]    | _config.seenRoomThemeCount;
    _config.ownedCatColors    = doc["catColors"]         | _config.ownedCatColors;
    _config.equippedCatColor  = doc["catColorEquipped"]  | _config.equippedCatColor;
    _config.seenCatColorCount = doc["seenCatColors"]     | _config.seenCatColorCount;
    _config.totalXp   = doc["xp"] | _config.totalXp;
    // Migration default is `true`, not the struct's `false`: a config file that predates this
    // field means the device was already set up before the wizard existed, so it shouldn't be
    // forced back through onboarding. Only a device with no config file at all (fresh install /
    // factory reset) keeps the struct default of `false` and sees the wizard, since fromJson()
    // never runs for it.
    _config.setupComplete = doc["setupComplete"] | true;
    _config.autoUpdateEnabled     = doc["autoUpdate"]   | _config.autoUpdateEnabled;
    _config.lastUpdateCheckVersion = doc["lastCheckVer"] | _config.lastUpdateCheckVersion;
    _config.lastUpdateCheckEpoch   = doc["lastCheckAt"]  | _config.lastUpdateCheckEpoch;
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
    doc["name"]      = _config.catName;
    doc["sickCooldown"] = _config.sickCooldownHours;
    doc["meds"]          = _config.lastMedsEpoch;
    doc["thirstForceMinutes"] = _config.thirstForceMinutes;
    doc["water"]           = _config.lastWaterEpoch;
    doc["points"]          = _config.points;
    doc["blanketColors"]   = _config.ownedBlanketColors;
    doc["blanketEquipped"] = _config.equippedBlanketColor;
    doc["stuffies"]        = _config.ownedStuffies;
    doc["stuffyEquipped"]  = _config.equippedStuffy;
    doc["seenStuffies"]    = _config.seenStuffyCount;
    doc["seenBlankets"]    = _config.seenBlanketColorCount;
    doc["roomThemes"]        = _config.ownedRoomThemes;
    doc["roomThemeEquipped"] = _config.equippedRoomTheme;
    doc["seenRoomThemes"]    = _config.seenRoomThemeCount;
    doc["catColors"]        = _config.ownedCatColors;
    doc["catColorEquipped"] = _config.equippedCatColor;
    doc["seenCatColors"]    = _config.seenCatColorCount;
    doc["xp"] = _config.totalXp;
    doc["setupComplete"]    = _config.setupComplete;
    doc["autoUpdate"]   = _config.autoUpdateEnabled;
    doc["lastCheckVer"] = _config.lastUpdateCheckVersion;
    doc["lastCheckAt"]  = _config.lastUpdateCheckEpoch;
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
