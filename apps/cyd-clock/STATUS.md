# CYD Clock — Session Status

## What's Working

- Firmware flashes and boots successfully via WSL + usbipd (`/dev/ttyUSB0`)
- WiFiManager captive portal (`CYD-Clock` AP) connects to home WiFi on boot
- Portal includes custom fields for **Latitude**, **Longitude**, and **UTC Offset** — saved to LittleFS `/config.json`
- NTP sync working — correct time and date displayed
- Portrait layout (`rotation=0`, 240×320, USB at bottom) confirmed correct orientation
- Clock screen: `HH:MM` (font 7, large), seconds (font 4), date, weather row
- Timer screen: tap anywhere on clock to switch, tap to pause/resume, resets back to clock when done
- Flicker fixed — text draws with `TFT_BLACK` background (no `fillScreen` in draw loop)
- **No grey strip** — display driver was wrong (ILI9341 on an ST7789 panel). Fixed by switching to `ST7789_DRIVER=1` + `TFT_INVERSION_ON=1` + `CGRAM_OFFSET=1`

## Display Configuration (Confirmed)

- **Driver IC:** ST7789 (not ILI9341 as originally assumed — this specific board ships with ST7789)
- **Rotation:** 0 → portrait 240×320, USB at bottom
- **Key flags:** `ST7789_DRIVER=1`, `TFT_INVERSION_ON=1`, `CGRAM_OFFSET=1`
- Note: other CYD boards in this set have ILI9341 — keep separate platformio configs if flashing both

## Layout (Implemented)

Portrait 240×320, USB at bottom:

```
┌─────────────────────┐  y=0
│  12:34  ²³  18C Clear│  ← 40px header (clock + weather, always on)
├─────────────────────┤  y=40
│                     │
│    pixel cat        │  ← 175px animal zone (touch to feed)
│   (animated)        │
│                     │
│   tap to feed       │
├─────────────────────┤  y=215
│  [1m] [5m][10m][30m]│  ← 50px quick-pick buttons
├─────────────────────┤  y=265
│  05:00      [||] [X]│  ← 55px timer row (font 6 digits)
└─────────────────────┘  y=320
        [USB]
```

### Features
- **Header**: HH:MM + seconds + weather temp/description, redraws every second
- **Pixel cat**: white cat with ears, eyes (blink every 4s), whiskers, nose, paws, tail
  - Idle: neutral expression, slow blink
  - Happy (3s): big W-smile + sparkles — triggered by tapping the animal zone
  - Celebrating (6s): bounce + sparkles — triggered automatically when timer finishes
- **Quick-pick buttons**: [1m] [5m] [10m] [30m] — tap to start that timer, active button highlighted
- **Timer row**: MM:SS in font 6 (green=running, yellow=paused, red=finished)
  - [||/▶] button: pause / resume
  - [X] button: reset timer and clear active pick
- Dirty-flag rendering — only redraws zones that changed, no flicker

## Freenove ESP32-S3 CYD Bring-up

- Board attaches as `/dev/ttyACM0` (native USB-Serial/JTAG) rather than `/dev/ttyUSB0`
- Colors were inverted on first flash (white background, black cat) — `tft.invertDisplay(false)` in `setup()` was unconditionally undoing the `TFT_INVERSION_ON=1` build flag needed for this panel; now guarded to `BOARD_CYD` only
- Touch, backlight, and full app (clock/weather/timer/config UI) confirmed working
- Flash: `pnpm nx run cyd-clock:flash-freenove-s3`

## Known Issues

### Weather Not Showing
Location defaults to `0.0, 0.0` until set via the WiFiManager portal. To configure:
1. Reset WiFi credentials: power cycle while holding BOOT, or call `wm.resetSettings()` in code
2. Connect to `CYD-Clock` AP
3. Go to `192.168.4.1` in browser
4. Set WiFi credentials + latitude/longitude/UTC offset

### UTC Offset
Currently hardcoded default in `ConfigManager` — user needs to set via portal on first boot.

