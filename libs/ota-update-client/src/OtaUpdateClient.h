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
    // manifestUrl is this app's dist/<app>/manifest.json on GitHub Pages (always
    // fresh for this app regardless of what else changed in the latest GitHub
    // Release — see OtaUpdateClient.cpp for why that distinction matters).
    // assetName is this board's OTA bin filename, e.g. "cyd-clock-cyd-ota.bin",
    // matched against the manifest's builds[].ota.path.
    // Skips the check entirely (no update available) when currentVersion is "dev".
    OtaCheckResult checkForUpdate(const char* manifestUrl, const char* currentVersion, const char* assetName);

    // Streams downloadUrl into Update.h. onProgress, if given, is called with
    // (bytesWritten, totalBytes) so the caller can drive a UI.
    OtaApplyResult applyUpdate(const String& downloadUrl, void (*onProgress)(size_t, size_t) = nullptr);
};
