# Handoff: DIY-50 first-run setup wizard

**Branch**: `feature/diy-50-setup-wizard` (pushed, PR open, not yet merged)
**PR**: https://github.com/rcompton78/compton-diy/pull/37

## What to do

Flash `apps/cyd-clock` from this branch onto a real device (either `cyd` or
`freenove-s3` board) and walk through the new first-run setup wizard to verify
it end-to-end — this hasn't been tested on real hardware yet, only build-verified.

## Current state

- Implementation complete, both boards build clean (`cyd` 93.1% flash, `freenove-s3` 35.2% flash).
- Everything is committed and pushed on this branch.
- PR #37 is open against `master`, not yet merged. Jira card DIY-50 is in "In Review".
- No hardware testing has been done yet — that's the point of this handoff.

## Relevant files

- `src/ConfigManager.h` / `.cpp` — new `setupComplete` bool field on `AppConfig`. Migration default is `true` on load (so existing/already-configured devices skip the wizard); only a device with no config file at all defaults to `false`.
- `src/main.cpp` — the wizard itself:
  - `drawSetupPrompt()` — on-device screen shown until setup is done ("Almost there! Complete setup at: <ip>")
  - `CONFIG_SETUP_HTML`, `handleSetupGet()`, `handleSetupPost()` — the `/setup` web wizard (pick black/grey/white cat color, then name it)
  - `handleConfigHome()` — `/config` now redirects to `/setup` while incomplete
  - `loop()` — gates the normal dashboard draw behind `setupComplete`, mirroring the existing sleep-screen pattern

## Context and decisions

- Wizard flow: Wi-Fi connects → on-device screen shows the IP → visit that IP in a browser → WiFiManager's root menu → "Cat Control Panel" button → `/config` → redirected to `/setup` → pick a free-tier color (black/grey/white; tabby/calico stay store-only purchases) → name the cat → grants 70 starting points → redirects to `/config/store?welcome=1` with a "Welcome!" banner.
- `resetToDefaults()` (the store page's hidden "Reset Everything" easter egg) naturally resets `setupComplete` back to `false` too, since it just resets the whole `AppConfig` struct — so a factory reset correctly re-triggers the wizard with no extra code.
- Deliberately did **not** try to override WiFiManager's own root (`/`) page — it registers its own handler first and wins first-match, so redirecting root wasn't reliable. Gating `/config` instead achieves the same practical effect since that's the only way into the wizard from the root menu.

## How to continue

1. On the other machine: `git fetch origin && git checkout feature/diy-50-setup-wizard`
2. `pnpm install` (only if `node_modules` isn't already present there)
3. `pnpm nx run cyd-clock:flash-cyd` (or `flash-freenove-s3`) with the device attached.
4. Power on a device with **no existing `/config.json`** (fresh flash, or use the store's hidden reset — tap "Store" heading 7x, type `reset`) and confirm:
   - On-device screen shows "Almost there! Complete setup at: <ip>" instead of the clock/cat dashboard.
   - Visiting that IP → Cat Control Panel → gets redirected to `/setup`.
   - Wizard: pick a color, click Next, name the cat, submit → lands on `/config/store` with a green "Welcome! Here's 70 points to get started." banner, 70 points shown, and the chosen color equipped on the physical display.
5. Separately, flash a device that already has a saved config from *before* this branch (or just don't reset) and confirm it boots straight to the normal dashboard, skipping the wizard entirely (the migration-default behavior).
6. If everything checks out, report back and the PR can be merged. If something's broken, fix it on this branch and push — this file can be deleted once the PR merges.
