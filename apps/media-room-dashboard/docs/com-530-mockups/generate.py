#!/usr/bin/env python3
"""COM-530 dashboard mock-ups.

Draws the current main_page and three candidate redesigns as 240x320 SVGs
(the panel's real size, portrait), each in two states, and writes:

  index.html       every design side by side, for a phone browser
  svg/<id>-<n>.svg one file per screen, rendered to png/ by render.sh
  widgets/<id>.jsx a Claude Doc widget module per design (both states)

Fonts are Montserrat (LVGL's built-in family; ESPHome loads other sizes via
`font:`), at even sizes LVGL ships. Icons are Material Design Icons
(Apache-2.0), which ESPHome can load as a font with `glyphs:`, so any of these
designs can be built without bitmap assets.

Stdlib only: python3 generate.py
"""
import html
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

# Material Design Icons path data (24x24 viewBox), @mdi/svg 7.4.47.
ICONS = {
    "power": "M16.56,5.44L15.11,6.89C16.84,7.94 18,9.83 18,12A6,6 0 0,1 12,18A6,6 0 0,1 6,12C6,9.83 7.16,7.94 8.88,6.88L7.44,5.44C5.36,6.88 4,9.28 4,12A8,8 0 0,0 12,20A8,8 0 0,0 20,12C20,9.28 18.64,6.88 16.56,5.44M13,3H11V13H13",
    "game": "M7,6H17A6,6 0 0,1 23,12A6,6 0 0,1 17,18C15.22,18 13.63,17.23 12.53,16H11.47C10.37,17.23 8.78,18 7,18A6,6 0 0,1 1,12A6,6 0 0,1 7,6M6,9V11H4V13H6V15H8V13H10V11H8V9H6M15.5,12A1.5,1.5 0 0,0 14,13.5A1.5,1.5 0 0,0 15.5,15A1.5,1.5 0 0,0 17,13.5A1.5,1.5 0 0,0 15.5,12M18.5,9A1.5,1.5 0 0,0 17,10.5A1.5,1.5 0 0,0 18.5,12A1.5,1.5 0 0,0 20,10.5A1.5,1.5 0 0,0 18.5,9Z",
    "tv": "M21,17H3V5H21M21,3H3A2,2 0 0,0 1,5V17A2,2 0 0,0 3,19H8V21H16V19H21A2,2 0 0,0 23,17V5A2,2 0 0,0 21,3Z",
    "lamp": "M8,2H16L20,14H4L8,2M11,15H13V20H18V22H6V20H11V15Z",
    "floor_lamp": "M15,2L17,9H7L9,2M11,10H13V20H16V22H8V20H11V10Z",
    "string_lights": "M22.56 11.39C22.36 10.59 21.82 9.85 21.05 9.44L20.63 7.74C21.11 7.58 21.57 7.41 22 7.23V5C20 6.07 16.53 7.03 12 7.03S4 6.07 2 5V7.23C2.43 7.41 2.89 7.58 3.37 7.74L2.95 9.44C2.18 9.85 1.64 10.59 1.44 11.39C.905 13.57 .385 17.31 2.92 17.93C4 18.2 6 17.89 7.27 12.82C7.46 12 7.33 11.12 6.84 10.39L7.26 8.67C8.14 8.81 9.05 8.9 10 8.96V10.74C9.35 11.33 9 12.17 9 13C9 15.24 9.39 19 12 19C13.12 19 15 18.22 15 13C15 12.17 14.65 11.33 14 10.74V8.96C14.95 8.9 15.86 8.81 16.74 8.67L17.16 10.39C16.67 11.12 16.54 12 16.73 12.82C18 17.89 20 18.2 21.08 17.93C23.61 17.31 23.09 13.57 22.56 11.39M5.81 12.47C5.81 12.47 4.74 16.84 3.28 16.5C1.82 16.12 2.9 11.75 2.9 11.75S3.26 10.29 4.71 10.65 5.81 12.47 5.81 12.47M12 17.5C10.5 17.5 10.5 13 10.5 13S10.5 11.5 12 11.5 13.5 13 13.5 13 13.5 17.5 12 17.5M20.72 16.5C19.27 16.84 18.19 12.47 18.19 12.47S17.83 11 19.29 10.65 21.1 11.75 21.1 11.75 22.18 16.12 20.72 16.5Z",
}

FONT = "Montserrat, Helvetica, Arial, sans-serif"

