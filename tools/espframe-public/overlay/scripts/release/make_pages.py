#!/usr/bin/env python3
"""Package a compiled 10" firmware build for a GitHub release and GitHub Pages.

    python3 scripts/release/make_pages.py --version 2026.11.16-12 \
        --release-url https://github.com/<owner>/<repo>/releases/tag/2026.11.16-12

Reads ESPHome's build output for builds/guition-esp32-p4-jc8012p4a1.factory.yaml
and writes:

    dist/firmware/espframe-guition-esp32-p4-jc8012p4a1.bin       factory image (USB install)
    dist/firmware/espframe-guition-esp32-p4-jc8012p4a1-ota.bin   app image (OTA updates)
    dist/pages/manifest.json   ESP Web Tools + ESPHome update manifest
    dist/pages/index.html      browser install page
    dist/pages/*.bin           the two images, next to the manifest that names them

The manifest's paths are relative, so the frame downloads OTA images from the
same GitHub Pages site it read the manifest from.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ESPHOME_NAME = "immich-frame-10inch"
DEVICE = "guition-esp32-p4-jc8012p4a1"
CHIP = "ESP32-P4"
APP_NAME = "Espframe for Immich (Guition ESP32-P4 10\")"
BUILD_DIR = ROOT / "builds" / ".esphome" / "build" / ESPHOME_NAME / ".pioenvs" / ESPHOME_NAME
TEMPLATE = Path(__file__).resolve().parent / "install-page.html"


def md5sum(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--release-url", default="")
    parser.add_argument("--out", type=Path, default=ROOT / "dist")
    args = parser.parse_args()

    factory = BUILD_DIR / "firmware.factory.bin"
    ota = BUILD_DIR / "firmware.bin"
    for path in (factory, ota):
        if not path.exists():
            raise SystemExit(f"{path} not found; run scripts/firmware.sh compile first")

    firmware_dir = args.out / "firmware"
    pages_dir = args.out / "pages"
    for d in (firmware_dir, pages_dir):
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)

    bin_name = f"espframe-{DEVICE}.bin"
    ota_name = f"espframe-{DEVICE}-ota.bin"
    for src, name in ((factory, bin_name), (ota, ota_name)):
        shutil.copy2(src, firmware_dir / name)
        shutil.copy2(src, pages_dir / name)

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
    (pages_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    html = TEMPLATE.read_text()
    html = html.replace("{{APP_NAME}}", escape(APP_NAME)).replace("{{VERSION}}", escape(args.version))
    html = html.replace("{{RELEASE_URL}}", escape(args.release_url or "#"))
    (pages_dir / "index.html").write_text(html)
    (pages_dir / ".nojekyll").write_text("")
    print(f"dist/ ready for {args.version}")


if __name__ == "__main__":
    main()
