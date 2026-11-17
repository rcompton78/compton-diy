const assert = require("assert/strict");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawn } = require("child_process");

const root = path.resolve(__dirname, "..");
const appSource = fs.readFileSync(path.join(root, "docs/public/webserver/app.js"), "utf8");
const product = JSON.parse(fs.readFileSync(path.join(root, "product/espframe.json"), "utf8"));
const expectedBackupGroups = product.project.backup_export_groups;
const expectedBackupFields = product.project.backup_export_fields;
const configurationKeyByEndpointName = {};
for (const setting of product.settings) {
  configurationKeyByEndpointName[setting.entity.name] = setting.key;
}
for (const collectionName of ["web_static_entities", "web_manual_entities"]) {
  for (const [key, spec] of Object.entries(product.project[collectionName] || {})) {
    const entity = String(spec.entity || "");
    const slash = entity.indexOf("/");
    const domain = slash === -1 ? "" : entity.slice(0, slash);
    if (["number", "select", "switch", "text"].includes(domain)) {
      configurationKeyByEndpointName[entity.slice(slash + 1)] = key;
    }
  }
}
const smokeAlbumIds = [
  "11111111-1111-4111-8111-111111111111",
  "44444444-4444-4444-8444-444444444444",
];
const smokeAlbumLabels = ["Family", "Travel"];

function findExecutable(name) {
  const pathDirs = String(process.env.PATH || "")
    .split(path.delimiter)
    .filter(Boolean);
  for (const dir of pathDirs) {
    const candidate = path.join(dir, name);
    if (fs.existsSync(candidate)) return candidate;
  }
  return "";
}

function resolveChromePath() {
  const candidates = [
    process.env.CHROME_BIN,
    process.env.CHROME_PATH,
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    findExecutable("google-chrome"),
    findExecutable("google-chrome-stable"),
    findExecutable("chromium"),
    findExecutable("chromium-browser"),
  ];
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || "";
}

const chromePathCandidates = [
  process.env.CHROME_BIN,
  process.env.CHROME_PATH,
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/Applications/Chromium.app/Contents/MacOS/Chromium",
  "/usr/bin/google-chrome",
  "/usr/bin/google-chrome-stable",
  "/usr/bin/chromium",
  "/usr/bin/chromium-browser",
  "google-chrome",
  "google-chrome-stable",
  "chromium",
  "chromium-browser",
].filter(Boolean);
const chromePath = resolveChromePath();

function requireChromePath() {
  if (!chromePath) {
    throw new Error(`Google Chrome or Chromium is required for browser smoke tests. Checked: ${chromePathCandidates.join(", ")}`);
  }
  return chromePath;
}

function chromeSandboxArgs() {
  if (process.platform !== "linux") return [];
  if (typeof process.getuid !== "function" || process.getuid() !== 0) return [];
  return ["--no-sandbox"];
}

const validBackupFixture = {
  version: 1,
  connection: {
    immich_url: "https://imported.photos.example.com",
    api_key: "imported-api-key",
  },
  photos: {
    source: "Person",
    album_ids: "11111111-1111-4111-8111-111111111111",
    album_labels: "Family",
    person_ids: "22222222-2222-4222-8222-222222222222",
    person_labels: "Alex",
    tag_ids: "33333333-3333-4333-8333-333333333333",
    tag_labels: "Espframe",
    date_filter_enabled: true,
    date_filter_mode: "Relative Range",
    relative_amount: 3,
    relative_unit: "Years",
    orientation: "Portrait Only",
    portrait_pairing: true,
    display_mode: "Fill",
  },
  frequency: {
    interval: "30 seconds",
    conn_timeout: "5 minutes",
  },
  firmware_updates: {
    auto_update: true,
    update_frequency: "Weekly",
    manifest_url: "https://firmware.example.com/manifest.json",
  },
  clock: {
    show: true,
    format: "24 Hour",
    timezone: "Europe/London (GMT+0)",
    ntp_servers: ["0.pool.ntp.org", "1.pool.ntp.org", "2.pool.ntp.org"],
  },
  screen: {
    brightness_day: 90,
    brightness_night: 60,
    schedule_enabled: true,
    schedule_on_hour: 7,
    schedule_off_hour: 22,
    schedule_wake_timeout: 120,
    base_tone_enabled: true,
    base_tone: 35,
    warm_tones_enabled: true,
    warm_tone_intensity: 45,
    warm_tone_override: false,
    rotation: "180",
  },
};

const rejectedBackupFixture = {
  version: 1,
  photos: {
    album_ids: "not-an-album-uuid",
  },
};

const partialBackupFixture = {
  version: 1,
  connection: {
    immich_url: "https://partial-import.photos.example.com",
  },
  photos: {
    album_ids: "not-an-album-uuid",
  },
};

const missingVersionBackupFixture = {
  photos: {
    source: "Album",
  },
};

const futureVersionBackupFixture = {
  version: 2,
  connection: {
    immich_url: "https://future.photos.example.com",
  },
};

const unsupportedVersionBackupFixture = {
  version: 0,
  connection: {
    immich_url: "https://unsupported.photos.example.com",
  },
};

const scenarios = [
  { name: "wizard", configured: false, width: 1280, height: 900 },
  { name: "wizard-connection-save", configured: false, width: 1280, height: 900 },
  { name: "settings", configured: true, width: 1280, height: 900 },
  { name: "settings-mobile", configured: true, width: 390, height: 900 },
  {
    name: "firmware-up-to-date",
    configured: true,
    width: 1280,
    height: 900,
    firmwareUpdate: { value: "v1.0.0", state: "NO UPDATE", current_version: "v1.0.0" },
  },
  { name: "photo-source-reorder", configured: true, width: 1280, height: 900 },
  { name: "screen-rotation-portrait", configured: true, width: 1280, height: 900, query: "dev=experimental" },
  { name: "screen-tone-schedule", configured: true, width: 1280, height: 900 },
  { name: "daily-settings-controls", configured: true, width: 1280, height: 900 },
  { name: "backup-import-success", configured: true, width: 1280, height: 900, importFixture: validBackupFixture },
  { name: "backup-import-save-failure", configured: true, width: 1280, height: 900, importFixture: validBackupFixture, failedPostEndpoint: "Screen: Daytime Brightness" },
  { name: "backup-import-partial", configured: true, width: 1280, height: 900, importFixture: partialBackupFixture },
  { name: "backup-import-rejected", configured: true, width: 1280, height: 900, importFixture: rejectedBackupFixture },
  { name: "backup-import-missing-version", configured: true, width: 1280, height: 900, importFixture: missingVersionBackupFixture },
  { name: "backup-import-future-version", configured: true, width: 1280, height: 900, importFixture: futureVersionBackupFixture },
  { name: "backup-import-unsupported-version", configured: true, width: 1280, height: 900, importFixture: unsupportedVersionBackupFixture },
];