## Flash Usage (DIY-39, 2026-07-12)

Sizes from `pio run` on the current build:

| Board | Flash used | Partition | % used |
|---|---|---|---|
| `cyd` (esp32dev, 4MB) | 1,193,553 B | 1,310,720 B (1.25MB app slot) | **91.1%** |
| `freenove-s3` (esp32-s3, 8MB) | 1,150,389 B | 3,342,336 B (3.19MB app slot) | 34.4% |

`cyd` is the constrained board — only ~117KB of headroom left before it needs a bigger
partition scheme, and its 1.25MB app slot comes from the default esp32dev OTA-enabled
partition table (two app slots, required for the OTA/beta-push release flow).

Symbol-size breakdown of the `cyd` build (`xtensa-esp32-elf-nm --size-sort`) shows the
growth is **not** coming from our own code — the entire store/cosmetics system (all
stuffies, blanket colors, dressing room, config backup/import) is ~7KB of the 1.16MB
image. The bulk is framework/library code:

- mbedtls/TLS: ~129KB
- WiFi PHY/MAC (802.11/WPA): ~79KB
- WiFiManager + its captive-portal WebServer/DNSServer: ~55KB
- WiFi/HTTPClient: ~56KB
- lwIP TCP/IP stack: ~52KB
- libc printf/scanf family (float-capable): ~66KB
- esp-idf misc: ~46KB

The mbedtls/TLS cost traced to `libs/weather-client` using `WiFiClientSecure` +
`HTTPClient` to call the weather API over HTTPS (`client.setInsecure()` — no cert
validation, so the full TLS stack was paid for with none of the security benefit).

**Conclusions:**
- Adding more store items (stuffies, blanket colors, room themes) is cheap — each one is
  a few hundred bytes of vector-primitive draw code, not a real flash risk.
- The network/TLS stack, not the store system, was the actual lever for `cyd`'s
  headroom. Remaining candidate follow-ups if more headroom is ever needed:
  - Check whether Arduino-ESP32's newlib-nano/no-float-printf build option is available
    to shrink the printf/scanf family
  - Re-check WiFiManager's default partition table — a custom partition scheme without
    a second OTA slot would roughly double `cyd`'s available app space, at the cost of
    losing OTA updates on that board

### Update (DIY-40, 2026-07-13)

Confirmed Open-Meteo serves plain HTTP (no forced HTTPS redirect), so
`WeatherClient.cpp` was switched from `WiFiClientSecure`/`https://` to plain
`WiFiClient`/`http://`. Measured result on `cyd`:

| | Before | After | Saved |
|---|---|---|---|
| Flash used | 1,193,553 B | 1,068,401 B | ~122KB |
| Flash % | 91.1% | 81.5% | — |

`freenove-s3` also builds clean at 30.8% (1,028,985 / 3,342,336 B).

### Update (DIY-38, 2026-07-13) — Room themes

Added a third store cosmetic category, `RoomTheme`, mirroring the `Stuffy`/`BlanketColor`
catalog-driven pattern (purchase → own → equip → render). Backed by a shared
`zoneFillRect()`/`zoneBgColor()` primitive that all animal-zone erasures (cat box, sparkles,
points, name label, Zz overlay, meds-button hide) now go through instead of a hardcoded
black fill, so the equipped theme's backdrop shows across the *entire* 240×175 animal zone,
at all times (not just the sleep-peek scene like blankets/stuffies) — without reintroducing
the full-zone-redraw flicker bug DIY-8 originally fixed. A new one-shot `dirty.animalBg`
flag repaints the full zone only when the backdrop actually changes (boot, theme
bought/equipped/unequipped, reset, backup restore, wake-from-screensaver); routine redraws
stay as narrow as before.

