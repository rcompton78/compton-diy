#!/usr/bin/env python3
"""Export apps/espframe as the public, 10"-only espframe repo (COM-628).

    python3 tools/espframe-public/export.py <dest-checkout> [--commit]

compton-diy is the only source of espframe code; this copies it one way into a
checkout of the public repo (rcompton78/espframe), which friends fork to build,
flash and update their own Guition ESP32-P4 10" frames. It:

  0. refuses a <dest> that isn't empty, an espframe checkout or an earlier
     export (and, with --commit, a source tree with uncommitted changes);
  1. empties <dest> (everything but .git) and copies apps/espframe's tracked
     files to its root, leaving out the other boards, the 7" stand, Nx targets
     and internal notes (EXCLUDE);
  2. cuts the product contract down to the Guition, with no default update
     manifest (a local build doesn't check for updates; the public repo's
     release workflow bakes in the fork's own GitHub Pages manifest);
  3. applies text patches for docs that talk about the other boards or the
     source repo (PATCHES; each must match exactly once, so a source change
     that breaks one fails here rather than shipping stale text) and rewrites
     the docs' site-root links into links GitHub can follow;
  4. copies overlay/'s tracked files on top: the public repo's README,
     workflows and build scripts, which exist only there;
  5. regenerates the files built from the contract, scans the result for
     secrets and private details, then runs package.json's check:ci. Any hit
     or failure stops the export.

With --commit it commits the result in <dest> as "Sync <source sha>".
Pushing is left to you. Never edit the public repo's files by hand: change
apps/espframe or overlay/ here and export again.
"""

from __future__ import annotations

import argparse
import json
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
    "docs/testing.md",  # the source repo's CI and setup flows
    "docs/repository-governance.md",  # the original project's branch rules
    "docs/.vitepress/",  # the original project's docs site; the docs are read on GitHub here
    "README.md",  # replaced by overlay/README.md
]

GITIGNORE_EXTRA = """
# Public repo (tools/espframe-public in the source repo)
.env
dist/
"""

