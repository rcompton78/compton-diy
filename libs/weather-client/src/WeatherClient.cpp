#include "WeatherClient.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#define WEATHER_API_HOST "api.open-meteo.com"

bool WeatherClient::fetch(float latitude, float longitude) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    char url[256];
    snprintf(url, sizeof(url),
        "https://" WEATHER_API_HOST "/v1/forecast"
        "?latitude=%.4f&longitude=%.4f"
        "&current_weather=true",
        latitude, longitude);

    http.begin(client, url);
    http.setTimeout(10000);
    int code = http.GET();
    if (code != 200) { http.end(); return false; }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;

    JsonObject cw = doc["current_weather"];
    if (cw.isNull()) return false;
    _data.tempC        = cw["temperature"];
    _data.weatherCode  = cw["weathercode"];
    _data.windspeedKmh = cw["windspeed"];
    _data.valid        = true;
    return true;
}

const char* WeatherClient::descriptionForCode(int code) {
    if (code == 0)              return "Clear";
    if (code <= 2)              return "Partly Cloudy";
    if (code == 3)              return "Overcast";
    if (code <= 49)             return "Foggy";
    if (code <= 59)             return "Drizzle";
    if (code <= 69)             return "Rain";
    if (code <= 79)             return "Snow";
    if (code <= 82)             return "Rain Showers";
    if (code <= 86)             return "Snow Showers";
    if (code >= 95)             return "Thunderstorm";
    return "Unknown";
}