# Palette. Flat fills only: the panel is RGB565, so gradients band.
BG = "#0B0E13"
CARD = "#1A1F27"
CARD_EDGE = "#262C36"
PRESS = "#2C3440"
INK = "#F2F4F7"
QUIET = "#8A93A3"
FAINT = "#5B6474"
AMBER = "#F5A623"      # lights on (the current checked colour)
ON_AMBER = "#1A1205"
BLUE = "#3D7BFF"       # TV on / active input
BLUE_DIM = "#13213A"

# Shared sample states: the evening scene, and idle with a finger on
# Window Lights (the pressed state).
EVENING = dict(tv=True, src="game", lamp=True, lights=False, floor=True, pressed=None, batt=78)
IDLE = dict(tv=False, src=None, lamp=False, lights=False, floor=False, pressed="lights", batt=41)
STATES = [("Evening: TV on (Game), two lamps on", EVENING),
          ("Idle: everything off, finger on Window Lights", IDLE)]


class Svg:
    """Collects SVG elements. Every <text> gets a unique literal id, which the
    Claude Doc widget validator requires."""

    def __init__(self):
        self.parts = []
        self.ids = {}

    def add(self, s):
        self.parts.append(s)

    def rect(self, x, y, w, h, fill, rx=0, stroke=None, sw=1):
        st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"{st}/>')

    def circle(self, cx, cy, r, fill, stroke=None, sw=1):
        st = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
        self.add(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"{st}/>')

    def icon(self, name, x, y, size, fill):
        s = size / 24
        self.add(f'<path d="{ICONS[name]}" transform="translate({x} {y}) scale({s:g})" fill="{fill}"/>')

    def text(self, x, y, words, size, fill, weight=400, anchor="start", ls=0):
        slug = re.sub(r"[^a-z0-9]+", "-", words.lower()).strip("-")[:40] or "t"
        n = self.ids.get(slug, 0) + 1
        self.ids[slug] = n
        tid = slug if n == 1 else f"{slug}-{n}"
        lsa = f' letter-spacing="{ls}"' if ls else ""
        self.add(f'<text data-claude-text-id="{tid}" x="{x}" y="{y}" font-size="{size}" '
                 f'font-weight="{weight}" text-anchor="{anchor}" fill="{fill}"{lsa}>{html.escape(words)}</text>')

    def end(self):
        self.add("</g>")


def battery(g, st, x_right, y, colour=QUIET):
    """Horizontal battery glyph + % right-aligned at x_right, baseline y."""
    bx = x_right - 24
    g.rect(bx, y - 10, 20, 11, "none", rx=2, stroke=colour, sw=1.2)
    g.rect(bx + 20, y - 7, 2, 5, colour)
    g.rect(bx + 2, y - 8, round(16 * st["batt"] / 100), 7, colour, rx=1)
    g.text(bx - 5, y, f'{st["batt"]}%', 12, colour, 600, "end")


def status_bar(g, st):
    g.text(10, 17, "Media Room", 12, QUIET, 600)
    battery(g, st, 236, 17)


def ip_footer(g, y):
    g.text(120, y, "http://192.168.1.42", 10, FAINT, 500, "middle")


# ---------------------------------------------------------------------------
# Current main_page, as common/dashboard.yaml draws it today.
# ---------------------------------------------------------------------------
def current(g, st):
    g.rect(0, 0, 240, 320, "#000000")
    tiles = [
        ("power", 8, 8, ("Power", "Off") if st["tv"] else ("Power", "On"), st["tv"]),
        ("game", 124, 8, ("Play", "Game"), st["tv"] and st["src"] == "game"),
        ("tv", 8, 104, ("Watch", "TV"), st["tv"] and st["src"] == "tv"),
        ("lamp", 124, 104, ("Window", "Lamp"), st["lamp"]),
        ("lights", 8, 200, ("Window", "Lights"), st["lights"]),
        ("floor", 124, 200, ("Standing", "Lamp"), st["floor"]),
    ]
    for key, x, y, (l1, l2), on in tiles:
        pressed = st["pressed"] == key
        fill = "#F5A623" if on else ("#1E1E1E" if pressed else "#303030")
        ink = "#000000" if on else "#FFFFFF"
        g.rect(x, y, 108, 88, fill, rx=6)
        g.text(x + 54, y + 41, l1, 14, ink, 500, "middle")
        g.text(x + 54, y + 58, l2, 14, ink, 500, "middle")
    g.text(4, 310, "http://192.168.1.42", 14, "#909090")
    battery(g, st, 236, 310, "#909090")


# ---------------------------------------------------------------------------
# A: same 2x3 grid, polished. Status bar on top, an icon per button, a short
# label, an ON tag, blue for the TV and amber for lights.
# ---------------------------------------------------------------------------
def design_a(g, st):
    g.rect(0, 0, 240, 320, BG)
    status_bar(g, st)
    tiles = [
        ("power", "power", 8, 28, ("TV", "Power"), st["tv"], BLUE),
        ("game", "game", 124, 28, ("Play", "Game"), st["tv"] and st["src"] == "game", BLUE),
        ("tv", "tv", 8, 120, ("Watch", "TV"), st["tv"] and st["src"] == "tv", BLUE),
        ("lamp", "lamp", 124, 120, ("Window", "Lamp"), st["lamp"], AMBER),
        ("lights", "string_lights", 8, 212, ("Window", "Lights"), st["lights"], AMBER),
        ("floor", "floor_lamp", 124, 212, ("Standing", "Lamp"), st["floor"], AMBER),
    ]
    for key, icon, x, y, (l1, l2), on, accent in tiles:
        pressed = st["pressed"] == key
        if on:
            fill, ink, edge = accent, (ON_AMBER if accent == AMBER else "#FFFFFF"), None
            ic = ink
        elif pressed:
            fill, ink, ic, edge = PRESS, INK, INK, INK
        else:
            fill, ink, ic, edge = CARD, INK, QUIET, CARD_EDGE
        g.rect(x, y, 108, 84, fill, rx=14, stroke=edge, sw=2 if pressed else 1)
        g.icon(icon, x + 12, y + 10, 28, ic)
        if on:
            g.text(x + 96, y + 22, "ON", 10, ink, 700, "end", ls=1)
        g.text(x + 12, y + 57, l1, 14, ink, 600)
        g.text(x + 12, y + 74, l2, 14, ink, 600)
    ip_footer(g, 314)


# ---------------------------------------------------------------------------
# B: grouped. A TV card (power + an input switcher that reads as "pick one")
# and a row of three light tiles.
# ---------------------------------------------------------------------------
def design_b(g, st):
    g.rect(0, 0, 240, 320, BG)
    status_bar(g, st)
    g.text(12, 38, "TV", 10, FAINT, 700, ls=1.5)
    g.rect(8, 44, 224, 124, CARD, rx=14, stroke=CARD_EDGE)
    # Power button + state line.
    if st["tv"]:
        g.circle(40, 80, 22, BLUE)
        g.icon("power", 28, 68, 24, "#FFFFFF")
        g.text(72, 76, "TV is on", 16, INK, 600)
        g.text(72, 94, "Showing: Game" if st["src"] == "game" else "Showing: TV", 12, QUIET, 500)
    else:
        g.circle(40, 80, 22, PRESS, stroke=CARD_EDGE)
        g.icon("power", 28, 68, 24, QUIET)
        g.text(72, 76, "TV is off", 16, INK, 600)
        g.text(72, 94, "Tap to turn on", 12, QUIET, 500)
    # Input switcher.
    g.rect(18, 114, 204, 44, BG, rx=11)
    for key, icon, x0, label, tx in (("game", "game", 20, "Game", 41), ("tv", "tv", 120, "Watch TV", 129)):
        active = st["tv"] and st["src"] == key
        if active:
            g.rect(x0, 116, 100, 40, BLUE, rx=9)
        ink = "#FFFFFF" if active else (QUIET if st["tv"] else FAINT)
        g.icon(icon, tx, 127, 18, ink)
        g.text(tx + 24, 141, label, 14 if key == "game" else 13, ink, 600)
    g.text(12, 188, "LIGHTS", 10, FAINT, 700, ls=1.5)
    for key, icon, x, l1, l2 in (("lamp", "lamp", 8, "Window", "Lamp"),
                                 ("lights", "string_lights", 85, "Window", "Lights"),
                                 ("floor", "floor_lamp", 162, "Standing", "Lamp")):
        on, pressed = st[key], st["pressed"] == key
        if on:
            fill, ink, ic, edge = AMBER, ON_AMBER, ON_AMBER, None
        elif pressed:
            fill, ink, ic, edge = PRESS, INK, INK, INK
        else:
            fill, ink, ic, edge = CARD, INK, QUIET, CARD_EDGE
        g.rect(x, 196, 70, 98, fill, rx=14, stroke=edge, sw=2 if pressed else 1)
        g.icon(icon, x + 20, 210, 30, ic)
        g.text(x + 35, 262, l1, 12, ink, 600, "middle")
        g.text(x + 35, 278, l2, 12, ink, 600, "middle")
    ip_footer(g, 312)


# ---------------------------------------------------------------------------
# C: "now showing" hero. What the TV is doing is the biggest thing on screen;
# the lights become a list of rows with on/off switches.
# ---------------------------------------------------------------------------
def design_c(g, st):
    g.rect(0, 0, 240, 320, BG)
    status_bar(g, st)
    if st["tv"]:
        g.rect(8, 28, 224, 140, BLUE_DIM, rx=16, stroke=BLUE, sw=1.5)
        g.text(20, 50, "NOW SHOWING", 10, "#8FB3FF", 700, ls=1.5)
        g.text(20, 82, "Game" if st["src"] == "game" else "TV", 28, INK, 600)
        g.text(20, 101, "Roku TV is on", 12, QUIET, 500)
        g.circle(202, 58, 20, BLUE)
        g.icon("power", 191, 47, 22, "#FFFFFF")
    else:
        g.rect(8, 28, 224, 140, CARD, rx=16, stroke=CARD_EDGE)
        g.text(20, 50, "TV", 10, FAINT, 700, ls=1.5)
        g.text(20, 82, "Off", 28, QUIET, 600)
        g.text(20, 101, "Tap power to turn on", 12, FAINT, 500)
        g.circle(202, 58, 20, PRESS, stroke=CARD_EDGE)
        g.icon("power", 191, 47, 22, INK)
    for key, icon, x0, label, tx in (("game", "game", 20, "Game", 40), ("tv", "tv", 124, "Watch TV", 129)):
        active = st["tv"] and st["src"] == key
        if active:
            g.rect(x0, 118, 96, 38, BLUE, rx=10)
        else:
            g.rect(x0, 118, 96, 38, "#1A2740" if st["tv"] else BG, rx=10,
                   stroke="#2C3D5E" if st["tv"] else CARD_EDGE)
        ink = "#FFFFFF" if active else (QUIET if st["tv"] else FAINT)
        g.icon(icon, tx, 128, 18, ink)
        g.text(tx + 23, 142, label, 13 if key == "game" else 12, ink, 600)
    for key, icon, y, label in (("lamp", "lamp", 178, "Window Lamp"),
                                ("lights", "string_lights", 220, "Window Lights"),
                                ("floor", "floor_lamp", 262, "Standing Lamp")):
        on, pressed = st[key], st["pressed"] == key
        g.rect(8, y, 224, 36, PRESS if pressed else CARD, rx=12,
               stroke=INK if pressed else CARD_EDGE, sw=2 if pressed else 1)
        g.icon(icon, 20, y + 7, 22, AMBER if on else QUIET)
        g.text(52, y + 23, label, 14, INK if on or pressed else QUIET, 600)
        g.rect(182, y + 8, 38, 20, AMBER if on else "#343B47", rx=10)
        g.circle(210 if on else 192, y + 18, 8, "#FFFFFF" if on else "#C9CED6")
    ip_footer(g, 314)


DESIGNS = [
    ("current", "Today", current,
     ["What common/dashboard.yaml draws now: six grey buttons, amber when on.",
      "The power label names the action (Power Off), not the state.",
      "No icons; everything is the same size and weight."]),
    ("a", "A. Polished grid", design_a,
     ["Same 2x3 grid and button sizes, so the smallest firmware change.",
      "Icon + short label per button, an ON tag, blue for the TV and amber for lights.",
      "Battery % moves to a status bar at the top; the IP shrinks to a footer line."]),
    ("b", "B. Grouped: TV card + light tiles", design_b,
     ["Splits the screen into TV and Lights, so it reads at a glance.",
      "The power button says the state in words (TV is on, Showing: Game).",
      "Game / Watch TV is a two-way switcher, which is how the inputs behave."]),
    ("c", "C. Now showing + light switches", design_c,
     ["What the TV is doing is the biggest thing on screen.",
      "Lights become rows with on/off switches (LVGL's switch widget).",
      "Lights are a smaller target (36 px tall) than in A or B (84-98 px)."]),
]


def screen_svg(fn, st):
    g = Svg()
    fn(g, st)
    return g


def standalone(g):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 240 320" width="240" height="320" '
            f'font-family="{FONT}">' + "".join(g.parts) + "</svg>")


