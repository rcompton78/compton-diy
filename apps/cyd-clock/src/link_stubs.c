// Linker-wrapped replacements for two large error-string tables (COM-386). platformio.ini
// passes -Wl,--wrap=mbedtls_strerror and -Wl,--wrap=esp_err_to_name, so every call to
// either (from WiFiClientSecure, esp_err.c's ESP_ERROR_CHECK abort path, etc.) lands here
// instead of pulling in mbedtls' error.c (~15.1KB) and esp-idf's esp_err_to_name table
// (~7.5KB). mbedtls errors report the numeric code (look it up in mbedtls/error.h), and
// esp_err_to_name keeps the generic names.
#include <stdio.h>
#include <stddef.h>
#include "esp_err.h"

void __wrap_mbedtls_strerror(int ret, char *buf, size_t buflen) {
    if (buflen) snprintf(buf, buflen, "mbedtls error -0x%04X", (unsigned)(ret < 0 ? -ret : ret));
}

// Returns string literals only, keeping the original's static-lifetime, reentrant contract:
// callers may hold the pointer, log two codes in one call, or call it from any task, early
// boot or an ISR. Only the generic codes (0x101-0x10C) keep their names. Everything else
// reads like the original's own fallback, with the subsystem range for WiFi and flash.
// Most esp-idf call sites (e.g. ESP_ERROR_CHECK's abort message) also print the hex code.
const char *__wrap_esp_err_to_name(esp_err_t code) {
    static const char *const GENERIC[] = {
        "ESP_ERR_NO_MEM", "ESP_ERR_INVALID_ARG", "ESP_ERR_INVALID_STATE",
        "ESP_ERR_INVALID_SIZE", "ESP_ERR_NOT_FOUND", "ESP_ERR_NOT_SUPPORTED",
        "ESP_ERR_TIMEOUT", "ESP_ERR_INVALID_RESPONSE", "ESP_ERR_INVALID_CRC",
        "ESP_ERR_INVALID_VERSION", "ESP_ERR_INVALID_MAC", "ESP_ERR_NOT_FINISHED",
    };
    if (code == ESP_OK) return "ESP_OK";
    if (code == ESP_FAIL) return "ESP_FAIL";
    if (code >= ESP_ERR_NO_MEM && code < ESP_ERR_NO_MEM + (esp_err_t)(sizeof(GENERIC) / sizeof(GENERIC[0])))
        return GENERIC[code - ESP_ERR_NO_MEM];
    if (code >= ESP_ERR_WIFI_BASE && code < ESP_ERR_WIFI_BASE + 0x1000) return "ESP_ERR_WIFI (see code)";
    if (code >= ESP_ERR_FLASH_BASE && code < ESP_ERR_FLASH_BASE + 0x1000) return "ESP_ERR_FLASH (see code)";
    return "UNKNOWN ERROR";
}
