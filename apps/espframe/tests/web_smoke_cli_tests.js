const assert = require("assert/strict");

const { scenarioOutcome, scenarios, selectedScenariosFromArgs } = require("./web_smoke_tests");

function scenarioNames(selectedScenarios) {
  return selectedScenarios.map((scenario) => scenario.name);
}

function captureScenarioList() {
  const lines = [];
  const originalLog = console.log;
  console.log = (line) => lines.push(String(line));
  try {
    assert.deepEqual(selectedScenariosFromArgs(["--list"]), []);
  } finally {
    console.log = originalLog;
  }
  return lines;
}

const allScenarioNames = scenarioNames(scenarios);

assert.deepEqual(selectedScenariosFromArgs([]), scenarios, "no args should run every smoke scenario");
assert.deepEqual(captureScenarioList(), allScenarioNames, "--list should print every scenario name");
assert.deepEqual(
  scenarioNames(selectedScenariosFromArgs(["--scenario", "wizard-connection-save"])),
  ["wizard-connection-save"],
  "--scenario should select one named scenario"
);
assert.deepEqual(
  scenarioNames(selectedScenariosFromArgs(["--scenario=settings-mobile", "--scenario", "backup-import-success"])),
  ["settings-mobile", "backup-import-success"],
  "--scenario=value and repeated --scenario should preserve selection order"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario"]),
  /--scenario requires a scenario name/,
  "--scenario without a value should fail"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario="]),
  /--scenario requires a scenario name/,
  "--scenario= without a value should fail"
);
assert.throws(
  () => selectedScenariosFromArgs(["--scenario", "unknown"]),
  /Unknown browser smoke scenario: unknown/,
  "unknown scenarios should fail with a useful message"
);
assert.throws(
  () => selectedScenariosFromArgs(["--unknown"]),
  /Unknown browser smoke option: --unknown/,
  "unknown options should fail"
);

const wizardScenario = { name: "wizard" };
// The dumped DOM always contains the scenario script, so text that appears
// in the script source must not count as a pass, even when it spells out the
// pass attribute or a whole <html> tag carrying it.
const scriptOnlyDom = `<html><head></head><body><script>
  const markup = '<html data-smoke-wizard="pass">';
  const attribute = 'data-smoke-wizard="pass"';
  document.documentElement.setAttribute("data-smoke-wizard", "pass");
  pre.id = "smoke-error-wizard";
</script></body></html>`;
assert.deepEqual(
  scenarioOutcome(wizardScenario, scriptOnlyDom),
  { status: "missing", error: "" },
  "a pass marker that only appears in script source should not count as a pass"
);
assert.deepEqual(
  scenarioOutcome(wizardScenario, `<html data-smoke-wizard="pass"><head></head><body>${scriptOnlyDom}</body></html>`),
  { status: "pass", error: "" },
  "the pass attribute on the <html> element should count as a pass"
);
assert.deepEqual(
  scenarioOutcome(
    wizardScenario,
    '<html lang="en" data-smoke-wizard="fail"><body><pre id="smoke-error-wizard">Error: Missing text: A &amp; B &lt;C&gt;</pre></body></html>'
  ),
  { status: "fail", error: "Error: Missing text: A & B <C>" },
  "a failed scenario should report its decoded error"
);
assert.equal(
  scenarioOutcome(wizardScenario, '<html data-smoke-wizard-connection-save="pass"><body></body></html>').status,
  "missing",
  "another scenario's pass attribute should not count"
);

console.log("web smoke cli tests passed");
