# SPDX-License-Identifier: GPL-3.0-or-later
"""Safeguard both mutually exclusive v1.5.0 owner-operated release uploaders."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / ".github/workflows/release-assets-v15-source.yml"
FULL = ROOT / ".github/workflows/release-assets-v15.yml"
PREPARE = ROOT / ".github/workflows/release.yml"
SOURCE_MARKER = "FrameTrellis-1.5.0-SOURCE-BUILD-OWNER-APPROVED"
FULL_MARKER = "FrameTrellis-1.5.0-OWNER-APPROVED"


class ReleaseScopeWorkflowTests(unittest.TestCase):
    def test_mutually_exclusive_owner_approval_marks(self):
        src = SOURCE.read_text(encoding="utf-8")
        full = FULL.read_text(encoding="utf-8")
        self.assertIn("contains(github.event.release.body, '" + SOURCE_MARKER + "')", src)
        self.assertIn("!contains(github.event.release.body, '" + FULL_MARKER + "')", src)
        self.assertIn("contains(github.event.release.body, '" + FULL_MARKER + "')", full)
        self.assertIn("!contains(github.event.release.body, '" + SOURCE_MARKER + "')", full)
        self.assertIn("grep -Fxq", src)
        self.assertIn("grep -Fxq", full)
        self.assertNotIn("workflow_dispatch:", src)

    def test_five_files_and_exact_source_ci(self):
        src = SOURCE.read_text(encoding="utf-8")
        for feature in (
            "source.outputs.sha", "ci.yml desktop-platforms.yml",
            "actions/workflows/release.yml/runs?status=success",
            "head_sha == $sha", 'head_branch == "main"',
            'event == "push"', 'conclusion == "success"',
            "actions/download-artifact@v7",
            "packaging/verify_release_assets.py",
            "sha256sum --check SHA256SUMS.txt",
            "packaging/verify_published_windows_v15.py",
            '[[ "$existing" == "0" ]]',
        ):
            with self.subTest(feature=feature):
                self.assertIn(feature, src)
        paths = re.findall(r"\bFrameTrellis-1\.5\.0-windows-x64[.\-][\w.\-]+", src)
        self.assertEqual(set(paths), {
            "FrameTrellis-1.5.0-windows-x64-setup.exe",
            "FrameTrellis-1.5.0-windows-x64.zip",
            "FrameTrellis-1.5.0-windows-x64.spdx.json",
            "FrameTrellis-1.5.0-windows-x64-third-party-sources.tar",
        })

    def test_preparation_keeps_legacy_gate_and_explicit_source_option(self):
        prepare = PREPARE.read_text(encoding="utf-8")
        self.assertIn('policy="--windows-only-assets"', prepare)
        self.assertIn('if [[ "$project" == "1.5.0" ]]', prepare)
        self.assertIn('policy="--v15-source-build-assets"', prepare)
        self.assertIn("manual-release-assets", prepare)
        self.assertNotIn("gh release create", prepare)

    def test_website_republishes_after_either_uploader(self):
        page_workflow = (ROOT / ".github/workflows/pages.yml").read_text(
            encoding="utf-8"
        )
        for name in (
            "Attach verified release assets",
            "Attach approved v1.5.0 three-platform assets",
            "Attach approved v1.5.0 Windows assets (source builds on Linux and macOS)",
        ):
            with self.subTest(workflow=name):
                self.assertIn('"' + name + '"', page_workflow)
        self.assertIn("github.event.workflow_run.conclusion == 'success'", page_workflow)

    def test_uploaders_cannot_create_tags_or_releases(self):
        for path in (SOURCE, FULL):
            source = path.read_text(encoding="utf-8")
            with self.subTest(path=path.name):
                self.assertNotIn("gh release create", source)
                self.assertNotIn("git push", source)
                self.assertNotIn("git tag ", source)
                self.assertIn("release:\n    types: [published]", source)


if __name__ == "__main__":
    unittest.main()
