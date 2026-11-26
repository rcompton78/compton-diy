# Workflow Config

## Linear
- Team key: COM
- Team ID: f1e10efe-5164-4d71-a8b2-b16d93f1447f
- Default label: EspFrame Public

## Repository
- Base branch: main
- Branch prefix: feature/
- Git host: github
- Tracker: linear

## Build
<!-- check:pr's browser smoke tests (test:web-smoke) need Chrome or Chromium; set CHROME_BIN (or CHROME_PATH) if it isn't in a standard location. -->
- Lint: npm run check:fast
- Build: npm run webserver:build
- Test: npm run check:pr

## Open Code Review Gate
- Enabled: no

## Bot Thread Resolution
- pr-revbot[bot]: auto-resolves