# (file, old, new): old must occur exactly once in the copied file.
PATCHES: list[tuple[str, str, str]] = [
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
    (
        "hardware/stand-10/requirements.txt",
        "Rebuild the venv with `pnpm nx run espframe-stand-10:cad-setup`.",
        "Rebuild the venv with `hardware/stand-10/setup.sh`.",
    ),
    (
        "hardware/stand-10/stand10/params.py",
        "the 7\" stand's printed and measured fits (apps/espframe/hardware/stand).",
        "an earlier 7\" stand's printed and measured fits.",
    ),
    (
        "hardware/stand-10/docs/background.md",
        "the lessons carried over from the 7\" stand in `../stand`.",
        "the lessons carried over from an earlier 7\" stand.",
    ),
    (
        "hardware/stand-10/README.md",
        "the frame is wood-look PLA (the 7\" README's wood-look notes apply).",
        "the frame is wood-look PLA.",
    ),
    (
        "scripts/check_timezones.py",
        'f"run scripts/setup.sh (installs it into apps/espframe/.venv, which "\n'
        '    f"npm run test:timezones uses), or pip install -r {REQUIREMENTS_PATH} in a venv"',
        'f"pip install -r {REQUIREMENTS_PATH} into .venv (which npm run "\n'
        '    f"test:timezones uses) or another venv"',
    ),
    (
        "scripts/requirements-timezones.txt",
        "# (and fix any zone the checker then flags) when the weekly tzdata-drift\n"
        "# workflow reports a newer release with real-world rule changes.",
        "# (and fix any zone the checker then flags) when a newer tzdata release\n"
        "# has real-world rule changes.",
    ),
    (
        "docs/screen-settings.md",
        "On the 7\" and 10\" models, 0 and 180 are landscape",
        "On the 10\" model, 0 and 180 are landscape",
    ),
    (
        "docs/home-assistant.md",
        "portrait on the 7\" and 10\")",
        "portrait on the 10\")",
    ),
    (
        "builds/guition-esp32-p4-jc8012p4a1.factory.yaml",
        "# Compiled by CI (see .github/workflows/release.yml) — not used locally.\n"
        "# For local development, see devices/guition-esp32-p4-jc8012p4a1/esphome.yaml.",
        "# Built by scripts/firmware.sh, locally and in .github/workflows/*.yml.\n"
        "# devices/guition-esp32-p4-jc8012p4a1/dev.yaml is a local alternative that\n"
        "# takes Wi-Fi from secrets.yaml.",
    ),
    (
        "common/addon/firmware_update.yaml",
        "        # firmware_version defaults to \"dev\" unless scripts/espframe-esphome.sh\n"
        "        # threads a real RELEASE_VERSION in via -s (see COM-279) -- an",
        "        # firmware_version defaults to \"dev\" unless scripts/firmware.sh\n"
        "        # threads a real RELEASE_VERSION in via -s -- an",
    ),
    (
        "package.json",
        '    "docs:dev": "vitepress dev docs",\n'
        '    "docs:build": "vitepress build docs",\n'
        '    "docs:preview": "vitepress preview docs"\n',
        "",
    ),
    (
        "package.json",
        '    "webserver:typecheck": "tsc --noEmit --strict --target ES2018 --lib ES2018,DOM --skipLibCheck docs/webserver/src/web_contracts.ts",\n',
        '    "webserver:typecheck": "tsc --noEmit --strict --target ES2018 --lib ES2018,DOM --skipLibCheck docs/webserver/src/web_contracts.ts"\n',
    ),
    (
        "package.json",
        " && npm run docs:build\"",
        "\"",
    ),
    (
        "docs/install.md",
        "<EspInstallButton />",
        "Open your fork's install page, `https://<owner>.github.io/<repo>/` (see the README), and click the **Install** button for your board there.",
    ),
    (
        "docs/install.md",
        "Connect the display via USB-C, then click the install button below.",
        "Connect the display via USB-C.",
    ),
    (
        "docs/install.md",
        "Click **Install Espframe for Immich**, choose",
        "Click the **Install** button for your board's chip, choose",
    ),
    (
        "docs/install.md",
        "The button above installs only the original build. The installer at `https://rcompton78.github.io/compton-diy/espframe/` has two 10\" buttons, because",
        "The install page has two buttons, because",
    ),
    # The v3-chip build (COM-690, exported since COM-691). Its compton-diy
    # manifest URLs go; scripts/firmware.sh sets the fork's own with -s.
    (
        "builds/guition-esp32-p4-jc8012p4a1-v3.factory.yaml",
        "# Compiled by CI (see .github/workflows/release.yml).\n",
        "# Built by scripts/firmware.sh --chip v3, locally and in .github/workflows/*.yml.\n",
    ),
    (
        "builds/guition-esp32-p4-jc8012p4a1-v3.factory.yaml",
        '  # Its own manifest: the pre-v3 build shares the ESP32-P4 chip family, and\n'
        '  # OTA takes the first chipFamily match (COM-427).\n'
        '  firmware_manifest_url: "https://rcompton78.github.io/compton-diy/espframe/manifest-guition-esp32-p4-jc8012p4a1-v3.json"\n'
        '  # A pre-v3 frame\'s backup restored onto this board carries the pre-v3\n'
        '  # manifest, whose image can\'t boot a v3 chip; on_boot moves it onto ours.\n'
        '  sibling_firmware_manifest_url: "https://rcompton78.github.io/compton-diy/espframe/manifest-guition-esp32-p4-jc8012p4a1.json"\n',
        '  # Both empty here, like every local build. The release workflow sets this\n'
        '  # build\'s own manifest (the pre-v3 build shares the ESP32-P4 chip family,\n'
        '  # and OTA takes the first chipFamily match) and, as the sibling, the pre-v3\n'
        '  # manifest, which a pre-v3 frame\'s backup restored onto this board carries\n'
        '  # and whose image can\'t boot a v3 chip; on_boot moves it onto ours.\n'
        '  firmware_manifest_url: ""\n'
        '  sibling_firmware_manifest_url: ""\n',
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
        "## 10\" Screen Stays Black or Keeps Rebooting\n",
    ),

]
FIRMWARE_UPDATE_INTRO = """OTA and HTTP updates from GitHub. A frame flashed from a fork's install page checks that fork's own manifest, which the fork's release workflow publishes to GitHub Pages: `https://<owner>.github.io/<repo>/manifest.json` for the original 10" build, and `https://<owner>.github.io/<repo>/manifest-guition-esp32-p4-jc8012p4a1-v3.json` for the v3 chip build. Each lists a single build: both are ESP32-P4, ESPHome installs the first build whose chip family matches, and neither build boots on the other's chip. A frame from the install page whose saved manifest URL names the other build's manifest (from a restored backup, say) is moved back onto its own at boot, as is a local build given one of the fork's manifest URLs. Releases from anyone else's fork, including the one you forked from, never reach it. A local build has no manifest URL, so it never checks for updates unless you set one (the README's `FIRMWARE_MANIFEST_URL`, or **Stable Manifest URL** below). Controls: device web UI at `http://<device-ip>/` under **Firmware** (and in Home Assistant).

"""

