#!/usr/bin/env python3
"""
generate_release.py — shared ESP32 release tool for the compton-diy monorepo.

Merges PlatformIO build artifacts into a single flashable binary per board
variant, generates an ESP Web Tools manifest (one or many builds), and copies
the flasher HTML into a dist/ folder.

Single-board app:
    python3 tools/esp-flasher/generate_release.py \
        --project-dir apps/cyd-clock \
        --name "CYD Clock" \
        --build cyd:ESP32

Multi-board app:
    python3 tools/esp-flasher/generate_release.py \
        --project-dir apps/bambu-status-bar \
        --name "Bambu Status Bar" \
        --build c3:ESP32-C3 \
        --build s3:ESP32-S3 \
        --build pico32:ESP32

Output:
    dist/<app-slug>/
        <app-slug>-<env>.bin    merged firmware per variant
        manifest.json           ESP Web Tools manifest (all variants)
        index.html              web flasher page
        esp-web-tools.js        bundled locally (no CDN at flash time)
"""

import argparse
import csv
import json
import os
import subprocess
import sys
import urllib.request

SCRIPT_DIR       = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT        = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
ESP_WEB_TOOLS_URL = "https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"


# ── esptool helpers ──────────────────────────────────────────────────────────

def find_pio_python():
    result = subprocess.run(
        ["python3", "-m", "platformio", "system", "info", "--json-output"],
        capture_output=True, text=True
    )
    try:
        return json.loads(result.stdout).get("python_exe", sys.executable)
    except Exception:
        return sys.executable


def find_esptool(pio_python):
    result = subprocess.run(
        [pio_python, "-m", "esptool", "version"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return [pio_python, "-m", "esptool"]
    raise RuntimeError("esptool not found — ensure PlatformIO is installed.")


def get_fs_offset(project_dir, env_name):
    """Return LittleFS/SPIFFS partition offset from the partition CSV."""
    pio_ini = os.path.join(project_dir, "platformio.ini")
    partition_file = "partitions.csv"

    in_env = False
    with open(pio_ini) as f:
        for line in f:
            stripped = line.strip()
            if stripped.startswith(f"[env:{env_name}]"):
                in_env = True
            elif stripped.startswith("[env:") or stripped.startswith("[env]"):
                in_env = False
            if in_env and stripped.startswith("board_build.partitions"):
                partition_file = stripped.split("=", 1)[1].strip()
                break

    partition_path = os.path.join(project_dir, partition_file)
    if not os.path.exists(partition_path):
        return "0x290000"

    with open(partition_path, newline="") as f:
        reader = csv.DictReader(row for row in f if not row.startswith("#"))
        for row in reader:
            name    = row.get("Name", "").strip()
            subtype = row.get("SubType", "").strip()
            offset  = row.get("Offset", "").strip()
            if name in ("spiffs", "littlefs") or subtype in ("spiffs", "littlefs"):
                return offset

    return "0x290000"


def bootloader_offset(chip):
    """ESP32-C3 and ESP32-S3 use 0x0 for bootloader; classic ESP32 uses 0x1000."""
    return "0x0" if chip.upper() in ("ESP32-C3", "ESP32-S3") else "0x1000"


def merge_firmware(project_dir, env_name, chip, output_path):
    build_dir  = os.path.join(project_dir, ".pio", "build", env_name)
    pio_python = find_pio_python()
    esptool    = find_esptool(pio_python)

    bootloader = os.path.join(build_dir, "bootloader.bin")
    partitions = os.path.join(build_dir, "partitions.bin")
    firmware   = os.path.join(build_dir, "firmware.bin")
    fs_bin     = os.path.join(build_dir, "littlefs.bin")
    fs_offset  = get_fs_offset(project_dir, env_name)

    parts = [
        bootloader_offset(chip), bootloader,
        "0x8000",                partitions,
        "0x10000",               firmware,
    ]
    if os.path.exists(fs_bin):
        parts += [fs_offset, fs_bin]

    cmd = esptool + [
        "--chip", chip.lower().replace("-", ""),
        "merge_bin",
        "--fill-flash-size", "4MB",
        "-o", output_path,
    ] + parts

    print("  Merging:", " ".join(cmd))
    result = subprocess.run(cmd)
    if result.returncode != 0:
        raise RuntimeError(f"merge_bin failed for env:{env_name}")


# ── manifest + HTML ──────────────────────────────────────────────────────────

def generate_manifest(app_name, version, builds, output_path):
    """
    builds: list of {"chipFamily": "ESP32-C3", "bin": "app-c3.bin"}
    """
    manifest = {
        "name": app_name,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": b["chipFamily"],
                "parts": [{"path": b["bin"], "offset": 0}]
            }
            for b in builds
        ]
    }
    with open(output_path, "w") as f:
        json.dump(manifest, f, indent=2)


def download_esp_web_tools(dist_dir):
    dest = os.path.join(dist_dir, "esp-web-tools.js")
    if os.path.exists(dest):
        return
    print("==> Downloading esp-web-tools bundle")
    urllib.request.urlretrieve(ESP_WEB_TOOLS_URL, dest)


def copy_flasher_html(app_name, app_slug, version, output_path):
    template = os.path.join(SCRIPT_DIR, "flasher-template.html")
    with open(template) as f:
        html = f.read()

    html = html.replace("{{APP_NAME}}", app_name)
    html = html.replace("{{APP_SLUG}}", app_slug)
    html = html.replace("{{VERSION}}", version)
    html = html.replace("{{MANIFEST}}", "manifest.json")

    with open(output_path, "w") as f:
        f.write(html)


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-dir", required=True)
    parser.add_argument("--name",        required=True, help="Human-readable app name")
    parser.add_argument("--build",       required=True, action="append",
                        metavar="ENV:CHIP",
                        help="Board variant, e.g. c3:ESP32-C3. Repeat for multiple.")
    parser.add_argument("--version",     default="dev")
    args = parser.parse_args()

    project_dir = os.path.join(REPO_ROOT, args.project_dir)
    app_slug    = os.path.basename(args.project_dir)
    dist_dir    = os.path.join(REPO_ROOT, "dist", app_slug)
    os.makedirs(dist_dir, exist_ok=True)

    # Parse --build env:CHIP pairs
    variants = []
    for b in args.build:
        if ":" not in b:
            parser.error(f"--build must be in ENV:CHIP format, got: {b}")
        env_name, chip = b.split(":", 1)
        variants.append({"env": env_name, "chip": chip})

    # Merge firmware for each variant
    builds_for_manifest = []
    for v in variants:
        suffix   = f"-{v['env']}" if len(variants) > 1 else ""
        bin_name = f"{app_slug}{suffix}.bin"
        print(f"==> Merging {v['env']} ({v['chip']})")
        merge_firmware(project_dir, v["env"], v["chip"],
                       os.path.join(dist_dir, bin_name))
        builds_for_manifest.append({"chipFamily": v["chip"], "bin": bin_name})

    print("==> Generating manifest")
    generate_manifest(args.name, args.version, builds_for_manifest,
                      os.path.join(dist_dir, "manifest.json"))

    print("==> Downloading esp-web-tools")
    download_esp_web_tools(dist_dir)

    print("==> Copying flasher HTML")
    copy_flasher_html(args.name, app_slug, args.version,
                      os.path.join(dist_dir, "index.html"))

    print(f"==> Done — dist/{app_slug}/ ({len(variants)} variant(s))")


if __name__ == "__main__":
    main()
