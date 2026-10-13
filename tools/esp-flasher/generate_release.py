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
        <app-slug>-<env>-ota.bin  app-only image per variant, for OTA
        manifest.json           shared manifest: every variant whose chipFamily
                                is unique across the app's variants
        manifest-<env>.json     one single-build manifest per variant
                                (multi-variant apps only)
        index.html              web flasher page

Both ESP Web Tools and ESPHome's http_request update pick the *first* build
whose chipFamily matches the connected chip, so no manifest written here may
list two builds with the same chipFamily (COM-427: the espframe 7" Waveshare
S3 kept resolving to the Freenove S3 build). Apps with two variants on the
same chip get them only in their own manifest-<env>.json; the flasher page
then shows one install button per device instead of one auto-detecting
button.
"""

import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
from html import escape

SCRIPT_DIR       = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT        = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))


# ── esptool helpers ──────────────────────────────────────────────────────────

def find_esptool():
    # PlatformIO downloads esptool into its core dir's packages/tool-esptoolpy/.
    # Match scripts/pio.sh's own PLATFORMIO_CORE_DIR resolution (env var if
    # set, else REPO_ROOT/.platformio-core) rather than PlatformIO's unisolated
    # default of ~/.platformio -- this repo deliberately redirects the core
    # dir per project so toolchain caches can't clobber each other.
    core_dir = os.environ.get("PLATFORMIO_CORE_DIR", os.path.join(REPO_ROOT, ".platformio-core"))
    pio_esptool = os.path.join(core_dir, "packages", "tool-esptoolpy", "esptool.py")
    if os.path.exists(pio_esptool):
        return [sys.executable, pio_esptool]
    # Fall back to system esptool module
    result = subprocess.run(
        [sys.executable, "-m", "esptool", "version"],
        capture_output=True, text=True
    )
    if result.returncode == 0:
        return [sys.executable, "-m", "esptool"]
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

    # Partition CSVs use a commented-out header (# Name, Type, SubType, Offset, Size, Flags).
    # Provide fieldnames explicitly so DictReader doesn't treat the first data row as headers.
    fields = ["Name", "Type", "SubType", "Offset", "Size", "Flags"]
    with open(partition_path, newline="") as f:
        reader = csv.DictReader(
            (row for row in f if not row.startswith("#")),
            fieldnames=fields,
        )
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
    esptool    = find_esptool()

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

def duplicate_chip_families(builds):
    seen, dupes = set(), set()
    for b in builds:
        (dupes if b["chipFamily"] in seen else seen).add(b["chipFamily"])
    return dupes


def generate_manifest(app_name, version, builds, output_path):
    """
    builds: list of {"chipFamily": "ESP32-C3", "bin": "app-c3.bin",
                     "ota_bin": "app-c3-ota.bin", "ota_md5": "..."}
    """
    dupes = duplicate_chip_families(builds)
    if dupes:
        # Consumers take the first chipFamily match, so a duplicate would
        # silently flash one board's firmware onto another (COM-427).
        raise ValueError(
            f"{os.path.basename(output_path)} would list more than one build for "
            f"chipFamily {', '.join(sorted(dupes))}; give each such variant its own manifest"
        )
    manifest = {
        "name": app_name,
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [
            {
                "chipFamily": b["chipFamily"],
                "parts": [{"path": b["bin"], "offset": 0}],
                "ota": {"path": b["ota_bin"], "md5": b["ota_md5"]}
            }
            for b in builds
        ]
    }
    with open(output_path, "w") as f:
        json.dump(manifest, f, indent=2)


def md5sum(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()



def variant_manifest_name(env):
    return f"manifest-{env}.json"


def write_manifests(app_name, version, builds, dist_dir):
    """
    Writes the shared manifest.json plus, for a multi-variant app, one
    manifest-<env>.json per variant. Each build needs "env" and "label" keys
    on top of generate_manifest()'s. Returns True when two variants share a
    chipFamily, i.e. the shared manifest can't serve them and the flasher
    page needs per-device buttons.
    """
    dupes = duplicate_chip_families(builds)
    shared = [b for b in builds if b["chipFamily"] not in dupes]
    generate_manifest(app_name, version, shared,
                      os.path.join(dist_dir, "manifest.json"))
    if len(builds) > 1:
        for b in builds:
            generate_manifest(f"{app_name}: {b['label']}", version, [b],
                              os.path.join(dist_dir, variant_manifest_name(b["env"])))
    return bool(dupes)


def install_buttons_html(builds, per_device):
    if not per_device:
        return ('<esp-web-install-button manifest="manifest.json">'
                '</esp-web-install-button>')
    return "\n    ".join(
        f'<div class="device">{escape(b["label"])}</div>\n'
        f'    <esp-web-install-button manifest="{variant_manifest_name(b["env"])}">'
        f'</esp-web-install-button>'
        for b in builds
    )


def copy_flasher_html(app_name, app_slug, version, builds, per_device, output_path):
    template = os.path.join(SCRIPT_DIR, "flasher-template.html")
    with open(template) as f:
        html = f.read()

    html = html.replace("{{APP_NAME}}", app_name)
    html = html.replace("{{APP_SLUG}}", app_slug)
    html = html.replace("{{VERSION}}", version)
    html = html.replace("{{INSTALL_STEP}}",
                        "Click <strong>Install</strong> under your device"
                        if per_device else "Click <strong>Install</strong> below")
    html = html.replace("{{INSTALL_BUTTONS}}", install_buttons_html(builds, per_device))

    with open(output_path, "w") as f:
        f.write(html)


# ── main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--project-dir", required=True)
    parser.add_argument("--name",        required=True, help="Human-readable app name")
    parser.add_argument("--build",       action="append", default=[],
                        metavar="ENV:CHIP[:LABEL]",
                        help="PlatformIO board variant, e.g. c3:ESP32-C3. LABEL names the device on "
                             "the flasher page (defaults to ENV). Repeat for multiple.")
    parser.add_argument("--esphome",     action="append", default=[],
                        metavar="ENV:CHIP:ESPHOME_NAME[:LABEL]",
                        help="ESPHome device variant, e.g. freenove-s3:ESP32-S3:immich-frame-freenove-s3. "
                             "ESPHome already merges bootloader+partitions+app, so unlike --build this just "
                             "copies builds/.esphome/build/ESPHOME_NAME/.pioenvs/ESPHOME_NAME/firmware{.factory,}.bin. "
                             "LABEL names the device on the flasher page (defaults to ENV). Repeat for multiple.")
    parser.add_argument("--version",     default="dev")
    args = parser.parse_args()

    if not args.build and not args.esphome:
        parser.error("at least one --build or --esphome variant is required")

    project_dir = os.path.join(REPO_ROOT, args.project_dir)
    app_slug    = os.path.basename(args.project_dir)
    dist_dir    = os.path.join(REPO_ROOT, "dist", app_slug)
    os.makedirs(dist_dir, exist_ok=True)

    # Parse --build env:CHIP[:label]
    variants = []
    for b in args.build:
        parts = b.split(":", 2)
        if len(parts) < 2:
            parser.error(f"--build must be in ENV:CHIP[:LABEL] format, got: {b}")
        env_name, chip = parts[:2]
        label = parts[2] if len(parts) == 3 else env_name
        variants.append({"env": env_name, "chip": chip, "label": label})

    # Parse --esphome env:CHIP:esphome_name[:label]
    esphome_variants = []
    for e in args.esphome:
        parts = e.split(":", 3)
        if len(parts) < 3:
            parser.error(f"--esphome must be in ENV:CHIP:ESPHOME_NAME[:LABEL] format, got: {e}")
        env_name, chip, esphome_name = parts[:3]
        label = parts[3] if len(parts) == 4 else env_name
        esphome_variants.append({"env": env_name, "chip": chip,
                                 "esphome_name": esphome_name, "label": label})

    multi_variant = (len(variants) + len(esphome_variants)) > 1

    # Merge firmware for each PlatformIO variant
    builds_for_manifest = []
    for v in variants:
        # A single --build variant is a real, permanent category here (a
        # single-board app, per this script's own docstring) — no env suffix.
        suffix   = f"-{v['env']}" if multi_variant else ""
        bin_name = f"{app_slug}{suffix}.bin"
        print(f"==> Merging {v['env']} ({v['chip']})")
        merge_firmware(project_dir, v["env"], v["chip"],
                       os.path.join(dist_dir, bin_name))

        # Raw (unmerged) app image — needed for OTA uploads, which flash only
        # the app partition and choke on a bootloader+partition-table image.
        ota_bin_name = f"{app_slug}{suffix}-ota.bin"
        ota_src = os.path.join(project_dir, ".pio", "build", v["env"], "firmware.bin")
        ota_dest = os.path.join(dist_dir, ota_bin_name)
        shutil.copy(ota_src, ota_dest)
        builds_for_manifest.append({
            "env": v["env"], "label": v["label"],
            "chipFamily": v["chip"], "bin": bin_name,
            "ota_bin": ota_bin_name, "ota_md5": md5sum(ota_dest),
        })

    # Copy already-merged firmware for each ESPHome variant. Unlike --build,
    # always suffix by device env even with only one variant wired in today —
    # ESPHome apps are expected to grow more devices (see espframe's skipped
    # P4), and an unsuffixed name would silently start meaning something
    # different the moment a second device gets added.
    for v in esphome_variants:
        suffix   = f"-{v['env']}"
        bin_name = f"{app_slug}{suffix}.bin"
        pioenvs_dir = os.path.join(project_dir, "builds", ".esphome", "build",
                                    v["esphome_name"], ".pioenvs", v["esphome_name"])
        factory_src = os.path.join(pioenvs_dir, "firmware.factory.bin")
        ota_src     = os.path.join(pioenvs_dir, "firmware.bin")
        if not os.path.exists(factory_src):
            raise RuntimeError(
                f"ESPHome factory binary not found: {factory_src} "
                f"(run the espframe build-{v['env']} target first)"
            )
        print(f"==> Copying ESPHome build for {v['env']} ({v['chip']})")
        shutil.copy(factory_src, os.path.join(dist_dir, bin_name))

        ota_bin_name = f"{app_slug}{suffix}-ota.bin"
        ota_dest = os.path.join(dist_dir, ota_bin_name)
        shutil.copy(ota_src, ota_dest)
        builds_for_manifest.append({
            "env": v["env"], "label": v["label"],
            "chipFamily": v["chip"], "bin": bin_name,
            "ota_bin": ota_bin_name, "ota_md5": md5sum(ota_dest),
        })

    print("==> Generating manifests")
    per_device = write_manifests(args.name, args.version, builds_for_manifest, dist_dir)

    print("==> Copying flasher HTML")
    copy_flasher_html(args.name, app_slug, args.version, builds_for_manifest,
                      per_device, os.path.join(dist_dir, "index.html"))

    print(f"==> Done — dist/{app_slug}/ ({len(variants) + len(esphome_variants)} variant(s))")


if __name__ == "__main__":
    main()
