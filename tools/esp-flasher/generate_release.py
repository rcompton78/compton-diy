#!/usr/bin/env python3
"""
generate_release.py — shared ESP32 release tool for the compton-diy monorepo.

Merges PlatformIO build artifacts into a single flashable binary, generates
an ESP Web Tools manifest, and copies the flasher HTML into a dist/ folder.

Usage (called by NX release targets):
    python3 tools/esp-flasher/generate_release.py \
        --project-dir apps/cyd-clock \
        --env cyd \
        --chip ESP32 \
        --name "CYD Clock" \
        --version 1.0.0

Output:
    dist/<app-name>/
        <app-name>.bin          merged firmware (bootloader + partitions + app)
        manifest.json           ESP Web Tools manifest
        index.html              web flasher page
"""

import argparse
import csv
import json
import os
import shutil
import subprocess
import sys
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT   = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))


def find_pio_python():
    """Return path to the Python used by PlatformIO (has esptool bundled)."""
    result = subprocess.run(
        ["python3", "-m", "platformio", "system", "info", "--json-output"],
        capture_output=True, text=True
    )
    try:
        info = json.loads(result.stdout)
        return info.get("python_exe", sys.executable)
    except Exception:
        return sys.executable


def find_esptool(pio_python):
    result = subprocess.run(
        [pio_python, "-m", "esptool", "version"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return [pio_python, "-m", "esptool"]
    raise RuntimeError("esptool not found. Ensure PlatformIO is installed.")


def get_fs_offset(project_dir, env_name):
    """Read partition CSV and return the LittleFS/SPIFFS partition offset."""
    pio_ini = os.path.join(project_dir, "platformio.ini")
    partition_file = "partitions.csv"

    # Try to read partition filename from platformio.ini
    with open(pio_ini) as f:
        for line in f:
            if line.strip().startswith("board_build.partitions"):
                partition_file = line.split("=", 1)[1].strip()
                break

    partition_path = os.path.join(project_dir, partition_file)
    if not os.path.exists(partition_path):
        return "0x290000"  # safe default for 4MB flash

    with open(partition_path, newline="") as f:
        reader = csv.DictReader(row for row in f if not row.startswith("#"))
        for row in reader:
            name    = row.get("Name", "").strip()
            subtype = row.get("SubType", "").strip()
            offset  = row.get("Offset", "").strip()
            if name in ("spiffs", "littlefs") or subtype in ("spiffs", "littlefs"):
                return offset

    return "0x290000"


def merge_firmware(project_dir, env_name, chip, output_path):
    build_dir  = os.path.join(project_dir, ".pio", "build", env_name)
    pio_python = find_pio_python()
    esptool    = find_esptool(pio_python)

    # Standard ESP32 flash image layout
    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")

    # App offset varies by chip
    app_offset = "0x10000"
    if chip.upper() in ("ESP32-C3", "ESP32-S3"):
        app_offset = "0x10000"

    fs_offset = get_fs_offset(project_dir, env_name)
    fs_bin    = os.path.join(build_dir, "littlefs.bin")

    parts = [
        "0x1000", bootloader,
        "0x8000", partitions,
        app_offset, firmware,
    ]

    if os.path.exists(fs_bin):
        parts += [fs_offset, fs_bin]

    cmd = esptool + [
        "--chip", chip.lower().replace("-", ""),
        "merge_bin",
        "--fill-flash-size", "4MB",
        "-o", output_path,
    ] + parts

    print("Merging:", " ".join(cmd))
    result = subprocess.run(cmd)
    if result.returncode != 0:
        raise RuntimeError("merge_bin failed")


def generate_manifest(app_name, version, chip, bin_filename, output_path):
    manifest = {
        "name": app_name,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{
            "chipFamily": chip.upper(),
            "parts": [{"path": bin_filename, "offset": 0}]
        }]
    }
    with open(output_path, "w") as f:
        json.dump(manifest, f, indent=2)


ESP_WEB_TOOLS_URL = "https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"

def download_esp_web_tools(dist_dir):
    dest = os.path.join(dist_dir, "esp-web-tools.js")
    if os.path.exists(dest):
        return
    print("==> Downloading esp-web-tools bundle")
    urllib.request.urlretrieve(ESP_WEB_TOOLS_URL, dest)


def copy_flasher_html(app_name, app_slug, version, manifest_filename, output_path):
    template = os.path.join(SCRIPT_DIR, "flasher-template.html")
    with open(template) as f:
        html = f.read()

    html = html.replace("{{APP_NAME}}", app_name)
    html = html.replace("{{APP_SLUG}}", app_slug)
    html = html.replace("{{VERSION}}", version)
    html = html.replace("{{MANIFEST}}", manifest_filename)

    with open(output_path, "w") as f:
        f.write(html)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-dir", required=True)
    parser.add_argument("--env",         required=True)
    parser.add_argument("--chip",        required=True, help="ESP32 | ESP32-C3 | ESP32-S3")
    parser.add_argument("--name",        required=True, help="Human-readable app name")
    parser.add_argument("--version",     default="dev")
    args = parser.parse_args()

    project_dir = os.path.join(REPO_ROOT, args.project_dir)
    app_slug    = os.path.basename(args.project_dir)
    dist_dir    = os.path.join(REPO_ROOT, "dist", app_slug)
    bin_name    = f"{app_slug}.bin"
    os.makedirs(dist_dir, exist_ok=True)

    print(f"==> Merging firmware for {args.name} ({args.chip})")
    merge_firmware(project_dir, args.env, args.chip, os.path.join(dist_dir, bin_name))

    print("==> Generating manifest")
    generate_manifest(args.name, args.version, args.chip, bin_name,
                      os.path.join(dist_dir, "manifest.json"))

    print("==> Downloading esp-web-tools")
    download_esp_web_tools(dist_dir)

    print("==> Copying flasher HTML")
    copy_flasher_html(args.name, app_slug, args.version, "manifest.json",
                      os.path.join(dist_dir, "index.html"))

    print(f"==> Done. Output: dist/{app_slug}/")


if __name__ == "__main__":
    main()