function browserScriptForScenario(scenario) {
  return `
    window.__smoke = {
      posts: [],
      postRecords: [],
      errors: [],
      downloads: 0,
      exportPayloads: [],
      inputClicks: 0,
      importFixture: ${JSON.stringify(scenario.importFixture || null)},
      failedPostEndpoint: ${JSON.stringify(scenario.failedPostEndpoint || "")},
      // ESPHome 2026.x update entity JSON: the latest version is "value";
      // there is no latest_version field.
      firmwareUpdate: ${JSON.stringify(scenario.firmwareUpdate || { value: "v1.0.1", state: "UPDATE AVAILABLE", current_version: "v1.0.0" })}
    };
    window.addEventListener("error", function (event) {
      window.__smoke.errors.push(event.message || "browser error");
    });
    window.addEventListener("unhandledrejection", function (event) {
      window.__smoke.errors.push(String(event.reason || "unhandled rejection"));
    });

    const NativeBlob = window.Blob;
    window.Blob = class SmokeBlob extends NativeBlob {
      constructor(parts, options) {
        super(parts, options);
        this.__smokeText = (parts || []).map((part) => typeof part === "string" ? part : "").join("");
      }
    };

    URL.createObjectURL = function (blob) {
      if (blob && typeof blob.__smokeText === "string") window.__smoke.exportPayloads.push(blob.__smokeText);
      return "blob:espframe-smoke";
    };
    URL.revokeObjectURL = function () {};
    HTMLAnchorElement.prototype.click = function () {
      if (this.download) window.__smoke.downloads += 1;
    };
    FileReader.prototype.readAsText = function (file) {
      // FileReader.prototype.result is a getter with no setter, so a plain
      // assignment is silently ignored and the app would read null.
      Object.defineProperty(this, "result", {
        configurable: true,
        value: file && file.__smokeContent ? file.__smokeContent : ""
      });
      if (this.onload) setTimeout(() => this.onload({ target: this }), 0);
    };
    HTMLInputElement.prototype.click = function () {
      if (this.type !== "file") return;
      window.__smoke.inputClicks += 1;
      if (!window.__smoke.importFixture) return;
      Object.defineProperty(this, "files", {
        configurable: true,
        value: [{ name: "espframe-config-smoke.json", __smokeContent: JSON.stringify(window.__smoke.importFixture) }]
      });
      setTimeout(() => this.dispatchEvent(new Event("change")), 0);
    };

    class SmokeEventSource {
      constructor(url) {
        this.url = url;
        this.listeners = {};
        window.__smoke.eventSource = this;
        setTimeout(() => {
          if (this.onopen) this.onopen({ type: "open" });
          this.dispatch("log", { msg: "Smoke log line", lvl: 3 });
          // ESPHome 2026.x event shape: "id" is the object id; "name_id" is
          // the "domain/Name" form the page keys on. The configuration API
          // snapshot has no version, so this event is where the page gets it.
          this.dispatch("state", {
            name_id: "text_sensor/Firmware: Version", id: "text_sensor-firmware__version",
            domain: "text_sensor", value: "v1.0.0", state: "v1.0.0"
          });
          this.dispatch("state", { id: "text_sensor/Screen: Sunrise", value: "06:30" });
          this.dispatch("state", { id: "text_sensor/Screen: Sunset", value: "21:45" });
        }, 25);
      }
      addEventListener(type, listener) {
        if (!this.listeners[type]) this.listeners[type] = [];
        this.listeners[type].push(listener);
      }
      dispatch(type, data) {
        (this.listeners[type] || []).forEach((listener) => {
          listener({ data: JSON.stringify(data) });
        });
      }
      close() {}
    }
    window.EventSource = SmokeEventSource;

    const configured = ${JSON.stringify(scenario.configured)};
    const endpointValues = {
      "Connection: Server URL": configured ? "https://photos.example.com" : "",
      "Connection: API Key": configured ? "fixture-api-key" : "",
      "Firmware: Version": "v1.0.0",
      "Photos: Source": "Album",
      "Photos: Album IDs": ${JSON.stringify(smokeAlbumIds.join(","))},
      "Photos: Album Labels": ${JSON.stringify(smokeAlbumLabels.join(","))},
      "Photos: Person IDs": "22222222-2222-4222-8222-222222222222",
      "Photos: Person Labels": "Alex",
      "Photos: Tag IDs": "33333333-3333-4333-8333-333333333333",
      "Photos: Tag Labels": "Espframe",
      "Photos: Date Filter": true,
      "Photos: Date Filter Mode": "Fixed Range",
      "Photos: Date From": "2024-01-01",
      "Photos: Date To": "2026-06-07",
      "Photos: Relative Amount": 2,
      "Photos: Relative Unit": "Years",
      "Photos: Orientation": "Any",
      "Photos: Portrait Pairing": true,
      "Photos: Display Mode": "Fill",
      "Photos: Slideshow Interval": "15 seconds",
      "Device: Metadata Date": true,
      "Device: Metadata Location": true,
      "Device: Metadata Date Format": "Date Taken",
      "Device: Metadata Date Taken Format": "1 January, 2026",
      "Screen: Connection Timeout": "10 minutes",
      "Clock: Show": true,
      "Clock: Format": "24 Hour",
      "Clock: Timezone": "Europe/London (GMT+0)",
      "Clock: NTP Server 1": "0.pool.ntp.org",
      "Clock: NTP Server 2": "1.pool.ntp.org",
      "Clock: NTP Server 3": "2.pool.ntp.org",
      "Firmware: Auto Update": true,
      "Firmware: Update Frequency": "Daily",
      "Firmware: Manifest URL": "",
      "Screen: Daytime Brightness": 100,
      "Screen: Nighttime Brightness": 75,
      "Screen: Schedule Enabled": false,
      "Screen: Schedule On Hour": 6,
      "Screen: Schedule Off Hour": 23,
      "Screen: Schedule Wake Timeout": 60,
      "Screen: Tone Adjustment": false,
      "Screen: Display Tone": 0,
      "Screen: Night Tone Adjustment": false,
      "Screen: Warm Tone Intensity": 50,
      "Screen: Warm Tone Override": false,
      "Screen: Rotation": "0",
      "Developer: Features": false
    };
    const configurationKeyByEndpointName = ${JSON.stringify(configurationKeyByEndpointName)};
    const configurationEndpointNameByKey = Object.fromEntries(
      Object.entries(configurationKeyByEndpointName).map(([name, key]) => [key, name])
    );

    function configurationSnapshotValues() {
      const values = {};
      Object.entries(configurationEndpointNameByKey).forEach(([key, name]) => {
        if (Object.prototype.hasOwnProperty.call(endpointValues, name)) values[key] = endpointValues[name];
      });
      return values;
    }

    function endpointNameForUrl(decoded) {
      return Object.keys(endpointValues)
        .sort((left, right) => right.length - left.length)
        .find((name) => decoded.indexOf(name) !== -1) || "";
    }

    function requestParam(decoded, body, name) {
      const queryIndex = decoded.indexOf("?");
      const query = queryIndex === -1 ? "" : decoded.slice(queryIndex + 1);
      const params = new URLSearchParams(body || query);
      return params.get(name);
    }

    function updateEndpointValueFromPost(decoded, body) {
      const endpointName = endpointNameForUrl(decoded);
      if (!endpointName) return;
      if (decoded.indexOf("/turn_on") !== -1) {
        endpointValues[endpointName] = true;
      } else if (decoded.indexOf("/turn_off") !== -1) {
        endpointValues[endpointName] = false;
      } else if (decoded.indexOf("/set") !== -1) {
        const option = requestParam(decoded, body, "option");
        const value = requestParam(decoded, body, "value");
        if (option !== null) endpointValues[endpointName] = option;
        else if (value !== null) endpointValues[endpointName] = value;
      }
    }

    window.fetch = function (url, options) {
      const method = options && options.method ? options.method : "GET";
      const decoded = decodeURIComponent(String(url));
      const body = options && options.body != null ? String(options.body) : "";
      if (decoded === "/espframe/api/v1/configuration") {
        if (method === "GET") {
          return Promise.resolve({
            ok: true,
            status: 200,
            json: () => Promise.resolve({ api_version: 1, values: configurationSnapshotValues(), unavailable: [] })
          });
        }
        if (window.__smoke.configurationUpdateInFlight) {
          return Promise.resolve({
            ok: false,
            status: 409,
            json: () => Promise.resolve({ api_version: 1, status: "rejected", error: "update_in_progress" })
          });
        }
        window.__smoke.configurationUpdateInFlight = true;
        const encoded = new URLSearchParams(body).get("configuration");
        const update = encoded ? JSON.parse(encoded) : { values: {} };
        const failedKey = configurationKeyByEndpointName[window.__smoke.failedPostEndpoint];
        if (failedKey && Object.prototype.hasOwnProperty.call(update.values, failedKey)) {
          window.__smoke.posts.push(decoded);
          window.__smoke.postRecords.push({ url: decoded, body });
          return new Promise((resolve) => setTimeout(() => {
            window.__smoke.configurationUpdateInFlight = false;
            resolve({
              ok: false,
              status: 422,
              json: () => Promise.resolve({ api_version: 1, status: "rejected", error: "smoke_failure", field: failedKey })
            });
          }, 0));
        }
        window.__smoke.posts.push(decoded);
        window.__smoke.postRecords.push({ url: decoded, body });
        return new Promise((resolve) => setTimeout(() => {
          Object.entries(update.values || {}).forEach(([key, value]) => {
            const endpointName = configurationEndpointNameByKey[key];
            if (endpointName) endpointValues[endpointName] = value;
          });
          window.__smoke.configurationUpdateInFlight = false;
          resolve({
            ok: true,
            status: 202,
            json: () => Promise.resolve({ api_version: 1, status: "accepted", updated: Object.keys(update.values || {}).length })
          });
        }, 0));
      }
      if (method === "POST") {
        window.__smoke.posts.push(decoded);
        window.__smoke.postRecords.push({ url: decoded, body });
        if (window.__smoke.failedPostEndpoint && decoded.indexOf(window.__smoke.failedPostEndpoint) !== -1) {
          return Promise.resolve({ ok: false, status: 500, json: () => Promise.resolve({}) });
        }
        updateEndpointValueFromPost(decoded, body);
      }
      if (decoded.indexOf("Firmware: Update") !== -1 && window.__smoke.firmwareUpdateFails) {
        return Promise.resolve({ ok: false, status: 500, json: () => Promise.resolve({}) });
      }
      if (decoded.indexOf("Firmware: Update") !== -1) {
        return Promise.resolve({
          ok: true,
          status: 200,
          json: () => Promise.resolve(Object.assign({
            name_id: "update/Firmware: Update", id: "update-firmware__update", domain: "update"
          }, window.__smoke.firmwareUpdate))
        });
      }
      const endpointName = endpointNameForUrl(decoded);
      const value = endpointName ? endpointValues[endpointName] : "";
      const state = value === true ? "ON" : value === false ? "OFF" : String(value);
      return Promise.resolve({
        ok: true,
        status: 200,
        json: () => Promise.resolve({ value, state, option: [] })
      });
    };
  `;
}

