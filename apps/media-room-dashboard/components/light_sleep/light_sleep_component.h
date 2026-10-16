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
// GPIO17 also needs an RTC pull-up in deep sleep. On the S3 (where
// SOC_GPIO_SUPPORT_RTC_INDEPENDENT is set) gpio_pullup_en() and ESPHome's
// gpio_sleep_set_pull_mode() only set the digital pad's pull, and arming ext0
// switches the pin to the RTC mux, where that pull no longer applies. The
// FT6336U's INT line then floated and woke ext0 3-20s into every deep sleep
// with no touch (confirmed on-device with the Sleep Log below). on_powerdown()
// enables the RTC pull-up (ext0 keeps the RTC_PERIPH domain powered, so it
// holds), and setup() hands the pin back to the digital mux after a wake.
//
// Every other pin is latched through deep sleep too. The S3 can't hold a
// single digital pin in deep sleep (no SOC_GPIO_SUPPORT_HOLD_SINGLE_IO_IN_DSLP),
// so deep_sleep_1's ext0 setup calls gpio_deep_sleep_hold_en(), and ESP-IDF
// then isolates and holds every digital pad (backlight off, display SPI/CS/DC
// and touch I2C disabled). On wake ESP-IDF only releases the wake pins, so
// the firmware ran normally after a touch wake but the screen stayed black
// (confirmed on-device). setup() releases the global hold, at a priority
// above the display, backlight and buses, so their setup drives real pins.
//
// That isolation also left the FT6336U's RST (GPIO18) and I2C lines floating
// with their pulls off, and deep sleep still woke on ext0 every minute or two
// with no touch (pin high at entry). on_powerdown() now drives
// sleep_hold_high_pins high (touch RST) and gives sleep_hold_pullup_pins
// (touch I2C) a pull-up, each with its own gpio_hold_en, which the isolation
// pass skips. setup() releases them again.
//
// Wake guard. Anything that still pulses INT low without a touch would
// reboot the device and reconnect wifi/HA. check_touch_wake() runs early in
// boot (an on_boot lambda in boards/freenove-s3.yaml, before wifi starts):
// after an ext0 wake it asks the touch chip whether a finger is (or just
// was) down, and if not, sleep_again_no_touch() goes straight back to deep
// sleep, logged as "no-touch" in the Sleep Log.
//
// Pins that keep their level through light sleep. With PM enabled, ESP-IDF
// sets CONFIG_PM_SLP_DISABLE_GPIO by default: at startup it gives every pin a
// "disabled, floating" sleep config and switches to it on every light sleep.
// That cut the gpio backlight (and floated the display's CS/DC) dozens of
// times a second, which showed as screen flicker -- only off a USB host,
// since CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION suppresses light sleep while one
// is attached (confirmed on a phone charger). gpio_sleep_sel_dis() opts the
// pins in keep_level_pins out of that switch so they keep their normal
// config; it runs in setup(), after ESP-IDF's startup isolation, and
// gpio_config() never turns it back on. The wakeup pin is already opted out
// by gpio_wakeup_enable().
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
#include <cstring>
#include <string>
#include <vector>
#include <sys/time.h>

#include "esphome/core/component.h"
#include "esphome/core/log.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace esphome::light_sleep {

static const char *const TAG = "light_sleep";

static constexpr uint32_t LOG_MAGIC = 0xC0214A13;  // bump when SleepLog changes
static constexpr int LOG_SIZE = 5;
// Home Assistant truncates text sensor states past 255 characters.
static constexpr size_t LOG_TEXT_MAX = 255;

struct SleepLogEntry {
  uint32_t seq;
  int32_t down_s;  // seconds between going down and this boot, -1 if unknown
  uint8_t reset_reason;
  uint8_t wake_cause;
  uint8_t pin_level_at_entry;  // 0/1, or 2 if unknown
  uint8_t no_touch;            // 1 if the wake guard sent it back to sleep
  uint8_t powerdown_errors;    // failed GPIO/RTC/sleep calls while going down
};

