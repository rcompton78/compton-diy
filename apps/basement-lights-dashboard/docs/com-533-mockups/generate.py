#!/usr/bin/env python3
"""COM-533 basement lights dashboard mock-ups.

Draws candidate layouts for the Elecrow CrowPanel 5.0" HMI as SVGs at the
panel's real size: three landscape (800x480, A-C, four states each) and three
portrait (480x800, P1-P3, three states each; P2 is what shipped, since the
panel's USB-C exits the side in landscape). Writes:

  index.html       every design with its states, for a phone or desktop browser
  svg/<id>-<n>.svg one file per screen, rendered to png/ by render.sh

The screen has three toggles, each bound to an HA light group:
Office (light.dad_s_office_lights), Board Games (light.basement_board_games)
and All (light.basement_lights). A group reads "on" when any member is on.

Fonts are Montserrat (LVGL's built-in family; ESPHome loads other sizes via
`font:`). Icons are Material Design Icons (Apache-2.0), which ESPHome loads as
a font with `glyphs:`, so every design builds without bitmap assets. Fills are
flat: the panel is RGB565, so gradients band. The icon "halo" is a plain
circle object, not an LVGL shadow (shadows are slow to redraw at 800x480).

Stdlib only: python3 generate.py
"""
import html
import os

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 800, 480

# Material Design Icons path data (24x24 viewBox), @mdi/svg 7.4.47.
ICONS = {
    "desk_lamp": "M10.85,2L9.18,4.5L10.32,5.25L7.14,10C7.1,10 7.05,10 7,10A2,2 0 0,0 5,12C5,12.94 5.66,13.75 6.58,13.95L10.62,20H7V22H17V20H13L8.53,13.28C8.83,12.92 9,12.47 9,12C9,11.7 8.93,11.4 8.8,11.13L12,6.37C11.78,8.05 12.75,9.89 14.45,11L18.89,4.37C17.2,3.24 15.12,3.04 13.65,3.87L10.85,2M18.33,7L16.67,9.5C17.35,9.95 18.29,9.77 18.75,9.08C19.21,8.39 19,7.46 18.33,7Z",
    "dice": "M19.78,3H11.22C10.55,3 10,3.55 10,4.22V8H16V14H19.78C20.45,14 21,13.45 21,12.78V4.22C21,3.55 20.45,3 19.78,3M12.44,6.67C11.76,6.67 11.21,6.12 11.21,5.44C11.21,4.76 11.76,4.21 12.44,4.21A1.23,1.23 0 0,1 13.67,5.44C13.67,6.12 13.12,6.67 12.44,6.67M18.56,12.78C17.88,12.79 17.33,12.24 17.32,11.56C17.31,10.88 17.86,10.33 18.54,10.32C19.22,10.31 19.77,10.86 19.78,11.56C19.77,12.23 19.23,12.77 18.56,12.78M18.56,6.67C17.88,6.68 17.33,6.13 17.32,5.45C17.31,4.77 17.86,4.22 18.54,4.21C19.22,4.2 19.77,4.75 19.78,5.44C19.78,6.12 19.24,6.66 18.56,6.67M4.22,10H12.78A1.22,1.22 0 0,1 14,11.22V19.78C14,20.45 13.45,21 12.78,21H4.22C3.55,21 3,20.45 3,19.78V11.22C3,10.55 3.55,10 4.22,10M8.5,14.28C7.83,14.28 7.28,14.83 7.28,15.5C7.28,16.17 7.83,16.72 8.5,16.72C9.17,16.72 9.72,16.17 9.72,15.5A1.22,1.22 0 0,0 8.5,14.28M5.44,11.22C4.77,11.22 4.22,11.77 4.22,12.44A1.22,1.22 0 0,0 5.44,13.66C6.11,13.66 6.66,13.11 6.66,12.44V12.44C6.66,11.77 6.11,11.22 5.44,11.22M11.55,17.33C10.88,17.33 10.33,17.88 10.33,18.55C10.33,19.22 10.88,19.77 11.55,19.77A1.22,1.22 0 0,0 12.77,18.55H12.77C12.77,17.88 12.23,17.34 11.56,17.33H11.55Z",
    "home_bulb": "M12 3L2 12H5V20H19V12H22M13 18H11V17H13M13.5 14.58V16H10.5V14.58A3 3 0 1 1 13.5 14.58Z",
}

FONT = "Montserrat, Helvetica, Arial, sans-serif"

