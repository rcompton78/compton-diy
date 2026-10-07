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

## Flash Sale API Polling (DIY-79, 2026-07-22)

Replaced the compile-time `FLASH_SALES[]` table (DIY-54's fix for the earlier
daily-recurring bug) with a live poll of `cat-buddy-api` (DIY-59, a new
NestJS service in the `compton-apps` repo) — a sale can now be scheduled by
writing a `flash_sales` row, no firmware release required.

- **Polling**: `fetchFlashSale()` hits `GET /flash-sale/current` with a
  static `api-key` header on `FLASH_SALE_POLL_INTERVAL_MS` (15 min), placed
  in `loop()` right next to the OTA check for the same reason — it runs
  ahead of the sleep-window/setup-prompt early returns so neither can starve
  it. A 404 (no active sale) is treated as normal, not an error; a
  network/parse failure leaves the last-known state alone rather than
  blanking out a real sale over one flaky poll.
- **TLS**: same root-pinning approach as DIY-41's OTA fix, and it turned out
  to be the *same* root — `cat-buddy.richcompton.com`'s chain (Nginx Proxy
  Manager / Let's Encrypt) resolves to ISRG Root X1 too, just via a newer
  intermediate path (`YE2` → `Root YE` → `ISRG Root X2` → `ISRG Root X1`).
  `CAT_BUDDY_API_CA_CERT` in `Config.h` reuses that exact cert. If no CA is
  pinned, `fetchFlashSale()` refuses to poll an `https://` URL rather than
  falling back to `WiFiClientSecure::setInsecure()` — no build can send the
  token over an unverified connection.
