#!/usr/bin/env python3
"""COM-629 basement lights dashboard mock-ups for the Freenove 2.8" (320x240).

Three side-by-side tiles (Office, Board Games, All), landscape, drawn at the
panel's real size, with three header/footer options (L1-L3), and the idle
screen saver: the time floating around a dimmed screen (S1 plain, S2 with a
line saying what's on); L1 and S1 shipped. Reuses COM-533's palette, icons
and SVG helpers.
Writes index.html and svg/<id>-<n>.svg.

Stdlib only: python3 generate.py
"""
import html
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "com-533-mockups"))
from generate import (  # noqa: E402
    AMBER, AMBER_HALO, BG, CARD, CARD_EDGE, FAINT, FONT, INK, OFF_HALO, ON_AMBER, PRESS, QUIET, Svg,
)

W, H = 320, 240

EVENING = dict(office=True, games=False, pressed=None, ha=True, clock="7:42", ampm="PM")
PRESSED = dict(office=False, games=False, pressed="games", ha=True, clock="11:06", ampm="AM")
OFFLINE = dict(office=False, games=False, pressed=None, ha=False, clock="", ampm="")
STATES = [
    ("Evening: Office on", EVENING),
    ("All off, finger on Board Games", PRESSED),
    ("HA disconnected: every toggle --", OFFLINE),
]

TILES = [("office", "desk_lamp", "Office"), ("games", "dice", "Board Games"), ("all", "home_bulb", "All lights")]


def is_on(st, key):
    return st["office"] or st["games"] if key == "all" else st[key]


def colours(on, pressed):
    if pressed:
        return PRESS, INK, INK, "#38414F", INK, 2
    if on:
        return AMBER, ON_AMBER, ON_AMBER, AMBER_HALO, None, 0
    return CARD, INK, QUIET, OFF_HALO, CARD_EDGE, 1


def tiles(g, st, top, bottom):
    h = bottom - top
    for i, (key, icon, name) in enumerate(TILES):
        x, w = 6 + i * 104, 100
        on = st["ha"] and is_on(st, key)
        fill, ink, ic, halo, edge, sw = colours(on, st["pressed"] == key)
        cx = x + w / 2
        g.rect(x, top, w, h, fill, rx=16, stroke=edge, sw=sw)
        hy = top + h * 0.33
        g.circle(cx, hy, 30, halo)
        g.icon(icon, cx, hy, 38, ic)
        g.text(cx, top + h * 0.66, name, 13, ink, 600, "middle")
        label = ("ON" if on else "OFF") if st["ha"] else "--"
        pw = 14 + 9 * len(label)
        py = bottom - 34
        g.rect(cx - pw / 2, py, pw, 22, "#00000033" if on else OFF_HALO, rx=11)
        g.text(cx, py + 15.5, label, 11, ink if on else QUIET, 700, "middle", ls=1.2)


def header(g, st):
    g.text(10, 21, "Basement", 15, INK, 600)
    if st["clock"]:
        g.text(W - 10, 21, f'{st["clock"]} {st["ampm"]}', 14, QUIET, 600, "end")


def footer(g, st):
    if st["ha"]:
        g.text(W / 2, H - 5, "http://192.168.1.58", 10, FAINT, 500, "middle")


def design_l1(g, st):
    g.rect(0, 0, W, H, BG)
    header(g, st)
    tiles(g, st, 32, 232)


def design_l2(g, st):
    g.rect(0, 0, W, H, BG)
    tiles(g, st, 8, 232)


def design_l3(g, st):
    g.rect(0, 0, W, H, BG)
    header(g, st)
    tiles(g, st, 32, 220)
    footer(g, st)


DESIGNS = [
    ("l1", "L1. Header: title and time", design_l1,
     ["A slim header with \"Basement\" and the time, tiles 100×200 below.",
      "The portal address is only on the connecting page and in HA/the router."]),
    ("l2", "L2. Tiles only", design_l2,
     ["No header or footer: the biggest tiles (100×224).",
      "The time is still on the screen saver, which is what's showing most of the day."]),
    ("l3", "L3. Header and portal address footer", design_l3,
     ["Like the 5\": title and time on top, the portal address along the bottom.",
      "Tiles shrink to 100×188 and the address is small at this size."]),
]


