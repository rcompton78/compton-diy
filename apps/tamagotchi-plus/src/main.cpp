// Tamagotchi+ — base firmware (COM-295) + graphics engine spike (COM-296).
//
// Brings up the display, touch, Wi-Fi provisioning and OTA self-update. The main screen
// is a pixel-art pet scene (see FrameRenderer/PetScene) with the name, version and Wi-Fi
// status drawn on a UI layer above it. Tap the pet to make it happy; hold to swap to the
// "sick" palette. Real pet features land on top of this in later cards.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ImprovWiFiLibrary.h>
#include <esp_ota_ops.h>

#include "Config.h"
#include "Cst816Touch.h"
#include "FrameRenderer.h"
#include "OtaUpdateClient.h"
#include "PaletteIndex.h"
#include "PetScene.h"
#include "generated/sprite_assets.h"

// ── Layout ────────────────────────────────────────────────────────────────────
static constexpr int CX = SCREEN_WIDTH / 2;

// UI layer (physical px). The pet scene fills the whole screen underneath.
static constexpr int TITLE_Y        = 14;
static constexpr int VERSION_Y      = 42;
static constexpr int STATUS_Y       = 58;   // two lines of font 2, 16px apart
static constexpr int STATUS_H       = 34;
static constexpr int HINT_Y         = 262;

static constexpr unsigned long TOUCH_POLL_MS  = 20;
static constexpr unsigned long STATUS_POLL_MS = 500;
static constexpr unsigned long HOLD_MS        = 700;   // press this long to swap palettes
static constexpr int           RELEASE_SAMPLES = 3;    // consecutive no-touch polls (60ms) = real release
static constexpr unsigned long MIN_FRAME_MS   = 16;    // cap redraws at ~60 fps
static constexpr unsigned long STATS_LOG_MS   = 5000;

static constexpr uint16_t C_DIM    = 0x8410;  // mid grey (direct-to-tft message screens)
static constexpr uint16_t C_ACCENT = TFT_CYAN;

// ── Globals ───────────────────────────────────────────────────────────────────
TFT_eSPI        tft;
FrameRenderer   renderer(tft);
PetScene        pet(renderer);
Cst816Touch     touchDriver(I2C_SDA, I2C_SCL, TOUCH_RST, SCREEN_WIDTH, SCREEN_HEIGHT);
WiFiManager     wm;
ImprovWiFi      improvSerial(&Serial);
OtaUpdateClient otaClient;

static bool          rendererReady  = false;
static bool          sceneDirty     = true;
static unsigned long lastFrameAt    = 0;
static unsigned long lastStatsLog   = 0;

static String        statusLine1, statusLine2;   // last-drawn Wi-Fi status
static unsigned long lastStatusPoll = 0;

static bool          pressed        = false;  // debounced: survives single dropped touch reads
static int           untouchedPolls = 0;
static bool          holdHandled    = false;
static unsigned long pressStart     = 0;
static unsigned long lastTouchPoll  = 0;

static bool          appMarkedValid  = false;
static bool          otaCheckedOnce  = false;
static unsigned long lastUpdateCheck = 0;

// ── Drawing ───────────────────────────────────────────────────────────────────
// Everything on the main screen goes through the renderer: text is drawn onto its UI
// layer (colours are locked-palette indices, index 0 = see-through) and composed over the
// pet scene on the next present().
static void drawStatus() {
    if (!rendererReady) return;
    TFT_eSprite& ui = renderer.ui();
    ui.fillRect(0, STATUS_Y, SCREEN_WIDTH, STATUS_H, PAL_TRANSPARENT);
    ui.setTextColor(PAL_WHITE);
    ui.drawCentreString(statusLine1, CX, STATUS_Y, 2);
    ui.setTextColor(PAL_LIGHT_GREY);
    ui.drawCentreString(statusLine2, CX, STATUS_Y + 16, 2);
    renderer.uiChanged();
    sceneDirty = true;
}

