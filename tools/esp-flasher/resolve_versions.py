#!/usr/bin/env python3
"""
resolve_versions.py — per-project release versions for release.yml (COM-428).

Every device's OTA check (ESPHome's http_request update, libs/ota-update-client)
installs whenever the published manifest's version differs from its own. A single
run-wide RELEASE_VERSION therefore made every device reinstall the same code and
reboot after every master push, whatever the push touched. Instead, each project
with a `release` target gets:

  - the new run version when it is "bumped": `nx affected` (since the last-release
    tag) lists it, or a changed file matches one of its `{workspaceRoot}/...` Nx
    target inputs (shared build scripts like scripts/pio.sh that Nx affected
    can't see, since no project owns them);
  - otherwise the version of the newest GitHub release whose notes list it under
    "Changed: ...", i.e. the version its devices are already running. Building it
    again under that version is an Nx cache hit on the self-hosted runner (same
    bytes), and even a cache miss keeps the version devices compare against.

When no earlier release lists a project (a new project) it is bumped, so the
fail-safe is the old behaviour: one extra update, never a stuck device.

`--force-bump a,b` (release.yml's manual `force_bump` input) bumps projects whose
source didn't change but whose firmware should still reach devices, e.g. after
rotating a secret baked in at build time; a carried project would otherwise be
rebuilt with the new secret under its old version, so no device installs it.

    python3 tools/esp-flasher/resolve_versions.py \
        --base <last-release sha> --head <sha> --version 2026.10.13-139 \
        --output versions.json

Writes {"version": ..., "bumped": [...], "carried": {project: version}}.
"""

import argparse
import json
import re
import subprocess
import sys

# Matches the line release.yml's "Create GitHub release" step writes:
# "Automated build from <sha>. Changed: espframe, cyd-clock."
CHANGED_RE = re.compile(r"Changed: ([A-Za-z0-9_.,\s-]*?)\.(?:\s|$)")


def changed_projects(body):
    """Project names a release's notes list as changed (empty if none)."""
    m = CHANGED_RE.search(body or "")
    if not m:
        return []
    return [p.strip() for p in m.group(1).split(",") if p.strip()]


def glob_to_regex(pattern):
    """Nx-style input glob (`**` crosses directories, `*` doesn't) as a regex."""
    out = []
    i = 0
    while i < len(pattern):
        if pattern.startswith("**/", i):
            out.append("(?:.*/)?")
            i += 3
        elif pattern.startswith("**", i):
            out.append(".*")
            i += 2
        elif pattern[i] == "*":
            out.append("[^/]*")
            i += 1
        elif pattern[i] == "?":
            out.append("[^/]")
            i += 1
        else:
            out.append(re.escape(pattern[i]))
            i += 1
    return re.compile("".join(out) + r"\Z")


def workspace_inputs(project_config):
    """Every `{workspaceRoot}/...` file glob in any of a project's target inputs."""
    globs = set()
    for target in project_config.get("targets", {}).values():
        for entry in target.get("inputs", []) or []:
            if isinstance(entry, str) and entry.startswith("{workspaceRoot}/"):
                globs.add(entry[len("{workspaceRoot}/"):])
    return sorted(globs)


def latest_listed_version(project, releases):
    """Tag of the newest published release whose notes list `project`, or None.

    `releases` is newest first, as the GitHub API returns them. Drafts and
    prereleases (push-beta's "-beta" builds) never reach devices' manifests.
    """
    for rel in releases:
        if rel.get("draft") or rel.get("prerelease"):
            continue
        if project in changed_projects(rel.get("body")):
            return rel["tag_name"]
    return None


def resolve(projects, affected, changed_files, project_inputs, releases, version, forced=()):
    """Decide each release project's version.

    projects       — names of every project with a `release` target
    affected       — names `nx show projects --affected` returned
    changed_files  — repo-relative paths changed between base and head
    project_inputs — {project: [workspace-relative input globs]}
    releases       — GitHub releases, newest first
    version        — this run's new version
    forced         — names to bump regardless ("all" bumps every project)
    """
    bumped, carried, reasons = [], {}, {}
    for project in sorted(projects):
        if project in forced or "all" in forced:
            reasons[project] = "forced"
        elif project in affected:
            reasons[project] = "nx affected"
        else:
            hits = [f for g in project_inputs.get(project, [])
                    for f in changed_files if glob_to_regex(g).match(f)]
            if hits:
                reasons[project] = "shared input changed: " + ", ".join(sorted(set(hits)))
            else:
                previous = latest_listed_version(project, releases)
                if previous is not None:
                    carried[project] = previous
                    reasons[project] = f"unchanged since release {previous}"
                    continue
                reasons[project] = "no earlier release lists it"
        bumped.append(project)
    return {"version": version, "bumped": bumped, "carried": carried, "reasons": reasons}


# ── I/O ──────────────────────────────────────────────────────────────────────

def run(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


def nx_json(*args):
    return json.loads(run(["pnpm", "--silent", "nx", *args, "--json"]))


def fetch_releases(repo):
    # gh fills in {owner}/{repo} from the checkout's remote.
    path = f"repos/{repo or '{owner}/{repo}'}/releases?per_page=100"
    cmd = ["gh", "api", "--paginate", "--jq", ".[] | {tag_name, body, draft, prerelease}", path]
    return [json.loads(line) for line in run(cmd).splitlines() if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--base", required=True, help="last-release sha")
    parser.add_argument("--head", required=True)
    parser.add_argument("--version", required=True, help="this run's new version")
    parser.add_argument("--repo", default="", help="owner/name (default: the current repo)")
    parser.add_argument("--force-bump", default="",
                        help="comma-separated projects to bump even if unchanged, or 'all'")
    parser.add_argument("--output", help="write the JSON here as well as to stdout")
    args = parser.parse_args()

    projects = nx_json("show", "projects", "--withTarget", "release")
    forced = {p.strip() for p in args.force_bump.split(",") if p.strip()}
    unknown = forced - set(projects) - {"all"}
    if unknown:
        sys.exit(f"--force-bump: no release project named {', '.join(sorted(unknown))}")
    affected = set(nx_json("show", "projects", "--affected", "--withTarget", "release",
                           f"--base={args.base}", f"--head={args.head}"))
    changed_files = run(["git", "diff", "--name-only", args.base, args.head]).splitlines()
    project_inputs = {p: workspace_inputs(nx_json("show", "project", p)) for p in projects}

    try:
        releases = fetch_releases(args.repo)
    except (subprocess.CalledProcessError, json.JSONDecodeError) as e:
        # Fail safe: with no history every project bumps, like before COM-428.
        print(f"warning: could not list releases ({e}); bumping every project", file=sys.stderr)
        releases = []

    result = resolve(projects, affected, changed_files, project_inputs, releases, args.version, forced)
    text = json.dumps(result, indent=2)
    print(text)
    if args.output:
        with open(args.output, "w") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
