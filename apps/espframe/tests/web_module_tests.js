const assert = require("assert/strict");
const fs = require("fs");
const path = require("path");

const root = path.resolve(__dirname, "..");
const template = fs.readFileSync(path.join(root, "docs/webserver/src/app.template.ts"), "utf8");
const publicApp = fs.readFileSync(path.join(root, "docs/public/webserver/app.js"), "utf8");
const endpointsSource = fs.readFileSync(path.join(root, "docs/webserver/src/endpoints.ts"), "utf8");

const modules = {
  "__ESPFRAME_WEB_CONTRACTS__": "web_contracts.ts",
  "__ESPFRAME_WEB_APP_SHELL__": "app_shell.ts",
  "__ESPFRAME_WEB_ENDPOINTS__": "endpoints.ts",
  "__ESPFRAME_WEB_RUNTIME_STATE__": "runtime_state.ts",
  "__ESPFRAME_WEB_STARTUP_WIZARD__": "startup_wizard.ts",
  "__ESPFRAME_WEB_SETTINGS_IMMICH_CARDS__": "settings_immich_cards.ts",
  "__ESPFRAME_WEB_SETTINGS_SCREEN_CARDS__": "settings_screen_cards.ts",
  "__ESPFRAME_WEB_SETTINGS_FIRMWARE_CARD__": "settings_firmware_card.ts",
  "__ESPFRAME_WEB_SETTINGS_CONTROLS__": "settings_controls.ts",
  "__ESPFRAME_WEB_LIVE_HELPERS__": "live_helpers.ts",
  "__ESPFRAME_WEB_BACKUP_IMPORT__": "backup_import.ts",
  "__ESPFRAME_WEB_COMPAT_HELPERS__": "compat.ts",
};

for (const [placeholder, filename] of Object.entries(modules)) {
  assert.ok(template.includes(placeholder), `${placeholder} must be present in app.template.ts`);
  const source = fs.readFileSync(path.join(root, "docs/webserver/src", filename), "utf8");
  assert.ok(source.trim().length > 0, `${filename} must not be empty`);
}

assert.equal(/__ESPFRAME_[A-Z0-9_]+__/.test(publicApp), false, "public app must not contain generator placeholders");
assert.match(publicApp, /function renderSettings\(\)/, "public app should include the settings renderer");
assert.match(publicApp, /function importConfig\(\)/, "public app should include backup import behavior");
assert.match(publicApp, /BACKUP_CONFIG_VERSION\s*=/, "public app should include generated backup version");
assert.match(publicApp, /BACKUP_SCHEMA\s*=/, "public app should include generated backup schema");
assert.match(publicApp, /function renderWizard\(\)/, "public app should include the startup wizard");
assert.ok(publicApp.includes("/espframe/api/v1/configuration"), "public app should use the versioned configuration API");
assert.ok(endpointsSource.includes("configurationUpdateQueue"), "configuration writes should be serialized");
assert.ok(endpointsSource.includes("configurationUpdateQueue = request.catch"), "the configuration queue should continue after a failed save");
assert.ok(publicApp.includes("customElements.define"), "public app should register its component root");
assert.ok(publicApp.includes('"album_order"'), "public app should include album order in photo-source apply keys");
assert.ok(publicApp.includes("Move album up"), "public app should include album reorder controls");
assert.ok(publicApp.includes("movePhotoIdRow"), "public app should keep photo ID and label rows reorderable");

// Backup import validation runs against the generated product metadata, so select options
// from both product settings and static entities (e.g. clock size/position) are covered.
function generatedJsonVar(name) {
  const match = publicApp.match(new RegExp(`\\bvar ${name} = (.*);\\s*$`, "m"));
  assert.ok(match, `public app should define ${name}`);
  return JSON.parse(match[1]);
}

function loadBackupImportValidation() {
  const vm = require("vm");
  const productSettings = generatedJsonVar("PRODUCT_SETTINGS");
  const staticEntities = generatedJsonVar("STATIC_ENTITIES");
  const backupSchema = generatedJsonVar("BACKUP_SCHEMA");
  const helperNames = ["productSettingOptions", "staticEntityOptions"];
  const helpers = helperNames.map((name) => {
    const match = template.match(new RegExp(`\\n  function ${name}\\([\\s\\S]*?\\n  }\\n`));
    assert.ok(match, `app.template.ts should define ${name}`);
    return match[0];
  });
  const endpoints = {};
  for (const entry of backupSchema) {
    for (const key of entry.state_keys) endpoints[key] = true; // stub: validation only checks presence
  }
  const context = {
    PRODUCT_SETTINGS: productSettings,
    STATIC_ENTITIES: staticEntities,
    endpoints,
  };
  const backupImportSource = fs.readFileSync(path.join(root, "docs/webserver/src/backup_import.ts"), "utf8");
  // The template helpers carry TypeScript annotations; strip them the same way the web build does.
  const helperSource = require("esbuild").transformSync(helpers.join("\n"), { loader: "ts", target: "es2018" }).code;
  vm.runInNewContext(
    `${helperSource}\n${backupImportSource}\nthis.validate = validateProductSettingBackupImport;\nthis.staticEntityOptions = staticEntityOptions;`,
    context,
  );
  const entryFor = (group, field) => {
    const entry = backupSchema.find((item) => item.group === group && item.field === field);
    assert.ok(entry, `backup schema should include ${group}.${field}`);
    return entry;
  };
  const validate = (entry, value) => ({ ...context.validate(entry, value) });
  const staticEntityOptions = (key) => Array.from(context.staticEntityOptions(key));
  return { validate, entryFor, staticEntityOptions };
}

{
  const { validate, entryFor, staticEntityOptions } = loadBackupImportValidation();

  assert.deepEqual(staticEntityOptions("clock_size"), ["Small", "Medium", "Large"]);
  assert.deepEqual(staticEntityOptions("not_a_static_entity"), []);
  const clockSize = entryFor("clock", "size");
  const clockPosition = entryFor("clock", "position");
  const clockFormat = entryFor("clock", "format");

  for (const option of ["Small", "Medium", "Large"]) {
    assert.deepEqual(validate(clockSize, option), { ok: true, value: option, message: "" });
  }
  assert.equal(validate(clockPosition, "Top Right").ok, true);
  assert.deepEqual(validate(clockSize, "Huge"), {
    ok: false,
    value: "Huge",
    message: "clock.size had an unsupported option - not imported",
  });
  assert.deepEqual(validate(clockPosition, "Middle"), {
    ok: false,
    value: "Middle",
    message: "clock.position had an unsupported option - not imported",
  });
  assert.equal(validate(clockFormat, "12 Hour").ok, true);
  assert.equal(validate(clockFormat, "13 Hour").ok, false);

  const invalidClockFixture = JSON.parse(
    fs.readFileSync(path.join(root, "tests/fixtures/compat/espframe-config-v1-invalid-clock-options.json"), "utf8"),
  );
  assert.equal(validate(clockSize, invalidClockFixture.clock.size).ok, false);
  assert.equal(validate(clockPosition, invalidClockFixture.clock.position).ok, false);
}

console.log("web module tests passed");
