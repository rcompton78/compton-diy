// Tamagotchi+ — base firmware scaffold (COM-295).
//
// Brings up the display, touch, Wi-Fi provisioning and OTA self-update, and shows a
// placeholder main screen (name, version, Wi-Fi status, tap feedback). Pet features
// land on top of this in later cards.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ImprovWiFiLibrary.h>
#include <esp_ota_ops.h>

#include "Config.h"
#include "Cst816Touch.h"
#include "OtaUpdateClient.h"

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int CX = SCREEN_WIDTH / 2;

static constexpr int TITLE_Y        = 28;
static constexpr int VERSION_Y      = 58;
static constexpr int STATUS_Y       = 80;   // two lines of font 2, 16px apart
static constexpr int STATUS_H       = 34;
static constexpr int DIVIDER_Y      = 118;
static constexpr int TAP_ZONE_TOP   = 122;
static constexpr int TAP_ZONE_BOT   = 246;
static constexpr int TAP_COUNT_Y    = 252;

static constexpr int RING_RADIUS    = 18;
static constexpr unsigned long RING_VISIBLE_MS = 350;
static constexpr unsigned long TOUCH_POLL_MS   = 20;
static constexpr unsigned long STATUS_POLL_MS  = 500;

static constexpr uint16_t C_DIM    = 0x8410;  // mid grey
static constexpr uint16_t C_ACCENT = TFT_CYAN;

// ── Globals ───────────────────────────────────────────────────────────────────
TFT_eSPI        tft;
Cst816Touch     touchDriver(I2C_SDA, I2C_SCL, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);
WiFiManager     wm;
ImprovWiFi      improvSerial(&Serial);
OtaUpdateClient otaClient;

static String        statusLine1, statusLine2;   // last-drawn Wi-Fi status
static unsigned long lastStatusPoll = 0;

static bool          wasTouched      = false;
static unsigned long lastTouchPoll   = 0;
static bool          ringVisible     = false;
static unsigned long ringShownAt     = 0;
static uint32_t      tapCount        = 0;

static bool          appMarkedValid  = false;
static bool          otaCheckedOnce  = false;
static unsigned long lastUpdateCheck = 0;

// ── Drawing ───────────────────────────────────────────────────────────────────
static void drawTapCount() {
    tft.fillRect(0, TAP_COUNT_Y, SCREEN_WIDTH, 16, TFT_BLACK);
    tft.setTextColor(C_DIM, TFT_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "Taps: %lu", (unsigned long)tapCount);
    tft.drawCentreString(buf, CX, TAP_COUNT_Y, 2);
}

static void drawTapZoneIdle() {
    tft.fillRect(0, TAP_ZONE_TOP, SCREEN_WIDTH, TAP_ZONE_BOT - TAP_ZONE_TOP, TFT_BLACK);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString("Tap anywhere", CX, (TAP_ZONE_TOP + TAP_ZONE_BOT) / 2 - 8, 2);
}

static void drawStatus() {
    tft.fillRect(0, STATUS_Y, SCREEN_WIDTH, STATUS_H, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(statusLine1, CX, STATUS_Y, 2);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString(statusLine2, CX, STATUS_Y + 16, 2);
}

static void drawMainScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_ACCENT, TFT_BLACK);
    tft.drawCentreString(DEVICE_NAME, CX, TITLE_Y, 4);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString(String("v") + FIRMWARE_VERSION, CX, VERSION_Y, 2);
    drawStatus();
    tft.drawFastHLine(20, DIVIDER_Y, SCREEN_WIDTH - 40, 0x2104);
    drawTapZoneIdle();
    drawTapCount();
}

// Full-screen message used before the main screen exists (boot/connecting) and while
// an OTA update is in progress.
static void drawMessage(const char* title, const String& detail) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(C_ACCENT, TFT_BLACK);
    tft.drawCentreString(DEVICE_NAME, CX, 90, 4);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawCentreString(title, CX, 135, 2);
    tft.setTextColor(C_DIM, TFT_BLACK);
    tft.drawCentreString(detail, CX, 155, 2);
}

