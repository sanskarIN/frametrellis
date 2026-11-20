# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard the separately approved v1.5.1 five-asset release route."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / ".github/workflows/release-assets-v151-source.yml"
PREPARE = ROOT / ".github/workflows/release.yml"
LEGACY = ROOT / ".github/workflows/release-assets.yml"
PAGES = ROOT / ".github/workflows/pages.yml"
MARKER = "FrameTrellis-1.5.1-SOURCE-BUILD-OWNER-APPROVED"
NAME = "Attach approved v1.5.1 Windows assets (source builds on Linux and macOS)"


class PatchReleaseWorkflowTests(unittest.TestCase):
    def test_exact_owner_marker_tag_and_version_are_not_inherited(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("tag_name == 'v1.5.1'", source)
        self.assertIn("contains(github.event.release.body, '" + MARKER + "')", source)
        self.assertIn("grep -Fxq", source)
        self.assertIn('"$project" == "1.5.1"', source)
        self.assertIn("--v151-source-build-assets", source)
        self.assertIn("docs/releases/v1.5.1.md", source)
        self.assertNotIn("FrameTrellis-1.5.0-SOURCE-BUILD-OWNER-APPROVED", source)
        self.assertNotIn("workflow_dispatch:", source)

    def test_immutable_sha_and_five_payloads_verified(self):
        source = SOURCE.read_text(encoding="utf-8")
        for token in (
            '[[ "$tag_sha" == "$main_sha" ]]',
            'head_sha == $sha',
            'head_branch == "main"',
            "ci.yml desktop-platforms.yml",
            "actions/download-artifact@v7",
            "packaging/verify_release_assets.py --version 1.5.1",
            "sha256sum --check SHA256SUMS.txt",
            "packaging/verify_published_windows_v15.py",
            '[[ "$existing" == "0" ]]',
        ):
            with self.subTest(token=token):
                self.assertIn(token, source)
        names = set(re.findall(r"FrameTrellis-1\\.5\\.1-windows-x64[.\\-][\\w.\\-]+", source))
        self.assertEqual(names, {
            "FrameTrellis-1.5.1-windows-x64-setup.exe",
            "FrameTrellis-1.5.1-windows-x64.zip",
            "FrameTrellis-1.5.1-windows-x64.spdx.json",
            "FrameTrellis-1.5.1-windows-x64-third-party-sources.tar",
        })

    def test_all_workflows_keep_strict_version_routing(self):
        prep = PREPARE.read_text(encoding="utf-8")
        legacy = LEGACY.read_text(encoding="utf-8")
        pages = PAGES.read_text(encoding="utf-8")
        self.assertIn('elif [[ "$project" == "1.5.1" ]]', prep)
        self.assertIn('policy="--v151-source-build-assets"', prep)
        self.assertIn("docs/releases/v1.5.1.md", prep)
        self.assertIn("tag_name != 'v1.5.1'", legacy)
        self.assertIn('"' + NAME + '"', pages)
        self.assertIn("github.event.workflow_run.conclusion == 'success'", pages)

    def test_no_automated_release_creation(self):
        source = SOURCE.read_text(encoding="utf-8")
        for forbidden in ("gh release create", "git tag ", "git push", "gh release edit"):
            self.assertNotIn(forbidden, source)
        self.assertIn("release:\\n    types: [published]".replace("\\n", "\n"), source)


if __name__ == "__main__":
    unittest.main()