# Private-detail scan, over every non-binary file's text and every file's path.
# A hit fails the export unless the matched text is in ALLOW or the file is
# under ALLOW_PATHS for that kind of hit.
CREDENTIAL_KEY = r"[\w-]*(?:ssid|password|passwd|api_key|api-key|apikey|encryption_key|secret|token)"
SCAN = [
    (re.compile(r"compton|linear\.app|/hdd/|home-diy|homelab|bytesafe|bitwarden|\bbws\b|/home/|/Users/|/mnt/[a-z]/|[A-Z]:\\Users|t3clones|\.t3/|\.secrets\b|runs-on:.*self-hosted", re.I), "private name or path"),
    (re.compile(r"\b(?:10\.\d{1,3}\.\d{1,3}\.\d{1,3}|172\.(?:1[6-9]|2\d|3[01])\.\d{1,3}\.\d{1,3}|192\.168\.\d{1,3}\.\d{1,3}|100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}|169\.254\.\d{1,3}\.\d{1,3})\b|\bf[cd][0-9a-f]{2}:[0-9a-f:]+", re.I), "private IP"),
    (re.compile(r"\b[\w-]+\.(?:lan|home\.arpa|ts\.net|duckdns\.org|local)\b", re.I), "private hostname"),
    # Any URL whose host isn't a known public site or an example (see URL_HOSTS_OK).
    (re.compile(r"https?://(?P<host>[^/\s\"'`)<>\]]+)", re.I), "URL host"),
    (re.compile(r"\b[\w.+-]+@(?:gmail|googlemail|outlook|hotmail|live|icloud|me|proton|protonmail|yahoo)\.\w+", re.I), "personal email"),
    # A literal value for a credential-like key: quoted anywhere (YAML, JSON,
    # code), or bare at the start of a YAML or env line. Substitutions, !secret,
    # <placeholders> and the values in CREDENTIAL_OK are fine.
    (re.compile(r"[\"']?" + CREDENTIAL_KEY + r"[\"']?\s*(?::\s*\w+\s*)?[:=]\s*[\"'](?P<v>[^\"'\n]{4,})[\"']", re.I), "literal credential"),
    # An Immich API key in a header (curl -H "x-api-key: ...").
    (re.compile(r"api-key\s*:\s*(?P<v>[^\s\"'`]{4,})", re.I), "literal credential"),
    (re.compile(r"^\s*(?:#\s*|export\s+)?" + CREDENTIAL_KEY + r"\s*[:=]\s*(?P<v>[^\s\"'#]{4,})", re.I), "literal credential"),
    (re.compile(r"freenove|waveshare|\.\./stand\b|hardware/stand/|apps/espframe|pnpm nx", re.I), "source-repo or other-board reference"),
    (re.compile(r"(?:ghp|github_pat|gho|ghs|ghu)_[A-Za-z0-9_]{20,}|xox[bp]-[A-Za-z0-9-]{10,}|AKIA[0-9A-Z]{16}|-----BEGIN [A-Z ]*PRIVATE KEY"), "token or key"),
]
ALLOW = {
    "private IP": {
        "192.168.4.1",  # the setup hotspot's captive portal
        "192.168.1.30",  # docs and test example Immich host
        "192.168.0.1",  # web UI placeholder Immich URL
    },
}
# Credential-like values that aren't credentials: identifiers (a key naming
# another key, a workflow permission), booleans, substitutions, !secret and
# <placeholders>, and the tests' made-up keys.
CREDENTIAL_OK = re.compile(
    r"api_key|read|write|true|false|True|False|null|none|None|\$\{.*|!secret.*|!lambda.*|<.*"
    r"|fixture-read-only-api-key|imported-api-key"
    # Code, not values: a TS type or a property/expression (`apiKey: string;`).
    r"|\w+[;,]|string|number|boolean|(?:self|this|state|props|os|process|id|args|config|S)\.\w+.*|\w+\(.*"
)
URL_HOSTS_OK = re.compile(
    r"(?:[\w-]+\.)*(?:github\.com|github\.io|githubusercontent\.com|npmjs\.org|immich\.app|esphome\.io"
    r"|home-assistant\.io|polyformproject\.org|opencollective\.com|w3\.org|schema\.org|json-schema\.org"
    r"|makerworld\.com|buymeacoffee\.com|aliexpress\.com|gnu\.org|fsf\.org|wikipedia\.org|laskakit\.cz"
    r"|unpkg\.com|valgrind\.org|tidelift\.com|opensource\.org|libjpeg-turbo\.org|microsoft\.com"
    r"|google\.com|googlesource\.com|cppreference\.com|mozilla\.org|ietf\.org|espressif\.com|pool\.ntp\.org"
    r"|example\.com|example\.org|example\.net|example\.test|[\w-]+\.invalid|immich\.local|homeassistant\.local|espframe\.local|immich-frame[\w-]*\.local)(?::\d*\w*)?"
    r"|localhost(?::\d+)?|127\.0\.0\.1(?::\S*)?|<[^>]+>(?::\d+)?|\$\{[^}]+\}|\{[^}]+\}"
    r"|192\.168\.(?:4\.1|1\.30|0\.1)(?::\d+)?|\.\.\.|\*",
    re.I,
)
# Vendored third-party source credits its authors by email.
ALLOW_PATHS = {"personal email": ("components/libwebp-esp32/",)}

