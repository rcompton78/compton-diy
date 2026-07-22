---
name: flash-sale
description: Add, list, or remove a cyd-clock store flash-sale item by writing directly to cat-buddy-api's flash_sales database (prod on cat-buddy.richcompton.com, or the local dev instance). Use when the user asks to "put X on sale", "add a flash sale", "start a sale on <item>", "remove the flash sale", "clear the sale", "what's currently on sale", or similar (DIY-59/DIY-79).
allowed-tools: [Bash]
---

# Flash Sale

Manages `cat-buddy-api`'s `flash_sales` table directly via SQL — there's no admin
UI/write endpoint yet (that's DIY-80, a follow-up card). Any store item can go on sale;
`cyd-clock` firmware applies a flat 50% discount (`FLASH_SALE_DISCOUNT_PERCENT` in
`apps/cyd-clock/src/main.cpp`) to whatever item's `id` matches the currently active row —
no price is stored in the API itself.

## Environment

Default to **prod** unless the user says "dev"/"local"/"test":

- **Prod**: `ssh compton@cat-buddy.richcompton.com "docker exec -i cat-buddy-db psql -U postgres -d cat_buddy"` (append `-c "<query>"` for a single statement, or pipe SQL via stdin)
- **Dev**: `docker exec -i cat-buddy-db psql -U postgres -d cat_buddy` run locally — only works if the dev container is up (`docker ps --format '{{.Names}}' | grep cat-buddy-db`; if not, tell the user to start it, e.g. via `nx start-infra cat-buddy` in the `compton-apps` repo — don't start it yourself unless asked)

## Valid item ids

An item's `id` must match one already used by the device's store catalogs in
`apps/cyd-clock/src/main.cpp` — a typo here won't error, it'll just silently never match
(`flashSalePrice()` falls back to full price with no error). Look up current ids live
rather than trusting a hardcoded list here, since the catalogs grow over time:

```bash
grep -A3 'CAT_COLORS\[\] = {\|STUFFIES\[\] = {\|BLANKET_COLORS\[\] = {\|ROOM_THEMES\[\] = {\|ACCESSORIES\[\] = {' apps/cyd-clock/src/main.cpp
```

Plus the one special case not from a catalog array: `right_arm_slot` (the one-time
right-arm buddy slot unlock).

## Steps

1. **List current sales** (always do this first, for context — a stale/forgotten sale
   from earlier testing is an easy thing to leave active by accident):
   ```sql
   SELECT id, item_id, start_at, end_at FROM flash_sales ORDER BY start_at DESC;
   ```

2. **Add a sale.** Confirm with the user: item id (validated against the live catalog
   grep above — if it doesn't match anything, say so and ask them to pick a real one),
   and the window (default to "starts now" if unspecified; ask for a duration if not
   given — don't assume how long). Only one sale is expected active at a time
   (`flashSalePrice()`/`isFlashSaleActive()` assume this), so if the list from step 1
   shows another currently-active row, flag the overlap and confirm before inserting —
   don't silently create two active sales.
   ```sql
   INSERT INTO flash_sales (item_id, start_at, end_at)
   VALUES ('<item_id>', now(), now() + interval '<duration>');
   ```

3. **Remove a sale.** From the list in step 1, confirm which row (by `id`) before
   deleting — don't guess which one "the current sale" means if more than one exists.
   ```sql
   DELETE FROM flash_sales WHERE id = '<uuid>';
   ```

4. **Verify** by hitting the live endpoint (works for both environments; swap the host):
   ```bash
   curl -s -H "api-key: <token>" https://cat-buddy.richcompton.com/api/flash-sale/current
   ```
   The token isn't stored in this repo (it's a GitHub Actions secret — `CAT_BUDDY_API_TOKEN`
   here, `CB_PROD_API_TOKEN` in `compton-apps`) — ask the user for it if needed, or skip
   verification and just report the SQL result if they'd rather not share it in-session.

The device firmware only picks up a change on its next poll (`FLASH_SALE_POLL_INTERVAL_MS`,
15 min) or when someone hits "Poll now" on its `/config/flashsale` web page — mention this
so the user doesn't expect an instant change on the device screen.