# Palette, shared with media-room-dashboard (COM-530).
BG = "#0B0E13"
CARD = "#1A1F27"
CARD_EDGE = "#262C36"
PRESS = "#2C3440"
INK = "#F2F4F7"
QUIET = "#8A93A3"
FAINT = "#5B6474"
AMBER = "#F5A623"
AMBER_HALO = "#FFC861"
ON_AMBER = "#1A1205"
OFF_HALO = "#222833"

# Design C gives each room its own colour. (fill when on, halo when on,
# ink on the fill, the tint used for the icon and edge when off)
ROOM_COLOURS = {
    "office": ("#F5A623", "#FFC861", "#1A1205", "#C98A1E"),
    "games": ("#8B5CF6", "#A98BFF", "#FFFFFF", "#7C55D9"),
    "all": ("#FFF1D6", "#FFFFFF", "#1A1205", "#C9BFAE"),
}

ROOMS = [
    ("office", "desk_lamp", "Office", "Lamp · Desk light · Socket"),
    ("games", "dice", "Board Games", "Lightbar · Socket · Server socket"),
]

# The four moments every design is drawn in.
EVENING = dict(office=True, games=False, pressed=None, dim=False, clock="7:42", ampm="PM", date="Sat, Nov 1")
GAME_NIGHT = dict(office=True, games=True, pressed=None, dim=False, clock="8:15", ampm="PM", date="Sat, Nov 1")
ALL_OFF = dict(office=False, games=False, pressed="games", dim=False, clock="11:06", ampm="AM", date="Sun, Nov 2")
DIMMED = dict(EVENING, dim=True)
STATES = [
    ("Evening: Office on, Board Games off", EVENING),
    ("Game night: everything on", GAME_NIGHT),
    ("All off, finger on Board Games (pressed)", ALL_OFF),
    ("No touch for 30 s: backlight at 10%", DIMMED),
]


def any_on(st):
    return st["office"] or st["games"]


def all_summary(st):
    if st["office"] and st["games"]:
        return "Both rooms on"
    if st["office"]:
        return "Office is on"
    if st["games"]:
        return "Board Games is on"
    return "Everything is off"


class Svg:
    def __init__(self):
        self.parts = []

    def add(self, s):
        self.parts.append(s)

    def rect(self, x, y, w, h, fill, rx=0, stroke=None, sw=1, opacity=None):
        st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
        op = f' fill-opacity="{opacity}"' if opacity is not None else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"{op}{st}/>')

    def circle(self, cx, cy, r, fill):
        self.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"/>')

    def icon(self, name, cx, cy, size, fill):
        """Icon centred on (cx, cy)."""
        s = size / 24
        x, y = cx - size / 2, cy - size / 2
        self.add(f'<path d="{ICONS[name]}" transform="translate({x:g} {y:g}) scale({s:g})" fill="{fill}"/>')

    def text(self, x, y, words, size, fill, weight=400, anchor="start", ls=0):
        lsa = f' letter-spacing="{ls}"' if ls else ""
        self.add(f'<text x="{x}" y="{y}" font-size="{size}" font-weight="{weight}" '
                 f'text-anchor="{anchor}" fill="{fill}"{lsa}>{html.escape(words)}</text>')


def pill(g, x_right, y, label, fill, ink, centre=None):
    """State pill, top at y, right edge at x_right (or centred on centre)."""
    w = 18 + 11 * len(label)
    x = centre - w / 2 if centre is not None else x_right - w
    g.rect(x, y, w, 30, fill, rx=15)
    g.text(x + w / 2, y + 21, label, 14, ink, 700, "middle", ls=1.5)


def header(g, st, big_clock=False, w=W):
    r = w - 28
    if big_clock:
        # One <text> with a tspan, so AM/PM sits right after the digits.
        g.add(f'<text x="28" y="48" font-size="36" font-weight="600" fill="{INK}">{st["clock"]}'
              f'<tspan dx="6" font-size="16" fill="{QUIET}">{st["ampm"]}</tspan></text>')
        g.text(r, 30, "BASEMENT", 13, FAINT, 700, "end", ls=2.5)
        g.text(r, 50, st["date"], 16, QUIET, 600, "end")
    else:
        g.text(28, 42, "Basement", 24, INK, 600)
        g.text(r, 42, f'{st["clock"]} {st["ampm"]}', 22, QUIET, 600, "end")


def footer(g, w=W, h=H):
    g.text(w / 2, h - 10, "http://192.168.1.57", 13, FAINT, 500, "middle")


def tile_colours(on, pressed):
    """Amber scheme: (fill, ink, icon, halo, edge, edge width)."""
    if on:
        return AMBER, ON_AMBER, ON_AMBER, AMBER_HALO, None, 0
    if pressed:
        return PRESS, INK, INK, "#38414F", INK, 3
    return CARD, INK, QUIET, OFF_HALO, CARD_EDGE, 1.5


