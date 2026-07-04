#pragma once
#include <Arduino.h>

struct WeatherData {
    float tempC = 0;
    int   weatherCode = 0;   // WMO weather code
    float windspeedKmh = 0;
    bool  valid = false;
};

class WeatherClient {
public:
    bool fetch(float latitude, float longitude);
    const WeatherData& data() const { return _data; }
    static const char* descriptionForCode(int code);

private:
    WeatherData _data;
};