function smokeAssertionsForScenario(scenario) {
  return `
    (async function () {
      // Save checks inside waitFor swallow their errors while polling; the
      // last one is reported if the wait times out.
      let lastPendingError = null;
      function waitFor(check, timeoutMs, label) {
        const started = Date.now();
        return new Promise((resolve, reject) => {
          function poll() {
            lastPendingError = null;
            try {
              if (check()) return resolve();
            } catch (error) {
              return reject(error);
            }
            if (Date.now() - started > timeoutMs) return reject(new Error("Timed out waiting for " + label + (lastPendingError ? ": " + lastPendingError.message : "")));
            setTimeout(poll, 50);
          }
          poll();
        });
      }
      function pageText() {
        return document.body.innerText || "";
      }
      function requireText(text) {
        if (pageText().indexOf(text) === -1) throw new Error("Missing text: " + text);
      }
      function buttonByText(text) {
        return Array.from(document.querySelectorAll("button")).find((button) => button.textContent.trim() === text);
      }
      function clickButton(text) {
        const button = buttonByText(text);
        if (!button) throw new Error("Button not found: " + text);
        button.click();
        return button;
      }
      function clickTab(text) {
        const tab = Array.from(document.querySelectorAll(".sp-tab")).find((item) => item.textContent.trim() === text);
        if (!tab) throw new Error("Tab not found: " + text);
        tab.click();
        return tab;
      }
      function requirePostContains(label, fragment, extraFragment) {
        const configured = latestConfigurationValue(fragment);
        if (configured.found) {
          let matches = true;
          if (extraFragment === "turn_on") matches = configured.value === true;
          else if (extraFragment === "turn_off") matches = configured.value === false;
          else if (extraFragment && extraFragment.indexOf("option=") === 0) matches = String(configured.value) === extraFragment.slice(7);
          else if (extraFragment && extraFragment.indexOf("value=") === 0) matches = String(configured.value) === extraFragment.slice(6);
          if (matches) return;
        }
        const found = window.__smoke.posts.some((url) =>
          url.indexOf(fragment) !== -1 && (!extraFragment || url.indexOf(extraFragment) !== -1)
        );
        if (!found) throw new Error(label + " was not posted to the device");
      }
      function configurationUpdates() {
        return window.__smoke.postRecords.map((record) => {
          if (record.url !== "/espframe/api/v1/configuration") return null;
          const encoded = new URLSearchParams(record.body).get("configuration");
          return encoded ? JSON.parse(encoded).values : null;
        }).filter(Boolean);
      }
      function latestConfigurationValue(fragment) {
        // Prefer an exact name: "Device: Metadata Date" is also a prefix of
        // "Device: Metadata Date Format" and "Device: Metadata Date Taken Format".
        const endpointNames = Object.keys(configurationKeyByEndpointName);
        const endpoint = endpointNames.find((name) => name === fragment) ||
          endpointNames.find((name) => name.indexOf(fragment) !== -1);
        const key = endpoint && configurationKeyByEndpointName[endpoint];
        if (!key) return { found: false };
        const updates = configurationUpdates();
        for (let i = updates.length - 1; i >= 0; i--) {
          if (Object.prototype.hasOwnProperty.call(updates[i], key)) return { found: true, value: updates[i][key] };
        }
        return { found: false };
      }
      function hasConfigurationPost(fragment) {
        return latestConfigurationValue(fragment).found;
      }
      function latestPostRecord(fragment) {
        for (let i = window.__smoke.postRecords.length - 1; i >= 0; i--) {
          const record = window.__smoke.postRecords[i];
          if (record.url.indexOf(fragment) !== -1) return record;
        }
        throw new Error("POST record not found: " + fragment);
      }
      function postRecordParam(record, name) {
        const queryIndex = record.url.indexOf("?");
        const query = queryIndex === -1 ? "" : record.url.slice(queryIndex + 1);
        const params = new URLSearchParams(record.body || query);
        return params.get(name);
      }
      function requireLatestPostValue(label, fragment, expected) {
        const configured = latestConfigurationValue(fragment);
        const actual = configured.found ? String(configured.value) : postRecordParam(latestPostRecord(fragment), "value");
        if (actual !== expected) {
          throw new Error(label + " saved " + JSON.stringify(actual) + " instead of " + JSON.stringify(expected));
        }
      }
      function requireLatestPostParam(label, fragment, param, expected) {
        const configured = latestConfigurationValue(fragment);
        const actual = configured.found ? String(configured.value) : postRecordParam(latestPostRecord(fragment), param);
        if (actual !== expected) {
          throw new Error(label + " saved " + JSON.stringify(actual) + " instead of " + JSON.stringify(expected));
        }
      }
      function requireExportShape() {
        if (!window.__smoke.exportPayloads.length) throw new Error("Export payload was not captured");
        const exported = JSON.parse(window.__smoke.exportPayloads[0]);
        if (exported.version !== ${JSON.stringify(product.project.backup_config_version)}) {
          throw new Error("Exported backup version changed");
        }
        if (!exported.exported_at || typeof exported.exported_at !== "string") {
          throw new Error("Exported backup timestamp missing");
        }
        const expectedGroups = ${JSON.stringify(expectedBackupGroups)};
        const expectedFields = ${JSON.stringify(expectedBackupFields)};
        const actualGroups = Object.keys(exported).filter((key) => key !== "version" && key !== "exported_at");
        if (JSON.stringify(actualGroups) !== JSON.stringify(expectedGroups)) {
          throw new Error("Exported backup groups changed: " + JSON.stringify(actualGroups));
        }
        expectedGroups.forEach((group) => {
          const actualFields = Object.keys(exported[group] || {});
          const expectedGroupFields = expectedFields[group] || [];
          if (JSON.stringify(actualFields) !== JSON.stringify(expectedGroupFields)) {
            throw new Error("Exported backup fields changed for " + group + ": " + JSON.stringify(actualFields));
          }
        });
        if (!Array.isArray(exported.clock.ntp_servers) || exported.clock.ntp_servers.length !== 3) {
          throw new Error("Exported NTP servers must remain a three-item array");
        }
        if (exported.screen.schedule_wake_timeout !== 60) {
          throw new Error("Exported schedule wake timeout was not normalized");
        }
      }
      function selectByLabel(labelText) {
        const labels = Array.from(document.querySelectorAll("label"));
        const label = labels.find((item) => item.textContent.trim() === labelText);
        if (!label || !label.parentElement) throw new Error("Field not found: " + labelText);
        const select = label.parentElement.querySelector("select");
        if (!select) throw new Error("Select not found for field: " + labelText);
        return select;
      }
      function setSelect(labelText, value) {
        const select = selectByLabel(labelText);
        select.value = value;
        select.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function requireSelectIncludes(labelText, values) {
        const select = selectByLabel(labelText);
        values.forEach((value) => {
          if (!Array.from(select.options).some((option) => option.value === value)) {
            throw new Error(labelText + " is missing option: " + value);
          }
        });
      }
      function fieldByLabel(labelText) {
        const labels = Array.from(document.querySelectorAll("label"));
        const label = labels.find((item) => item.textContent.trim() === labelText);
        if (!label || !label.parentElement) throw new Error("Field not found: " + labelText);
        return label.parentElement;
      }
      function cardByTitle(title) {
        const card = Array.from(document.querySelectorAll(".card")).find((item) => {
          const heading = item.querySelector("h3");
          return heading && heading.textContent.trim() === title;
        });
        if (!card) throw new Error("Card not found: " + title);
        return card;
      }
      function expandCard(title) {
        const card = cardByTitle(title);
        if (card.classList.contains("collapsed")) {
          const header = card.querySelector(".card-header");
          if (!header) throw new Error("Card header not found: " + title);
          header.click();
        }
        return card;
      }
      function setRangeByLabel(labelText, value) {
        const inputEl = fieldByLabel(labelText).querySelector('input[type="range"]');
        if (!inputEl) throw new Error("Range input not found for field: " + labelText);
        inputEl.value = String(value);
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function setCardRange(cardTitle, index, value) {
        const inputs = Array.from(expandCard(cardTitle).querySelectorAll('input[type="range"]'));
        const inputEl = inputs[index];
        if (!inputEl) throw new Error("Range input " + index + " not found in card: " + cardTitle);
        inputEl.value = String(value);
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function inputByLabel(labelText) {
        const inputEl = fieldByLabel(labelText).querySelector("input");
        if (!inputEl) throw new Error("Input not found for field: " + labelText);
        return inputEl;
      }
      function setInputByLabel(labelText, value) {
        const inputEl = inputByLabel(labelText);
        inputEl.value = value;
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function inputByAriaLabel(labelText) {
        const inputEl = document.querySelector('input[aria-label="' + labelText + '"]');
        if (!inputEl) throw new Error("Input not found: " + labelText);
        return inputEl;
      }
      function setInputByAriaLabel(labelText, value) {
        const inputEl = inputByAriaLabel(labelText);
        inputEl.value = value;
        inputEl.dispatchEvent(new Event("input", { bubbles: true }));
        inputEl.dispatchEvent(new Event("change", { bubbles: true }));
      }
      function toggleByText(text) {
        const row = Array.from(document.querySelectorAll(".toggle-row")).find((item) =>
          Array.from(item.querySelectorAll("span")).some((span) => span.textContent.trim() === text)
        );
        if (!row) throw new Error("Toggle not found: " + text);
        const toggle = row.querySelector(".toggle");
        if (!toggle) throw new Error("Toggle control not found: " + text);
        return toggle;
      }
      function photoRows(labelText) {
        return Array.from(fieldByLabel(labelText).querySelectorAll(".photo-id-row"));
      }
      function photoRowValues(labelText) {
        return photoRows(labelText).map((row) =>
          Array.from(row.querySelectorAll("input")).map((inputEl) => inputEl.value)
        );
      }
      function requirePhotoSourceModes() {
        const sourceSelect = selectByLabel("Source");
        ["All Photos", "Favorites", "Album", "Person", "Tag", "Memories"].forEach((mode) => {
          if (!Array.from(sourceSelect.options).some((option) => option.value === mode)) {
            throw new Error("Missing photo source mode: " + mode);
          }
          setSelect("Source", mode);
        });
        setSelect("Source", "Album");
        requireText("Add an album");
        setSelect("Source", "Person");
        requireText("Add a person");
        setSelect("Source", "Tag");
        requireText("Add a tag");
      }
      async function requireAlbumReorderSave() {
        const startingIds = ${JSON.stringify(smokeAlbumIds)};
        const startingLabels = ${JSON.stringify(smokeAlbumLabels)};
        const expectedIds = startingIds.slice().reverse().join(",");
        const expectedLabels = JSON.stringify(startingLabels.slice().reverse());

        setSelect("Source", "Album");
        await waitFor(() => photoRows("Albums").length === 2, 3000, "album rows");

        const before = photoRowValues("Albums");
        if (JSON.stringify(before.map((row) => row[0])) !== JSON.stringify(startingIds)) {
          throw new Error("Unexpected starting album ID order: " + JSON.stringify(before));
        }
        if (JSON.stringify(before.map((row) => row[1])) !== JSON.stringify(startingLabels)) {
          throw new Error("Unexpected starting album label order: " + JSON.stringify(before));
        }

        const moveUp = photoRows("Albums")[1].querySelector('[aria-label="Move album up"]');
        if (!moveUp || moveUp.disabled) throw new Error("Second album row cannot move up");
        moveUp.click();

        await waitFor(() => {
          const values = photoRowValues("Albums");
          return values[0] && values[0][0] === startingIds[1] && values[0][1] === startingLabels[1];
        }, 3000, "album row visual reorder");

        await waitFor(() => {
          try {
            requireLatestPostValue("Album IDs", "Photos: Album IDs", expectedIds);
            requireLatestPostValue("Album labels", "Photos: Album Labels", expectedLabels);
            return window.__smoke.posts.some((url) => url.indexOf("Apply Photo Source") !== -1);
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "album reorder save");
      }
      async function requireScreenRotationPortraitFlow() {
        clickTab("Device");
        await waitFor(() => pageText().indexOf("Rotation") !== -1, 8000, "device settings");
        expandCard("Rotation");
        expandCard("Developer");
        requireText("Screen Brightness");
        requireText("Developer");
        requireText("Enable in-development features");

        // Portrait is a normal option (COM-362): all four rotations show
        // without Developer Features.
        requireSelectIncludes("Rotation", ["0", "90", "180", "270"]);

        setSelect("Rotation", "90");
        await waitFor(() => {
          try {
            requireLatestPostParam("Portrait rotation", "Screen: Rotation", "option", "90");
            requirePostContains("Portrait pairing off", "Photos: Portrait Pairing", "turn_off");
            return true;
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "portrait rotation save");

        // Toggling Developer Features must not reset a portrait rotation.
        toggleByText("Enable in-development features").click();
        await waitFor(() => {
          try {
            requirePostContains("Developer features enable", "Developer: Features", "turn_on");
            return true;
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "developer features enable");
        toggleByText("Enable in-development features").click();
        await waitFor(() => {
          try {
            requirePostContains("Developer features disable", "Developer: Features", "turn_off");
            requireLatestPostParam("Rotation kept", "Screen: Rotation", "option", "90");
            requireSelectIncludes("Rotation", ["0", "90", "180", "270"]);
            return true;
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "portrait rotation kept");
      }
      async function requireScreenToneScheduleControls() {
        clickTab("Device");
        await waitFor(() => pageText().indexOf("Night Schedule") !== -1, 8000, "device screen controls");
        expandCard("Screen Brightness");
        expandCard("Screen Tone");
        expandCard("Night Schedule");
        requireText("Screen Brightness");
        requireText("Screen Tone");
        requireText("Night Schedule");
        requireText("Screen Tone Adjustment");
        requireText("Night Tone Adjustment");
        requireText("Schedule Screen Off");

        expandCard("Screen Brightness");
        setRangeByLabel("Daytime Brightness", 85);
        setRangeByLabel("Nighttime Brightness", 55);

        expandCard("Screen Tone");
        toggleByText("Screen Tone Adjustment").click();
        setCardRange("Screen Tone", 0, 25);
        toggleByText("Night Tone Adjustment").click();
        setCardRange("Screen Tone", 1, 65);
        toggleByText("Turn on until sunrise").click();

        expandCard("Night Schedule");
        toggleByText("Schedule Screen Off").click();
        setSelect("On Time", "7");
        setSelect("Off Time", "21");
        setSelect("When Woken, Idle Time To Screen Off", "120");

        await waitFor(() => {
          try {
            requireLatestPostValue("Daytime brightness", "Screen: Daytime Brightness", "85");
            requireLatestPostValue("Nighttime brightness", "Screen: Nighttime Brightness", "55");
            requirePostContains("Base tone toggle", "Screen: Tone Adjustment", "turn_on");
            requireLatestPostValue("Base tone", "Screen: Display Tone", "25");
            requirePostContains("Night tone toggle", "Screen: Night Tone Adjustment", "turn_on");
            requireLatestPostValue("Night tone intensity", "Screen: Warm Tone Intensity", "65");
            requirePostContains("Warm tone override", "Screen: Warm Tone Override", "turn_on");
            requirePostContains("Schedule toggle", "Screen: Schedule Enabled", "turn_on");
            requireLatestPostValue("Schedule on hour", "Screen: Schedule On Hour", "7");
            requireLatestPostValue("Schedule off hour", "Screen: Schedule Off Hour", "21");
            requireLatestPostValue("Schedule wake timeout", "Screen: Schedule Wake Timeout", "120");
            return true;
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "screen tone and schedule saves");
      }
      async function requireDailySettingsControls() {
        expandCard("Connection");
        expandCard("Frequency");
        expandCard("Layout");
        expandCard("Metadata");

        requireText("Connection Timeout");
        requireText("Slideshow Interval");
        requireText("Portrait Pairing");
        requireText("Photo Orientation");
        requireText("Display Mode");
        requireText("Metadata");

        setSelect("Connection Timeout", "5 minutes");
        setSelect("Slideshow Interval", "30 seconds");
        toggleByText("Portrait Pairing").click();
        setSelect("Photo Orientation", "Landscape Only");
        setSelect("Display Mode", "Fit");
        setSelect("Date Taken Format", "January 1, 2026");
        setSelect("Date Format", "Relative Date");
        toggleByText("Location").click();
        toggleByText("Date").click();

        clickTab("Device");
        await waitFor(() => pageText().indexOf("Clock") !== -1, 8000, "clock settings");
        expandCard("Clock");
        requireText("Show Clock");
        requireText("NTP Servers");

        toggleByText("Show Clock").click();
        setSelect("Format", "12 Hour");
        setSelect("Timezone", "UTC (GMT+0)");
        setInputByAriaLabel("NTP Server 1", "time1.example.com");
        setInputByAriaLabel("NTP Server 2", "time2.example.com");
        setInputByAriaLabel("NTP Server 3", "time3.example.com");

        await waitFor(() => {
          try {
            requireLatestPostParam("Connection timeout", "Screen: Connection Timeout", "option", "5 minutes");
            requireLatestPostParam("Slideshow interval", "Photos: Slideshow Interval", "option", "30 seconds");
            requirePostContains("Portrait pairing", "Photos: Portrait Pairing", "turn_off");
            requireLatestPostParam("Photo orientation", "Photos: Orientation", "option", "Landscape Only");
            requireLatestPostParam("Display mode", "Photos: Display Mode", "option", "Fit");
            requireLatestPostParam("Metadata date taken format", "Device: Metadata Date Taken Format", "option", "January 1, 2026");
            requireLatestPostParam("Metadata date format", "Device: Metadata Date Format", "option", "Relative Date");
            requirePostContains("Metadata location toggle", "Device: Metadata Location", "turn_off");
            requirePostContains("Metadata date toggle", "Device: Metadata Date", "turn_off");
            requirePostContains("Show clock toggle", "Clock: Show", "turn_off");
            requireLatestPostParam("Clock format", "Clock: Format", "option", "12 Hour");
            requireLatestPostParam("Timezone", "Clock: Timezone", "option", "UTC (GMT+0)");
            requireLatestPostValue("NTP server 1", "Clock: NTP Server 1", "time1.example.com");
            requireLatestPostValue("NTP server 2", "Clock: NTP Server 2", "time2.example.com");
            requireLatestPostValue("NTP server 3", "Clock: NTP Server 3", "time3.example.com");
            return true;
          } catch (error) {
            lastPendingError = error;
            return false;
          }
        }, 8000, "daily settings saves");
      }
      async function requireWizardConnectionSave() {
        await waitFor(() => pageText().indexOf("connect your photo frame") !== -1, 8000, "wizard connection step");
        requireText("Immich Server URL");
        requireText("API Key");

        setInputByLabel("Immich Server URL", "setup.photos.example.com/");
        setInputByLabel("API Key", "setup-api-key");
        clickButton("Connect");

        await waitFor(() => pageText().indexOf("Clock & timezone") !== -1, 8000, "wizard clock step");
        requireLatestPostValue("Wizard server URL", "Connection: Server URL", "https://setup.photos.example.com");
        requireLatestPostValue("Wizard API key", "Connection: API Key", "setup-api-key");

        clickButton("Done");
        await waitFor(() => pageText().indexOf("Photo Source") !== -1, 8000, "settings after wizard");
      }

      function firmwareCardText() {
        const card = Array.from(document.querySelectorAll(".fw-body"))
          .find((body) => body.textContent.indexOf("Check for Update") !== -1);
        return card ? card.innerText : "";
      }
      function installedLabelText() {
        const label = Array.from(document.querySelectorAll(".fw-body .fw-label"))
          .find((item) => item.firstChild && item.firstChild.textContent === "Installed");
        return label ? label.textContent : "";
      }
      async function requireInstalledVersion(version) {
        const expected = "Installed " + version;
        try {
          await waitFor(() => installedLabelText() === expected, 2000, expected);
        } catch (_) {
          throw new Error("Expected " + JSON.stringify(expected) + ", card shows " + JSON.stringify(installedLabelText()));
        }
      }
      function firmwareInstallButton() {
        return Array.from(document.querySelectorAll(".fw-updates button"))
          .find((button) => button.textContent.trim().indexOf("Install") === 0);
      }
      // A failed OTA puts the update entity back to UPDATE AVAILABLE; the
      // Firmware card must re-enable Install and say the install failed.
      async function requireFailedInstallRecovers() {
        // ESPHome 2026.x event shape: "id" is the object id; "name_id" is
        // the "domain/Name" form the page keys on.
        const events = window.__smoke.eventSource;
        const updateEvent = (state) => ({
          name_id: "update/Firmware: Update", id: "update-firmware__update", domain: "update",
          value: "v1.0.1", state
        });
        events.dispatch("state", Object.assign(updateEvent("UPDATE AVAILABLE"), { current_version: "v1.0.0" }));
        const installButton = firmwareInstallButton();
        if (!installButton) throw new Error("Firmware Install button not found");
        installButton.click();
        const installingButton = firmwareInstallButton();
        if (!installingButton || !installingButton.disabled || installingButton.textContent.indexOf("Installing") === -1) {
          throw new Error("Install button did not show Installing");
        }
        if (!window.__smoke.posts.some((url) => url.indexOf("Firmware: Update/install") !== -1)) {
          throw new Error("Install did not post to the update entity");
        }
        // An update event without a state (cached state still UPDATE
        // AVAILABLE) must not count as a failed install.
        events.dispatch("state", {
          name_id: "update/Firmware: Update", id: "update-firmware__update", domain: "update", value: "v1.0.1"
        });
        if (pageText().indexOf("Install failed") !== -1) throw new Error("Stateless update event reported as a failure");
        events.dispatch("state", updateEvent("INSTALLING"));
        if (pageText().indexOf("Install failed") !== -1) throw new Error("INSTALLING reported as a failure");
        events.dispatch("state", updateEvent("UPDATE AVAILABLE"));
        await waitFor(() => pageText().indexOf("Install failed") !== -1, 2000, "install failure message");
        const retryButton = firmwareInstallButton();
        if (!retryButton || retryButton.disabled || retryButton.textContent.trim() !== "Install") {
          throw new Error("Firmware Install button not re-enabled after a failed install");
        }
        requireText("v1.0.1");

        // A successful install reboots the frame, which then reports its new
        // version; the card must show it and drop the pending install.
        retryButton.click();
        events.dispatch("state", {
          name_id: "text_sensor/Firmware: Version", id: "text_sensor-firmware__version",
          domain: "text_sensor", value: "v1.0.1", state: "v1.0.1"
        });
        events.dispatch("state", {
          name_id: "update/Firmware: Update", id: "update-firmware__update", domain: "update",
          value: "", state: "UNKNOWN", current_version: "", title: ""
        });
        await waitFor(() => {
          const updates = document.querySelector(".fw-updates");
          const card = updates && updates.closest(".fw-body");
          return card && /Installed\\s*v1\\.0\\.1/.test(card.innerText) && card.innerText.indexOf("Installing") === -1;
        }, 2000, "installed version after a successful install");

        // An install this page didn't start (auto-update, Home Assistant, or a
        // reload mid-install) must still disable Install.
        events.dispatch("state", {
          name_id: "update/Firmware: Update", id: "update-firmware__update", domain: "update",
          value: "v1.0.2", state: "UPDATE AVAILABLE", current_version: "v1.0.1"
        });
        // A newer release while the state stays UPDATE AVAILABLE must update
        // the "Stable" version shown.
        events.dispatch("state", Object.assign(updateEvent("UPDATE AVAILABLE"), { value: "v1.0.3" }));
        await waitFor(() => {
          const row = document.querySelector(".fw-updates");
          return row && row.textContent.indexOf("v1.0.3") !== -1;
        }, 2000, "newer stable version shown while still UPDATE AVAILABLE");
        events.dispatch("state", Object.assign(updateEvent("INSTALLING"), { value: "v1.0.3" }));
        await waitFor(() => {
          const button = firmwareInstallButton();
          return button && button.disabled && button.textContent.indexOf("Installing") !== -1;
        }, 2000, "Install disabled during an install started elsewhere");
      }

      // The installed version matches the latest, so Check for Update must
      // say so and offer no Install; after an update reboots the frame onto
      // a new version, the card must show it.
      async function requireFirmwareUpToDate() {
        clickTab("Device");
        expandCard("Firmware");
        await requireInstalledVersion("v1.0.0");
        const checkButton = clickButton("Check for Update");
        await waitFor(() => checkButton.textContent.trim() === "Check for Update", 7000, "firmware check");
        if (firmwareCardText().indexOf("Up to date") === -1) throw new Error("Firmware card did not say Up to date");
        if (firmwareInstallButton()) throw new Error("Install offered for the version already installed");
        if (document.querySelector(".fw-updates").textContent.indexOf("Stable") !== -1) {
          throw new Error("Stable row shown for the version already installed");
        }
        window.__smoke.eventSource.dispatch("state", {
          name_id: "text_sensor/Firmware: Version", id: "text_sensor-firmware__version",
          domain: "text_sensor", value: "v1.0.1", state: "v1.0.1"
        });
        await requireInstalledVersion("v1.0.1");

        // When the frame doesn't answer the update entity request, the card
        // must not claim the firmware is up to date.
        window.__smoke.firmwareUpdateFails = true;
        clickButton("Check for Update");
        await waitFor(() => buttonByText("Check for Update") && !buttonByText("Check for Update").disabled, 7000, "failed firmware check");
        const checkText = firmwareCardText();
        if (checkText.indexOf("Up to date") !== -1) throw new Error("Failed update check reported Up to date");
        if (checkText.indexOf("Couldn't check") === -1) throw new Error("Failed update check not reported");
      }

      try {
        if (${JSON.stringify(scenario.name)} === "wizard") {
          await waitFor(() => pageText().indexOf("connect your photo frame") !== -1, 8000, "wizard");
          requireText("Immich Server URL");
          requireText("API Key");
          clickTab("Device");
          requireText("Import Settings");
        } else if (${JSON.stringify(scenario.name)} === "wizard-connection-save") {
          await requireWizardConnectionSave();
        } else {
          await waitFor(() => pageText().indexOf("Photo Source") !== -1, 8000, "settings");
          // Settings cards start collapsed, and innerText skips the bodies of
          // collapsed cards and inactive tabs, so open what each check reads.
          clickTab("Device");
          expandCard("Firmware");
          requireText("Installed");
          requireText("Auto updates");
          clickTab("Immich");
          expandCard("Connection");
          requireText("Immich Server URL");
          expandCard("Advanced Filters");
          requireText("Filter by Date");
          const filterModes = Array.from(fieldByLabel("Mode").querySelectorAll("button")).map((item) => item.textContent.trim());
          if (JSON.stringify(filterModes) !== JSON.stringify(["Fixed", "Relative"])) {
            throw new Error("Unexpected date filter modes: " + JSON.stringify(filterModes));
          }
          expandCard("Photo Source");
          requireSelectIncludes("Source", ["All Photos", "Favorites", "Album", "Person", "Tag", "Memories"]);

          if (${JSON.stringify(scenario.name)} === "settings" || ${JSON.stringify(scenario.name)} === "settings-mobile") {
            requirePhotoSourceModes();
            clickTab("Device");
            expandCard("Firmware");
            clickButton("Export");
            clickButton("Import");
            if (window.__smoke.downloads !== 1) throw new Error("Export did not trigger a download");
            requireExportShape();
            if (window.__smoke.inputClicks !== 1) throw new Error("Import did not open the file picker");
            await requireInstalledVersion("v1.0.0");
            const checkButton = clickButton("Check for Update");
            await waitFor(() => checkButton.textContent.trim() === "Check for Update", 7000, "firmware check");
            requireText("Stable");
            requireText("v1.0.1");
            await requireFailedInstallRecovers();
            const logsTab = Array.from(document.querySelectorAll(".sp-tab")).find((tab) => tab.textContent.trim() === "Logs");
            if (!logsTab) throw new Error("Logs tab not found");
            logsTab.click();
            requireText("Clear");
            await waitFor(() => pageText().indexOf("Smoke log line") !== -1, 8000, "log line");
            if (${JSON.stringify(scenario.name)} === "settings-mobile") {
              if (document.documentElement.scrollWidth > window.innerWidth + 4) {
                throw new Error("Mobile viewport has horizontal overflow");
              }
            }
          }

          if (${JSON.stringify(scenario.name)} === "firmware-up-to-date") {
            await requireFirmwareUpToDate();
          }

          if (${JSON.stringify(scenario.name)} === "photo-source-reorder") {
            await requireAlbumReorderSave();
          }

          if (${JSON.stringify(scenario.name)} === "screen-rotation-portrait") {
            await requireScreenRotationPortraitFlow();
          }

          if (${JSON.stringify(scenario.name)} === "screen-tone-schedule") {
            await requireScreenToneScheduleControls();
          }

          if (${JSON.stringify(scenario.name)} === "daily-settings-controls") {
            await requireDailySettingsControls();
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-success") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Settings imported successfully") !== -1, 8000, "successful import");
            if (!hasConfigurationPost("Connection: Server URL")) {
              throw new Error("Import did not post connection URL");
            }
            requirePostContains("Import text field", "Connection: Server URL");
            requirePostContains("Import switch field", "Firmware: Auto Update", "turn_on");
            requirePostContains("Import select field", "Photos: Source", "option=Person");
            requirePostContains("Import number field", "Screen: Daytime Brightness", "value=90");
            requirePostContains("Import aggregate NTP field", "Clock: NTP Server 1");
            requirePostContains("Import URL field", "Firmware: Manifest URL");
            requirePostContains("Import normalized schedule setting", "Screen: Schedule Wake Timeout", "value=120");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-save-failure") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Imported with 1 failed setting") !== -1, 8000, "failed import save");
            requirePostContains("Failed import still attempted daytime brightness", "Screen: Daytime Brightness", "value=90");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-partial") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Imported with 1 skipped setting") !== -1, 8000, "partial import");
            requirePostContains("Partial import text field", "Connection: Server URL");
            if (hasConfigurationPost("Photos: Album IDs")) {
              throw new Error("Skipped album IDs were posted to the device");
            }
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-rejected") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Import skipped 1 setting") !== -1, 8000, "rejected import");
            if (hasConfigurationPost("Photos: Album IDs")) {
              throw new Error("Rejected album IDs were posted to the device");
            }
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-missing-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Invalid config file - missing version") !== -1, 8000, "missing version rejection");
            if (window.__smoke.posts.length) throw new Error("Missing-version backup wrote settings to the device");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-future-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Unsupported backup version 2 - this device supports version 1") !== -1, 8000, "future version rejection");
            if (window.__smoke.posts.length) throw new Error("Future-version backup wrote settings to the device");
          }

          if (${JSON.stringify(scenario.name)} === "backup-import-unsupported-version") {
            clickButton("Import");
            await waitFor(() => pageText().indexOf("Unsupported backup version 0") !== -1, 8000, "unsupported version rejection");
            if (window.__smoke.posts.length) throw new Error("Unsupported-version backup wrote settings to the device");
          }
        }
        if (window.__smoke.errors.length) throw new Error(window.__smoke.errors.join("; "));
        document.documentElement.setAttribute("data-smoke-${scenario.name}", "pass");
      } catch (error) {
        document.documentElement.setAttribute("data-smoke-${scenario.name}", "fail");
        const pre = document.createElement("pre");
        pre.id = "smoke-error-${scenario.name}";
        pre.textContent = error && error.stack ? error.stack : String(error);
        document.body.appendChild(pre);
      }
    })();
  `;
}

