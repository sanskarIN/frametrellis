# SPDX-License-Identifier: GPL-3.0-or-later
"""Protect the exact v1.5.3 source-build publication route."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class V153ReleaseSafetyTests(unittest.TestCase):
    def read(self, path):
        return (ROOT / path).read_text(encoding="utf-8")

    def test_separate_owner_marker_and_frozen_main(self):
        source = self.read(".github/workflows/release-assets-v153-source.yml")
        for token in (
            "tag_name == 'v1.5.3'",
            "FrameTrellis-1.5.3-SOURCE-BUILD-OWNER-APPROVED",
            "FrameTrellis-1.5.3-OWNER-APPROVED",
            "grep -Fxq",
            '[[ "$tag_sha" == "$main_sha" ]]',
            "ci.yml desktop-platforms.yml package-windows.yml website.yml",
            'head_sha == $sha',
            'head_branch == "main"',
            'event == "push"',
            "runs?branch=main&status=success&per_page=100",
            "--v153-source-build-assets",
            "docs/releases/v1.5.3.md",
            ".draft == false and .prerelease == false",
        ):
            self.assertIn(token, source)
        for forbidden in (
            "gh release create", "gh release edit", "gh release delete",
            "git tag ", "git push", "workflow_dispatch:", "--clobber",
            "FrameTrellis-1.5.2-SOURCE-BUILD-OWNER-APPROVED",
        ):
            self.assertNotIn(forbidden, source)

    def test_assets_are_verified_before_upload(self):
        source = self.read(".github/workflows/release-assets-v153-source.yml")
        for token in (
            "packaging/verify_release_assets.py --version 1.5.3",
            'expected_names("1.5.3")',
            "sha256sum --check SHA256SUMS.txt",
            "result.startswith(source)",
            '[[ "$existing" == "0" ]]',
        ):
            self.assertLess(source.index(token), source.index('gh release upload'))
        self.assertIn("packaging/verify_published_windows_v15.py", source)
        for name in (
            "FrameTrellis-1.5.3-windows-x64-setup.exe",
            "FrameTrellis-1.5.3-windows-x64.zip",
            "FrameTrellis-1.5.3-windows-x64.spdx.json",
            "FrameTrellis-1.5.3-windows-x64-third-party-sources.tar",
            "SHA256SUMS.txt",
        ):
            self.assertIn(name, source)

    def test_notes_cannot_authorize_publication(self):
        notes = self.read("docs/releases/v1.5.3.md")
        self.assertNotIn("FrameTrellis-1.5.3-SOURCE-BUILD-OWNER-APPROVED", notes)
        self.assertNotIn("FrameTrellis-1.5.3-OWNER-APPROVED", notes)
        cmake = self.read("CMakeLists.txt")
        match = re.search(r"project\(FrameTrellis\s+VERSION\s+(\d+\.\d+\.\d+)", cmake)
        self.assertIsNotNone(match)
        version = tuple(map(int, match.group(1).split(".")))
        if version < (1, 5, 3):
            self.assertIn("NOT PUBLISHED", notes.upper())

    def test_preparation_and_website_routing(self):
        prepare = self.read(".github/workflows/release.yml")
        legacy = self.read(".github/workflows/release-assets.yml")
        pages = self.read(".github/workflows/pages.yml")
        self.assertIn('policy="--v153-source-build-assets"', prepare)
        self.assertIn("env.RELEASE_VERSION == '1.5.3'", prepare)
        self.assertIn('"docs/releases/v1.5.3.md"', prepare)
        self.assertIn("tag_name != 'v1.5.3'", legacy)
        self.assertIn(
            "Attach approved v1.5.3 Windows assets (source builds on Linux and macOS)",
            pages,
        )
        self.assertNotIn("contents: write", prepare)
        self.assertNotIn("gh release create", prepare)


if __name__ == "__main__":
    unittest.main()
