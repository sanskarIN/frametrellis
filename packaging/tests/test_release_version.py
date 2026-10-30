# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Regression checks for stable and prerelease Windows asset naming."""

import importlib.util
import subprocess
import sys
import unittest
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "release_version.py"
spec = importlib.util.spec_from_file_location("release_version", SOURCE)
assert spec and spec.loader
# Avoid requiring third-party dependencies: import directly from the packaging folder.
sys.path.insert(0, str(SOURCE.parent))
from release_version import resolve_release_version  # noqa: E402


class ReleaseVersionTests(unittest.TestCase):
    def test_stable_release(self):
        result = resolve_release_version("v1.4.0", "1.4.0")
        self.assertEqual(result.version, "1.4.0")
        self.assertEqual(result.package_version, "1.4.0")
        self.assertFalse(result.prerelease)

    def test_prerelease_separates_tag_and_artifact_version(self):
        for tag in ("v1.4.0-alpha.1", "v1.4.0-beta.2", "v1.4.0-rc.1"):
            with self.subTest(tag=tag):
                result = resolve_release_version(tag, "1.4.0")
                self.assertEqual(result.tag, tag)
                self.assertEqual(result.version, tag[1:])
                self.assertEqual(result.package_version, "1.4.0")
                self.assertTrue(result.prerelease)

    def test_tag_requires_matching_cmake_version(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            resolve_release_version("v1.4.0-beta.1", "1.3.0")

    def test_malformed_and_unsafe_tags_rejected(self):
        for tag in (
            "main",
            "1.4.0",
            "v1.4",
            "v1.4.0/",
            "v1.4.0-",
            "v1.4.0-alpha.",
            "v1.4.0-rc..1",
            "v1.4.0-beta/asset",
            "v1.4.0\nversion=evil",
        ):
            with self.subTest(tag=tag), self.assertRaises(ValueError):
                resolve_release_version(tag, "1.4.0")

    def test_cli_outputs_are_safe_for_github_actions(self):
        process = subprocess.run(
            [
                sys.executable,
                str(SOURCE),
                "--tag",
                "v1.4.0-beta.1",
                "--project-version",
                "1.4.0",
            ],
            check=True,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            process.stdout,
            "tag=v1.4.0-beta.1\n"
            "version=1.4.0-beta.1\n"
            "package_version=1.4.0\n"
            "prerelease=true\n",
        )


if __name__ == "__main__":
    unittest.main()