# Screen saver: dimmed backlight, the time drifting to a new spot each minute.
SAVER = [
    ("7:42 PM, Office on", dict(clock="7:42", ampm="PM", x=36, y=92, office=True, games=False)),
    ("A minute later, somewhere else", dict(clock="7:43", ampm="PM", x=120, y=170, office=True, games=False)),
    ("11:06 PM, everything off", dict(clock="11:06", ampm="PM", x=70, y=128, office=False, games=False)),
]


def saver_text(g, st, status):
    g.rect(0, 0, W, H, "#000000")
    g.add(f'<text x="{st["x"]}" y="{st["y"]}" font-size="44" font-weight="600" fill="{INK}">{st["clock"]}'
          f'<tspan dx="5" font-size="16" fill="{QUIET}">{st["ampm"]}</tspan></text>')
    if status:
        on = [n for k, n in (("office", "Office"), ("games", "Board Games")) if st[k]]
        words = (" and ".join(on) + " on") if on else "All lights off"
        colour = AMBER if on else FAINT
        g.circle(st["x"] + 5, st["y"] + 22, 4, colour)
        g.text(st["x"] + 15, st["y"] + 27, words, 13, colour if on else QUIET, 600)


DESIGNS_SAVER = [
    ("s1", "S1. Screen saver: just the time", lambda g, st: saver_text(g, st, False),
     ["After the idle timeout the toggles go, the backlight dims, and the time moves to a new spot every minute.",
      "A touch brings the toggles back; that touch never toggles a light."]),
    ("s2", "S2. Screen saver: time and what's on", lambda g, st: saver_text(g, st, True),
     ["S1 plus one line under the time: which rooms are on (amber), or \"All lights off\".",
      "Lets you check the lights from across the room without waking it."]),
]


def standalone(g, dim=False):
    if dim:
        # Approximates the dimmed backlight.
        g.rect(0, 0, W, H, "#000000", opacity=0.45)
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
            f'font-family="{FONT}">' + "".join(g.parts) + "</svg>")


def emit(did, title, fn, notes, states, dim=False):
    shots = []
    for i, (caption, st) in enumerate(states):
        g = Svg()
        fn(g, st)
        svg = standalone(g, dim)
        with open(os.path.join(HERE, "svg", f"{did}-{i + 1}.svg"), "w", encoding="utf-8") as f:
            f.write(svg)
        shots.append(f"<figure>{svg}<figcaption>{html.escape(caption)}</figcaption></figure>")
    lis = "".join(f"<li>{html.escape(n)}</li>" for n in notes)
    return (f'<section><h2>{html.escape(title)}</h2><ul>{lis}</ul>'
            f'<div class="shots">{"".join(shots)}</div></section>')


def main():
    os.makedirs(os.path.join(HERE, "svg"), exist_ok=True)
    sections = [emit(*d, STATES) for d in DESIGNS]
    sections += [emit(*d, SAVER, dim=True) for d in DESIGNS_SAVER]
    page = f"""<!doctype html>
<!-- Generated by generate.py (COM-629). Don't edit by hand: rerun python3 generate.py. -->
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>COM-629 basement lights on the Freenove 2.8": mock-ups</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Montserrat:wght@400;500;600;700&display=swap">
<style>
body{{margin:0;padding:16px;background:#f4f5f7;color:#1b1f24;font:15px/1.45 system-ui,sans-serif}}
h1{{font-size:20px;margin:0 0 4px}} p.lead{{margin:0 0 16px;color:#555}}
section{{background:#fff;border-radius:12px;padding:14px;margin:0 0 16px;box-shadow:0 1px 2px #0001}}
h2{{font-size:17px;margin:0 0 6px}}
.shots{{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:14px;margin-top:10px}}
figure{{margin:0}}
figure svg{{width:100%;height:auto;display:block;border-radius:8px;border:10px solid #111;box-sizing:border-box}}
figcaption{{font-size:12px;color:#666;margin-top:4px}}
ul{{margin:0;padding-left:18px}}
</style></head><body>
<h1>Basement lights on the Freenove 2.8": mock-ups (COM-629)</h1>
<p class="lead">320×240 landscape, drawn at the panel's real resolution, same palette as the 5" (COM-533 P2).
Three side-by-side tiles; L1-L3 differ only in the header and footer. S1/S2 are the idle screen saver.
<strong>L1 and S1 were picked</strong>.</p>
{"".join(sections)}
</body></html>
"""
    with open(os.path.join(HERE, "index.html"), "w", encoding="utf-8") as f:
        f.write(page)


if __name__ == "__main__":
    main()
