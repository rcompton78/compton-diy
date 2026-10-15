#pragma once

// COM-214: ESP-IDF automatic light sleep, plus the hand-off to deep sleep and
// the wake diagnostics used to chase deep sleep's self-wake bug.
//
// Light sleep (tier 1). Unlike ESPHome's deep_sleep component
// (esp_deep_sleep_start), this never reboots -- RAM, the WiFi association, and
// the HA native API's TCP connection all survive a sleep cycle, so wake is
// near-instant. ESP-IDF's own measurements put WiFi-connected auto light sleep
// at ~1-2.5mA average (vs ~20mA for modem-sleep-only with the CPU active), see
// https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/low-power-mode/low-power-mode-wifi.html
// esp_pm_configure() hands control to ESP-IDF's power management, which drops
// the CPU into light sleep whenever FreeRTOS's idle task has nothing scheduled,
// waking in sync with the AP's DTIM beacon. gpio_wakeup_enable +
// esp_sleep_enable_gpio_wakeup on top means a touch (GPIO17 low) breaks out
// of a sleep cycle immediately instead of waiting for the next beacon. How
// much of the ~1-2.5mA is reachable depends on how often ESPHome's loop, LVGL
// and the touch poll wake the CPU; that hasn't been measured yet.
//
// Deep sleep (tier 3) and the self-wake bug. The first deep sleep + light
// sleep build self-woke within seconds of sleeping. Its last attempted fix
// dropped deep_sleep_1's ext1 config and relied on this component's GPIO
// wakeup for deep sleep too, but on the ESP32-S3 GPIO wakeup is light-sleep
// only (esp32s3 soc_caps.h has no SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP; S3 deep
// sleep wakes from a pin only via ext0/ext1). Worse, esp_sleep_enable_gpio_wakeup
// leaves RTC_GPIO_TRIG_EN set in ESP-IDF's global wake triggers, and nothing
// cleared it before esp_deep_sleep_start(), so every deep sleep went down with
// a light-sleep-only trigger still armed. on_powerdown() below now undoes all
// of light sleep's setup first: auto light sleep off, the GPIO wake source
// disarmed. deep_sleep_1 (boards/freenove-s3.yaml) arms its own ext0 wake on
// GPIO17 after that, as the only wake source.
//
// Diagnostics. Battery-only testing has no serial console, so the reason for
// every boot is kept where HA can show it:
//  - wake_reason(): this boot's esp_reset_reason() + esp_sleep_get_wakeup_cause().
//  - sleep_log(): the last LOG_SIZE boots, newest first, kept in RTC memory
//    (survives deep sleep, not power loss). Each entry has the boot's wake
//    cause, how long it was down (system time keeps running on the RTC timer
//    through deep sleep) and GPIO17's level when it went down -- a low level
//    there means ext0 would wake it straight back up.
// on_powerdown() also runs on a safe reboot (OTA, the restart button), not
// just deep sleep, so the entry it opens is only labelled a sleep if the next
// boot's reset reason says it woke from deep sleep.

#include <cinttypes>
#include <cstdio>
#include <string>
#include <sys/time.h>

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace esphome::light_sleep {

static const char *const TAG = "light_sleep";

static constexpr uint32_t LOG_MAGIC = 0xC0214A11;
static constexpr int LOG_SIZE = 5;

struct SleepLogEntry {
  uint32_t seq;
  int32_t down_s;  // seconds between going down and this boot, -1 if unknown
  uint8_t reset_reason;
  uint8_t wake_cause;
  uint8_t pin_level_at_entry;  // 0/1, or 2 if unknown
};

struct SleepLog {
  uint32_t magic;
  uint32_t seq;
  // Set by on_powerdown(), consumed by the next boot's setup().
  bool pending;
  int64_t pending_at_s;
  uint8_t pending_pin_level;
  int count;
  SleepLogEntry entries[LOG_SIZE];  // entries[0] is the newest
};

// RTC slow memory: kept through deep sleep, garbage/zero after power loss,
// hence the magic check in setup().
RTC_NOINIT_ATTR static SleepLog s_sleep_log;

static int64_t now_s() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

static const char *reset_reason_str(uint8_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power-on";
    case ESP_RST_EXT:
      return "external pin";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_DEEPSLEEP:
      return "deep sleep";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_USB:
      return "USB";
    case ESP_RST_JTAG:
      return "JTAG";
    default:
      return "other";
  }
}

static const char *wake_cause_str(uint8_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "none";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "timer";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "touchpad";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    default:
      return "other";
  }
}

class LightSleepComponent : public Component {
 public:
  void set_wakeup_pin(uint8_t pin) { this->wakeup_pin_ = pin; }

  // Before most components, so the wake record exists before anything that
  // publishes it.
  float get_setup_priority() const override { return setup_priority::BUS; }

  void setup() override {
    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);

    this->record_boot_();

