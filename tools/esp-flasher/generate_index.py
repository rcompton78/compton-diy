#!/usr/bin/env python3
"""
generate_index.py — builds dist/index.html linking to all app flashers.
Run after all per-app release targets have completed.
"""

import argparse
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT   = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", default="dev")
    args = parser.parse_args()

    dist_dir = os.path.join(REPO_ROOT, "dist")
    apps = sorted(
        d for d in os.listdir(dist_dir)
        if os.path.isdir(os.path.join(dist_dir, d))
    )

    cards = ""
    for app in apps:
        name = app.replace("-", " ").title()
        cards += f"""
    <a class="card" href="./{app}/">
      <div class="name">{name}</div>
      <div class="sub">Flash firmware</div>
    </a>"""

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>compton-diy Flasher</title>
  <style>
    *, *::before, *::after {{ box-sizing: border-box; }}
    body {{
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: #0f0f0f; color: #e0e0e0;
      max-width: 640px; margin: 0 auto; padding: 48px 24px;
    }}
    h1 {{ font-size: 1.4rem; margin-bottom: 4px; }}
    .version {{ color: #555; font-size: 0.85rem; margin-bottom: 32px; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr)); gap: 16px; }}
    .card {{
      display: block; text-decoration: none; color: inherit;
      background: #1a1a1a; border: 1px solid #2a2a2a;
      border-radius: 10px; padding: 20px;
      transition: border-color 0.15s;
    }}
    .card:hover {{ border-color: #0070f3; }}
    .name {{ font-weight: 600; margin-bottom: 4px; }}
    .sub {{ font-size: 0.8rem; color: #555; }}
  </style>
</head>
<body>
  <h1>compton-diy Flasher</h1>
  <p class="version">{args.version}</p>
  <div class="grid">{cards}
  </div>
</body>
</html>"""

    with open(os.path.join(dist_dir, "index.html"), "w") as f:
        f.write(html)

    print(f"==> Generated dist/index.html with {len(apps)} app(s)")


if __name__ == "__main__":
    main()
