// Tamagotchi+ — base firmware (COM-295) + graphics engine spike (COM-296).
//
// Brings up the display, touch, Wi-Fi provisioning and OTA self-update. The main screen
// is a pixel-art pet scene (see FrameRenderer/PetScene) with the name, version, battery/USB
// status (COM-298) and Wi-Fi status drawn on a UI layer above it. Tap the pet to make it
// happy; hold to swap to the "sick" palette. Real pet features land on top of this in
// later cards.

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ImprovWiFiLibrary.h>
#include <esp_ota_ops.h>
#include <Preferences.h>

#include "Battery.h"
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
static constexpr int VERSION_Y      = 42;   // version + power status share this line
static constexpr int VERSION_H      = 16;
static constexpr int STATUS_Y       = 58;   // two lines of font 2, 16px apart
static constexpr int STATUS_H       = 34;
static constexpr int HINT_Y         = 262;

static constexpr unsigned long TOUCH_POLL_MS  = 20;
static constexpr unsigned long STATUS_POLL_MS = 500;
static constexpr unsigned long HOLD_MS        = EGG_RESET_HOLD_MS;  // hold this long to reset the egg
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
Battery         battery;

static bool          rendererReady  = false;
static bool          sceneDirty     = true;
static unsigned long lastFrameAt    = 0;
static unsigned long lastStatsLog   = 0;

static String        statusLine1, statusLine2;   // last-drawn Wi-Fi status
static String        powerText;                  // last-drawn battery/USB status
static unsigned long lastStatusPoll = 0;

static bool          pressed        = false;  // debounced: survives single dropped touch reads
static int           untouchedPolls = 0;
static bool          holdHandled    = false;
static unsigned long pressStart     = 0;
static unsigned long lastTouchPoll  = 0;

// Hatch state, persisted so the egg keeps its age across reboots (see Config.h).
Preferences          eggPrefs;
static unsigned long eggElapsedMs = 0;   // powered-on time so far, NOT wall clock
static unsigned long eggTargetMs  = 0;   // rolled once, when the egg is created
static bool          eggHatched   = false;
static unsigned long lastEggSave  = 0;
static unsigned long lastEggTick  = 0;

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

static void drawVersionLine() {
    if (!rendererReady) return;
    TFT_eSprite& ui = renderer.ui();
    ui.fillRect(0, VERSION_Y, SCREEN_WIDTH, VERSION_H, PAL_TRANSPARENT);
    ui.setTextColor(PAL_LIGHT_GREY);
    ui.drawCentreString(String("v") + FIRMWARE_VERSION + "   " + powerText, CX, VERSION_Y, 2);
    renderer.uiChanged();
    sceneDirty = true;
}

