#!/usr/bin/env python3
"""
Tests for generate_release.py's manifest/flasher output (COM-427).

    python3 -m unittest discover -s tools/esp-flasher -p 'test_*.py'
"""

import json
import os
import tempfile
import unittest

import generate_release as gr


def build(env, chip, label=None):
    return {
        "env": env, "label": label or env, "chipFamily": chip,
        "bin": f"app-{env}.bin", "ota_bin": f"app-{env}-ota.bin", "ota_md5": "0" * 32,
    }


# espframe's real shape: two ESP32-S3 boards and one ESP32-P4.
ESPFRAME = [
    build("freenove-s3", "ESP32-S3", "Freenove ESP32-S3 CYD"),
    build("waveshare-esp32-s3-touch-lcd-7", "ESP32-S3", "Waveshare ESP32-S3-Touch-LCD-7"),
    build("guition-esp32-p4-jc8012p4a1", "ESP32-P4", "Guition ESP32-P4 JC8012P4A1"),
]

# cyd-clock's shape: one board per chip.
CYD_CLOCK = [build("cyd", "ESP32"), build("freenove-s3", "ESP32-S3")]


class ManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def read(self, name):
        with open(os.path.join(self.dir, name)) as f:
            return json.load(f)

    def test_generate_manifest_rejects_duplicate_chip_family(self):
        with self.assertRaisesRegex(ValueError, "ESP32-S3"):
            gr.generate_manifest("x", "1", ESPFRAME, os.path.join(self.dir, "manifest.json"))
        self.assertFalse(os.path.exists(os.path.join(self.dir, "manifest.json")))

    def test_shared_manifest_drops_ambiguous_chip_families(self):
        per_device = gr.write_manifests("espframe", "1.2.3", ESPFRAME, self.dir)
        self.assertTrue(per_device)
        shared = self.read("manifest.json")
        self.assertEqual([b["chipFamily"] for b in shared["builds"]], ["ESP32-P4"])
        self.assertEqual(shared["builds"][0]["ota"]["path"], "app-guition-esp32-p4-jc8012p4a1-ota.bin")

    def test_each_variant_gets_its_own_single_build_manifest(self):
        gr.write_manifests("espframe", "1.2.3", ESPFRAME, self.dir)
        for b in ESPFRAME:
            m = self.read(f"manifest-{b['env']}.json")
            self.assertEqual(m["version"], "1.2.3")
            self.assertEqual(len(m["builds"]), 1)
            self.assertEqual(m["builds"][0]["ota"]["path"], b["ota_bin"])
            self.assertEqual(m["builds"][0]["parts"], [{"path": b["bin"], "offset": 0}])

    def test_unique_chips_keep_one_shared_manifest(self):
        per_device = gr.write_manifests("CYD Clock", "1", CYD_CLOCK, self.dir)
        self.assertFalse(per_device)
        self.assertEqual([b["chipFamily"] for b in self.read("manifest.json")["builds"]],
                         ["ESP32", "ESP32-S3"])

    def test_single_variant_writes_no_per_variant_manifest(self):
        gr.write_manifests("Tamagotchi+", "1", [build("waveshare-s3-169", "ESP32-S3")], self.dir)
        self.assertEqual(os.listdir(self.dir), ["manifest.json"])


class FlasherHtmlTests(unittest.TestCase):
    def render(self, builds, per_device):
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, "index.html")
            gr.copy_flasher_html("app", "app", "1", builds, per_device, out)
            with open(out) as f:
                return f.read()

    def test_per_device_buttons_point_at_variant_manifests(self):
        html = self.render(ESPFRAME, True)
        self.assertNotIn("{{", html)
        self.assertNotIn('manifest="manifest.json"', html)
        for b in ESPFRAME:
            self.assertIn(f'manifest="manifest-{b["env"]}.json"', html)
            self.assertIn(b["label"], html)

    def test_single_button_uses_shared_manifest(self):
        html = self.render(CYD_CLOCK, False)
        self.assertNotIn("{{", html)
        self.assertEqual(html.count("<esp-web-install-button"), 1)
        self.assertIn('manifest="manifest.json"', html)


if __name__ == "__main__":
    unittest.main()
