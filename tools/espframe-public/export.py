#!/usr/bin/env python3
"""Export apps/espframe as the public, 10"-only espframe repo (COM-628).

    python3 tools/espframe-public/export.py <dest-checkout> [--commit]

compton-diy is the only source of espframe code; this copies it one way into a
checkout of the public repo (rcompton78/espframe), which friends fork to build,
flash and update their own Guition ESP32-P4 10" frames. It:

  1. empties <dest> (everything but .git) and copies apps/espframe's tracked
     files to its root, leaving out the other boards, the 7" stand, Nx targets
     and internal notes (EXCLUDE);
  2. cuts the product contract down to the Guition, with no default update
     manifest (a local build never checks for updates; the public repo's
     release workflow bakes in the fork's own GitHub Pages manifest), and
     regenerates the files built from it;
  3. applies text patches for docs that talk about the other boards or
     compton-diy (PATCHES; each must match exactly once, so a source change
     that breaks one fails here rather than shipping stale text);
  4. copies overlay/ on top: the public repo's README, workflows and build
     scripts, which exist only there;
  5. scans the result for secrets and private details, then runs the checks
     the public CI runs. Any hit or failure stops the export.

With --commit it commits the result in <dest> as "Sync from compton-diy <sha>".
Pushing is left to you. Never edit the public repo's files by hand: change
apps/espframe or overlay/ here and export again.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
SRC = REPO / "apps" / "espframe"
OVERLAY = HERE / "overlay"

DEVICE_SLUG = "immich-frame"  # the Guition ESP32-P4 JC8012P4A1 in product/contract/devices.json

# Paths relative to apps/espframe. A trailing slash excludes a whole directory.
EXCLUDE = [
    "devices/freenove-s3/",
    "devices/waveshare-esp32-s3-touch-lcd-7/",
    "builds/freenove-s3.yaml",
    "builds/freenove-s3.factory.yaml",
    "builds/waveshare-esp32-s3-touch-lcd-7.yaml",
    "builds/waveshare-esp32-s3-touch-lcd-7.factory.yaml",
    "hardware/stand/",
    "hardware/stand-10/project.json",
    "project.json",
    "HANDOFF.md",
    ".agents/",
    ".github/",
    "renovate.json",
    "docs/phase-1-product-metadata.md",
    "docs/phase-3-reset-architecture.md",
    "docs/phase-4-release-proven-architecture.md",
    "docs/reset-architecture-v2.md",
    "docs/tech-debt-cleanup-backlog.md",
    "docs/roadmap.md",
    "README.md",  # replaced by overlay/README.md
]

GITIGNORE_EXTRA = """
# Public repo (tools/espframe-public in the source repo)
.env
dist/
"""

# (file, old, new): old must occur exactly once in the copied file.
PATCHES: list[tuple[str, str, str]] = [
    # Builds compile only this repo's components/ (the builds/*.yaml entry points
    # add them as a local source). The source repo's device.yaml also names a git
    # copy for ESPHome-dashboard package installs, which every build would clone.
    (
        "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml",
        '  espframe_component_url: "https://github.com/jtenniswood/espframe"\n'
        '  espframe_component_ref: "main"\n',
        "",
    ),
    (
        "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml",
        "# -----------------------------------------------------------------------------\n"
        "# External components (espframe: gsl3680, remote_image, espframe)\n"
        "# -----------------------------------------------------------------------------\n"
        "external_components:\n"
        "  - source:\n"
        "      type: git\n"
        "      url: ${espframe_component_url}\n"
        "      ref: ${espframe_component_ref}\n"
        "      path: components\n"
        "    components: [gsl3680, remote_image, espframe]\n"
        "    refresh: 0s\n\n",
        "# External components (gsl3680, remote_image, espframe) come from this repo's\n"
        "# components/, added by each entry point (builds/*.yaml, dev.yaml).\n\n",
    ),
    (
        "devices/guition-esp32-p4-jc8012p4a1/dev.yaml",
        '  espframe_component_url: "../.."\n  espframe_component_ref: "HEAD"\n',
        "",
    ),
    (
        "devices/guition-esp32-p4-jc8012p4a1/dev.yaml",
        "packages:\n  espframe: !include packages.yaml",
        "packages:\n  espframe: !include packages.yaml\n\n"
        "external_components:\n"
        "  - source:\n"
        "      type: local\n"
        "      path: ../../components\n"
        "    components: [gsl3680, remote_image, espframe]",
    ),
    (
        "docs/manual-setup.md",
        "# ESPHome Manual Setup for Espframe\n\n",
        "# ESPHome Manual Setup for Espframe\n\n"
        "> **This page installs the original project.** The package method below builds "
        "[jtenniswood/espframe](https://github.com/jtenniswood/espframe) from the ESPHome dashboard. "
        "To build this repository's code, use `scripts/firmware.sh` as described in the README.\n\n",
    ),
    (
        "docs/screen-settings.md",
        " The small Freenove S3 screen is natively portrait, so on it 90 and 270 are landscape.",
        "",
    ),
    (
        "common/addon/firmware_update.yaml",
        "  # The shared manifest every device used to poll by default. It can't list\n"
        "  # two builds for one chipFamily (ESPHome takes the first match, so the 7\"\n"
        "  # Waveshare installed the Freenove S3 build, COM-427), so each device now\n"
        "  # defaults to its own manifest-<device>.json instead. A saved URL survives\n"
        "  # reflashes, so on_boot below moves a device off this one.\n",
        "  # A saved manifest URL survives reflashes; on_boot moves a device that\n"
        "  # still has this one saved onto firmware_manifest_url. Empty: never.\n",
    ),
    (
        "docs/webserver/src/app.template.ts",
        "  // A quarter turn from the board's native orientation: portrait on the 7\"\n"
        "  // and 10\". The firmware derives the real layout from the rotated display\n"
        "  // size (screen_layout_portrait), which also covers the natively portrait\n"
        "  // Freenove S3, so this only drives the web UI's pairing hint.",
        "  // A quarter turn from the board's native orientation: portrait on the\n"
        "  // 10\". The firmware derives the real layout from the rotated display\n"
        "  // size (screen_layout_portrait), so this only drives the web UI's\n"
        "  // pairing hint.",
    ),
    (
        "docs/manual-setup.md",
        'ntp_server_1: "172.20.32.1"\nntp_server_2: "172.20.41.1"\nntp_server_3: "172.20.32.1"',
        'ntp_server_1: "time.cloudflare.com"\nntp_server_2: "time.google.com"\nntp_server_3: "pool.ntp.org"',
    ),
    (
        "common/addon/screen_rotation.yaml",
        "(the Freenove S3 is natively",
        "(a natively portrait panel is",
    ),
    (
        "hardware/stand-10/README.md",
        ' It follows the 7" stand in [`../stand`](../stand/README.md) (COM-363 and its follow-ups) and reuses its measured fits, cable and jack.',
        ' It follows an earlier 7" stand design and reuses its measured fits, cable and jack.',
    ),
    (
        "hardware/stand-10/README.md",
        "Every merge to `master` that changes the stand attaches `espframe-stand-10-<version>.tar.gz` to that build's [GitHub release](https://github.com/rcompton78/compton-diy/releases).",
        "Every release built by this repo's Actions attaches `espframe-stand-10-<version>.tar.gz` to the GitHub release (see the main README).",
    ),
    (
        "hardware/stand-10/README.md",
        "```bash\npnpm nx run espframe-stand-10:cad\n```",
        "```bash\nhardware/stand-10/setup.sh\ncd hardware/stand-10 && .venv/bin/python -m stand10.build\n```",
    ),
    (
        "hardware/stand-10/README.md",
        "This creates (or refreshes) a local Python venv",
        "`setup.sh` creates (or refreshes) a local Python venv",
    ),
    (
        "hardware/stand-10/README.md",
        "`pnpm nx run espframe-stand-10:release` runs `cad` and packs the tarball into `dist/espframe-stand-10/` (`scripts/pack-hardware-release.sh`, which refuses to pack on any `FAIL`). On merge to `master`, `.github/workflows/release.yml` runs it on the self-hosted runner, with the venv cached outside the checkout.",
        "`scripts/release/pack_stand.sh` packs the tarball into `dist/` and refuses to pack on any `FAIL`; `.github/workflows/release.yml` runs both on every release.",
    ),
    (
        "hardware/stand-10/setup.sh",
        " build123d's OCP wheels are\n# ~150 MB, so `pnpm install` never runs this; only the cad/release targets do (CI's\n# release workflow runs it on the self-hosted runner, where the venv is cached).\n#\n# On that runner .venv is a symlink to a persistent dir outside the checkout, so\n# resolve it first: `python -m venv` refuses a symlinked target, and `rm -rf` on\n# the link would only remove the link.",
        " build123d's OCP wheels are\n# ~150 MB. If .venv is a symlink, it's resolved first: `python -m venv` refuses a\n# symlinked target, and `rm -rf` on the link would only remove the link.",
    ),
    (
        "hardware/stand-10/.gitignore",
        "rebuild with `pnpm nx run espframe-stand-10:cad`.",
        "rebuild with `.venv/bin/python -m stand10.build`.",
    ),
]

# Whole sections to drop: (file, start marker, end marker); the end marker is kept.
DROP_SECTIONS: list[tuple[str, str, str]] = [
    (
        "docs/firmware-update.md",
        "OTA and HTTP updates from GitHub. Each device checks",
        "During OTA updates, Espframe keeps",
    ),
    (
        "docs/troubleshooting.md",
        "## Wrong Firmware After an Update\n",
        "## WiFi Setup Problems\n",
    ),
]
FIRMWARE_UPDATE_INTRO = """OTA and HTTP updates from GitHub. A frame flashed from a fork's install page checks that fork's own manifest, `https://<owner>.github.io/<repo>/manifest.json`, which the fork's release workflow publishes to GitHub Pages. Releases from anyone else's fork, including the one you forked from, never reach it. A local build has no manifest URL, so it never checks for updates unless you set one (the README's `FIRMWARE_MANIFEST_URL`, or **Stable Manifest URL** below). Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

"""

# Private-detail scan. Each hit fails the export unless the line matches ALLOW.
SCAN = [
    (re.compile(r"compton-diy|compton-apps|linear\.app|/hdd/|home-diy|homelab|bytesafe|bitwarden|\bbws\b", re.I), "private name or path"),
    (re.compile(r"\b(?:10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3})\b"), "private IP"),
    (re.compile(r"\b[\w.+-]+@(?:gmail|outlook|hotmail|icloud|proton)\.\w+", re.I), "personal email"),
    (re.compile(r"^\s*(?:wifi_)?(?:ssid|password|api_key|api_encryption_key|immich_api_key)\s*:\s*(?![\"']?(?:\$\{|!secret|\s*$|[\"']))[\"']?[^\s#]{4,}", re.I | re.M), "literal credential"),
    (re.compile(r"freenove|waveshare|stand/README", re.I), "other board"),
    (re.compile(r"(?:ghp|github_pat|gho|ghs)_[A-Za-z0-9_]{20,}|xox[bp]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}"), "token"),
]
ALLOW = {
    "private IP": [
        re.compile(r"192\.168\.4\.1\b"),  # the setup hotspot's captive portal
        re.compile(r"192\.168\.1\.30\b"),  # docs and test example Immich host
        re.compile(r"192\.168\.0\.1:2283"),  # web UI placeholder Immich URL
    ],
}
# Vendored third-party source credits its authors by email.
ALLOW_PATHS = {"personal email": ("components/libwebp-esp32/",)}
TEXT_SUFFIXES = {".md", ".yaml", ".yml", ".json", ".py", ".sh", ".ts", ".js", ".mjs", ".mts", ".css", ".html", ".txt", ".h", ".cpp", ".c", ".toml", ".cfg", ""}

# The checks that pass on the exported tree; overlay/.github/workflows/build.yml runs the same list.
CHECKS = [
    "check:schema",
    "check:generated",
    "check:ownership",
    "check:budgets",
    "check:firmware-fields",
    "webserver:typecheck",
    "test:web-compat",
    "test:web-modules",
    "test:web-smoke-cli",
    "test:helpers",
]


def die(msg: str) -> None:
    print(f"export: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list[str], cwd: Path, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, check=True, **kw)


def tracked_files() -> list[str]:
    out = subprocess.run(
        ["git", "ls-files", "-z", "--", "."], cwd=SRC, check=True, capture_output=True
    ).stdout.decode()
    return [p for p in out.split("\0") if p]


def excluded(path: str) -> bool:
    for ex in EXCLUDE:
        if ex.endswith("/") and path.startswith(ex):
            return True
        if path == ex:
            return True
    return False


def clear_dest(dest: Path) -> None:
    for child in dest.iterdir():
        if child.name == ".git":
            continue
        if child.is_dir() and not child.is_symlink():
            shutil.rmtree(child)
        else:
            child.unlink()


def copy_tree(dest: Path) -> None:
    for rel in tracked_files():
        if excluded(rel):
            continue
        src = SRC / rel
        if not src.exists():  # deleted in the working tree but not committed
            continue
        out = dest / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)


def filter_contract(dest: Path) -> None:
    path = dest / "product" / "contract" / "devices.json"
    devices = json.loads(path.read_text())
    keep = [d for d in devices if d.get("slug") == DEVICE_SLUG]
    if len(keep) != 1:
        die(f"expected one device with slug {DEVICE_SLUG!r} in devices.json, found {len(keep)}")
    device = keep[0]
    subs = device.get("package_substitutions")
    if not isinstance(subs, dict) or "firmware_manifest_url" not in subs:
        die("the Guition device has no package_substitutions.firmware_manifest_url to clear")
    subs["firmware_manifest_url"] = ""
    path.write_text(json.dumps(keep, indent=2) + "\n")

    fw = dest / "common" / "addon" / "firmware_update.yaml"
    text = fw.read_text()
    for key in ("firmware_manifest_url", "legacy_firmware_manifest_url"):
        new, n = re.subn(rf'^(  {key}: )"[^"]*"$', r'\1""', text, flags=re.M)
        if n != 1:
            die(f"common/addon/firmware_update.yaml: expected one {key} substitution, found {n}")
        text = new
    fw.write_text(text)


def apply_patches(dest: Path) -> None:
    for rel, old, new in PATCHES:
        path = dest / rel
        text = path.read_text()
        count = text.count(old)
        if count != 1:
            die(f"patch for {rel} matched {count} times (expected 1): {old[:80]!r}")
        path.write_text(text.replace(old, new))
    for rel, start, end in DROP_SECTIONS:
        path = dest / rel
        text = path.read_text()
        i, j = text.find(start), text.find(end)
        if i < 0 or j < i or text.count(start) != 1:
            die(f"section {start[:40]!r}..{end[:40]!r} not found once in {rel}")
        replacement = FIRMWARE_UPDATE_INTRO if rel == "docs/firmware-update.md" else ""
        path.write_text(text[:i] + replacement + text[j:])
    # Issue-tracker links point at a private workspace; keep the ticket ids, drop the URLs.
    for path in text_files(dest):
        text = path.read_text()
        new = re.sub(r" ?\(https://linear\.app/[^)\s]*\)", "", text)
        new = re.sub(r"https://linear\.app/\S*", "", new)
        if new != text:
            path.write_text(new)
    with (dest / ".gitignore").open("a") as f:
        f.write(GITIGNORE_EXTRA)


def copy_overlay(dest: Path) -> None:
    for src in sorted(OVERLAY.rglob("*")):
        if src.is_dir() or "__pycache__" in src.parts:
            continue
        out = dest / src.relative_to(OVERLAY)
        out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, out)


def regenerate(dest: Path) -> None:
    # The web bundle needs esbuild; borrow the source's node_modules for the run.
    modules = dest / "node_modules"
    if not (SRC / "node_modules").is_dir():
        die("apps/espframe/node_modules is missing; run `pnpm nx run espframe:install` first")
    modules.symlink_to(SRC / "node_modules")
    try:
        run([sys.executable, "scripts/generate_assets.py"], dest)
    finally:
        modules.unlink()


def text_files(dest: Path):
    for path in dest.rglob("*"):
        if ".git" in path.parts or "node_modules" in path.parts or not path.is_file():
            continue
        if path.suffix.lower() in TEXT_SUFFIXES:
            yield path


def scan(dest: Path) -> None:
    hits = []
    for path in text_files(dest):
        rel = path.relative_to(dest)
        if path.name == "secrets.yaml":
            hits.append(f"{rel}: secrets.yaml must never be exported")
            continue
        try:
            text = path.read_text()
        except UnicodeDecodeError:
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            for pattern, what in SCAN:
                for m in pattern.finditer(line):
                    if any(a.search(line) for a in ALLOW.get(what, [])):
                        continue
                    if str(rel).startswith(ALLOW_PATHS.get(what, ())):
                        continue
                    hits.append(f"{rel}:{lineno}: {what}: {line.strip()[:160]}")
    if hits:
        print("\n".join(hits), file=sys.stderr)
        die(f"private-detail scan found {len(hits)} hit(s); fix the source or PATCHES and export again")


def check(dest: Path) -> None:
    modules = dest / "node_modules"
    modules.symlink_to(SRC / "node_modules")
    try:
        failed = []
        for name in CHECKS:
            r = subprocess.run(["npm", "run", "-s", name], cwd=dest, capture_output=True, text=True)
            if r.returncode != 0:
                failed.append(name)
                sys.stderr.write(r.stdout[-2000:] + r.stderr[-2000:])
        if failed:
            die(f"checks failed in the export: {', '.join(failed)}")
    finally:
        modules.unlink()


def commit(dest: Path) -> None:
    sha = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=REPO, check=True, capture_output=True, text=True
    ).stdout.strip()
    dirty = subprocess.run(
        ["git", "status", "--porcelain", "--", "apps/espframe", "tools/espframe-public"],
        cwd=REPO, check=True, capture_output=True, text=True,
    ).stdout.strip()
    if dirty:
        die("apps/espframe or tools/espframe-public has uncommitted changes; commit them before --commit")
    run(["git", "add", "-A"], dest)
    if not subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=dest).returncode:
        print("export: nothing changed since the last sync")
        return
    run(["git", "commit", "-q", "-m", f"Sync from compton-diy {sha}"], dest)
    print(f"export: committed in {dest}; review it, then push")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("dest", type=Path, help="a checkout of the public repo (or an empty dir)")
    parser.add_argument("--commit", action="store_true", help="commit the result in dest")
    args = parser.parse_args()
    dest: Path = args.dest.resolve()

    if dest == REPO or REPO in dest.parents:
        die("dest must be outside this repo")
    dest.mkdir(parents=True, exist_ok=True)
    if args.commit and not (dest / ".git").exists():
        die("--commit needs dest to be a git checkout")

    clear_dest(dest)
    copy_tree(dest)
    filter_contract(dest)
    apply_patches(dest)
    copy_overlay(dest)
    regenerate(dest)
    scan(dest)
    check(dest)
    print(f"export: {dest} is ready")
    if args.commit:
        commit(dest)


if __name__ == "__main__":
    main()
