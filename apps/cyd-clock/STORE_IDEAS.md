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

Done: room/background themes (DIY-38) — `RoomTheme`/`ROOM_THEMES[]` in `apps/cyd-clock/src/main.cpp`, rendered behind the cat at all times (not just the sleep scene). Five flat-color themes pairing with the blanket palette, plus a "Starry Night" theme (moon + fixed star field) as the first real-art entry. Later real-art themes: "6-7" (DIY-87), "Clear Sky" (DIY-90) and the legendary "Hogwarts at Night" (COM-378, castle silhouette with flickering windows and a rare shooting star). More (fireplace, etc.) are still open — reuse `drawStarryNightBackground()`'s `tft.setViewport()` clipping pattern for any theme with per-pixel art rather than a flat fill.

## Mechanics-flavored (ties into the points economy)

- Limited-time/rotating item that changes weekly, using the RTC already available
- "Lucky" cheap item with a random color each purchase — surprise mechanic reusing the existing bitmask ownership
- Cosmetic rank/badge shown next to the points balance once total lifetime points cross a threshold (bragging rights, no gameplay effect)

## Theme-week exclusives

Theme weeks (DIY-108 Birthday, COM-379 Halloween) hand out cosmetics that can't be bought:
they're appended to their catalogs with cost 0 (or past `GLASSES_STORE_COUNT` in `GLASSES[]`) and granted/revoked by date. Ideas
left on the table:

- Pumpkin or ghost **stuffy** for Halloween. It was skipped because a stuffy needs four poses;
  revisit if flash on the `cyd` board is ever freed up.
- Flickering jack-o'-lantern eyes on Oct 31. This needs a periodic partial redraw, unlike the
  still ghost that shipped.
- A Christmas week (Dec 18–25?) on the same pattern: santa hat, snowflake glasses, a
  fireplace/snowfall room theme.

## Legendary tier

COM-382 introduced the legendary tier: `STORE_COST_LEGENDARY` (350 points, the top tier above Pikachu/Eevee's 300) and a gold
"★ LEGENDARY" store tag for anything priced at or above it (`storeItemTags()`). Shipped so far:

- **Sorting Hat** (head accessory).
- **Hogwarts Crest**, the first item in the new **chest badge** slot (`BADGES[]`). The slot is
  general, so more badges are now cheap to add: one catalog row plus its draw function, inside
  the shield-sized area on the chest (x cx−10..+10, y cy+6..+30). Ideas:
  - single-house crests (Gryffindor lion, etc.);
  - a gold star or sheriff badge;
  - a Pokémon gym badge to go with the Pikachu/Eevee stuffies.

- **Hogwarts at Night** room theme (COM-378): a castle on the Black Lake under a starry sky,
  with windows that flicker and a rare shooting star. Its two animations are the first periodic
  partial redraw of a room backdrop, so the flickering jack-o'-lantern eyes idea above could
  reuse the same pattern (`updateHogwartsNightAnim()`).

Still planned on the same tier: COM-375 (Glory the RainWing), COM-376 (Golden Snitch) and COM-377
(Shiny Mew).
