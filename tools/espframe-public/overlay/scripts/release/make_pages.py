#!/usr/bin/env python3
"""Package the compiled 10" firmware builds for a GitHub release and GitHub Pages.

    python3 scripts/release/make_pages.py --version 2026.11.16-12 \
        --release-url https://github.com/<owner>/<repo>/releases/tag/2026.11.16-12

Reads ESPHome's build output for both chip builds (scripts/firmware.sh compile
--chip pre-v3, then --chip v3) and writes, for each build <device>:

    dist/firmware/espframe-<device>.bin       factory image (USB install)
    dist/firmware/espframe-<device>-ota.bin   app image (OTA updates)
    dist/pages/<manifest>                     ESP Web Tools + ESPHome update manifest
    dist/pages/*.bin                          the images, next to the manifests that name them

plus dist/pages/index.html, the browser install page, with one button per build.

The two builds are both ESP32-P4, and ESP Web Tools and ESPHome's update both
take the first build whose chipFamily matches, so each manifest lists exactly
one build. The pre-v3 build keeps manifest.json, which frames installed before
the v3 build existed already poll. The manifests' paths are relative, so a frame
downloads OTA images from the same GitHub Pages site it read the manifest from.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHIP = "ESP32-P4"
APP_NAME = "Espframe for Immich (Guition ESP32-P4 10\")"
TEMPLATE = Path(__file__).resolve().parent / "install-page.html"

# One entry per chip build, in install-page order (v3 first: the pick for a
# new board). The release workflow passes each build the other's
# manifest as its sibling, so keep the manifest names in step with release.yml.
BUILDS = [
    {
        "chip": "v3",
        "device": "guition-esp32-p4-jc8012p4a1-v3",
        "esphome_name": "immich-frame-10inch-v3",
        "manifest": "manifest-guition-esp32-p4-jc8012p4a1-v3.json",
        "button": "Install: Guition ESP32-P4 JC8012P4A1 v3 chip (10 inch)",
    },
    {
        "chip": "pre-v3",
        "device": "guition-esp32-p4-jc8012p4a1",
        "esphome_name": "immich-frame-10inch",
        "manifest": "manifest.json",
        "button": "Install: Guition ESP32-P4 JC8012P4A1 (10 inch)",
    },
]


def md5sum(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def build_dir(esphome_name: str) -> Path:
    return ROOT / "builds" / ".esphome" / "build" / esphome_name / ".pioenvs" / esphome_name


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--release-url", default="")
    parser.add_argument("--out", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    for build in BUILDS:
        for name in ("firmware.factory.bin", "firmware.bin"):
            path = build_dir(build["esphome_name"]) / name
            if not path.exists():
                raise SystemExit(f"{path} not found; run scripts/firmware.sh compile --chip {build['chip']} first")

    firmware_dir = args.out / "firmware"
    pages_dir = args.out / "pages"
    for d in (firmware_dir, pages_dir):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    buttons = []
    for build in BUILDS:
        src = build_dir(build["esphome_name"])
        factory, ota = src / "firmware.factory.bin", src / "firmware.bin"
        bin_name = f"espframe-{build['device']}.bin"
        ota_name = f"espframe-{build['device']}-ota.bin"
        for path, name in ((factory, bin_name), (ota, ota_name)):
            shutil.copy2(path, firmware_dir / name)
            shutil.copy2(path, pages_dir / name)

        ota_entry = {"path": ota_name, "md5": md5sum(ota), "summary": f"Espframe {args.version}"}
        if args.release_url:
            ota_entry["release_url"] = args.release_url
        manifest = {
            "name": APP_NAME,
            "version": args.version,
            "new_install_prompt_erase": True,
            "builds": [
                {
                    "chipFamily": CHIP,
                    "parts": [{"path": bin_name, "offset": 0}],
                    "ota": ota_entry,
                }
            ],
        }
        (pages_dir / build["manifest"]).write_text(json.dumps(manifest, indent=2) + "\n")
        buttons.append(
            f'    <esp-web-install-button manifest="{escape(build["manifest"])}">\n'
            f'      <button slot="activate">{escape(build["button"])}</button>\n'
            '      <span slot="unsupported">Your browser can\'t flash over USB. Use Chrome or Edge on a desktop computer.</span>\n'
            '      <span slot="not-allowed">Flashing needs a secure (https) page.</span>\n'
            "    </esp-web-install-button>"
        )

    html = TEMPLATE.read_text()
    html = html.replace("{{APP_NAME}}", escape(APP_NAME)).replace("{{VERSION}}", escape(args.version))
    html = html.replace("{{RELEASE_URL}}", escape(args.release_url or "#"))
    html = html.replace("{{INSTALL_BUTTONS}}", "\n".join(buttons))
    (pages_dir / "index.html").write_text(html)
    (pages_dir / ".nojekyll").write_text("")
    print(f"dist/ ready for {args.version}")


if __name__ == "__main__":
    main()