Six themes shipped: five flat-color placeholders (Midnight, Twilight, Forest, Rosewood,
Amber — each paired with a blanket color, 40pts) sharing one `drawFlatThemeBackground()`
function driven by a per-theme `bgColor` field, plus "Starry Night" (200pts) — a moon and a
fixed 18-star field, the first room theme with real art. Its `drawStarryNightBackground()`
uses `tft.setViewport(x, y, w, h, false)` to clip drawing to whatever sub-rect is being
repainted while keeping absolute screen coordinates, so it can draw its whole scene
unconditionally on every call (including small per-element erasures) with only the relevant
slice reaching the display — the pattern to reuse for future real-art themes (fireplace,
etc., see `apps/cyd-clock/STORE_IDEAS.md`).

`cyd` flash: 81.7% (1,070,973 / 1,310,720 B), up from 81.5% post-DIY-40 — confirms the
"cheap" conclusion above held even with six themes including one with real art.
`freenove-s3`: 30.9% (1,031,553 / 3,342,336 B).

### Update (DIY-51, 2026-07-18) — Lifetime XP / level badges

Added a second, non-spendable progression counter (`totalXp`) on top of the existing
`points` economy — awarded 1:1 alongside points from the same 4 care actions and the
store cheat, driving a derived level via a gentle increasing curve (`xpForLevel()`/
`levelForXp()`, level `L`→`L+1` costs `20 + 10*(L-1)` XP). Every 5th level grants a bonus
to spendable points and plays a full-screen fireworks takeover on the physical device
(`triggerFireworks()`/`updateFireworksAnim()`, modeled on the sleep-screen's full-screen
ownership pattern) — this preempts the routine small in-zone Celebrate animation for that
touch rather than layering on top of it. The on-device dashboard shows the level as a
small color-coded medal (`drawLevelBadge()`) stacked above the points/sale-flash column;
its color advances through a fixed rainbow sequence every milestone (5 levels) and locks
to gold once the sequence is exhausted — the same tier logic (`medalTierForLevel()`) is
shared with the `/config/badges` web page so the two never drift out of sync. Both boards
build clean; **the milestone/fireworks decisions and the exact medal look went through
several rounds of on-device visual iteration** (badge column ordering, clipping past the
cat's head bounding box — not just its ear triangles as an earlier comment assumed,
stale-timestamp bug in the fireworks duration check, off-by-one in the color-tier
boundary) — see PR history for the specifics if similar TFT layout work comes up again.

`cyd` flash: 94.0% (1,232,417 / 1,310,720 B). This card's own cost is small — 93.5%
(1,225,389 B) measured on `master` immediately before this branch, so DIY-51 itself only
added ~7KB (0.5 points), not the jump the number suggests. The 81.7% figure two entries up
(DIY-38, 2026-07-13) is stale and no longer a useful baseline: DIY-47/48/50/52 landed
between then and now without a recorded flash entry and consumed nearly all of the
headroom DIY-40 bought back. `cyd` is now genuinely close to full — worth a flash-usage
pass (candidates from the DIY-39 entry above still apply) before the next `cyd`-side
feature, not just watching it. DIY-53 (test harness/emulator investigation) also flagged
flash-constrained iteration as
a pain point independent of this. `freenove-s3`: 35.6% (1,188,817 / 3,342,336 B), still
comfortable.

## Auto-Update Investigation (DIY-41, 2026-07-14)

Looked into whether the device can detect and install a new firmware release on its
own, instead of the user downloading a `.bin` and flashing over USB or the web
flasher.

### What already exists

- The release pipeline (`.github/workflows/release.yml` +
  `tools/esp-flasher/generate_release.py`) already builds a raw, unmerged
  `<app>-<env>-ota.bin` per board on every push to `master` and attaches it to a
  GitHub Release (tag `YYYY.MM.DD-<run>`).
- WiFiManager's captive-portal menu already includes an **`update`** entry
  (`main.cpp`, the `wm.setMenu(...)` call) — this is WiFiManager's built-in
  `/update` page (`WiFiManager.h` pulls in `<Update.h>`), which lets a user
  manually upload a `.bin` file from their browser to flash the *other* OTA app
  slot. The release notes already point at this: *"Upload the `*-ota.bin`
  matching your device via its config portal's Update page."*