- **Any item can go on sale**: the API only returns `{ itemId, startAt,
  endAt }` — no price, deliberately (DIY-59's scope is "what's on sale and
  when," not pricing policy). `flashSalePrice()` is now wired into every
  store category (cat colors, stuffies, blankets, room themes, accessories,
  right-arm slot), not just the original two, and computes a flat
  `FLASH_SALE_DISCOUNT_PERCENT` (50%) off whatever the matched item's own
  listed cost is — no per-item price table to keep in sync as new items are
  added.
- **Local testing**: `CAT_BUDDY_API_URL`/`CAT_BUDDY_API_TOKEN` are
  build-time values (see the new CLAUDE.md convention this introduced,
  root-level "Build-time secrets/config via env vars + local `.env`
  override") — a gitignored `apps/cyd-clock/.env` can point a dev build at
  a local/LAN `cat-buddy-api` instance over plain `http://` (skips TLS
  entirely, fine for a throwaway dev token) without touching tracked files.
  `/config/admin/flashsale` on the device shows the raw last-poll HTTP status +
  response body (not just a summary) and has a "Poll now" button, so you
  don't have to wait out the 15-minute interval to see what happened.
- **Real-hardware validation (2026-07-22)**: verified against a live,
  deployed `cat-buddy-api` (not just local dev) — WSL2 mirrored networking
  exposed a local dev instance to the physical device over LAN for initial
  testing; the CA pinning and prod token were then validated against the
  real `https://cat-buddy.richcompton.com` deployment, including inserting
  a real flash-sale row (Red Squirrel stuffy) and confirming the device
  picked it up and priced it correctly. One real bug hit along the way:
  PlatformIO's incremental build tracks build flags as part of its
  dependency signature, and a `flash-*` target's own `pio run -t upload`
  re-triggers that build — an env var exported in one shell/tool call
  doesn't survive into a separate one, so a token set via `export` (rather
  than `.env`) got silently wiped back to empty by the *next* command's
  rebuild, producing an intermittent 401 that looked like a token mismatch
  but wasn't. `.env` avoids this since it's read fresh on every invocation.
- **Cost**: `cyd` board is now at 95.4% flash usage — not much headroom
  left before the next feature needs to trim something.

## Theme Weeks — Birthday Theme (DIY-108, 2026-08-24)

Added the first "special theme week" — a date range the admin schedules from the
device's own secret admin screen, during which the cat gets birthday cosmetics
(party hat, birthday balloon sunglasses, a confetti/balloons room theme) that
auto-equip on day one and force-revert once the range ends. Meant to be a reusable
pattern for future theme weeks (Christmas, etc.), though those are expected to be
hardcoded additions later, not a generic system built now.

- **Entirely local/offline** — this went through two designs before landing here.
  The first polled a new `cat-buddy-api` `/theme-week` endpoint (mirroring
  flash-sale's pattern, with a `theme_weeks` table and a write side this time).
  The user then decided against storing theme-week schedules server-side at all,
  so that whole piece (entity/migration/controller/module) was dropped — never
  pushed, so it left no trace in `compton-apps`. The admin now enters a plain
  start/end **date** (no time-of-day — `/config/admin/themeweek`, an `<input
  type=date>` pair) directly on the device, persisted in `ConfigManager`
  (`themeWeekBirthdayStartDate`/`EndDate`, packed `YYYYMMDD`) and evaluated every
  `loop()` tick against the device's own NTP-synced clock — no network call
  involved at all, works offline, and actually *reduced* flash usage versus the
  network-polling version.
- **Theme-exclusive cosmetics** — the party hat (`ACCESSORIES[]`), birthday
  balloon sunglasses (`GLASSES[]`), and the "Birthday" room theme (`ROOM_THEMES[]`)
  are appended to the end of their catalogs but are never store-purchasable: store
  rendering, the purchase-lookup handler, and the "new store items!" badge all
  bound themselves to a `*_STORE_COUNT` constant (count of real, purchasable
  entries) rather than the catalog's full count, so the theme-exclusive entries
  never show up as buyable and never trip the badge. The dressing room needed *no*
  changes at all — it already only lists owned items, so once
  `applyThemeWeekCosmetics()` sets the owned bit, the existing equip/unequip UI
  just works, letting the user freely swap them out mid-week without any special
  casing.
- **Apply/revert transition**: `checkThemeWeekTransition()` compares the local
  date range against `activeThemeWeekKey` (what's currently applied) every tick.
  On day one it grants + force-equips the three items and snapshots whatever room
  theme was equipped beforehand (`preThemeWeekRoomTheme`). At the end of the
  range it clears the three owned bits and restores that snapshot — deliberately
  *not* touching `equippedAccessory`/`equippedGlasses` on revert, since
  `equippedAccessoryIndex()`/`equippedGlassesIndex()` already fall back once the
  owned bit clears; only the room theme needed an explicit restore, since its
  fallback picks "lowest owned theme," not "what the user had before."
- **A cupcake stuffy was tried and dropped**: the first cut of "birthday fun"
  used a 4th theme-exclusive item, a cupcake stuffy (own catalog entry in
  `STUFFIES[]`, 4 draw poses). On real hardware it was reported hard to see —
  root cause was pastel-on-pastel: the stuffy's wrapper/frosting colors were
  nearly identical to the Birthday room theme's own pale-pink backdrop
  (`0xFCF3`), since stuffies only ever render against the equipped room theme
  (the night "peek" scene's backdrop, not a fixed black). Fixed once (saturated
  colors + a `C_DARK` outline), but the user then decided to drop the cupcake
  entirely in favor of the balloon sunglasses instead — simpler (one catalog
  slot, no 4-pose art), and the same contrast lesson carried over directly into
  its design (bold single-color balloon lenses, no pastels, no fine internal
  detail).
- **Contrast is a recurring hazard for anything drawn on the animal zone**: the
  same pastel-vs-birthday-backdrop bug also hit the pre-existing "thirsty"
  water-drop indicator (drawn directly onto whatever room theme is equipped,
  same as every stuffy/accessory) — fixed the same way, a `C_DARK` outline
  added generically (not birthday-specific), so it holds up against any future
  room theme, not just this one pale one. Also had to nudge its position after
  the balloon sunglasses shipped, since the two ended up geometrically
  overlapping at the original coordinates.
- **Local testing**: no `cat-buddy-api` dependency anymore, so nothing to stand
  up — verified entirely through the device's own admin form. Real-hardware
  validation: `curl -X POST /config/admin/themeweek/set` with `start`/`end` form
  fields (same as a browser submission) → confirmed via
  `/config/backup/export`'s raw config JSON that apply/revert both fire
  correctly and immediately (no waiting on a poll interval), including edge
  cases (a same-day range, an already-past range immediately reverting on
  submit).

## Theme Weeks — Halloween Theme (COM-379, 2026-10-07)

The second theme week, built on the DIY-108 mechanism. It's on from **Oct 24 00:00 to
Nov 1 00:00 local time, every year**, with no setup, and reverts on its own afterwards.

- **Cosmetics** (all exclusive, appended past `*_STORE_COUNT`):
  - **Haunted Night** room theme: a deep purple sky with a few faint stars and a big orange
    moon top-left. The moon stays clear of the level medal and points column. Three black
    bats (one across the moon), a dark hill along the floor, and a lit jack-o'-lantern in the
    bottom-left corner.
  - **Witch Hat**: a black cone with a bent tip and an orange band with a yellow buckle. It has
    a 1px lavender outline, so it reads against the dark sky and on a black cat.
  - **Candy Corn Sunglasses**: yellow/orange/white point-down lenses with a charcoal outline.
    The eye rect is repainted with fur color first, so no sclera peeks out beside the narrow
    tip. On tabby/calico cats this covers the head pattern in that rect, the same as a blink.
  - **Jack-o'-lantern is night-only by choice.** It sits behind the Play button, so it only
    shows in the sleep-window peek scene, where the action buttons are hidden. No floor spot
    clears the buttons and name label, and the user chose to keep it as a night touch.
  - **Oct 31 only**: a small white ghost on the left side of Haunted Night, in open sky
    between the moon and the (sick-only) Meds button. It's plain backdrop art, not an animation.
    It first sat on the right, but on hardware the always-on Water button covered it. Placement
    has to avoid every animal-zone overlay, not just the cat: the Play/Meds/Water/Treat
    buttons, the boredom "Zz" marks (x 48–70 on the left), the sparkle ring and the badge
    column. The moon was shrunk to r20 for the same reason, to keep it clear of the Zz.
    `checkThemeWeekTransition()` forces one backdrop repaint when it appears or disappears.
- **A stuffy was deliberately skipped.** The card offered a pumpkin or ghost stuffy, but a
  stuffy needs four poses (peeking/full/held/held-peeking). That's roughly 4× the art and
  flash, and the same complexity the cupcake was dropped for in DIY-108.
- **Admin override** on `/config/admin/themeweek`, persisted as `themeWeekHalloweenMode` and
  included in backups:
  - **Auto** (default): uses the Oct 24–31 dates.
  - **Preview**: forces the full Oct 31 look, ghost included, on any date, for on-device
    checks before the 24th. It stays on until switched back.
  - **Off**: skips Halloween entirely.
- **Overlap: Birthday wins.** `desiredThemeWeekKey()` picks birthday, then halloween, then
  none. When the desired key differs from `activeThemeWeekKey`, it reverts the old theme and
  then applies the new one. Because the revert restores `preThemeWeekRoomTheme` first, a
  Birthday→Halloween hand-off still ends on Nov 1 with the user's original room theme, not the
  Birthday backdrop.
- **Exclusive-entry indexing changed.** DIY-108 assumed one exclusive entry per catalog
  (`*_STORE_COUNT = *_COUNT - 1`, and the item index equal to `*_STORE_COUNT`). Each catalog
  now subtracts the size of its exclusive block (2), and apply/revert use named indices
  (`ACCESSORY_IDX_*`, `GLASSES_IDX_*`, `ROOM_THEME_IDX_*`) looked up by key in
  `themeWeekItems()`. The `*_STORE_COUNT` values themselves didn't change, so the "new store
  items!" badge isn't affected. Party-hat/balloon/birthday indices are unchanged too, so a
  device upgraded mid-Birthday reverts correctly.
- **Flash** (`pio run` size report):

| Board | Before | After | Delta | % used |
|---|---|---|---|---|
| `cyd` | 1,285,205 B | 1,289,869 B | +4,664 B | 98.1% → **98.4%** (~20.9KB left) |
| `freenove-s3` | 1,241,957 B | 1,246,597 B | +4,640 B | 37.2% → 37.3% |

  `cyd` is now very close to full. The next feature on that board will most likely need to
  trim something first.

## Legendary Items — Hogwarts Crest & Sorting Hat (COM-382, 2026-10-07)

The first two **legendary**-tier store items, and a new cosmetic slot. A mockup of both items,
on every fur colour and room theme with the store listing, is in the COM-382 Artifact.

- **Legendary tier.** `STORE_COST_LEGENDARY = 350`, shared with COM-375 to COM-378. It started
  at 1000, but the user cut it to 350 after the first device flash: still the top tier, just above
  Pikachu/Eevee at 300. No existing item costs 350 or more, so only legendary items get the tag. The new
  `storeItemTags(baseCost, cost)` helper adds a gold **★ LEGENDARY** tag to any store row priced
  at that tier or higher, plus the existing 🔥 SALE tag while a flash sale is active. It replaces
  eight copies of the SALE markup, one per store section, so the later legendary cards get the
  marker for free. Both items use `flashSalePrice()` like every other item.
- **New chest-badge category (`BADGES[]`)**, a fourth worn slot beside the head
  (`ACCESSORIES[]`), the face (`GLASSES[]`) and the right arm. It uses the same model as glasses:
  - **Config:** `ownedBadges` (uint8_t bitmask), `equippedBadge` and `seenBadgeCount`, saved as
    `badges`/`badgeEquipped`/`seenBadges`. Backups pick them up through the shared
    `toJson()`/`fromJson()`, and an older config or backup simply owns no badges.
  - **Store and dressing room:** a "Badges - Chest" section in the store, the purchase and
    rollback branch, and a dressing-room radio with None. `BADGE_STORE_COUNT` feeds
    `hasNewStoreItems()`, and the ids go into `collectStoreItemIds()`, so the uniqueness check
    and the flash-sale id check cover them.
  - **Resolver:** `equippedBadgeIndex()` works like the glasses resolver, so a new purchase is
    equipped automatically.
- **Hogwarts Crest**: a 21×25 px shield with a C_DARK outline. It is quartered red, green, blue
  and yellow around an outlined gold plate with an "H". It is drawn last in `drawCat()`, over the
  tabby/calico body pattern and with the celebration bounce, at x cx−10..+10, y cy+6..+30. That
  is clear of the paws, a held stuffy (which starts at cx+29), a held toy (which starts at
  cx+24), the sparkle ring and every button. In the sleep scene it is drawn again after the
  blanket, so it reads as pinned to the blanket (the user's choice over hiding it).
  - **Hunger lines move.** The tummy rumble lines normally sit exactly where the crest goes, and
    their fur-colour erase would punch through it. With a badge equipped, `drawHungerLines()`
    moves them below the shield (cy+33/+38/+43, 5 px apart instead of 8).
- **Sorting Hat** (`ACCESSORIES[]`): a patched brown hat with a wide, droopy brim and a cone
  crumpled at a kink. The tip flops to the left, and a face is creased in (brow folds and a seam
  mouth). It has a 1px near-black outline and no band or buckle, so it reads differently from
  the black, right-bending witch hat.
  - **Placement:** the brim sits low on the brow (cy−57), which buys extra height under the
    cy−87 apex limit. The brim's lowest row is cy−52, above the blink rect (cy−50).
  - **Size:** the user asked for it a little bigger than the first mockup. It is now 65 px
    wide, against the witch hat's 55 px, and as tall as the zone allows.
- **Accessory indexing changed again.** The Sorting Hat is **appended after** the party/witch
  hats (index 12), not inserted before them. Ownership is an index-keyed bitmask, so shifting
  the theme-week hats would hand their bits to a different item on a device, or in a backup,
  that owned one mid-theme-week.
  - The purchasable accessories are therefore no longer a contiguous prefix. Store rendering and
    purchase iterate the whole catalog and filter with `isStoreAccessory()`. Its invariant is
    **store-purchasable ⇔ cost > 0**: theme-week exclusives must be cost 0 (a `static_assert`
    checks the party and witch hats), and every real store item must cost more than 0.
  - `ACCESSORY_IDX_PARTY_HAT`/`WITCH_HAT` are now fixed at 10 and 11, rather than derived from
    `ACCESSORY_STORE_COUNT`.
  - `ACCESSORY_STORE_COUNT` is now counted from that same invariant (`countStoreAccessories()`,
    which gives 11) instead of a hand-maintained subtraction, so the "new store items!" flash
    fires once for the hat, and once for the new badge section. These two points came out of
    the pre-PR independent review.
- **Flash** (`pio run` size report):

| Board | Before | After | Delta | % used |
|---|---|---|---|---|
| `cyd` | 1,289,869 B | 1,293,177 B | +3,308 B | 98.4% → **98.7%** (~17.1KB left) |
| `freenove-s3` | 1,246,597 B | 1,249,869 B | +3,272 B | 37.3% → 37.4% |

  This covers the new category plumbing, both pieces of art and the marker, offset by sharing
  the SALE markup. About 17.1KB of headroom is left on `cyd` for COM-375 to COM-378. If that runs
  short, the first candidates to trim are the long per-section `String` HTML builders in
  `handleConfigStoreGet()`/`handleConfigDressGet()`, which repeat the same row markup in every
  section and could share one row builder.

## Legendary Room Theme — Hogwarts at Night (COM-378, 2026-10-07)

The third legendary item and the first legendary room theme: a navy sky with Starry Night's star
field and a small moon, and a black castle silhouette standing on the Black Lake. It has a
crenellated curtain wall, a great hall with two keep towers behind the cat, two tall towers in
the gaps between the buttons and the cat, and two turrets peeking over the Meds and Water buttons,
with 20 warm windows. A mockup with both animations is in the COM-378 Artifact.

- **Price and store.** It reuses `STORE_COST_LEGENDARY` (350), so it gets the ★ LEGENDARY tag from
  `storeItemTags()` and goes through `flashSalePrice()` like every row. The label is gold
  (`#E8B030`, the crest's gold).
- **Room theme indexing changed, same as COM-382's accessories.** The theme is appended after the
  Birthday/Haunted Night theme-week entries (index 11), so `ROOM_THEMES[]`' purchasable entries
  aren't a contiguous prefix any more. `isStoreRoomTheme()` (store-purchasable ⇔ cost > 0) now
  filters the store listing and the purchase lookup. `ROOM_THEME_STORE_COUNT` is counted from it
  (10, so the "new store items!" flash fires once), and `ROOM_THEME_IDX_BIRTHDAY`/`HAUNTED_NIGHT`
  are fixed at 9 and 10, with a `static_assert` that both cost 0. Ownership, equip, backup and
  restore already went through the full catalog and the `uint16_t` bitmask, so they needed no
  change.
- **The lake is the sky colour on purpose.** The cat's name sits on it, and the name, the "Zz"
  marks and the points text all print over a box of the theme's `bgColor`. With the lake and sky
  both `C_HOGWARTS_SKY` there are no boxes. The castle art also stays out of the right-hand "Zz"
  box (x 188–210, y 114–128) and the badge column's text (x≥168, y<112).
- **Animations** (`updateHogwartsNightAnim()`, called every awake `loop()` tick):
  - *Window flicker:* every 6–15 s one of 8 windows goes dark for 1–3 s.
  - *Shooting star:* every 1–3 min a streak crosses the top-left sky in 9 frames (about 0.5 s).
  - Both repaint only their own pixels through `zoneFillRect()`, so they're limited to spots
    nothing is drawn over while they run. The flicker windows are in the tall towers
    (x 56–69 and 171–184, the gaps between the side buttons and the cat's clear rect) and in
    the turret tops above the Meds/Water buttons. The star's path is above the moon and the
    left "Zz", left of the cat's clear rect.
  - `drawHogwartsNightBackground()` reads the animation state (`hogwartsDarkWindow`,
    `hogwartsStarFrame`), so the many small erases elsewhere in the zone never bring back a dark
    window or wipe a star mid-flight.
  - The state resets while another theme is equipped. The animations pause in the sleep-window
    peek scene: a star already in flight is dropped there, with one full backdrop repaint.
- **Flash** (`pio run` size report):

| Board | Before | After | Delta | % used |
|---|---|---|---|---|
| `cyd` | 1,293,149 B | 1,294,077 B | +928 B | 98.7% → **98.7%** (~16.6KB left) |
| `freenove-s3` | 1,249,841 B | 1,250,781 B | +940 B | 37.4% → 37.4% |

  It's cheap because the stars reuse Starry Night's table, and the towers and windows are small
  data tables drawn by two loops.

## Build-Config Flash Diet (COM-386, 2026-10-07)

An audit of the `cyd` image found the build config, not our own code, held the easiest
headroom. The changes below are build-config only, applied to both envs in `platformio.ini`,
and nothing on screen changes:

- **Unused TFT_eSPI fonts dropped** (~12.6KB): `LOAD_FONT7`, `LOAD_FONT8`, `LOAD_GFXFF` and
  `SMOOTH_FONT`. Every `drawString`/`textWidth` call uses font 1, 2, 4 or 6 only. Re-add the
  matching flag before drawing with any other font, because an unloaded font maps to TFT_eSPI's
  empty `chrtbl_null` entry and draws nothing, rather than failing the build.
- **`CORE_DEBUG_LEVEL=0`** (~30.8KB): removes the Arduino core/WiFi/HTTP/FS `log_e` strings
  (the prebuilt default is ERROR). The firmware's own `Serial.print` output is unaffected.
- **`WM_NODEBUG`** (~18.7KB): removes WiFiManager's serial debug output.
- **`-Wl,--wrap=mbedtls_strerror` / `-Wl,--wrap=esp_err_to_name`** (~22.6KB):
  `src/link_stubs.c` replaces mbedtls' `error.c` table with a stub that prints the numeric
  code (e.g. `mbedtls error -0x7280`), and esp-idf's error-name table with one that names
  only the generic `ESP_ERR_*` codes (0x101–0x10C). Other codes read `UNKNOWN ERROR`, or the
  WiFi/flash range. The esp-idf stub returns string literals only, never a buffer, because
  callers rely on the original's static lifetime and reentrancy (two calls in one log line,
  ISRs, early boot). `ESP_ERROR_CHECK` panics still print the hex code alongside the name.

- **Flash** (`pio run` size report, dev build):

| Board | Before | After | Delta | % used |
|---|---|---|---|---|
| `cyd` | 1,293,557 B | 1,209,429 B | −84,128 B | 98.7% → **92.3%** (~99KB left) |
| `freenove-s3` | 1,250,189 B | 1,162,993 B | −87,196 B | 37.4% → 34.8% |

  Measured on top of COM-378 with `RELEASE_VERSION=dev` and an empty token. A release build's
  longer version and token strings add a few hundred bytes, which is why COM-378's own
  figure above reads 1,294,077 B.

The next candidates, measured in the audit but deliberately left out of this card, are a
shared row builder for the store/dress handlers (COM-387, ~4.5KB measured for the GET pages
alone), deflate-compressing the config HTML (~11KB, estimate), dropping `-fstack-protector`
(~11–14KB, at the cost of stack-overflow detection), and, as a last resort, the
`min_spiffs.csv` partition layout (a 1.875MB app slot). That last one needs a USB/web reflash
of every `cyd` device, wipes the LittleFS config unless the user backs it up first, and needs
`generate_release.py`'s LittleFS-offset lookup fixed.

## Store & Dressing Room Handlers Table-Driven (COM-387, 2026-10-07)

A flash diet from the cyd-clock flash audit (items F and H). It changes no behaviour: every page
and form submit works exactly as before.

- **One category layer over the eight catalogs.** `ItemCategory` (`ITEM_STUFFY` … `ITEM_TOY`, in
  store-section order) plus `catalogEntry()` (id, label, web colour, base cost), `isStoreItem()`,
  `ownedItems()`/`setItemOwned()`, `equippedItemField()` and `findCatalogIndex()`. They are plain
  switches rather than templates, so nothing gets instantiated once per catalog struct.
- **Store page:** one loop over the categories through `appendStoreRow()`/`appendStoreItemRow()`
  replaces the eight per-catalog loops and the old `storeItemTags()`/`storeItemAction()`/
  `storeItemActionStuffy()` helpers. The Right Arm Slot row goes through the same row builder.
- **Store purchase:** the eight-deep nested id lookup is one loop over the categories, and the
  purchase and save-failure rollback are one branch each (stuffy 2nd copy, right arm slot, or
  "set owned bit, auto-equip unless it's a toy").
- **Dressing room:** `appendPickRadio()` renders every radio option. `buildPickGroup()` covers
  blanket, room theme, accessory, glasses and badge, and `appendStuffyRadios()` is shared by the
  left-arm and right-arm pickers. On submit, `applyEquipField()` handles the six single-slot
  fields, and the stuffy/toy id lookups use `findCatalogIndex()`.
- **`loadPage()`** replaces the 13 copies of the `%%STYLE%%` substitution.
- **ConfigManager `load()`/`save()` go through a `String`** instead of passing ArduinoJson the
  `fs::File` stream, so only the String reader/writer (already needed by backup import/export)
  is compiled in. `save()` now also returns false on a short write, e.g. a full filesystem.
  Before, it reported success, so a store purchase on a full filesystem now rolls back with
  "Purchase failed to save" instead of claiming success.
- **Verified identical on a host harness** (not committed): master's and this branch's
  `main.cpp` and `ConfigManager.cpp` were both compiled for x86 against stub Arduino/WebServer/LittleFS
  headers and the real ArduinoJson, then driven through the same seeded scenario matrix. The
  matrix covers every `loadPage()` GET page, 600+ random store/dress GET states, every catalog
  entry equipped in turn (including Hogwarts at Night and the Sorting Hat), and store purchases
  of every item id from 120 states, each with and without a save failure. It also covers 6,000
  random dressing-room submits plus equip/unequip of every entry. The two 46 MB transcripts
  (status, headers, body, persisted config) were byte-identical. Five deliberately injected
  bugs were all caught. Backup export/import, save/load round-trips and files written by either
  version loading in the other all matched. The short-write check fails on master and passes here.
- **Flash** (`pio run` size report, dev build, on top of COM-386):

| Board | Before | After | Delta | % used |
|---|---|---|---|---|
| `cyd` | 1,209,477 B | 1,196,785 B | −12,692 B | 92.3% → **91.3%** (~111KB left) |
| `freenove-s3` | 1,162,993 B | 1,150,005 B | −12,988 B | 34.8% → 34.4% |

  Measured before COM-386 landed (on COM-378), it saved about the same: −12,924 B on `cyd` and
  −12,868 B on `freenove-s3`.

  The audit estimated 6.5–8.5KB. Sharing the dressing-room radio builder with the stuffy and
  right-arm pickers, and `loadPage()`, account for the rest. Deflate-compressing the served HTML
  (about 11KB, audit item I) is still open as a follow-up.

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