# package.json's check:ci: the checks that pass on the exported tree, which the
# public Build workflow also runs (plus scripts/check_timezones.py, which needs
# the pinned tzdata from pip). (The public repo ships package-lock.json and
# its CI uses npm, so the export runs npm too.)
CHECK_SCRIPT = "check:ci"


def die(msg: str) -> None:
    print(f"export: {msg}", file=sys.stderr)
    sys.exit(1)


def run(cmd: list[str], cwd: Path, **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, check=True, **kw)


def tracked_files() -> list[str]:
    """apps/espframe's tracked files, relative to it."""
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


def check_dest(dest: Path) -> None:
    """Refuse any dest that isn't empty, a public-repo checkout or an earlier export."""
    home = Path.home().resolve()
    if dest == REPO or REPO in dest.parents or dest in REPO.parents or dest in (home, Path(dest.anchor)):
        die(f"refusing {dest}: it is, contains or is inside this repo, or is your home or a root dir")
    if dest.exists() and not dest.is_dir():
        die(f"refusing {dest}: it isn't a directory")
    if not dest.exists() or not [c for c in dest.iterdir() if c.name != ".git"]:
        return
    if (dest / ".git").exists():
        origin = subprocess.run(
            ["git", "remote", "get-url", "origin"], cwd=dest, capture_output=True, text=True
        ).stdout.strip()
        if re.search(r"[:/]rcompton78/espframe(?:\.git)?/?$", origin):
            return
        die(f"refusing {dest}: it has files and its origin ({origin or 'none'}) isn't rcompton78/espframe")
    # Files copy_tree writes early, so an export that failed part-way still counts.
    if (dest / "common" / "addon" / "firmware_update.yaml").is_file() or (dest / "product" / "contract" / "devices.json").is_file():
        return
    die(f"refusing {dest}: it isn't empty, an espframe checkout or an earlier export, and the export empties it")


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
    devices = json.loads(path.read_text(encoding="utf-8"))
    keep = [d for d in devices if d.get("slug") == DEVICE_SLUG]
    if len(keep) != 1:
        die(f"expected one device with slug {DEVICE_SLUG!r} in devices.json, found {len(keep)}")
    device = keep[0]
    subs = device.get("package_substitutions")
    if not isinstance(subs, dict) or "firmware_manifest_url" not in subs:
        die("the Guition device has no package_substitutions.firmware_manifest_url to clear")
    subs["firmware_manifest_url"] = ""
    path.write_text(json.dumps(keep, indent=2) + "\n", encoding="utf-8")

    fw = dest / "common" / "addon" / "firmware_update.yaml"
    text = fw.read_text(encoding="utf-8")
    for key in ("firmware_manifest_url", "legacy_firmware_manifest_url"):
        new, n = re.subn(rf'^(  {key}: )"[^"]*"$', r'\1""', text, flags=re.M)
        if n != 1:
            die(f"common/addon/firmware_update.yaml: expected one {key} substitution, found {n}")
        text = new
    fw.write_text(text, encoding="utf-8")