- Both boards use partition tables with two OTA app slots (`ota_0`/`ota_1`), so
  the underlying `Update.h` write path (stream to inactive slot, verify, set boot
  partition) is already exercised by that manual upload flow — a self-triggered
  download-and-flash would reuse the exact same mechanism, just sourced from the
  network instead of a browser file picker.
- The firmware currently has **no embedded version string** — `main.cpp` never
  reports a build version anywhere, so there's nothing on-device to compare
  against a release tag yet.

### What "auto" would need

1. **A version to compare.** Inject the CI `RELEASE_VERSION` as a build flag
   (e.g. `-D FIRMWARE_VERSION=\"${RELEASE_VERSION}\"`) so each build knows its own
   tag; local/dev builds would fall back to `"dev"`.
2. **A way to ask "what's latest".** The natural source is the GitHub Releases
   API (`api.github.com/repos/.../releases/latest`) — but that, and the asset
   download itself (`objects.githubusercontent.com`), are **HTTPS-only**. There's
   no plain-HTTP fallback the way Open-Meteo offered for DIY-40.
3. **A way to fetch + apply the bin.** `HTTPClient` + `WiFiClientSecure` streamed
   into `Update.begin/write/end`, then reboot — same shape as the existing
   `/update` handler, just automated.

### The blocker: this undoes DIY-40's flash win, and only on one board

Re-adding `WiFiClientSecure`/mbedTLS for the version check + download reintroduces
almost exactly the ~122–129KB DIY-40 just removed. Impact differs sharply by
board:

| Board | Current (post DIY-38/40) | + TLS back (~125KB) | Headroom left |
|---|---|---|---|
| `cyd` (1.25MB app slot) | 81.7% (1,070,973 B) | **~91.2%** (~1,196,000 B) | ~114KB — back to the DIY-39 pre-fix squeeze |
| `freenove-s3` (3.19MB app slot) | 30.9% (1,031,553 B) | ~34.7% (~1,160,000 B) | ~2.1MB — plenty |

`cyd` is exactly the board DIY-40 fixed to buy back headroom; putting TLS back
for auto-update would erase that win and leave next-to-no margin for anything
else. `freenove-s3` has no such problem.

### Options considered

- **A — Full auto-update on both boards.** Rejected as-is: re-blows `cyd`'s flash
  budget for a feature that's a "nice to have," not a fix for a real constraint.
- **B — Self-hosted plain-HTTP version/asset endpoint** (e.g. a small always-on
  box on the LAN that mirrors the latest release and re-serves it over HTTP,
  the way Open-Meteo happens to). Avoids the TLS cost on-device entirely, but
  adds an infra dependency (something that has to stay running) for a hobby
  project — disproportionate for what this buys.
- **C — Board-gated: full auto-update on `freenove-s3` only, manual-only (existing
  `/update` page) on `cyd`.** Recommended. `freenove-s3` has the flash budget to
  spare and gets a real "checks GitHub, downloads, flashes, reboots" feature;
  `cyd` keeps the DIY-40 savings and stays on the existing manual upload flow
  (download `-ota.bin` from the release/web flasher, upload via the portal).
  Gate with the existing `BOARD_CYD` / `BOARD_FREENOVE_S3` build flags, same
  pattern already used to split touch/display backends.
- **D — Notify-only (no download), same board split.** A lighter version of C:
  poll the Releases API and just flag "update available" on-screen, leaving the
  download+flash manual either way. Doesn't avoid the TLS cost (the check itself
  still needs HTTPS), so it buys less for the same flash price as full C — not
  worth doing as a separate step.

### Recommendation

Implement option **C** as a follow-up card, scoped to `freenove-s3`:

1. Add `FIRMWARE_VERSION` build flag wired to `RELEASE_VERSION` (falls back to
   `"dev"` locally).
2. On `freenove-s3` only: periodic (e.g. every 12–24h, well under GitHub's
   unauthenticated 60 req/hr limit) check against
   `api.github.com/repos/rcompton78/compton-diy/releases/latest`, compare tag to
   `FIRMWARE_VERSION`, and if newer, download the `cyd-clock-freenove-s3-ota.bin`
   asset and flash it via `Update.h` — reusing the same slot-write mechanism the
   manual `/update` page already exercises.