CAMEL = ["stroke-width", "text-anchor", "font-size", "font-weight", "font-family", "letter-spacing"]


def to_jsx(s):
    for a in CAMEL:
        head, tail = a.split("-")
        s = s.replace(f" {a}=", f" {head}{tail.capitalize()}=")
    return s.replace('"', "'")


def widget(design_id, title, fn):
    """One Claude Doc widget: both states side by side at 1.4x."""
    g = Svg()
    g.text(24, 30, title, 15, "var(--cds-text-primary)", 600)
    for i, (caption, st) in enumerate(STATES):
        x = 24 + i * 376
        g.text(x, 58, caption, 11.5, "var(--cds-text-secondary)")
        g.add(f'<g data-claude-anchor="{design_id}-state-{i + 1}" transform="translate({x} 70) scale(1.4)">')
        fn(g, st)
        g.end()
    body = to_jsx("".join(g.parts))
    # Trim defaults, and hoist the icon paths into consts so each is sent once.
    for junk in (" fontWeight='400'", " textAnchor='start'", " rx='0'"):
        body = body.replace(junk, "")
    consts = []
    for name, d in ICONS.items():
        if f"d='{d}'" in body:
            body = body.replace(f"d='{d}'", "d={%s}" % name.upper())
            consts.append(f"const {name.upper()} = '{d}';")
    svg = (f"<svg viewBox='0 0 760 542' role='img' aria-label='{title}' fontFamily='{FONT}'>"
           + body + "</svg>")
    return " ".join(consts) + " export default () => " + svg + ";"


