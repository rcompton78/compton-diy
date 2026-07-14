#pragma once
#include <Arduino.h>

struct OtaCheckResult {
    bool    updateAvailable = false;
    bool    checkFailed = false;   // network/parse error, distinct from "no update"
    bool    skipped = false;       // currentVersion was "dev" — no check was made at all
    String  latestVersion;
    String  downloadUrl;
};

enum class OtaApplyResult { Success, DownloadFailed, FlashFailed };

class OtaUpdateClient {
public:
    // assetName is this board's OTA bin filename, e.g. "cyd-clock-cyd-ota.bin".
    // Skips the check entirely (no update available) when currentVersion is "dev".
    OtaCheckResult checkForUpdate(const char* releasesUrl, const char* currentVersion, const char* assetName);

    // Streams downloadUrl into Update.h. onProgress, if given, is called with
    // (bytesWritten, totalBytes) so the caller can drive a UI.
    OtaApplyResult applyUpdate(const String& downloadUrl, void (*onProgress)(size_t, size_t) = nullptr);
};
