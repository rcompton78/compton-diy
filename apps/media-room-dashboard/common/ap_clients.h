#pragma once

// COM-214: whether anything is joined to the fallback AP. ESPHome's wifi
// component doesn't expose this. Used to keep the offline deep-sleep
// countdown from firing while someone is re-entering wifi details from a
// phone (see the AP interval in common/dashboard.yaml).

#include <esp_wifi.h>

inline bool fallback_ap_has_clients() {
  wifi_sta_list_t list{};
  return esp_wifi_ap_get_sta_list(&list) == ESP_OK && list.num > 0;
}