def apply_patches(dest: Path) -> None:
    for rel, old, new in PATCHES:
        path = dest / rel
        text = path.read_text(encoding="utf-8")
        count = text.count(old)
        if count != 1:
            die(f"patch for {rel} matched {count} times (expected 1): {old[:80]!r}")
        path.write_text(text.replace(old, new), encoding="utf-8")
    for rel, start, end in DROP_SECTIONS:
        path = dest / rel
        text = path.read_text(encoding="utf-8")
        i, j = text.find(start), text.find(end)
        if i < 0 or j < i or text.count(start) != 1:
            die(f"section {start[:40]!r}..{end[:40]!r} not found once in {rel}")
        replacement = FIRMWARE_UPDATE_INTRO if rel == "docs/firmware-update.md" else ""
        path.write_text(text[:i] + replacement + text[j:], encoding="utf-8")
    # Issue-tracker links point at a private workspace; keep the ticket ids, drop the URLs.
    for path in text_files(dest):
        text = path.read_bytes().decode("utf-8", errors="surrogateescape")
        new = re.sub(r" ?\(https://linear\.app/[^)\s]*\)", "", text)
        new = re.sub(r"https://linear\.app/[^\s)\]>,]*", "", new)
        if new != text:
            path.write_bytes(new.encode("utf-8", errors="surrogateescape"))
    rewrite_doc_links(dest)
    with (dest / ".gitignore").open("a") as f:
        f.write(GITIGNORE_EXTRA)


def rewrite_doc_links(dest: Path) -> None:
    """Point docs/*.md's site-root links (VitePress) at files GitHub can show."""
    docs = dest / "docs"
    missing = []

    def target(path: str, md: str) -> str | None:
        path = path.strip("/")
        if not path:
            return "index.md"
        if (docs / f"{path}.md").is_file():
            return f"{path}.md"
        if (docs / "public" / path).is_file():
            return f"public/{path}"
        missing.append(f"docs/{md}: /{path}")
        return None

    for page in sorted(docs.glob("*.md")):
        text = page.read_text(encoding="utf-8")

        def link(m: re.Match) -> str:
            t = target(m.group(1), page.name)
            return m.group(0) if t is None else f"]({t}{m.group(2) or ''})"

        def src(m: re.Match) -> str:
            t = target(m.group(1), page.name)
            return m.group(0) if t is None else f'src="{t}"'

        new = re.sub(r"\]\(/([^)#\s]*)(#[^)\s]*)?\)", link, text)
        new = re.sub(r'src="/([^"]+)"', src, new)
        if new != text:
            page.write_text(new, encoding="utf-8")
    if missing:
        die("docs link to pages the export leaves out:\n  " + "\n  ".join(missing))


def copy_overlay(dest: Path) -> None:
    # Tracked files only: an ignored .env or .venv left in overlay/ must never ship.
    out = subprocess.run(
        ["git", "ls-files", "-z", "--", "."], cwd=OVERLAY, check=True, capture_output=True
    ).stdout.decode()
    for rel in filter(None, out.split("\0")):
        src = OVERLAY / rel
        if not src.exists():
            continue
        target = dest / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, target)


def regenerate(dest: Path) -> None:
    # The web bundle needs esbuild; borrow the source's node_modules for the run.
    modules = dest / "node_modules"
    modules.symlink_to(SRC / "node_modules")
    try:
        r = subprocess.run([sys.executable, "scripts/generate_assets.py"], cwd=dest)
        if r.returncode != 0:
            die("scripts/generate_assets.py failed in the export (output above)")
    finally:
        modules.unlink()


def text_files(dest: Path):
    """Every file in dest that isn't binary (no NUL in its first 8 KB)."""
    for path in sorted(dest.rglob("*")):
        if ".git" in path.parts or "node_modules" in path.parts or not path.is_file():
            continue
        with path.open("rb") as f:
            if b"\0" in f.read(8192):
                continue
        yield path