3. Add rollback safety regardless of board: call the ESP32 OTA "mark app valid"
   step after a successful post-boot self-check (WiFi connects + first weather
   fetch succeeds), so a broken auto-update reverts to the previous slot instead
   of bricking the device. This is missing today even for the manual upload path
   and is worth adding either way.
4. Leave `cyd` alone — manual `-ota.bin` download + WiFiManager `/update` upload,
   as it already works today.
5. Revisit full auto-update on `cyd` only if its headroom is bought back first
   (e.g. the no-float printf/custom-partition follow-ups noted in the DIY-39
   section above).

**Superseded by the decision below — both boards shipped auto-update, not
just `freenove-s3`.** Left the investigation and option analysis above intact
as the record of why this was a real trade-off, not a free feature.

### Implementation (DIY-41, 2026-07-14)

After the investigation above, the decision was made to ship auto-update on
**both** boards on this card, accepting the `cyd` flash cost, rather than
board-gating to `freenove-s3` only. If `cyd`'s remaining headroom turns out
to be too tight in practice, flash-savings work is a separate follow-up card,
not a blocker for this one.

What shipped:

- `FIRMWARE_VERSION` build flag, wired from CI's `RELEASE_VERSION`
  (`scripts/pio.sh` defaults it to `"dev"` outside the release workflow;
  `.github/workflows/release.yml`'s build step now exports it so compiled
  binaries and the release tag match).
- New shared lib `libs/ota-update-client` (`OtaUpdateClient`) — checks
  `api.github.com/repos/rcompton78/compton-diy/releases/latest` over
  `WiFiClientSecure`, compares the release tag against `FIRMWARE_VERSION`
  (plain string inequality — versions are monotonically increasing
  `YYYY.MM.DD-<run>` strings, no semver needed), and if different, downloads
  the board's `-ota.bin` asset and streams it into `Update.h`. Skips
  entirely on `"dev"` builds.
- `ConfigManager` gained `autoUpdateEnabled`/`lastUpdateCheckVersion`/
  `lastUpdateCheckEpoch`, covered by the existing DIY-35 backup/restore path
  for free.
- `main.cpp`: an 18h periodic check in `loop()` (mirrors the existing weather-
  fetch interval pattern), a blocking download-and-flash with an on-screen
  progress readout (`drawOtaProgress()` — this is a synchronous operation,
  same as the weather fetch; no FreeRTOS task juggling), and a new
  `/config/update` settings page (current version, last-checked info,
  auto-update toggle, manual "check now" button) following the existing
  `/config/backup` GET-renders/POST-mutates idiom. The existing WiFiManager
  `/update` manual-upload page is untouched.
