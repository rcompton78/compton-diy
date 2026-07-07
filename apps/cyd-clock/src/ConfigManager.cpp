#include "ConfigManager.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "../include/Config.h"

bool ConfigManager::begin() {
    return LittleFS.begin(true);
}

bool ConfigManager::load() {
    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) return false;

    JsonDocument doc;
    if (deserializeJson(doc, f)) { f.close(); return false; }

    _config.latitude         = doc["lat"]    | _config.latitude;
    _config.longitude        = doc["lon"]    | _config.longitude;
    _config.timezone         = doc["tz"].as<String>();
    _config.utcOffsetSeconds = doc["utc"]    | _config.utcOffsetSeconds;
    {
        int h = doc["hunger"] | _config.hungerMinutes;
        if (h >= 1 && h <= 1440) _config.hungerMinutes = h;
    }
    _config.lastTreatEpoch   = doc["treat"]  | _config.lastTreatEpoch;

    f.close();
    return true;
}

bool ConfigManager::save() {
    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) return false;

    JsonDocument doc;
    doc["lat"]    = _config.latitude;
    doc["lon"]    = _config.longitude;
    doc["tz"]     = _config.timezone;
    doc["utc"]    = _config.utcOffsetSeconds;
    doc["hunger"] = _config.hungerMinutes;
    doc["treat"]  = _config.lastTreatEpoch;

    serializeJson(doc, f);
    f.close();
    return true;
}
