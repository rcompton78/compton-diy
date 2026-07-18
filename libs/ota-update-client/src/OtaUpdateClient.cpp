#include "OtaUpdateClient.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <string.h>

// Root CA for *.github.io (GitHub Pages, serving both the manifest and the OTA
// binary itself — ISRG Root X1, via Let's Encrypt; confirmed against the live
// cert chain, not assumed). Pinned at the root rather than the leaf/intermediate
// — those rotate every ~90 days to a few years and would silently break this
// feature; this root is long-lived (valid to 2035) and part of every standard
// trust store. If GitHub Pages ever moves to a different issuing CA, checks will
// start failing closed (checkFailed, retried next interval) rather than falling
// back to no verification.
static const char* GITHUB_TRUST_ANCHORS =
"-----BEGIN CERTIFICATE-----\n"
"MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw\n"
"TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
"cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4\n"
"WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu\n"
"ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY\n"
"MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc\n"
"h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+\n"
"0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U\n"
"A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW\n"
"T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH\n"
"B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC\n"
"B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv\n"
"KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn\n"
"OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn\n"
"jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw\n"
"qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI\n"
"rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV\n"
"HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq\n"
"hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL\n"
"ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ\n"
"3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK\n"
"NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5\n"
"ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur\n"
"TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC\n"
"jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc\n"
"oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq\n"
"4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA\n"
"mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d\n"
"emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=\n"
"-----END CERTIFICATE-----\n";

OtaCheckResult OtaUpdateClient::checkForUpdate(const char* manifestUrl, const char* currentVersion, const char* assetName) {
    OtaCheckResult result;

    // "dev" builds have nothing to compare against and shouldn't self-update.
    if (strcmp(currentVersion, "dev") == 0) {
        result.skipped = true;
        return result;
    }

    WiFiClientSecure client;
    client.setCACert(GITHUB_TRUST_ANCHORS);
    HTTPClient http;
    http.begin(client, manifestUrl);
    http.setTimeout(10000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        result.checkFailed = true;
        return result;
    }

    // Filter down to just the fields we need to bound heap use.
    JsonDocument filter;
    filter["version"] = true;
    filter["builds"][0]["ota"]["path"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        result.checkFailed = true;
        return result;
    }

    String version = doc["version"] | "";
    if (version.isEmpty()) {
        result.checkFailed = true;
        return result;
    }
    result.latestVersion = version;

    // This app's manifest always reflects its own latest build (regenerated on
    // every push regardless of what else changed), so every build entry here
    // belongs to this app — just find the one for this board.
    String otaPath;
    for (JsonObject build : doc["builds"].as<JsonArray>()) {
        const char* path = build["ota"]["path"] | "";
        if (strcmp(path, assetName) == 0) {
            otaPath = path;
            break;
        }
    }

    if (otaPath.isEmpty()) {
        // Manifest fetched fine but no matching board entry — treat as a check
        // failure rather than silently reporting "no update available".
        result.checkFailed = true;
        return result;
    }

    // otaPath is relative to the manifest's own directory (both published
    // together under dist/<app>/ by tools/esp-flasher/generate_release.py).
    String base = manifestUrl;
    int lastSlash = base.lastIndexOf('/');
    result.downloadUrl = base.substring(0, lastSlash + 1) + otaPath;

    // Versions are monotonically increasing "YYYY.MM.DD-<run>" strings, so
    // "different from what's running" already means "newer" — no need to parse.
    result.updateAvailable = (version != currentVersion);
    return result;
}

OtaApplyResult OtaUpdateClient::applyUpdate(const String& downloadUrl, void (*onProgress)(size_t, size_t)) {
    WiFiClientSecure client;
    client.setCACert(GITHUB_TRUST_ANCHORS);
    HTTPClient http;
    // GitHub Pages doesn't normally redirect, but following forces us to handle
    // it correctly if that ever changes rather than failing silently.
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.begin(client, downloadUrl);
    http.setTimeout(20000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return OtaApplyResult::DownloadFailed;
    }

    int total = http.getSize();
    if (!Update.begin(total > 0 ? (size_t)total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        http.end();
        return OtaApplyResult::FlashFailed;
    }

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[1024];
    size_t written = 0;
    bool streamError = false;

    while (http.connected() && (total < 0 || written < (size_t)total)) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (!http.connected()) break;
            delay(1);
            continue;
        }
        size_t toRead = avail > sizeof(buf) ? sizeof(buf) : avail;
        size_t n = stream->readBytes(buf, toRead);
        if (n == 0) break;
        if (Update.write(buf, n) != n) {
            streamError = true;
            break;
        }
        written += n;
        if (onProgress) onProgress(written, total > 0 ? (size_t)total : written);
    }
    http.end();

    if (streamError || (total > 0 && written != (size_t)total)) {
        Update.abort();
        return streamError ? OtaApplyResult::FlashFailed : OtaApplyResult::DownloadFailed;
    }

    if (!Update.end(true) || !Update.isFinished()) {
        return OtaApplyResult::FlashFailed;
    }

    return OtaApplyResult::Success;
}