// Ring + dot at the touch point, clamped so it stays inside the tap zone. Cleared by
// loop() after RING_VISIBLE_MS.
static void drawTapRing(const TouchPoint& p) {
    int x = constrain(p.x, RING_RADIUS + 2, SCREEN_WIDTH - RING_RADIUS - 3);
    int y = constrain(p.y, TAP_ZONE_TOP + RING_RADIUS + 2, TAP_ZONE_BOT - RING_RADIUS - 3);
    tft.fillRect(0, TAP_ZONE_TOP, SCREEN_WIDTH, TAP_ZONE_BOT - TAP_ZONE_TOP, TFT_BLACK);
    tft.drawCircle(x, y, RING_RADIUS, C_ACCENT);
    tft.drawCircle(x, y, RING_RADIUS - 1, C_ACCENT);
    tft.fillCircle(x, y, 4, TFT_WHITE);
}

// ── Wi-Fi ─────────────────────────────────────────────────────────────────────
static void computeWifiStatus(String& line1, String& line2) {
    if (WiFi.status() == WL_CONNECTED) {
        line1 = "Wi-Fi: " + WiFi.SSID();
        line2 = WiFi.localIP().toString();
    } else if (wm.getConfigPortalActive()) {
        line1 = "Wi-Fi setup: join";
        line2 = "\"" WIFI_SETUP_AP_NAME "\"";
    } else if (wm.getWiFiIsSaved()) {
        line1 = "Wi-Fi: connecting...";
        line2 = wm.getWiFiSSID();
    } else {
        line1 = "Wi-Fi: not configured";
        line2 = "";
    }
}

static void pollWifiStatus() {
    String line1, line2;
    computeWifiStatus(line1, line2);
    if (line1 == statusLine1 && line2 == statusLine2) return;
    statusLine1 = line1;
    statusLine2 = line2;
    drawStatus();
}

// WiFiManager's portal-save path leaves WiFi.persistent(false) behind on ESP32 (it
// toggles it on only around its own WiFi.begin), so wrap Improv's connect to make sure
// credentials it provisions are always written to NVS and survive a reboot.
static bool improvConnect(const char* ssid, const char* password) {
    WiFi.persistent(true);
    bool ok = improvSerial.tryConnectToWifi(ssid, password);
    WiFi.persistent(false);
    return ok;
}

static void onImprovConnected(const char* ssid, const char* password) {
    (void)ssid;
    (void)password;
    // Improv got us on the network while the captive portal may still be up — tear it
    // down so the AP disappears and the device settles into plain STA mode.
    if (wm.getConfigPortalActive()) wm.stopConfigPortal();
    WiFi.mode(WIFI_STA);
}

static void setupWifi() {
    WiFi.mode(WIFI_STA);  // init driver so the saved SSID can be read from NVS
    WiFi.setAutoReconnect(true);

    improvSerial.setDeviceInfo(ImprovTypes::ChipFamily::CF_ESP32_S3, DEVICE_NAME,
                               FIRMWARE_VERSION, DEVICE_NAME);
    improvSerial.setCustomConnectWiFi(improvConnect);
    improvSerial.onImprovConnected(onImprovConnected);

    if (wm.getWiFiIsSaved()) {
        drawMessage("Connecting to Wi-Fi", wm.getWiFiSSID());
    } else {
        drawMessage("Starting Wi-Fi setup", WIFI_SETUP_AP_NAME);
    }

    // Non-blocking: autoConnect() tries the saved network (up to the connect timeout)
    // and, if that fails or nothing is saved, opens the captive portal and returns right
    // away. loop() then keeps both the portal (wm.process()) and Improv serial serviced.
    wm.setConfigPortalBlocking(false);
    wm.setConnectTimeout(WIFI_CONNECT_TIMEOUT_S);
    wm.setTitle(DEVICE_NAME);
    wm.setClass("invert");
    const char* menu[] = {"wifi", "info", "sep", "exit"};
    wm.setMenu(menu, sizeof(menu) / sizeof(menu[0]));
    wm.autoConnect(WIFI_SETUP_AP_NAME);
}