function htmlForScenario(scenario) {
  const escapedAppSource = appSource.replace(/<\/script/gi, "<\\/script");
  return `<!doctype html>
<html>
<head><meta charset="utf-8"><title>Espframe web smoke ${scenario.name}</title></head>
<body><esp-app></esp-app>
<script>${browserScriptForScenario(scenario)}</script>
<script>${escapedAppSource}</script>
<script>${smokeAssertionsForScenario(scenario)}</script>
</body>
</html>`;
}

function runChrome(args, timeoutMs) {
  return new Promise((resolve) => {
    const useProcessGroup = process.platform !== "win32";
    const child = spawn(requireChromePath(), args, {
      detached: useProcessGroup,
      stdio: ["ignore", "pipe", "pipe"],
    });
    let stdout = "";
    let stderr = "";
    let settled = false;
    let timer = null;
    let forceResolveTimer = null;
    let timedOut = false;

    child.stdout.setEncoding("utf8");
    child.stderr.setEncoding("utf8");
    child.stdout.on("data", (chunk) => {
      stdout += chunk;
    });
    child.stderr.on("data", (chunk) => {
      stderr += chunk;
    });

    function finish(result) {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      clearTimeout(forceResolveTimer);
      resolve(result);
    }

    timer = setTimeout(() => {
      timedOut = true;
      stderr += `\nChrome timed out after ${timeoutMs}ms`;
      if (useProcessGroup) {
        try {
          process.kill(-child.pid, "SIGKILL");
        } catch (_) {
          // Fall back to killing the browser wrapper if the process group is already gone.
          child.kill("SIGKILL");
        }
      } else {
        child.kill("SIGKILL");
      }
      forceResolveTimer = setTimeout(() => {
        finish({ status: null, signal: "timeout", stdout, stderr, timedOut });
      }, 1000);
    }, timeoutMs);

    child.on("close", (status, signal) => {
      finish({ status, signal, stdout, stderr, timedOut });
    });
  });
}