# ---------------------------------------------------------------------------
# A: three equal columns. Office, Board Games and All side by side, same
# size, so the screen reads as three big light switches.
# ---------------------------------------------------------------------------
def design_a(g, st):
    g.rect(0, 0, W, H, BG)
    header(g, st)
    cols = [(key, icon, name, sub, st[key]) for key, icon, name, sub in ROOMS]
    cols.append(("all", "home_bulb", "All lights", all_summary(st), any_on(st)))
    for i, (key, icon, name, sub, on) in enumerate(cols):
        x, y, w, h = 24 + i * 261, 68, 230, 372
        fill, ink, ic, halo, edge, sw = tile_colours(on, st["pressed"] == key)
        g.rect(x, y, w, h, fill, rx=26, stroke=edge, sw=sw)
        g.circle(x + w / 2, y + 128, 74, halo)
        g.icon(icon, x + w / 2, y + 128, 88, ic)
        g.text(x + w / 2, y + 258, name, 28, ink, 600, "middle")
        if key == "all":
            g.text(x + w / 2, y + 288, sub, 16, ink if on else QUIET, 500, "middle")
        pill(g, None, y + 312, "ON" if on else "OFF",
             ON_AMBER if on else OFF_HALO, AMBER if on else QUIET, centre=x + w / 2)
    footer(g)


# ---------------------------------------------------------------------------
# B: two big room cards and a full-width All bar under them, so the master
# control looks different from the rooms.
# ---------------------------------------------------------------------------
def room_card(g, st, key, icon, name, sub, x, colours):
    on = st[key]
    fill, ink, ic, halo, edge, sw = colours(key, on, st["pressed"] == key)
    y, w, h = 68, 364, 260
    g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
    g.circle(x + 80, y + 84, 52, halo)
    g.icon(icon, x + 80, y + 84, 64, ic)
    pill(g, x + w - 26, y + 26, "ON" if on else "OFF",
         "#00000033" if on else OFF_HALO, ink if on else QUIET)
    g.text(x + 32, y + 196, name, 32, ink, 600)
    g.text(x + 32, y + 228, sub, 16, ink if on else QUIET, 500)


def all_bar(g, st, colours):
    on = any_on(st)
    fill, ink, ic, halo, edge, sw = colours("all", on, st["pressed"] == "all")
    x, y, w, h = 24, 344, 752, 100
    g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
    g.circle(x + 64, y + h / 2, 34, halo)
    g.icon("home_bulb", x + 64, y + h / 2, 42, ic)
    g.text(x + 118, y + 46, "All basement lights", 26, ink, 600)
    g.text(x + 118, y + 74, all_summary(st), 16, ink if on else QUIET, 500)
    pill(g, x + w - 30, y + 35, "ON" if on else "OFF",
         "#00000033" if on else OFF_HALO, ink if on else QUIET)


def amber_colours(key, on, pressed):
    return tile_colours(on, pressed)


def design_b(g, st):
    g.rect(0, 0, W, H, BG)
    header(g, st)
    for i, (key, icon, name, sub) in enumerate(ROOMS):
        room_card(g, st, key, icon, name, sub, 24 + i * 388, amber_colours)
    all_bar(g, st, amber_colours)
    footer(g)


# ---------------------------------------------------------------------------
# C: B's layout, but every room has its own colour (Office amber, Board Games
# violet, All warm white), and a big clock and date in the header. Off rooms
# keep a hint of their colour in the icon and edge, so you learn the colours.
# ---------------------------------------------------------------------------
def room_colours(key, on, pressed):
    fill_on, halo_on, ink_on, tint = ROOM_COLOURS[key]
    if on:
        return fill_on, ink_on, ink_on, halo_on, None, 0
    if pressed:
        return PRESS, INK, tint, "#38414F", tint, 3
    return CARD, INK, tint, OFF_HALO, CARD_EDGE, 1.5


def design_c(g, st):
    g.rect(0, 0, W, H, BG)
    header(g, st, big_clock=True)
    for i, (key, icon, name, sub) in enumerate(ROOMS):
        room_card(g, st, key, icon, name, sub, 24 + i * 388, room_colours)
    all_bar(g, st, room_colours)
    footer(g)


