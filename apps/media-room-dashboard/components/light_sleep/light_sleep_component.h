#pragma once

// COM-214 option 3: ESP-IDF automatic light sleep.
//
// Unlike ESPHome's own deep_sleep component (esp_deep_sleep_start), this never
// reboots -- RAM, the WiFi association, and the HA native API's TCP connection
// all survive a sleep cycle, so wake is near-instant instead of paying a full
// boot + reconnect. The tradeoff is the power savings are smaller than deep
// sleep's near-zero idle current: ESP-IDF's own measurements for this exact
// mode put WiFi-connected auto light sleep at ~1-2.5mA average (vs ~20mA for
// modem-sleep-only with the CPU fully active), see
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html
//
// This is genuinely automatic once configured: esp_pm_configure() hands
// control to ESP-IDF's power-management layer, which drops the CPU into light
// sleep whenever FreeRTOS's idle task has nothing scheduled for a while, and
// wakes it back up in sync with the AP's DTIM beacon to keep the WiFi
// connection alive -- no explicit "enter sleep" action needed from app logic,
// unlike deep_sleep.enter. gpio_wakeup_enable/esp_sleep_enable_gpio_wakeup on
// top of that just means a touch (GPIO17 low) breaks out of a sleep cycle
// immediately rather than waiting for the next DTIM wake.
//
// First attempt at this (before boards/freenove-s3.yaml's gpio backlight
// output + CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION) visibly flickered the
// backlight -- light sleep clock-gates APB, which stops LEDC's PWM waveform
// generation entirely -- and dropped the USB serial console on every sleep
// cycle. Both are addressed at the board-config level now, not here.
//
// How much of the ~1-2.5mA regime is actually achievable still depends on how
// long the idle gaps between scheduled work (LVGL ticks, the touchscreen's
// i2c poll loop) end up being -- if those keep firing every few ms even while
// nothing is happening on screen, the PM system won't find long enough gaps.
// That's the next thing to check once this is on-device.
//
// The wakeup pin also needs its own internal pull-up, enabled here rather
// than relying on the touchscreen driver: boards/freenove-s3.yaml leaves the
// ft63x6 touchscreen's interrupt_pin unset (for an unrelated reset-timing
// reason -- see that file), so the touchscreen driver's own
// pin_mode(FLAG_INPUT | FLAG_PULLUP) code path never runs, and neither
// deep_sleep's esp32_ext1_wakeup nor this component's gpio_wakeup_enable
// configure a pull on their own -- both just hand ESP-IDF a raw pin number.
// Without it, GPIO17 floats whenever the FT6336U's open-drain INT line isn't
// actively pulling it low, which caused spurious deep-sleep wakes with no
// touch involved (confirmed on-device). rtc_gpio_pullup_en, not a plain
// digital pull-up, because this pin needs to hold its pull through deep
// sleep, not just during normal operation -- a regular gpio_set_pull_mode
// pull is not guaranteed to survive that.
//
// rtc_gpio_pullup_en alone still wasn't enough -- also confirmed spuriously
// waking on-device, just less often. Root cause: deep sleep powers down the
// RTC peripheral domain by default, and the internal RTC pull-up/pulldown
// resistors don't function at all while that domain is off, regardless of
// having "enabled" them -- see
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html.
// esp_sleep_pd_config below forces that domain to stay powered through deep
// sleep so the pull-up actually holds. This costs a bit of extra deep-sleep
// current versus the alternative (a real external pull-up resistor on
// GPIO17, which would let the RTC peripheral domain power down normally for
// the lowest possible deep-sleep draw) -- worth revisiting if deep-sleep
// current turns out to matter more than the convenience of not needing to
// solder one on.
//
// Still wasn't enough with deep_sleep_1 ALSO configuring esp32_ext1_wakeup
// on the same GPIO17 (boards/freenove-s3.yaml) -- deep sleep kept spuriously
// self-waking within seconds, but only once both components were active
// together (plain deep sleep alone, before light_sleep existed, was
// reliable). ESP-IDF documents esp_sleep_enable_gpio_wakeup() (this
// component) and esp_sleep_enable_ext1_wakeup() (deep_sleep's ext1 config)
// as alternatives, not something meant to run together on one pin. Fixed by
// removing deep_sleep_1's ext1 config entirely -- this component's GPIO
// wakeup source is valid for deep sleep too on an RTC-capable pin like
// GPIO17, so it's now the only wakeup-source registration on this pin,
// serving both light sleep and deep sleep.

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_sleep.h"

namespace esphome::light_sleep {

static const char *const TAG = "light_sleep";

class LightSleepComponent : public Component {
 public:
  void set_wakeup_pin(uint8_t pin) { this->wakeup_pin_ = pin; }

  void setup() override {
    esp_pm_config_t pm_config = {};
    // 160MHz/40MHz(XTAL) matches ESP-IDF's own documented reference config for
    // this mode -- see the file comment above. Dropping the ceiling from the
    // board's default 240MHz costs some headroom during active UI rendering
    // in exchange for the DFS range light sleep needs; revisit if the UI
    // feels sluggish during active use.
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 40;
    pm_config.light_sleep_enable = true;
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }

    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);

    // Keeps the RTC pull-up above actually functional through deep sleep --
    // see the file comment.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    rtc_gpio_pullup_en(pin);
    rtc_gpio_pulldown_dis(pin);

    gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Light Sleep (COM-214 option 3):");
    ESP_LOGCONFIG(TAG, "  Wakeup pin: GPIO%u", this->wakeup_pin_);
  }

 protected:
  uint8_t wakeup_pin_{0};
};

}  // namespace esphome::light_sleep