async function runScenario(scenario) {
  console.log(`running web browser smoke scenario: ${scenario.name}`);
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "espframe-web-smoke-"));
  const htmlPath = path.join(tempDir, `${scenario.name}.html`);
  const userDataDir = path.join(tempDir, "chrome-profile");
  const query = scenario.query ? `?${scenario.query}` : "";
  fs.writeFileSync(htmlPath, htmlForScenario(scenario));
  const result = await runChrome(
    [
      "--headless=new",
      "--disable-gpu",
      "--disable-background-networking",
      "--disable-component-extensions-with-background-pages",
      "--disable-default-apps",
      "--disable-extensions",
      "--no-first-run",
      "--no-default-browser-check",
      "--no-service-autorun",
      ...chromeSandboxArgs(),
      `--user-data-dir=${userDataDir}`,
      `--window-size=${scenario.width},${scenario.height}`,
      "--virtual-time-budget=16000",
      "--dump-dom",
      `file://${htmlPath}${query}`,
    ],
    30000
  );

  const outcome = scenarioOutcome(scenario, result.stdout || "");
  if (outcome.status === "pass") return;
  // A failure the scenario recorded explains more than how Chrome exited.
  if (outcome.status === "fail") {
    assert.fail(`Browser smoke scenario ${scenario.name} failed: ${outcome.error || "no error captured"}`);
  }
  assert.equal(result.timedOut, false, `Chrome timed out for ${scenario.name}:\n${result.stderr || ""}`);
  assert.equal(result.status, 0, `Chrome failed for ${scenario.name}:\n${result.stderr || ""}`);
  assert.fail(`Browser smoke scenario ${scenario.name} failed: scenario never finished`);
}

