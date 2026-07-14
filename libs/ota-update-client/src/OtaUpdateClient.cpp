#include "OtaUpdateClient.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <string.h>

// Root CAs for api.github.com (USERTrust ECC, via Sectigo) and the release-asset
// redirect target objects.githubusercontent.com (ISRG Root X1, via Let's Encrypt).
// Pinned at the root rather than the leaf/intermediate — those rotate every ~90
// days to a few years and would silently break this feature; these roots are
// long-lived (valid to 2035/2038) and part of every standard trust store. If
// GitHub ever moves to a different issuing CA, checks will start failing closed
// (checkFailed, retried next interval) rather than falling back to no verification.
static const char* GITHUB_TRUST_ANCHORS =
"-----BEGIN CERTIFICATE-----\n"
"MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL\n"
"MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl\n"
"eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT\n"
"JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx\n"
"MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT\n"
"Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg\n"
"VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm\n"
"aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo\n"
"I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng\n"
"o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G\n"
"A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD\n"
"VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB\n"
"zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW\n"
"RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=\n"
"-----END CERTIFICATE-----\n"
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

OtaCheckResult OtaUpdateClient::checkForUpdate(const char* releasesUrl, const char* currentVersion, const char* assetName) {
    OtaCheckResult result;

    // "dev" builds have nothing to compare against and shouldn't self-update.
    if (strcmp(currentVersion, "dev") == 0) {
        result.skipped = true;
        return result;
    }

    WiFiClientSecure client;
    client.setCACert(GITHUB_TRUST_ANCHORS);
    HTTPClient http;
    http.begin(client, releasesUrl);
    // GitHub's API rejects requests with no User-Agent.
    http.addHeader("User-Agent", "cyd-clock-ota");
    http.addHeader("Accept", "application/vnd.github+json");
    http.setTimeout(10000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        result.checkFailed = true;
        return result;
    }

    // The releases/latest payload (release notes body, all assets' metadata)
    // can be tens of KB — filter down to just the fields we need to bound heap use.
    JsonDocument filter;
    filter["tag_name"] = true;
    filter["assets"][0]["name"] = true;
    filter["assets"][0]["browser_download_url"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        result.checkFailed = true;
        return result;
    }

    String tag = doc["tag_name"] | "";
    if (tag.isEmpty()) {
        result.checkFailed = true;
        return result;
    }
    result.latestVersion = tag;

    for (JsonObject asset : doc["assets"].as<JsonArray>()) {
        const char* name = asset["name"] | "";
        if (strcmp(name, assetName) == 0) {
            result.downloadUrl = asset["browser_download_url"] | "";
            break;
        }
    }

    if (result.downloadUrl.isEmpty()) {
        // Tag exists but no matching asset — treat as a check failure rather
        // than silently reporting "no update available".
        result.checkFailed = true;
        return result;
    }

    // Versions are monotonically increasing "YYYY.MM.DD-<run>" strings, so
    // "different from what's running" already means "newer" — no need to parse.
    result.updateAvailable = (tag != currentVersion);
    return result;
}

OtaApplyResult OtaUpdateClient::applyUpdate(const String& downloadUrl, void (*onProgress)(size_t, size_t)) {
    WiFiClientSecure client;
    client.setCACert(GITHUB_TRUST_ANCHORS);
    HTTPClient http;
    // Release asset URLs redirect from api.github.com to objects.githubusercontent.com.
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