    // deep_sleep_1 holds GPIO17's config through deep sleep (gpio_hold_en);
    // release it so the pin can be configured again.
    gpio_hold_dis(pin);
    // ESPHome never configures this pin: the touchscreen runs in polling mode
    // (interrupt_pin unset, see boards/freenove-s3.yaml) and deep_sleep_1 only
    // reads it. Make it a pulled-up input so light sleep's wake and
    // deep_sleep_1's KEEP_AWAKE check both read the real INT level, not a
    // floating or input-disabled pin.
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_pullup_en(pin);
    gpio_pulldown_dis(pin);

    esp_pm_config_t pm_config = {};
    // 160MHz/40MHz(XTAL) matches ESP-IDF's own documented reference config for
    // this mode -- see the file comment above.
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 40;
    pm_config.light_sleep_enable = true;
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }

    gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
  }

  // Runs after teardown, just before deep_sleep_1 arms ext0 and calls
  // esp_deep_sleep_start() (and also before a safe reboot).
  void on_powerdown() override {
    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);

    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 160;
    pm_config.light_sleep_enable = false;
    esp_err_t pm_err = esp_pm_configure(&pm_config);
    gpio_wakeup_disable(pin);
    esp_err_t src_err = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);

    int level = gpio_get_level(pin);
    s_sleep_log.pending = true;
    s_sleep_log.pending_at_s = now_s();
    s_sleep_log.pending_pin_level = static_cast<uint8_t>(level);

    ESP_LOGI(TAG, "Powering down: GPIO%u=%s, auto light sleep off (%s), GPIO wake disarmed (%s)",
             this->wakeup_pin_, level ? "high" : "LOW", esp_err_to_name(pm_err), esp_err_to_name(src_err));
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Light Sleep (COM-214):");
    ESP_LOGCONFIG(TAG, "  Wakeup pin: GPIO%u", this->wakeup_pin_);
    ESP_LOGCONFIG(TAG, "  This boot: %s", this->wake_reason().c_str());
    ESP_LOGCONFIG(TAG, "  Sleep log: %s", this->sleep_log().c_str());
  }

  // e.g. "deep sleep, woke on EXT0" or "power-on".
  std::string wake_reason() const {
    std::string out = reset_reason_str(this->reset_reason_);
    if (this->wake_cause_ != ESP_SLEEP_WAKEUP_UNDEFINED) {
      out += ", woke on ";
      out += wake_cause_str(this->wake_cause_);
    }
    return out;
  }

  // Newest first, e.g. "#7 EXT0 after 42s (pin high) | #6 power-on". Kept
  // well under HA's 255-character state limit.
  std::string sleep_log() const {
    std::string out;
    char buf[64];
    for (int i = 0; i < s_sleep_log.count; i++) {
      const SleepLogEntry &e = s_sleep_log.entries[i];
      const char *what = e.reset_reason == ESP_RST_DEEPSLEEP ? wake_cause_str(e.wake_cause)
                                                              : reset_reason_str(e.reset_reason);
      if (e.down_s >= 0) {
        snprintf(buf, sizeof(buf), "#%" PRIu32 " %s after %" PRId32 "s (pin %s)", e.seq, what, e.down_s,
                 e.pin_level_at_entry == 0 ? "LOW" : "high");
      } else {
        snprintf(buf, sizeof(buf), "#%" PRIu32 " %s", e.seq, what);
      }
      if (!out.empty())
        out += " | ";
      out += buf;
    }
    return out;
  }

 protected:
  void record_boot_() {
    this->reset_reason_ = static_cast<uint8_t>(esp_reset_reason());
    this->wake_cause_ = static_cast<uint8_t>(esp_sleep_get_wakeup_cause());

    if (s_sleep_log.magic != LOG_MAGIC || s_sleep_log.count < 0 || s_sleep_log.count > LOG_SIZE) {
      s_sleep_log = {};
      s_sleep_log.magic = LOG_MAGIC;
    }

    SleepLogEntry entry{};
    entry.seq = ++s_sleep_log.seq;
    entry.reset_reason = this->reset_reason_;
    entry.wake_cause = this->wake_cause_;
    entry.down_s = -1;
    entry.pin_level_at_entry = 2;
    if (s_sleep_log.pending) {
      entry.down_s = static_cast<int32_t>(now_s() - s_sleep_log.pending_at_s);
      entry.pin_level_at_entry = s_sleep_log.pending_pin_level;
      s_sleep_log.pending = false;
    }

    for (int i = LOG_SIZE - 1; i > 0; i--)
      s_sleep_log.entries[i] = s_sleep_log.entries[i - 1];
    s_sleep_log.entries[0] = entry;
    if (s_sleep_log.count < LOG_SIZE)
      s_sleep_log.count++;

    ESP_LOGI(TAG, "Boot: %s", this->wake_reason().c_str());
  }

  uint8_t wakeup_pin_{0};
  uint8_t reset_reason_{0};
  uint8_t wake_cause_{0};
};

}  // namespace esphome::light_sleep
