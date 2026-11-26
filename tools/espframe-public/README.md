# espframe public repo export

[rcompton78/espframe](https://github.com/rcompton78/espframe) is a public, 10"-only copy of `apps/espframe` (Guition ESP32-P4 JC8012P4A1 plus the 10" stand) for friends to fork, build, flash and update themselves (COM-628). compton-diy stays the only place espframe code is written; changes flow one way, from here to there.

## Publishing

```bash
git clone git@github.com:rcompton78/espframe.git ~/t3clones/espframe-public   # first time only
python3 tools/espframe-public/export.py ~/t3clones/espframe-public --commit
git -C ~/t3clones/espframe-public push
```

Commit your `apps/espframe` and `tools/espframe-public` changes first: `--commit` refuses a dirty tree, so every public commit names a real compton-diy commit (`Sync <sha>`). Without `--commit` it only writes the tree, for a look before committing. It needs `apps/espframe/node_modules` (`pnpm nx run espframe:install`) to regenerate the web bundle and run the checks.

## What it does

`export.py` first checks the destination: it must be empty (apart from `.git`), an earlier export (even a failed one), or a checkout whose `origin` is `rcompton78/espframe`, and never this repo, a folder holding it, or your home folder. With `--commit` it also refuses uncommitted changes in `apps/espframe` or `tools/espframe-public`, before touching anything. Then it empties the destination (all but `.git`) and rebuilds it:

1. Copies `apps/espframe`'s tracked files to the repo root, minus `EXCLUDE`: the Freenove and Waveshare boards and builds, the 7" stand, Nx `project.json` files, `.agents/`, `HANDOFF.md`, the internal phase/roadmap/testing/governance docs, the original project's VitePress site config (`docs/.vitepress/`, plus `package.json`'s `docs:*` scripts) and the upstream README and GitHub templates.
2. Cuts `product/contract/devices.json` to the Guition and clears its `firmware_manifest_url`, and the legacy manifest URL in `common/addon/firmware_update.yaml`. A local build in the public repo has no update manifest; its release workflow bakes in the fork's own GitHub Pages manifest with `-s firmware_manifest_url`.
3. Applies `PATCHES` and `DROP_SECTIONS` (docs and comments that mention the other boards, compton-diy paths or Nx commands). Each must match exactly once, so a source edit that breaks one fails the export instead of shipping stale text. Linear links are stripped (the ticket ids stay), and `docs/*.md`'s site-root links (`](/api-key)`, `src="/x.png"`) are rewritten to files GitHub can show; a link to a page the export leaves out fails it.
4. Copies `overlay/`'s tracked files on top: the public `README.md`, `.github/workflows/{build,release}.yml`, `scripts/firmware.sh` (install/compile/flash/logs with the pinned ESPHome), `scripts/release/` (install page, manifests, stand tarball), `.nvmrc` and the public repo's own `.claude/workflow.config.md`.

The public repo ships both Guition builds, the original (pre-v3 ESP32-P4 chip) and the v3 chip build (COM-690, exported since COM-691); `scripts/firmware.sh --chip pre-v3|v3` picks one. The export blanks the v3 entry point's compton-diy manifest and sibling URLs (a `PATCHES` entry). The release workflow compiles both, each with its own Pages manifest and the other's as `sibling_firmware_manifest_url`, and `make_pages.py` writes one single-build manifest per build (both are ESP32-P4, and ESP Web Tools and ESPHome take the first chipFamily match, COM-427) plus an install page with one button each. The pre-v3 build keeps `manifest.json`, which frames installed before the v3 build existed already poll; the v3 build's is `manifest-guition-esp32-p4-jc8012p4a1-v3.json`. Keep those names in step between `make_pages.py`'s `BUILDS` and `release.yml`.
5. Regenerates the contract-derived files (`scripts/generate_assets.py`), then fails on any hit from the private-detail scan, which reads every non-binary file and every path (compton/homelab names, home and worktree paths, self-hosted runners, private IPs and hostnames, URLs to hosts outside `URL_HOSTS_OK`, personal emails, quoted or bare literal credentials and API-key headers, tokens and keys, other-board names, any `secrets.yaml` or `.env`), or if `npm run check:ci` fails (the public Build workflow runs it too, plus the timezone check).

On any failure it says the destination is incomplete; don't commit it. Anything that should exist only in the public repo goes in `overlay/` (committed: untracked files there are never copied); never edit the public repo by hand, or the next export overwrites it. Friends' forks only change when they pull from it.