struct SleepLog {
  uint32_t magic;
  uint32_t seq;
  // Set by on_powerdown(), consumed by the next boot's setup().
  bool pending;
  int64_t pending_at_s;
  uint8_t pending_pin_level;
  uint8_t pending_errors;
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
  void add_keep_level_pin(uint8_t pin) { this->keep_level_pins_.push_back(pin); }
  void add_sleep_hold_high_pin(uint8_t pin) { this->sleep_hold_high_pins_.push_back(pin); }
  void add_sleep_hold_pullup_pin(uint8_t pin) { this->sleep_hold_pullup_pins_.push_back(pin); }

  // Before every other component: the deep-sleep pad hold has to be released
  // before the buses, display and backlight configure their pins, and the
  // wake record has to exist before anything publishes it.
  float get_setup_priority() const override { return setup_priority::BUS + 100.0f; }

  void setup() override {
    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);

    this->record_boot_();

    // Release the digital pad hold left over from deep sleep, and the touch
    // lines' own holds (see the file comment). Harmless when not set.
    gpio_deep_sleep_hold_dis();
    for (uint8_t p : this->sleep_hold_high_pins_)
      this->setup_errors_ += !this->check_(gpio_hold_dis(static_cast<gpio_num_t>(p)), "gpio_hold_dis", p);
    for (uint8_t p : this->sleep_hold_pullup_pins_)
      this->setup_errors_ += !this->check_(gpio_hold_dis(static_cast<gpio_num_t>(p)), "gpio_hold_dis", p);

    // deep_sleep_1 holds GPIO17's config through deep sleep (gpio_hold_en)
    // and ext0 leaves it on the RTC mux with an RTC pull-up; release both so
    // the digital config below applies again.
    this->setup_errors_ += !this->check_(gpio_hold_dis(pin), "gpio_hold_dis", pin);
    this->setup_errors_ += !this->check_(rtc_gpio_pullup_dis(pin), "rtc_gpio_pullup_dis", pin);
    this->setup_errors_ += !this->check_(rtc_gpio_deinit(pin), "rtc_gpio_deinit", pin);
    // ESPHome never configures this pin: the touchscreen runs in polling mode
    // (interrupt_pin unset, see boards/freenove-s3.yaml) and deep_sleep_1 only
    // reads it. Make it a pulled-up input so light sleep's wake and
    // deep_sleep_1's KEEP_AWAKE check both read the real INT level, not a
    // floating or input-disabled pin.
    this->setup_errors_ += !this->check_(gpio_set_direction(pin, GPIO_MODE_INPUT), "gpio_set_direction", pin);
    this->setup_errors_ += !this->check_(gpio_pullup_en(pin), "gpio_pullup_en", pin);
    this->setup_errors_ += !this->check_(gpio_pulldown_dis(pin), "gpio_pulldown_dis", pin);

    esp_pm_config_t pm_config = {};
    // 160MHz/40MHz(XTAL) matches ESP-IDF's own documented reference config for
    // this mode -- see the file comment above.
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 40;
    pm_config.light_sleep_enable = true;
    this->setup_errors_ += !this->check_(esp_pm_configure(&pm_config), "esp_pm_configure", -1);

