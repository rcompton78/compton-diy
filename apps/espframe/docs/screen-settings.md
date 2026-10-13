---
title: Espframe Screen Brightness and Display Settings
description: Configure Espframe screen brightness, day and night schedules, display tone, and rotation from the device settings menu.
---

# Espframe Screen Brightness and Display Settings

Display controls in **Settings**: brightness (day/night), tone, optional schedule, and rotation. Available in the web UI and (where applicable) Home Assistant.

## Screen Brightness

**Screen Brightness** sets day and night levels; the frame switches by sunrise/sunset from your timezone. Sunrise/sunset shown below the sliders. In HA: **Screen: Backlight** (on/off + brightness).

<!-- ESPFRAME:SETTINGS_TABLE screen_brightness START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Daytime Brightness** | 100% | Day (10-100%) |
| **Nighttime Brightness** | 75% | Night (10-100%) |
<!-- ESPFRAME:SETTINGS_TABLE screen_brightness END -->

## Night Schedule

**Night Schedule** turns the display fully off outside a time window: it switches to a black page, pauses LVGL, turns the backlight light off, and forces the physical PWM output off. When **Schedule Screen Off** is off, only day/night brightness applies. On/Off are hour-of-day (0–23). In HA: **Screen: Schedule Enabled**, **Screen: Schedule On Hour**, **Screen: Schedule Off Hour**, **Screen: Schedule Wake Timeout**.

<!-- ESPFRAME:SETTINGS_TABLE night_schedule START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Schedule Screen Off** | Off | Use scheduled on/off |
| **On Time** | 6 | Backlight on (hour) |
| **Off Time** | 23 | Backlight off (hour) |
| **When Woken, Idle Time To Screen Off** | 60 seconds | How long a touch wake stays on during the off period |
<!-- ESPFRAME:SETTINGS_TABLE night_schedule END -->

In Home Assistant, **Screen: Sleep** and **Screen: Wake** expose the same sleep/wake behavior as the touchscreen controls. Sleep pauses the slideshow fetch loop instead of only dimming the panel.

## Rotation

**Rotation** rotates the LVGL display layer, so the picture and touch input turn together. This uses ESPHome 2026.4's LVGL rotation support.

Choose 0 or 180 degrees for a landscape frame. 90 and 270 degrees turn the frame to portrait, for a frame standing on its side. Pick whichever of the two puts the side you want at the bottom (for example, the side the power cable leaves from). The choice is saved and survives a reboot. On the 10" model, the firmware keeps its internal 90-degree panel offset and maps each choice onto the correct LVGL value.

In portrait, photos are aligned to the top of the screen (using your **Display Mode**, Fill or Fit), and portrait pairing turns off because there's no room for two photos side by side. Set **Photo Orientation** to portrait-only if you want every photo to suit the tall screen (see [Photo Sources](/photo-sources)).

<!-- ESPFRAME:SETTINGS_TABLE screen_rotation START -->
| Setting | Default | Description |
|---------|---------|-------------|
| **Rotation** | 0 degrees | Rotate the screen to 0, 90, 180 or 270 degrees. 90 and 270 are portrait. |
<!-- ESPFRAME:SETTINGS_TABLE screen_rotation END -->