function decodeHtmlText(text) {
  return text
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&#39;/g, "'")
    .replace(/&amp;/g, "&");
}

// Reads the scenario's outcome from Chrome's --dump-dom output. Only the
// attribute on the serialized <html> start tag counts: the inline scenario
// scripts are part of the dumped DOM too, so any token that also appears in
// their source text would match whether or not the scenario passed.
function scenarioOutcome(scenario, dom) {
  const htmlTag = /<html\b[^>]*>/i.exec(dom);
  const attr = htmlTag && new RegExp(`\\sdata-smoke-${scenario.name}="(pass|fail)"`).exec(htmlTag[0]);
  const errorMatch = new RegExp(`<pre id="smoke-error-${scenario.name}">([\\s\\S]*?)</pre>`).exec(dom);
  return {
    status: attr ? attr[1] : "missing",
    error: errorMatch ? decodeHtmlText(errorMatch[1]) : "",
  };
}

function selectedScenariosFromArgs(args) {
  const selectedNames = [];
  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i];
    if (arg === "--list") {
      scenarios.forEach((scenario) => console.log(scenario.name));
      return [];
    }
    if (arg === "--scenario") {
      const value = args[i + 1];
      if (!value) throw new Error("--scenario requires a scenario name");
      selectedNames.push(value);
      i += 1;
      continue;
    }
    if (arg.startsWith("--scenario=")) {
      const value = arg.slice("--scenario=".length);
      if (!value) throw new Error("--scenario requires a scenario name");
      selectedNames.push(value);
      continue;
    }
    throw new Error(`Unknown browser smoke option: ${arg}`);
  }
  if (!selectedNames.length) return scenarios;
  const scenarioByName = new Map(scenarios.map((scenario) => [scenario.name, scenario]));
  return selectedNames.map((name) => {
    const scenario = scenarioByName.get(name);
    if (!scenario) {
      throw new Error(`Unknown browser smoke scenario: ${name}. Run with --list to see available scenarios.`);
    }
    return scenario;
  });
}

async function main(args = process.argv.slice(2)) {
  const selectedScenarios = selectedScenariosFromArgs(args);
  for (const scenario of selectedScenarios) {
    await runScenario(scenario);
  }
  if (selectedScenarios.length) console.log("web browser smoke tests passed");
}

if (require.main === module) {
  main().catch((error) => {
    console.error(error);
    process.exit(1);
  });
}

module.exports = {
  main,
  scenarioOutcome,
  scenarios,
  selectedScenariosFromArgs,
};