    this->setup_errors_ += !this->check_(gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL), "gpio_wakeup_enable", pin);
    this->setup_errors_ += !this->check_(esp_sleep_enable_gpio_wakeup(), "esp_sleep_enable_gpio_wakeup", -1);

    for (uint8_t keep : this->keep_level_pins_)
      this->setup_errors_ += !this->check_(gpio_sleep_sel_dis(static_cast<gpio_num_t>(keep)), "gpio_sleep_sel_dis", keep);
  }

  // Runs after teardown, just before deep_sleep_1 arms ext0 and calls
  // esp_deep_sleep_start() (and also before a safe reboot).
  void on_powerdown() override {
    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);

    // Failures here are counted into the Sleep Log entry the next boot
    // writes (shown as "E<n>"): there's no console on battery, and a failed
    // call here is exactly what brings back floating pins and self-wakes.
    unsigned errors = 0;

    esp_pm_config_t pm_config = {};
    pm_config.max_freq_mhz = 160;
    pm_config.min_freq_mhz = 160;
    pm_config.light_sleep_enable = false;
    errors += !this->check_(esp_pm_configure(&pm_config), "esp_pm_configure", -1);
    errors += !this->check_(gpio_wakeup_disable(pin), "gpio_wakeup_disable", pin);
    errors += !this->check_(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO), "disable GPIO wake source", -1);

    // The digital pull-up from setup() stops applying once ext0 moves the pin
    // to the RTC mux; this one keeps INT from floating in deep sleep.
    errors += !this->check_(rtc_gpio_pullup_en(pin), "rtc_gpio_pullup_en", pin);
    errors += !this->check_(rtc_gpio_pulldown_dis(pin), "rtc_gpio_pulldown_dis", pin);

    // Keep the touch chip's RST and I2C lines defined through deep sleep
    // instead of isolated and floating (see the file comment).
    for (uint8_t p : this->sleep_hold_high_pins_) {
      auto gp = static_cast<gpio_num_t>(p);
      errors += !this->check_(gpio_set_direction(gp, GPIO_MODE_OUTPUT), "gpio_set_direction", p);
      errors += !this->check_(gpio_set_level(gp, 1), "gpio_set_level", p);
      errors += !this->check_(gpio_hold_en(gp), "gpio_hold_en", p);
    }
    for (uint8_t p : this->sleep_hold_pullup_pins_) {
      auto gp = static_cast<gpio_num_t>(p);
      errors += !this->check_(gpio_set_direction(gp, GPIO_MODE_INPUT), "gpio_set_direction", p);
      errors += !this->check_(gpio_pullup_en(gp), "gpio_pullup_en", p);
      errors += !this->check_(gpio_pulldown_dis(gp), "gpio_pulldown_dis", p);
      errors += !this->check_(gpio_hold_en(gp), "gpio_hold_en", p);
    }

    int level = gpio_get_level(pin);
    s_sleep_log.pending = true;
    s_sleep_log.pending_at_s = now_s();
    s_sleep_log.pending_pin_level = static_cast<uint8_t>(level);
    s_sleep_log.pending_errors = static_cast<uint8_t>(errors > 255 ? 255 : errors);

    ESP_LOGI(TAG, "Powering down: GPIO%u=%s, %u failed sleep call(s)", this->wakeup_pin_, level ? "high" : "LOW",
             errors);
  }

  void dump_config() override {
    ESP_LOGCONFIG(TAG, "Light Sleep (COM-214):");
    ESP_LOGCONFIG(TAG, "  Wakeup pin: GPIO%u", this->wakeup_pin_);
    for (uint8_t keep : this->keep_level_pins_)
      ESP_LOGCONFIG(TAG, "  Keeps level in light sleep: GPIO%u", keep);
    ESP_LOGCONFIG(TAG, "  This boot: %s", this->wake_reason().c_str());
    ESP_LOGCONFIG(TAG, "  Sleep log: %s", this->sleep_log().c_str());
  }

  // True when this boot is a deep-sleep wake on the touch INT pin (ext0, the
  // only pin wake deep_sleep_1 arms).
  bool woke_on_touch_pin() const {
    return this->reset_reason_ == ESP_RST_DEEPSLEEP && this->wake_cause_ == ESP_SLEEP_WAKEUP_EXT0;
  }

  // The wake guard's fallback: no touch behind this wake, so log it and go
  // straight back to deep sleep. Called before wifi has started, so this
  // skips ESPHome's teardown (deep_sleep_1.begin_sleep() would tear down
  // components that haven't set up yet) and re-implements the end of
  // deep_sleep_1's sequence instead. COUPLED TO deep_sleep_1 in
  // boards/freenove-s3.yaml: ext0 on wakeup_pin_, wake on LOW (`inverted:
  // true`), pin held through sleep. Change one, change the other.
  void sleep_again_no_touch() {
    s_sleep_log.entries[0].no_touch = 1;
    ESP_LOGI(TAG, "Woke on GPIO%u with no touch; back to deep sleep", this->wakeup_pin_);
    this->on_powerdown();
    auto pin = static_cast<gpio_num_t>(this->wakeup_pin_);
    gpio_hold_en(pin);
    gpio_deep_sleep_hold_en();
    esp_sleep_enable_ext0_wakeup(pin, 0);
    esp_deep_sleep_start();
  }

  // e.g. "deep sleep, woke on EXT0" or "power-on", plus a failure count if
  // any of setup()'s GPIO/RTC/sleep calls failed.
  std::string wake_reason() const {
    std::string out = reset_reason_str(this->reset_reason_);
    if (this->wake_cause_ != ESP_SLEEP_WAKEUP_UNDEFINED) {
      out += ", woke on ";
      out += wake_cause_str(this->wake_cause_);
    }
    if (this->setup_errors_ > 0)
      out += ", " + std::to_string(this->setup_errors_) + " sleep setup call(s) failed";
    return out;
  }

  // Newest first, e.g. "#7 EXT0 after 42s (pin high) | #6 power-on". "E<n>"
  // marks failed calls while going down. Entries that would push it past
  // HA's 255-character state limit are dropped.
  std::string sleep_log() const {
    std::string out;
    char buf[96];
    for (int i = 0; i < s_sleep_log.count; i++) {
      const SleepLogEntry &e = s_sleep_log.entries[i];
      const char *what = e.reset_reason == ESP_RST_DEEPSLEEP ? wake_cause_str(e.wake_cause)
                                                              : reset_reason_str(e.reset_reason);
      if (e.down_s >= 0) {
        const char *level = e.pin_level_at_entry == 0 ? "LOW" : e.pin_level_at_entry == 1 ? "high" : "?";
        char errs[8] = "";
        if (e.powerdown_errors > 0)
          snprintf(errs, sizeof(errs), " E%u", e.powerdown_errors);
        snprintf(buf, sizeof(buf), "#%" PRIu32 " %s%s after %" PRId32 "s (pin %s)%s", e.seq, what,
                 e.no_touch ? " no-touch" : "", e.down_s, level, errs);
      } else {
        snprintf(buf, sizeof(buf), "#%" PRIu32 " %s", e.seq, what);
      }
      size_t add = strlen(buf) + (out.empty() ? 0 : 3);
      if (out.size() + add > LOG_TEXT_MAX)
        break;
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

    // A power-on starts a fresh log, so each test run (e.g. moving the board
    // to a charger) reads cleanly from "#1 power-on".
    if (s_sleep_log.magic != LOG_MAGIC || s_sleep_log.count < 0 || s_sleep_log.count > LOG_SIZE ||
        this->reset_reason_ == ESP_RST_POWERON) {
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
      entry.powerdown_errors = s_sleep_log.pending_errors;
      s_sleep_log.pending = false;
    }

    for (int i = LOG_SIZE - 1; i > 0; i--)
      s_sleep_log.entries[i] = s_sleep_log.entries[i - 1];
    s_sleep_log.entries[0] = entry;
    if (s_sleep_log.count < LOG_SIZE)
      s_sleep_log.count++;

    ESP_LOGI(TAG, "Boot: %s", this->wake_reason().c_str());
  }

  // Logs a failed ESP-IDF call; returns whether it succeeded. Pass pin < 0
  // for calls that aren't scoped to a single GPIO.
  bool check_(esp_err_t err, const char *what, int pin) {
    if (err == ESP_OK)
      return true;
    if (pin >= 0) {
      ESP_LOGW(TAG, "%s(GPIO%d) failed: %s", what, pin, esp_err_to_name(err));
    } else {
      ESP_LOGW(TAG, "%s failed: %s", what, esp_err_to_name(err));
    }
    return false;
  }

  uint8_t wakeup_pin_{0};
  unsigned setup_errors_{0};
  std::vector<uint8_t> keep_level_pins_;
  std::vector<uint8_t> sleep_hold_high_pins_;
  std::vector<uint8_t> sleep_hold_pullup_pins_;
  uint8_t reset_reason_{0};
  uint8_t wake_cause_{0};
};

}  // namespace esphome::light_sleep
