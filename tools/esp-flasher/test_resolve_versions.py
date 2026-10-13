#!/usr/bin/env python3
"""
Tests for resolve_versions.py's per-project release versions (COM-428).

    python3 -m unittest discover -s tools/esp-flasher -p 'test_*.py'
"""

import unittest

import resolve_versions as rv


def release(tag, changed, **extra):
    body = f"Automated build from abc123. Changed: {','.join(changed)}."
    return {"tag_name": tag, "body": body + "\n\nFirmware: upload the *-ota.bin.", **extra}


PROJECTS = ["cyd-clock", "espframe", "espframe-stand", "media-room-dashboard", "tamagotchi-plus"]

INPUTS = {
    "cyd-clock": ["libs/ota-update-client/src/**", "scripts/pio.sh"],
    "espframe": ["scripts/espframe-esphome.sh"],
    "espframe-stand": ["scripts/pack-hardware-release.sh"],
    "media-room-dashboard": ["scripts/espframe-esphome.sh"],
    "tamagotchi-plus": ["libs/ota-update-client/src/**", "scripts/pio.sh", "tools/sprites/convert.py"],
}

# Newest first, like the GitHub API.
RELEASES = [
    release("2026.10.13-138", ["espframe"]),
    release("2026.10.12-137-beta", ["cyd-clock"], prerelease=True),
    release("2026.10.11-135", ["espframe-stand"]),
    release("2026.10.07-132", ["cyd-clock"]),
    release("2026.10.01-130", ["espframe", "media-room-dashboard", "tamagotchi-plus"]),
]


def resolve(affected=(), changed=(), releases=RELEASES):
    return rv.resolve(PROJECTS, set(affected), list(changed), INPUTS, releases, "2026.10.14-139")


class ChangedProjectsTests(unittest.TestCase):
    def test_current_notes_format(self):
        body = "Automated build from 06085b1. Changed: espframe.\n\nFirmware: upload ..."
        self.assertEqual(rv.changed_projects(body), ["espframe"])

    def test_several_projects(self):
        body = "Automated build from abc. Changed: cyd-clock,espframe-stand,tamagotchi-plus.\n"
        self.assertEqual(rv.changed_projects(body), ["cyd-clock", "espframe-stand", "tamagotchi-plus"])

    def test_old_single_line_format(self):
        body = "Automated build from d4c48d7. Changed: cyd-clock. Upload the *-ota.bin matching your device."
        self.assertEqual(rv.changed_projects(body), ["cyd-clock"])

    def test_notes_without_list(self):
        self.assertEqual(rv.changed_projects("Manual release"), [])
        self.assertEqual(rv.changed_projects(None), [])


class ResolveTests(unittest.TestCase):
    def test_unrelated_push_carries_every_project(self):
        # The card's case: an espframe-stand-only push must not re-version espframe.
        r = resolve(affected=["espframe-stand"])
        self.assertEqual(r["bumped"], ["espframe-stand"])
        self.assertEqual(r["carried"], {
            "cyd-clock": "2026.10.07-132",
            "espframe": "2026.10.13-138",
            "media-room-dashboard": "2026.10.01-130",
            "tamagotchi-plus": "2026.10.01-130",
        })

    def test_affected_project_bumps(self):
        r = resolve(affected=["espframe"])
        self.assertIn("espframe", r["bumped"])
        self.assertNotIn("espframe", r["carried"])
        self.assertEqual(r["version"], "2026.10.14-139")

    def test_prerelease_is_never_carried(self):
        # push-beta's prerelease lists cyd-clock but never reached a manifest.
        self.assertEqual(resolve()["carried"]["cyd-clock"], "2026.10.07-132")

    def test_shared_script_bumps_its_projects(self):
        r = resolve(changed=["scripts/espframe-esphome.sh"])
        self.assertEqual(r["bumped"], ["espframe", "media-room-dashboard"])
        self.assertIn("shared input changed", r["reasons"]["espframe"])

    def test_shared_lib_glob_bumps_its_projects(self):
        r = resolve(changed=["libs/ota-update-client/src/OtaUpdateClient.cpp"])
        self.assertEqual(r["bumped"], ["cyd-clock", "tamagotchi-plus"])

    def test_release_tooling_change_bumps_nothing(self):
        # generate_release.py only rewrites manifests; the firmware is unchanged.
        r = resolve(changed=["tools/esp-flasher/generate_release.py", "CLAUDE.md"])
        self.assertEqual(r["bumped"], [])

    def test_project_never_released_bumps(self):
        r = resolve(releases=RELEASES[:1])
        self.assertEqual(r["bumped"], ["cyd-clock", "espframe-stand", "media-room-dashboard", "tamagotchi-plus"])
        self.assertEqual(r["carried"], {"espframe": "2026.10.13-138"})

    def test_no_release_history_bumps_everything(self):
        self.assertEqual(resolve(releases=[])["bumped"], PROJECTS)


class GlobTests(unittest.TestCase):
    def test_double_star_crosses_directories(self):
        g = rv.glob_to_regex("libs/touch-driver/src/**")
        self.assertTrue(g.match("libs/touch-driver/src/a/b.cpp"))
        self.assertFalse(g.match("libs/touch-driver/test/a.cpp"))

    def test_single_star_stays_in_directory(self):
        g = rv.glob_to_regex("apps/x/assets/*.png")
        self.assertTrue(g.match("apps/x/assets/egg.png"))
        self.assertFalse(g.match("apps/x/assets/sub/egg.png"))

    def test_exact_path(self):
        g = rv.glob_to_regex("scripts/pio.sh")
        self.assertTrue(g.match("scripts/pio.sh"))
        self.assertFalse(g.match("scripts/pio.sh.bak"))


class WorkspaceInputsTests(unittest.TestCase):
    def test_collects_workspace_globs_across_targets(self):
        config = {"targets": {
            "build": {"inputs": ["default", "{projectRoot}/src/**", "{workspaceRoot}/scripts/pio.sh",
                                 {"env": "RELEASE_VERSION"}]},
            "build-x": {"inputs": ["{workspaceRoot}/scripts/pio.sh", "{workspaceRoot}/libs/a/src/**"]},
            "flash": {},
        }}
        self.assertEqual(rv.workspace_inputs(config), ["libs/a/src/**", "scripts/pio.sh"])


if __name__ == "__main__":
    unittest.main()
