# mipi_dsi (local override for ESP32-P4 v3 chips)

A copy of ESPHome 2026.6.4's `mipi_dsi` display component (MIT/GPLv3, see
`LICENSE.md`), with two changes. Only `builds/guition-esp32-p4-jc8012p4a1-v3.factory.yaml`
lists it in `external_components`, so every other build keeps using ESPHome's own copy.

1. `mipi_dsi.cpp` passes `phy_clk_src = 0` to `esp_lcd_new_dsi_bus()`. 2026.6.4 passes
   `MIPI_DSI_PHY_CLK_SRC_DEFAULT`, which this IDF aliases to the pre-v3 legacy PLL
   reference. ESP32-P4 v3.x silicon has no such source, so `setup()` calls `abort()`
   and the frame boots into safe mode with a black screen.
2. `models/` holds only `JC8012P4A1-V2`, the newer Guition panel's init table, which
   2026.6.4 doesn't have. The other upstream models are left out.

Delete this directory (and the `mipi_dsi` entry in the v3 build's `external_components`)
once `product/contract/project.json` pins an ESPHome release that ships both changes
(the `JC8012P4A1-V2` model arrived in 2026.8; check that release's `mipi_dsi.cpp`
leaves `phy_clk_src` at 0 before deleting). Keep the v3 build's explicit timings when you do: they are Guition's P4V3X
demo values, not the upstream model's defaults (see COM-690).