// ── OTA ───────────────────────────────────────────────────────────────────────
static void drawOtaProgress(size_t written, size_t total) {
    static int lastPct = -1;
    int pct = total > 0 ? (int)(written * 100 / total) : 0;
    if (pct == lastPct) return;
    lastPct = pct;

    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%d%%", pct);
    tft.fillRect(0, 175, SCREEN_WIDTH, 30, TFT_BLACK);
    tft.setTextColor(C_ACCENT, TFT_BLACK);
    tft.drawCentreString(pctStr, CX, 178, 4);
}

// Checks the manifest and, if a newer build is published, downloads + flashes it and
// reboots. Blocks for the duration of the download (with a progress screen). Unversioned
// "dev" builds are skipped inside checkForUpdate(), so a locally flashed build never
// auto-replaces itself with the published release.
static void checkForUpdate() {
    OtaCheckResult result = otaClient.checkForUpdate(OTA_MANIFEST_URL, FIRMWARE_VERSION, OTA_ASSET_NAME);
    if (result.skipped || result.checkFailed || !result.updateAvailable) return;

    drawMessage("Updating firmware", result.latestVersion + " - do not power off");
    drawOtaProgress(0, 1);
    OtaApplyResult applyResult = otaClient.applyUpdate(result.downloadUrl, drawOtaProgress);
    if (applyResult == OtaApplyResult::Success) {
        drawMessage("Update complete", "Rebooting...");
        delay(500);
        ESP.restart();
    }

    // Download or flash failed — the running firmware is untouched; retry next interval.
    drawMainScreen();
}

// ── Setup / loop ──────────────────────────────────────────────────────────────
void setup() {
    pinMode(SYS_EN_PIN, OUTPUT);
    digitalWrite(SYS_EN_PIN, HIGH);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.begin(115200);

    pinMode(TFT_BACKLIGHT_PIN, OUTPUT);
    digitalWrite(TFT_BACKLIGHT_PIN, HIGH);
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    touchDriver.begin();

    setupWifi();

    computeWifiStatus(statusLine1, statusLine2);
    drawMainScreen();
}

void loop() {
    wm.process();
    improvSerial.handleSerial();

    unsigned long now = millis();

    if (now - lastStatusPoll >= STATUS_POLL_MS) {
        lastStatusPoll = now;
        pollWifiStatus();
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Getting onto the network is the bar for "this build boots fine" — confirm it
        // before the first OTA check can overwrite the other slot. Deliberately gated on
        // Wi-Fi rather than local display/touch init: a pending-verify image only ever
        // arrives via OTA (so the device was online before), and an update that breaks
        // Wi-Fi should roll back, since a device that can't get online can never receive
        // the fix OTA. (No-op today: PlatformIO's prebuilt Arduino core doesn't enable
        // CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE, but this keeps the intent right.)
        if (!appMarkedValid) {
            appMarkedValid = true;
            esp_ota_mark_app_valid_cancel_rollback();
        }
        if (!otaCheckedOnce || now - lastUpdateCheck >= UPDATE_CHECK_INTERVAL_MS) {
            otaCheckedOnce  = true;
            lastUpdateCheck = now;
            checkForUpdate();  // reboots if an update was applied
            now = millis();    // the check/download blocks, so refresh the timer snapshot
        }
    }

    if (now - lastTouchPoll >= TOUCH_POLL_MS) {
        lastTouchPoll = now;
        TouchPoint p;
        bool touched = touchDriver.read(p);
        if (touched && !wasTouched) {  // count the press edge, not every polled frame
            tapCount++;
            drawTapRing(p);
            drawTapCount();
            ringVisible = true;
            ringShownAt = now;
        }
        wasTouched = touched;
    }

    if (ringVisible && now - ringShownAt >= RING_VISIBLE_MS) {
        ringVisible = false;
        drawTapZoneIdle();
    }
}
