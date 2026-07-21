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
- **`drawHeld`/`drawHeldPeeking` are exact mirrors of the left slot's
  `drawFull`/`drawPeeking`** — same size, same shape, same relative
  offsets, just anchored on the right (`cx+38, cy-8` / `cx+40, cy-6`
  instead of `cx-38, cy-8` / `cx-40, cy-6`). For species with left-right
  symmetric art (teddy, bunny, penguin, unicorn — body/feet/patches already
  centered on `bx`), the mirror needed no offset changes beyond the anchor
  sign. Squirrel's tail-tip poke is one-sided, not a symmetric pair, so its
  offsets are sign-flipped (`bx+10` → `bx-10` etc.) to poke toward the body
  on this side too, instead of literally translating the left art and
  having the tail point away from the cat.
  - **This went through two earlier, discarded designs** worth knowing
    about if the pose ever looks wrong again: (1) a compact custom
    "sitting on the paw" pose anchored at `cx+20, cy+30`, dropped because
    day/night placement didn't match the left slot's height; (2) reusing
    that compact pose's anchor but keeping the head at the left's exact
    mirror (`cx+40`) while shifting only the body left of it to dodge the
    blanket edge — that decentered body from head and looked diagonal.
- **`drawRightArmStuffy(cx, cy, hasBlanket)`** (defined just above
  `drawSleepingCat()`) is the single place that picks `drawHeld` vs.
  `drawHeldPeeking`, and is called separately by each scene — once from
  `drawAnimal()`'s day branch (`hasBlanket=false`, always) and once from
  `drawSleepingCat()` (`hasBlanket` from the equipped blanket check already
  done there for the left slot). It is **not** called from inside
  `drawCat()` — earlier revisions did that (a single call site shared by
  day and blanket-less night), but once `drawHeld` became full-size, its
  body would poke out past the blanket's right edge if left running
  unconditionally under `drawCat()` with no way to swap in the head-only
  pose. `drawSleepingCat()` still draws the blanket, then the left
  slot's `drawPeeking`/`drawFull`, then `drawRightArmStuffy()` last, so
  `drawHeldPeeking` always lands on top of the blanket.

## What's left / how to test

Flashed to the `freenove-s3` board for live checking (not `cyd`) — build
succeeds on both envs, `cyd` flash usage is ~94.9%, still within budget.
Live hardware feedback drove two rounds of pose fixes already (day/night
position mismatch, then a diagonal-looking body); re-check the current
`drawHeld`/`drawHeldPeeking` mirror-of-left approach against real hardware
rather than assuming it's finished.

Manual test plan:
1. Buy a stuffy (e.g. teddy) in the store. Confirm the existing left-slot
   dressing room option shows it; confirm the new "Right Arm Buddy"
   dressing-room section says "Not unlocked yet — visit the Store."
2. Buy the "Right Arm Buddy Slot" (200 points). Confirm the dressing room's
   right-arm section now lists the owned stuffy + "None", defaulting to
   **None** (not auto-equipped).
3. Equip a stuffy on the right arm. Confirm it renders full-size, mirrored
   beside the head (matching the left slot's size/shape/height) **during
   the day**. At night, confirm it shows the same full pose if no blanket
   is equipped, or just the head poking out on top of the blanket if one
   **is** equipped.
4. Equip a *different* stuffy on the left (night) slot than the right arm,
   confirm both render independently and don't interfere with each other,
   and don't visually collide now that the right slot is full-size.
5. Equip the *same* stuffy on both arms — confirm no crash/glitch.
6. Check each species' silhouette reads correctly mirrored, especially
   squirrel's tail-tip poke (`drawSquirrelHeld`/`drawSquirrelHeldPeeking`
   in `main.cpp`, search for `DIY-64`) — its offsets were hand-mirrored
   rather than a straight reuse of the left art, most likely to need
   tuning.
7. Pick "None" for the right arm — confirm it disappears (day and night).

## Not yet done

- Not committed. `git status` on this branch will show the three modified
  files uncommitted.
- No PR yet — this was paused specifically to hand off to another machine
  for hardware testing before continuing the `/wf:create-pr` workflow.