static void drawHint() {
    if (!rendererReady) return;
    TFT_eSprite& ui = renderer.ui();
    ui.fillRect(0, HINT_Y, SCREEN_WIDTH, 8, PAL_TRANSPARENT);
    ui.setTextColor(PAL_LIGHT_GREY);
    ui.drawCentreString(pet.state() == PetScene::State::Egg ? "tap: rock egg   hold 5s: reset"
                                                             : "tap: heart   hold 5s: reset egg",
                        CX, HINT_Y, 1);
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
    drawVersionLine();
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

// ── Power ─────────────────────────────────────────────────────────────────────
// "USB" = a USB host (computer/hub) is connected; a plain wall charger isn't detectable
// on this board (see Battery.h). The shown % is rate-limited, so it ramps rather than jumps
// when the cable goes in or out.
static String computePowerText() {
    String s = battery.usbConnected() ? "USB  " : "";
    return s + battery.percent() + "%";
}

static void pollPowerStatus(unsigned long now) {
    battery.update(now);
    String text = computePowerText();
    if (text == powerText) return;
    powerText = text;
    drawVersionLine();
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

// ── Hatch timer ───────────────────────────────────────────────────────────────
// Counts powered-on time only: the elapsed total is accumulated while running and flushed
// to NVS periodically, so time spent powered off never advances the egg.
static unsigned long rollHatchTarget() {
#if HATCH_TEST_MODE
    return HATCH_TEST_MS;
#else
    return HATCH_MIN_MS + (esp_random() % (HATCH_MAX_MS - HATCH_MIN_MS));
#endif
}

static void saveEggState() {
    eggPrefs.putULong("elapsed", eggElapsedMs);
    eggPrefs.putULong("target", eggTargetMs);
    eggPrefs.putBool("hatched", eggHatched);
}

static void loadEggState() {
    eggPrefs.begin("tamagotchi", false);
    eggElapsedMs = eggPrefs.getULong("elapsed", 0);
    eggTargetMs  = eggPrefs.getULong("target", 0);
    eggHatched   = eggPrefs.getBool("hatched", false);
    // A fresh device (or one whose target predates a test-mode change) rolls a new window.
    if (eggTargetMs == 0) {
        eggTargetMs  = rollHatchTarget();
        eggElapsedMs = 0;
        saveEggState();
    }
#if HATCH_TEST_MODE
    // In test mode the constant always wins, so editing HATCH_TEST_MS takes effect on the
    // next boot instead of waiting for a reset to roll a fresh target.
    if (eggTargetMs != HATCH_TEST_MS) {
        eggTargetMs = HATCH_TEST_MS;
        if (eggElapsedMs > eggTargetMs) eggElapsedMs = 0;
        saveEggState();
    }
#endif
    Serial.printf("egg: %s, %lu/%lu ms elapsed (powered-on)\n",
                  eggHatched ? "hatched" : "incubating", eggElapsedMs, eggTargetMs);
}

static void resetEgg(unsigned long now) {
    eggElapsedMs = 0;
    eggTargetMs  = rollHatchTarget();
    eggHatched   = false;
    saveEggState();
    lastEggTick = now;
    lastEggSave = now;
    pet.resetToEgg(now);
    sceneDirty = true;
    drawHint();
    Serial.printf("egg: reset, new target %lu ms\n", eggTargetMs);
}

static void tickHatch(unsigned long now) {
    unsigned long delta = now - lastEggTick;
    lastEggTick = now;
    if (eggHatched) return;

    if (pet.state() == PetScene::State::Egg) {
        eggElapsedMs += delta;
        pet.setEggProgress((float)eggElapsedMs / (float)eggTargetMs, now);
        if (eggElapsedMs >= eggTargetMs) {
            Serial.println("egg: hatching");
            pet.startHatch(now);
            drawHint();
        }
        if (now - lastEggSave >= HATCH_SAVE_MS) {
            lastEggSave = now;
            saveEggState();
        }
    } else if (pet.hatchDone()) {
        eggHatched = true;       // once hatched, always hatched
        saveEggState();
        drawHint();
        Serial.println("egg: hatched");
    }
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
        loadEggState();
        lastEggTick = lastEggSave = millis();
        pet.begin(millis(), eggHatched);
    } else {
        Serial.println("gfx: renderer init failed (out of DMA/PSRAM memory?)");
    }

    touchDriver.begin();
    battery.begin();

    setupWifi();

    computeWifiStatus(statusLine1, statusLine2);
    powerText = computePowerText();
    drawMainScreen();
}

// Tap = press shorter than HOLD_MS (fires on release so a hold doesn't also count as a
// tap). Hold = fires once, as soon as the press reaches HOLD_MS, and resets the egg. The CST816T read can
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
            resetEgg(now);
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

// Rough performance numbers for the COM-296 spike plus the raw power readings (handy for
// checking the battery curve against a multimeter), logged to serial.
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
    Serial.printf("power: %.3f V, target %.1f%%, shown %d%%, usb host %s\n", battery.volts(),
                  battery.targetPercent(), battery.percent(), battery.usbConnected() ? "yes" : "no");
    renderer.resetStats();
}

void loop() {
    wm.process();
    improvSerial.handleSerial();

    unsigned long now = millis();

    if (now - lastStatusPoll >= STATUS_POLL_MS) {
        lastStatusPoll = now;
        pollWifiStatus();
        pollPowerStatus(now);
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

    tickHatch(now);

    if (now - lastTouchPoll >= TOUCH_POLL_MS) {
        lastTouchPoll = now;
        pollTouch(now);
    }

    renderFrame(now);
    logStats(now);
}