static void drawHint() {
    if (!rendererReady) return;
    TFT_eSprite& ui = renderer.ui();
    ui.fillRect(0, HINT_Y, SCREEN_WIDTH, 8, PAL_TRANSPARENT);
    ui.setTextColor(PAL_LIGHT_GREY);
    ui.drawCentreString(pet.isSick() ? "tap: pet  hold: normal" : "tap: pet  hold: sick", CX, HINT_Y, 1);
    renderer.uiChanged();
    sceneDirty = true;
}

static void drawMessage(const char* title, const String& detail);

static void drawMainScreen() {
    if (!rendererReady) {
        // No frame buffer (out of memory at boot): say so on the raw panel instead.
        drawMessage("Display error", "renderer init failed");
        return;
    }
    TFT_eSprite& ui = renderer.ui();
    ui.fillSprite(PAL_TRANSPARENT);
    ui.setTextColor(PAL_BLUE);
    ui.drawCentreString(DEVICE_NAME, CX, TITLE_Y, 4);
    ui.setTextColor(PAL_LIGHT_GREY);
    ui.drawCentreString(String("v") + FIRMWARE_VERSION, CX, VERSION_Y, 2);
    drawStatus();
    drawHint();
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

    rendererReady = renderer.begin();
    if (rendererReady) {
        renderer.setUiPalette(assets::PALETTE_NORMAL);
        pet.begin(millis());
    } else {
        Serial.println("gfx: renderer init failed (out of DMA/PSRAM memory?)");
    }

    touchDriver.begin();

    setupWifi();

    computeWifiStatus(statusLine1, statusLine2);
    drawMainScreen();
}

// Tap = press shorter than HOLD_MS (fires on release so a hold doesn't also count as a
// tap). Hold = fires once, as soon as the press reaches HOLD_MS. The CST816T read can
// drop an occasional sample mid-press (I2C NACK), so a release only counts after
// RELEASE_SAMPLES no-touch polls in a row. Otherwise one glitch would restart the hold
// timer (double palette swap) or end the press as a spurious tap.
static void pollTouch(unsigned long now) {
    TouchPoint p;  // position unused for now: the whole screen is one tap target
    if (touchDriver.read(p)) {
        untouchedPolls = 0;
        if (!pressed) {
            pressed     = true;
            pressStart  = now;
            holdHandled = false;
        } else if (!holdHandled && now - pressStart >= HOLD_MS) {
            holdHandled = true;
            pet.toggleSick();
            drawHint();
        }
    } else if (pressed && ++untouchedPolls >= RELEASE_SAMPLES) {
        pressed = false;
        if (!holdHandled) pet.onTap(now);
    }
}

static void renderFrame(unsigned long now) {
    if (pet.update(now)) sceneDirty = true;
    if (!rendererReady || !sceneDirty || now - lastFrameAt < MIN_FRAME_MS) return;
    lastFrameAt = now;
    sceneDirty  = false;
    pet.draw(now);
    renderer.present();
}

// Rough performance numbers for the COM-296 spike, logged to serial.
static void logStats(unsigned long now) {
    if (!rendererReady || now - lastStatsLog < STATS_LOG_MS) return;
    float secs = (now - lastStatsLog) / 1000.0f;
    lastStatsLog = now;
    const FrameRenderer::Stats& st = renderer.stats();
    if (st.frames) {
        Serial.printf("gfx: %.1f fps (redraw-on-change), frame %.2f ms avg / %.2f ms max, "
                      "compose %.2f ms avg | heap %u free, psram %u free | assets %u B\n",
                      st.frames / secs, st.totalUs / 1000.0f / st.frames, st.maxTotalUs / 1000.0f,
                      st.composeUs / 1000.0f / st.frames, (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(), (unsigned)assets::TOTAL_PIXEL_BYTES);
    }
    renderer.resetStats();
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
        pollTouch(now);
    }

    renderFrame(now);
    logStats(now);
}