DESIGNS = [
    ("a", "A. Three big switches", design_a,
     ["Office, Board Games and All are the same size, side by side: three light switches.",
      "Amber when on, like the media room remote. Big targets (230×372) from across the room.",
      "All looks just like a room, so it's the least obvious that it controls both."]),
    ("b", "B. Two rooms + an All bar", design_b,
     ["The two rooms get big cards with what's in each group; All is a wide bar underneath.",
      "The All bar says what's on (Office is on / Both rooms on), so its state makes sense when one room is on.",
      "Amber when on, same palette as the media room remote."]),
    ("c", "C. Room colours + clock", design_c,
     ["B's layout, but each room has its own colour: Office amber, Board Games violet, All warm white.",
      "Off rooms keep a hint of their colour in the icon, so the colours are learnable at a glance.",
      "Big clock and date in the header (HA time, no extra hardware). Uses more of the colour screen."]),
]


# ---------------------------------------------------------------------------
# Portrait (480x800), for the panel turned so its USB-C exits the bottom.
# Same three toggles, same amber palette as design B.
# ---------------------------------------------------------------------------
PW, PH = 480, 800


def p_status(g, st):
    g.rect(0, 0, PW, PH, BG)
    header(g, st, w=PW)


def p_room_card(g, st, key, icon, name, sub, y, h):
    """B's room card, full width: halo and pill on the top row, name and
    members along the bottom."""
    on = st[key]
    fill, ink, ic, halo, edge, sw = tile_colours(on, st["pressed"] == key)
    x, w = 24, PW - 48
    g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
    g.circle(x + 80, y + 82, 52, halo)
    g.icon(icon, x + 80, y + 82, 64, ic)
    pill(g, x + w - 26, y + 26, "ON" if on else "OFF",
         "#00000033" if on else OFF_HALO, ink if on else QUIET)
    g.text(x + 32, y + h - 62, name, 34, ink, 600)
    g.text(x + 32, y + h - 30, sub, 16, ink if on else QUIET, 500)


def p_all_bar(g, st, y, h):
    on = any_on(st)
    fill, ink, ic, halo, edge, sw = tile_colours(on, st["pressed"] == "all")
    x, w = 24, PW - 48
    g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
    g.circle(x + 62, y + h / 2, 34, halo)
    g.icon("home_bulb", x + 62, y + h / 2, 42, ic)
    g.text(x + 114, y + h / 2 - 4, "All lights", 26, ink, 600)
    g.text(x + 114, y + h / 2 + 22, all_summary(st), 16, ink if on else QUIET, 500)
    pill(g, x + w - 26, y + h / 2 - 15, "ON" if on else "OFF",
         "#00000033" if on else OFF_HALO, ink if on else QUIET)


# P1: B turned on its side. The two room cards stacked, the All bar under them.
def design_p1(g, st):
    p_status(g, st)
    p_room_card(g, st, "office", "desk_lamp", "Office", ROOMS[0][3], 76, 270)
    p_room_card(g, st, "games", "dice", "Board Games", ROOMS[1][3], 362, 270)
    p_all_bar(g, st, 648, 112)
    footer(g, PW, PH)


# P2: a switch plate. Three equal full-width rows, icon left and pill right,
# All last. The most like a wall switch, and the biggest All target.
def design_p2(g, st):
    p_status(g, st)
    rows = [(key, icon, name, sub, st[key]) for key, icon, name, sub in ROOMS]
    rows.append(("all", "home_bulb", "All lights", all_summary(st), any_on(st)))
    x, w, h, gap = 24, PW - 48, 214, 14
    for i, (key, icon, name, sub, on) in enumerate(rows):
        y = 76 + i * (h + gap)
        fill, ink, ic, halo, edge, sw = tile_colours(on, st["pressed"] == key)
        g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
        g.circle(x + 82, y + h / 2, 54, halo)
        g.icon(icon, x + 82, y + h / 2, 64, ic)
        g.text(x + 160, y + h / 2 - 6, name, 30, ink, 600)
        g.text(x + 160, y + h / 2 + 24, sub, 16 if key == "all" else 14, ink if on else QUIET, 500)
        pill(g, x + w - 24, y + 24, "ON" if on else "OFF",
             "#00000033" if on else OFF_HALO, ink if on else QUIET)
    footer(g, PW, PH)