def scan(dest: Path) -> None:
    hits = []

    def check_text(rel: str, where: str, text: str) -> None:
        for pattern, what in SCAN:
            for m in pattern.finditer(text):
                if m.group(0) in ALLOW.get(what, ()):
                    continue
                if what == "literal credential" and CREDENTIAL_OK.fullmatch(m.group("v").strip()):
                    continue
                if what == "URL host" and URL_HOSTS_OK.fullmatch(m.group("host")):
                    continue
                if what == "private hostname" and re.fullmatch(r"immich|homeassistant|espframe|immich-frame[\w-]*", m.group(0).lower().removesuffix(".local")):
                    continue
                if rel.startswith(ALLOW_PATHS.get(what, ())):
                    continue
                hits.append(f"{where}: {what}: {text.strip()[:160]}")

    for path in sorted(dest.rglob("*")):
        if ".git" in path.parts or "node_modules" in path.parts:
            continue
        rel = str(path.relative_to(dest))
        check_text(rel, f"{rel} (path)", rel)
        if path.name in ("secrets.yaml", ".env") or (path.name.startswith(".env.") and path.name != ".env.example"):
            hits.append(f"{rel}: must never be exported")
    for path in text_files(dest):
        rel = str(path.relative_to(dest))
        text = path.read_bytes().decode("utf-8", errors="replace")
        for lineno, line in enumerate(text.splitlines(), 1):
            check_text(rel, f"{rel}:{lineno}", line)
    hits = list(dict.fromkeys(hits))
    if hits:
        print("\n".join(hits), file=sys.stderr)
        die(f"private-detail scan found {len(hits)} hit(s); fix the source or PATCHES and export again")


def check(dest: Path) -> None:
    modules = dest / "node_modules"
    modules.symlink_to(SRC / "node_modules")
    try:
        r = subprocess.run(["npm", "run", "-s", CHECK_SCRIPT], cwd=dest, capture_output=True, text=True)
        if r.returncode != 0:
            sys.stderr.write(r.stdout[-4000:] + r.stderr[-4000:])
            die(f"npm run {CHECK_SCRIPT} failed in the export")
    finally:
        modules.unlink()


def require_clean_source() -> None:
    dirty = subprocess.run(
        ["git", "status", "--porcelain", "--", "apps/espframe", "tools/espframe-public"],
        cwd=REPO, check=True, capture_output=True, text=True,
    ).stdout.strip()
    if dirty:
        die("apps/espframe or tools/espframe-public has uncommitted changes; commit them before --commit")


def commit(dest: Path) -> None:
    sha = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"], cwd=REPO, check=True, capture_output=True, text=True
    ).stdout.strip()
    run(["git", "add", "-A"], dest)
    if not subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=dest).returncode:
        print("export: nothing changed since the last sync")
        return
    run(["git", "commit", "-q", "-m", f"Sync {sha}"], dest)
    print(f"export: committed in {dest}; review it, then push")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("dest", type=Path, help="a checkout of the public repo (or an empty dir)")
    parser.add_argument("--commit", action="store_true", help="commit the result in dest")
    args = parser.parse_args()
    dest: Path = args.dest.resolve()

    check_dest(dest)
    if args.commit:
        if not (dest / ".git").exists():
            die("--commit needs dest to be a git checkout")
        require_clean_source()
    if not (SRC / "node_modules").is_dir():
        die("apps/espframe/node_modules is missing; run `pnpm nx run espframe:install` first")
    dest.mkdir(parents=True, exist_ok=True)

    clear_dest(dest)
    try:
        copy_tree(dest)
        filter_contract(dest)
        apply_patches(dest)
        copy_overlay(dest)
        regenerate(dest)
        scan(dest)
        check(dest)
    except BaseException:
        hint = ""
        if subprocess.run(["git", "-C", str(dest), "rev-parse", "--verify", "-q", "HEAD"], capture_output=True).returncode == 0:
            hint = f" (or restore it with `git -C {dest} checkout . && git -C {dest} clean -fd`)"
        print(f"export: {dest} is incomplete; don't commit it. Fix the problem and export again{hint}.", file=sys.stderr)
        raise
    print(f"export: {dest} is ready")
    if args.commit:
        commit(dest)


if __name__ == "__main__":
    main()
