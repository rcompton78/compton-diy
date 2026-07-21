# DIY-64: second stuffy slot (right arm) — handoff

This file is scratch notes for continuing this work across sessions/machines
and can be deleted once the work merges.

## Where things live

- **Repo**: `compton-diy`, branch `feature/diy-64-second-stuffy-slot-right-arm`
  (pushed to `origin`, tracks Jira card DIY-64).
- Jira: https://rcompton78.atlassian.net/browse/DIY-64 — currently **In
  Progress**.
- All changes are in `apps/cyd-clock/src/`:
  `ConfigManager.h`, `ConfigManager.cpp`, `main.cpp`.
- The approved implementation plan (context, data model, full code sketch)
  is at `/home/compton/.claude/plans/wild-greeting-patterson.md` on the
  machine that wrote this — it's a local Claude Code plan file, not checked
  into the repo, so it won't be there on a fresh machine. This handoff doc
  is the portable summary.

## Getting set up on a new machine

```bash
git clone git@github.com:rcompton78/compton-diy.git
cd compton-diy
git checkout feature/diy-64-second-stuffy-slot-right-arm
pnpm install
```

Build (both boards, no flashing):
```bash
pnpm nx run cyd-clock:build
```

Flash a connected board:
```bash
pnpm nx run cyd-clock:flash-cyd          # original ESP32-2432S028 (CYD)
pnpm nx run cyd-clock:flash-freenove-s3  # Freenove ESP32-S3 CYD
```
Also flash the filesystem image at least once per device (LittleFS holds the
web config UI and persisted `AppConfig` JSON):
```bash
pnpm nx run cyd-clock:flash-fs
```

## What this feature does

Today the pet-care cat has one purchasable "stuffy" slot (teddy bear, bunny,
squirrel, penguin, or unicorn) that only ever shows up in the **night sleep
scene**, tucked in beside the sleeping cat's head. This card adds a
**second, independent slot** for the cat's right arm/paw:

- Buy the "Right Arm Buddy Slot" once in the store (200 points, separate
  purchase from any individual stuffy).
- Once unlocked, equip any **already-owned** stuffy there from the dressing
  room — independent of whatever's equipped on the original (left/night)
  slot. The same stuffy can be equipped on both arms at once.
- The right-arm stuffy is visible **day and night**, in a new small "held"
  pose sitting on the cat's right paw — until you explicitly pick "None"
  for it.
- Unlocking the slot does **not** auto-equip anything — it starts empty and
  you choose from the dressing room, unlike every other catalog in this app
  (which auto-equips on first purchase).

## Key design decisions (in case picking this back up cold)

- **No new ownership bitmask.** A stuffy bought once (`ownedStuffies`) can
  go in either or both slots — only the *equip position* is separate
  (`equippedStuffy` for the existing left/night slot, new
  `equippedStuffyRight` for this one).
- **`equippedStuffyRightIndex()`** (next to `equippedStuffyIndex()` in
  `main.cpp`) deliberately does **not** fall back to "lowest owned index"
  like every other `equipped*Index()` resolver in this file — unlocking the
  slot has no natural "first purchase" moment to auto-equip from, so it
  just returns `-1` (nothing shown) until the user picks explicitly.
- **Single draw call site for day + night.** `drawSleepingCat()` calls
  `drawCat()` internally, and the daytime path (`drawAnimal()`) calls
  `drawCat()` directly — so the new right-arm draw call lives *inside*
  `drawCat()` itself (right after the existing paws/tail block), not
  duplicated in both callers. This is different from the existing
  left/night stuffy, which is intentionally layered on top *only* inside
  `drawSleepingCat()`.
- **New `drawHeld` pose per stuffy**, not a reuse of the existing
  `drawPeeking`/`drawFull` art — those are designed for the sleep scene's
  beside-the-head placement, not a paw-sized pose. All five stuffies
  (`STUFFIES[]` in `main.cpp`) got a new compact `drawXHeld()` function,
  anchored around `cx+20, cy+30` (near the right paw at
  `cx+8..+32, cy+42..+56`, drawn after the tail so it layers on top). First-
  pass geometry — **not yet verified on real hardware**, see below.

## What's left / how to test

Nothing has been flashed or tested on real hardware yet — only
`pnpm nx run cyd-clock:build` has been run (succeeds on both `cyd` and
`freenove-s3` envs; `cyd` flash usage is ~94.8%, still within budget).

Manual test plan once flashed:
1. Buy a stuffy (e.g. teddy) in the store. Confirm the existing left-slot
   dressing room option shows it; confirm the new "Right Arm Buddy"
   dressing-room section says "Not unlocked yet — visit the Store."
2. Buy the "Right Arm Buddy Slot" (200 points). Confirm the dressing room's
   right-arm section now lists the owned stuffy + "None", defaulting to
   **None** (not auto-equipped).
3. Equip a stuffy on the right arm. Confirm it renders on the right
   paw **during the day**, and confirm it also shows up in the **night
   sleep scene** without needing anything additional.
4. Equip a *different* stuffy on the left (night) slot than the right arm,
   confirm both render independently and don't interfere with each other.
5. Equip the *same* stuffy on both arms — confirm no crash/glitch.
6. Visually check the right-arm "held" pose doesn't clash badly with the
   tail or the existing right paw — this is the part most likely to need
   pixel-position tuning (`drawXHeld()` functions in `main.cpp`, search for
   `DIY-64`).
7. Pick "None" for the right arm — confirm it disappears (day and night).

If the held pose needs repositioning, all five `drawXHeld()` functions
share the same `bx = cx + 20, by = cy + 30` anchor — nudge that and rebuild.

## Not yet done

- Not committed. `git status` on this branch will show the three modified
  files uncommitted.
- No PR yet — this was paused specifically to hand off to another machine
  for hardware testing before continuing the `/wf:create-pr` workflow.