def main():
    os.makedirs(os.path.join(HERE, "svg"), exist_ok=True)
    os.makedirs(os.path.join(HERE, "widgets"), exist_ok=True)
    sections = []
    for did, title, fn, notes in DESIGNS:
        shots = []
        for i, (caption, st) in enumerate(STATES):
            svg = standalone(screen_svg(fn, st))
            with open(os.path.join(HERE, "svg", f"{did}-{i + 1}.svg"), "w", encoding="utf-8") as f:
                f.write(svg)
            shots.append(f'<figure>{svg}<figcaption>{html.escape(caption)}</figcaption></figure>')
        with open(os.path.join(HERE, "widgets", f"{did}.jsx"), "w", encoding="utf-8") as f:
            f.write(widget(did, title, fn))
        lis = "".join(f"<li>{html.escape(n)}</li>" for n in notes)
        sections.append(f'<section><h2>{html.escape(title)}</h2><div class="shots">{"".join(shots)}</div>'
                        f"<ul>{lis}</ul></section>")
    page = f"""<!doctype html>
<!-- Generated by generate.py (COM-530). Don't edit by hand: rerun python3 generate.py. -->
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>COM-530 media room remote mock-ups</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700&display=swap">
<style>
body{{margin:0;padding:16px;background:#f4f5f7;color:#1b1f24;font:15px/1.45 system-ui,sans-serif}}
h1{{font-size:20px;margin:0 0 4px}} p.lead{{margin:0 0 16px;color:#555}}
section{{background:#fff;border-radius:12px;padding:14px;margin:0 0 16px;box-shadow:0 1px 2px #0001}}
h2{{font-size:17px;margin:0 0 10px}}
.shots{{display:flex;gap:12px;flex-wrap:wrap}}
figure{{margin:0;flex:1 1 150px;max-width:300px}}
figure svg{{width:100%;height:auto;display:block;border-radius:6px;border:6px solid #111;box-sizing:border-box}}
figcaption{{font-size:12px;color:#666;margin-top:4px}}
ul{{margin:10px 0 0;padding-left:18px}}
</style></head><body>
<h1>Media room remote: dashboard mock-ups (COM-530)</h1>
<p class="lead">240×320 portrait, drawn at the panel's real resolution. Each design shows the same two moments.
Same six buttons and the same behaviour in every design; only the look changes.</p>
{"".join(sections)}
</body></html>
"""
    with open(os.path.join(HERE, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)


if __name__ == "__main__":
    main()
