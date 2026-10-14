---
title: Espframe Touch Controls
description: Use Espframe touchscreen gestures to wake the display, put the screen to sleep, and move forward or back through the Immich slideshow.
---

# Espframe Touch Controls

On the slideshow screen you can wake the display, turn it off with a timed hold, and move to the next or previous photo.

## Gestures

| Action | Gesture |
|--------|---------|
| **Wake** | Tap the screen when it's off. |
| **Sleep** | Press and hold for 3 seconds, then release. |
| **Show IP address** | Tap the photo once. The frame's IP address appears at the top of the screen for 5 seconds. |
| **Next photo** | Double-tap the right half of the screen. |
| **Previous photo** | Double-tap the left half of the screen. |

Both taps of a double tap must land on the same half; a tap on each half counts as two single taps. "Left" and "right" follow the **Screen: Rotation** setting, so they are always the viewer's left and right.

Going back is one photo deep: double-tapping the left half again returns to the photo you were on. A portrait pair comes back as a single photo. Right after boot there is no previous photo yet, and the gesture does nothing.

A tap that wakes the screen only wakes it; it never changes the photo. A hold shorter than 3 seconds does nothing.

The **Photos: Next** and **Photos: Previous** buttons in Home Assistant do the same as the double taps.