# P3: two tall room columns side by side, each listing its lights, and a
# wide All bar along the bottom.
def design_p3(g, st):
    p_status(g, st)
    members = {"office": ["Lamp", "Desk light", "Socket"],
               "games": ["Lightbar", "Socket", "Server socket"]}
    for i, (key, icon, name, _sub) in enumerate(ROOMS):
        on = st[key]
        fill, ink, ic, halo, edge, sw = tile_colours(on, st["pressed"] == key)
        x, y, w, h = 24 + i * 224, 76, 208, 556
        cx = x + w / 2
        g.rect(x, y, w, h, fill, rx=28, stroke=edge, sw=sw)
        g.circle(cx, y + 150, 72, halo)
        g.icon(icon, cx, y + 150, 88, ic)
        g.text(cx, y + 290, name, 28 if len(name) < 10 else 25, ink, 600, "middle")
        for j, m in enumerate(members[key]):
            g.text(cx, y + 330 + j * 26, m, 16, ink if on else QUIET, 500, "middle")
        pill(g, None, y + h - 62, "ON" if on else "OFF",
             "#00000033" if on else OFF_HALO, ink if on else QUIET, centre=cx)
    p_all_bar(g, st, 648, 112)
    footer(g, PW, PH)


PORTRAIT_DESIGNS = [
    ("p1", "P1. Stacked rooms + All bar (B, portrait)", design_p1,
     ["Design B turned on its side: Office and Board Games as full-width cards, All underneath.",
      "Room cards keep the member list; All says what's on."]),
    ("p2", "P2. Switch plate: three equal rows", design_p2,
     ["Three full-width rows the same size, like a three-gang wall switch.",
      "All is as big a target as the rooms, but looks like a third room."]),
    ("p3", "P3. Two tall rooms + All bar", design_p3,
     ["Office and Board Games side by side as tall columns, each listing its lights on separate lines.",
      "Biggest icons; the All bar along the bottom is closest to your hand on a wall."]),
]
PORTRAIT_STATES = STATES[:3]


def standalone(g, w=W, h=H):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" width="{w}" height="{h}" '
            f'font-family="{FONT}">' + "".join(g.parts) + "</svg>")


def screen(fn, st, w=W, h=H):
    g = Svg()
    fn(g, st)
    if st["dim"]:
        # Approximates a 10% backlight: the panel can't go darker per pixel.
        g.rect(0, 0, w, h, "#000000", opacity=0.82)
    return standalone(g, w, h)


def emit(did, title, fn, notes, states, w=W, h=H, portrait=False):
    """Write svg/<did>-<n>.svg for each state and return the design's
    index.html section."""
    shots = []
    for i, (caption, st) in enumerate(states):
        svg = screen(fn, st, w, h)
        with open(os.path.join(HERE, "svg", f"{did}-{i + 1}.svg"), "w", encoding="utf-8") as f:
            f.write(svg)
        shots.append(f"<figure>{svg}<figcaption>{html.escape(caption)}</figcaption></figure>")
    lis = "".join(f"<li>{html.escape(n)}</li>" for n in notes)
    cls = "shots portrait" if portrait else "shots"
    return (f'<section><h2>{html.escape(title)}</h2><ul>{lis}</ul>'
            f'<div class="{cls}">{"".join(shots)}</div></section>')


def main():
    os.makedirs(os.path.join(HERE, "svg"), exist_ok=True)
    sections = [emit(*d, STATES) for d in DESIGNS]
    sections += [emit(*d, PORTRAIT_STATES, PW, PH, portrait=True) for d in PORTRAIT_DESIGNS]
    page = f"""<!doctype html>
<!-- Generated by generate.py (COM-533). Don't edit by hand: rerun python3 generate.py. -->
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>COM-533 basement lights dashboard mock-ups</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700&display=swap">
<style>
body{{margin:0;padding:16px;background:#f4f5f7;color:#1b1f24;font:15px/1.45 system-ui,sans-serif}}
h1{{font-size:20px;margin:0 0 4px}} p.lead{{margin:0 0 16px;color:#555}}
section{{background:#fff;border-radius:12px;padding:14px;margin:0 0 16px;box-shadow:0 1px 2px #0001}}
h2{{font-size:17px;margin:0 0 6px}}
.shots{{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:14px;margin-top:10px}}
.shots.portrait{{grid-template-columns:repeat(auto-fill,minmax(200px,1fr))}}
figure{{margin:0}}
figure svg{{width:100%;height:auto;display:block;border-radius:8px;border:10px solid #111;box-sizing:border-box}}
figcaption{{font-size:12px;color:#666;margin-top:4px}}
ul{{margin:0;padding-left:18px}}
</style></head><body>
<h1>Basement lights dashboard: mock-ups (COM-533)</h1>
<p class="lead">Elecrow CrowPanel 5.0" HMI, drawn at the panel's real resolution: 800×480 landscape (A-C), then
480×800 portrait (P1-P3) once it turned out the USB-C exits the side. <strong>P2 was picked</strong>.
Every design has the same three toggles (Office, Board Games, All, each an HA light group).</p>
{"".join(sections)}
</body></html>
"""
    with open(os.path.join(HERE, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)


if __name__ == "__main__":
    main()
