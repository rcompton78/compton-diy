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
        int t = doc["thirstCooldown"] | _config.thirstCooldownHours;
        if (t >= 1 && t <= 168) _config.thirstCooldownHours = t;
    }
    _config.lastWaterEpoch = doc["water"] | _config.lastWaterEpoch;

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
    doc["boredom"] = _config.boredomMinutes;
    doc["play"]    = _config.lastPlayEpoch;
    doc["sleepBed"]  = _config.sleepBedMinutes;
    doc["sleepWake"] = _config.sleepWakeMinutes;
    doc["name"]      = _config.catName;
    doc["sickCooldown"] = _config.sickCooldownHours;
    doc["meds"]          = _config.lastMedsEpoch;
    doc["thirstCooldown"] = _config.thirstCooldownHours;
    doc["water"]           = _config.lastWaterEpoch;

    serializeJson(doc, f);
    f.close();
    return true;
}
