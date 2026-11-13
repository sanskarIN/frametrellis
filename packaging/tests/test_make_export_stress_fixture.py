# SPDX-License-Identifier: GPL-3.0-or-later
"""Network-free tests for FrameTrellis synthetic extended-export fixture."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from fractions import Fraction
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "make_export_stress_fixture.py"
SPEC = importlib.util.spec_from_file_location("make_export_stress_fixture", SCRIPT)
assert SPEC and SPEC.loader
fixture = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(fixture)


def sample() -> dict:
    def track(name: str, kind: str, with_clip: bool) -> dict:
        return {
            "id": name,
            "kind": kind,
            "clips": [{
                "id": "clip-" + name, "media": "media-" + name,
                "start": 0, "in": 0, "duration": 49, "gain": 1.0,
            }] if with_clip else [],
        }
    return {
        "format": "org.frametrellis.project", "version": 6,
        "project": {
            "name": "FrameTrellis self-test",
            "settings": {"width": 640, "height": 360, "frameRate": "25/1"},
            "tracks": [
                track("v1", "video", True), track("v2", "video", True),
                track("a1", "audio", True), track("a2", "audio", False),
            ],
        },
    }


class ExtendedFixtureTests(unittest.TestCase):
    def test_repeats_three_tracks_without_modifying_original(self):
        original = sample()
        stretched, seconds = fixture.stretch(original, 30)
        self.assertEqual(seconds, Fraction(49 * 30, 25))
        self.assertEqual(original["project"]["tracks"][0]["clips"][0]["start"], 0)
        self.assertEqual(len(original["project"]["tracks"][0]["clips"]), 1)
        tracks = stretched["project"]["tracks"]
        self.assertEqual([len(t["clips"]) for t in tracks], [30, 30, 30, 0])
        self.assertEqual([c["start"] for c in tracks[0]["clips"]], [49 * i for i in range(30)])
        self.assertEqual({c["in"] for c in tracks[0]["clips"]}, {0})
        ids = [c["id"] for t in tracks for c in t["clips"]]
        self.assertEqual(len(ids), len(set(ids)))

    def test_rejects_bad_shape_mismatched_duration_and_wrong_project(self):
        bad = sample()
        bad["project"]["name"] = "private project"
        with self.assertRaisesRegex(ValueError, "disposable"):
            fixture.stretch(bad, 30)
        bad = sample()
        bad["project"]["tracks"][2]["clips"][0]["duration"] = 40
        with self.assertRaisesRegex(ValueError, "single duration"):
            fixture.stretch(bad, 30)
        bad = sample()
        bad["project"]["tracks"][0]["clips"][0]["start"] = 20
        with self.assertRaisesRegex(ValueError, "frame zero"):
            fixture.stretch(bad, 30)

    def test_rejects_invalid_repeat_count(self):
        for count in (0, 1, 51, -1):
            with self.assertRaisesRegex(ValueError, "between 2 and 50"):
                fixture.stretch(sample(), count)

    def test_writes_new_project_and_never_overwrites(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            src = root / "self-test.ftproj"
            out = root / "long.ftproj"
            src.write_text(json.dumps(sample()), encoding="utf-8")
            self.assertEqual(fixture.make_fixture(src, out, 2), Fraction(98, 25))
            contents = out.read_bytes()
            with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
                fixture.make_fixture(src, out, 2)
            self.assertEqual(out.read_bytes(), contents)
            self.assertEqual(json.loads(src.read_text())["project"]["name"],
                             "FrameTrellis self-test")


if __name__ == "__main__":
    unittest.main()
