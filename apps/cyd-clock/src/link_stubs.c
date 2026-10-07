// Linker-wrapped replacements for two large error-string tables (COM-386). platformio.ini
// passes -Wl,--wrap=mbedtls_strerror and -Wl,--wrap=esp_err_to_name, so every call to
// either (from WiFiClientSecure, esp_err.c's ESP_ERROR_CHECK abort path, etc.) lands here
// instead of pulling in mbedtls' error.c (~15.1KB) and esp-idf's esp_err_to_name table
// (~7.5KB). Both still report the numeric code, so logs and panics stay diagnosable
// (look the code up in mbedtls/error.h or esp_err.h).
#include <stdio.h>
#include <stddef.h>
#include "esp_err.h"

void __wrap_mbedtls_strerror(int ret, char *buf, size_t buflen) {
    if (buflen) snprintf(buf, buflen, "mbedtls error -0x%04X", (unsigned)(ret < 0 ? -ret : ret));
}

// Thread-local so concurrent callers (prebuilt esp-idf ESP_LOGE runs on several tasks) each
// get their own buffer instead of overwriting each other's text.
const char *__wrap_esp_err_to_name(esp_err_t code) {
    static __thread char buf[24];
    snprintf(buf, sizeof(buf), "ESP_ERR 0x%x", (unsigned)code);
    return buf;
}
