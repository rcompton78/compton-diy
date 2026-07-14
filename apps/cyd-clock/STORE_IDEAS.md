# Store — Future Ideas

Brainstormed under DIY-36 ("add some fun stuff to the store"). No work was done on that
card — it was closed without implementation so these ideas aren't lost. Pull from this
list when planning the next store card.

## More stuffies

Same catalog pattern as the existing stuffies (`STUFFIES[]` in `apps/cyd-clock/src/main.cpp`) —
easy to slot in, no other plumbing changes needed.

- Black cat — high-contrast silhouette, would read well against the existing bear/bunny/squirrel/penguin style
- Dinosaur (little T-rex with tiny arms) — fun shape for the vector-primitive drawing technique
- Dragon as a next premium tier above the current 150pt stuffies, with wings and a longer tail for a "reach goal" feel

## New categories (bigger lift)

- Hats/accessories worn directly on the cat (party hat, bow, tiny crown) rather than a stuffy beside it — a different equip slot, more visible since it's on the cat itself
- Blanket patterns (polka dot, plaid) instead of just solid colors — reuses the existing blanket color-catalog mechanic with an extra draw variant

Done: room/background themes (DIY-38) — `RoomTheme`/`ROOM_THEMES[]` in `apps/cyd-clock/src/main.cpp`, rendered behind the cat at all times (not just the sleep scene). Five flat-color themes pairing with the blanket palette, plus a "Starry Night" theme (moon + fixed star field) as the first real-art entry. More real-art themes (fireplace, etc.) are still open — reuse `drawStarryNightBackground()`'s `tft.setViewport()` clipping pattern for any theme with per-pixel art rather than a flat fill.

## Mechanics-flavored (ties into the points economy)

- Limited-time/rotating item that changes weekly, using the RTC already available
- "Lucky" cheap item with a random color each purchase — surprise mechanic reusing the existing bitmask ownership
- Cosmetic rank/badge shown next to the points balance once total lifetime points cross a threshold (bragging rights, no gameplay effect)