- `esp_ota_mark_app_valid_cancel_rollback()` called after the first
  successful post-boot weather fetch. **Caveat, not yet verified on real
  hardware**: PlatformIO's Arduino framework for ESP32 ships a prebuilt
  core, so whether `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is actually baked
  into that image (i.e. whether a bad-but-bootable update truly auto-reverts)
  isn't controllable from this repo's `platformio.ini`. What's certain
  either way: `Update.h` validates the image (magic byte/size/CRC) before
  committing the new boot partition, so a corrupt/truncated download can't
  become bootable — a firmware that flashes clean and then crashes is the
  unverified case.

**Post-implementation flash sizes** (`pio run` size report, both boards
build clean):

| Board | Flash used | Partition | % used | Headroom |
|---|---|---|---|---|
| `cyd` | 1,213,757 B | 1,310,720 B | **92.6%** | ~97KB (7.4%) |
| `freenove-s3` | 1,170,089 B | 3,342,336 B | 35.0% | ~2.07MB (65.0%) |

`cyd`'s actual cost landed a bit above the ~125KB estimate — about 142.8KB
over the pre-DIY-41 baseline of 1,070,973 B, since this also adds
`HTTPClient`'s redirect handling, `Update.h`'s OTA-write path, `ArduinoJson`'s
streaming filter parse, and pinned CA certs, on top of bare `WiFiClientSecure`.

### Real-hardware validation (2026-07-14)

Flashed a `cyd` board over USB/WSL and exercised the full path against the
real GitHub release pipeline (a genuine `master` release, `2026.07.14-40`,
already existed with both boards' OTA assets attached):

- Built with a deliberately old `FIRMWARE_VERSION` (`2020.01.01-1`), flashed,
  confirmed via `strings` on the ELF that the version embedded correctly.
- Used temporary serial instrumentation (since removed) to confirm: the
  version check succeeded against the pinned-CA connection, found the real
  release as newer, and downloaded+flashed it — heap stayed healthy
  throughout (~227KB free at start, bottomed out around **163KB free**
  during the download, well clear of exhaustion). `applyResult=Success`,
  device rebooted cleanly and reconnected to WiFi.
- This resolves both risks flagged during planning: `HTTPClient` redirect
  handling to `objects.githubusercontent.com` works correctly, and heap
  headroom on `cyd` (the tighter of the two boards) is not a real concern.
- One side effect worth noting for anyone repeating this test: a *successful*
  test run **replaces the flashed build** with whatever's actually on
  `master` — by design, that's the feature working. Re-flash from this
  branch afterward (with `FIRMWARE_VERSION=dev` to avoid an immediate
  repeat) to keep testing branch-local changes.
- Iterated the UX after this test based on live feedback: the `/config/update`
  page now distinguishes "check skipped (dev build)" from "already up to
  date" from "check failed", and a found update sends an immediate "Found
  &lt;version&gt; — installing" response before blocking on the download
  (previously the manual "check now" button just dropped the connection
  mid-request with no feedback). The on-device screen also shows the found
  version before the progress bar starts.
- Bumped `UPDATE_CHECK_INTERVAL_MS` from 18h to 1h after confirming the real
  cost per check (~5.5KB, ~0.3s network time) — GitHub's 60 req/hr
  unauthenticated limit is shared across all unauthenticated API traffic
  from the same IP, and even at 1h with both boards checking independently
  that's only ~2 req/hr, nowhere near the limit.

### Security fix: TLS certificate pinning (2026-07-14)

An automated security review flagged the initial implementation's use of
`client.setInsecure()` for both the GitHub API check and the firmware
download — this skips certificate validation entirely, meaning a MITM
attacker (rogue AP, ARP/DNS spoofing on the same LAN) could serve arbitrary
firmware that the device would flash and boot. For a weather API call
(DIY-40's original use of `setInsecure()`) that's a shrug; for something
that ends in `Update.end(true)` and a reboot into attacker-controlled code,
it's a real vulnerability. Fixed by pinning the actual root CAs instead:

- **USERTrust ECC Certification Authority** (root of `api.github.com`'s
  chain, via Sectigo) — valid to 2038.
- **ISRG Root X1** (root of `objects.githubusercontent.com`'s chain, via
  Let's Encrypt — the release-asset redirect target) — valid to 2035.

Both chains were captured live (`openssl s_client -showcerts`) and verified
against these exact roots before pinning. Root-level (not leaf/intermediate)
pinning was chosen deliberately: GitHub's leaf and intermediate certs rotate
on the order of weeks to a couple of years, which would silently break this
feature; the roots above are long-lived, self-signed, and part of every
standard trust store, so this should hold for years without a firmware
update of its own. Cost: +2.9KB on `cyd` (92.3% → 92.5%).

**Residual risk, not addressed here**: this closes the network-MITM vector
but does not verify the firmware image's *authenticity* beyond "served by
something holding a cert that chains to these roots" — i.e. no code-signing.
If GitHub's account, the repo, or its release pipeline were ever compromised,
a malicious binary attached to a release would still flash and boot without
detection. Adding a detached signature check (e.g. Ed25519, public key baked
into firmware, verified before `Update.end(true)`) would close that gap too,
but is a meaningfully bigger lift (key management, a CI signing step) and
was treated as out of scope for this card. Flagging it here as a known,
accepted gap rather than silently omitting it.

## Black Cat Flash Sale Fix (DIY-54, 2026-07-19)

The black cat flash sale (DIY-48) was reported stuck showing well past its
3pm-10pm window. Investigation ruled out both an obvious suspects: NTP resync
(confirmed `ntpClient.update()` retries every ~60s and never gets stuck — see
Jira comment) and `isBlackCatSaleActive()`'s own time math (correct and
recomputed fresh on every call, no cached/stale boolean anywhere).

**Actual root cause**: every `/config/*` page — including `/config/store`,
where the sale badge and discounted price live — was served via
`wm.server->send(200, "text/html", page)` with no `Cache-Control` header. The
server always computes fresh state per request, but nothing told the browser
not to cache the response. A tab opened during the sale window kept showing
that cached snapshot indefinitely unless hard-refreshed. This explained every
symptom: the sale correctly appeared when first loaded (a real request), a
freshly-flashed second device correctly showed no sale (never-cached URL),
and the original device "never turned off" (same tab, never re-fetched).

Fixed by adding `sendHtmlPage()` — wraps `wm.server->send(200, "text/html",
...)` with `Cache-Control: no-store` — and routing all 11 dynamic page
handlers through it instead of calling `wm.server->send()` directly. This
also fixes staleness for every other config page (points balance, owned
items, etc.), not just the sale badge.

Verified live: temporarily narrowed the sale window to a 1-minute range
(10:24-10:25pm) on the `freenove-s3` test board, confirmed the store page
correctly stopped showing the sale on the next load after the window closed
(previously it would have stayed stuck), then reverted before merging.

**Also landed on this card**:
- The sale itself was redefined as a one-time absolute date/time window
  (`BLACK_CAT_SALE_START`/`END`, `YYYYMMDDHHMM` as `int64_t` — a 32-bit
  `long` overflows at this value) instead of a recurring daily 3pm-10pm
  check, so it can't silently turn back on every day forever.
- White was added to the Store's cat-color list (always "Owned", since it's
  the free default equipped via `EQUIP_NONE` rather than a `CAT_COLORS[]`
  entry) — previously it was chooseable from the setup wizard and Dress page
  but looked absent from the Store, which was inconsistent now that reset
  routes back through the same wizard-driven color choice as first-run setup.
  The "(default)" suffix was also dropped from all three white-cat labels
  (wizard, Store, Dress) since it's just a normal pick now, not a fallback
  that needs calling out as special.

## Branch & Files

- Branch: `feature/DIY-1-cyd-clock-weather-timer`
- Jira card: DIY-1
- Main firmware: `apps/cyd-clock/src/main.cpp`
- Config: `apps/cyd-clock/include/Config.h`
- PlatformIO: `apps/cyd-clock/platformio.ini` (upload_port hardcoded to `/dev/ttyUSB0` for WSL)

## Flash Commands (WSL)

```bash
# Attach device (PowerShell, run once per session)
usbipd attach --wsl --busid <busid>

# Install tooling (first time only)
pnpm setup

# Build
pnpm nx run cyd-clock:build

# Flash firmware
pnpm nx run cyd-clock:flash-cyd

# Flash filesystem (only needed if data/ changes)
pnpm nx run cyd-clock:flash-fs

# Serial monitor
pnpm nx run cyd-clock:monitor
```

## CI Pipeline

- Triggers on every push to `master`
- Builds and packages every project (`nx run-many`) so the web flasher index always stays complete; a GitHub Release is only created if `nx affected` finds a project with a `release` target changed since the prior commit
- `release` target builds both `cyd` and `freenove-s3` envs and packages a `.bin`/manifest entry for each
- Deploys web flasher to GitHub Pages: `https://rcompton78.github.io/compton-diy/`
- `NPM_TOKEN` secret set on repo for Bytesafe `@compton` registry
