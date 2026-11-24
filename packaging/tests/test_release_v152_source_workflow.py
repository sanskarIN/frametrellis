# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard v1.5.2's distinct, exact-SHA, owner-approved five-file upload path."""
from __future__ import annotations

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / ".github/workflows/release-assets-v152-source.yml"
PREPARE = ROOT / ".github/workflows/release.yml"
LEGACY = ROOT / ".github/workflows/release-assets.yml"
PAGES = ROOT / ".github/workflows/pages.yml"
PREVIOUS = ROOT / ".github/workflows/release-assets-v151-source.yml"
WEBSITE = ROOT / ".github/workflows/website.yml"
RELEASE_VERSION = ROOT / "packaging/release_version.py"
MARKER = "FrameTrellis-1.5.2-SOURCE-BUILD-OWNER-APPROVED"
NAME = "Attach approved v1.5.2 Windows assets (source builds on Linux and macOS)"


class V152ReleaseSafetyTests(unittest.TestCase):
    def test_exact_version_tag_and_separate_owner_marker(self):
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("tag_name == 'v1.5.2'", source)
        self.assertIn("contains(github.event.release.body, '" + MARKER + "')", source)
        self.assertIn("grep -Fxq", source)
        self.assertIn('"$project" == "1.5.2"', source)
        self.assertIn("--v152-source-build-assets", source)
        self.assertIn("docs/releases/v1.5.2.md", source)
        self.assertNotIn("FrameTrellis-1.5.1-SOURCE-BUILD-OWNER-APPROVED", source)
        self.assertNotIn("workflow_dispatch:", source)
        self.assertIn("head -n 20", source)

    def test_sha_artifacts_checksum_and_exact_five_assets(self):
        source = SOURCE.read_text(encoding="utf-8")
        for token in (
            '[[ "$tag_sha" == "$main_sha" ]]',
            'head_sha == $sha',
            'head_branch == "main"',
            "ci.yml desktop-platforms.yml package-windows.yml website.yml",
            "actions/download-artifact@v7",
            "packaging/verify_release_assets.py --version 1.5.2",
            "sha256sum --check SHA256SUMS.txt",
            "packaging/verify_published_windows_v15.py",
            '[[ "$existing" == "0" ]]',
        ):
            with self.subTest(token=token):
                self.assertIn(token, source)
        names = set(re.findall(r"FrameTrellis-1\.5\.2-windows-x64[.\-][\w.\-]+", source))
        self.assertEqual(names, {
            "FrameTrellis-1.5.2-windows-x64-setup.exe",
            "FrameTrellis-1.5.2-windows-x64.zip",
            "FrameTrellis-1.5.2-windows-x64.spdx.json",
            "FrameTrellis-1.5.2-windows-x64-third-party-sources.tar",
        })

    def test_website_build_is_a_successful_exact_main_sha_release_gate(self):
        # The uploader's jq check also requires head_branch == main,
        # event == push and a completed/success result on the frozen SHA.
        # Without an unconditional main-push website trigger, a C++-only
        # release candidate could never meet this requirement.
        source = SOURCE.read_text(encoding="utf-8")
        website = WEBSITE.read_text(encoding="utf-8")
        self.assertIn(
            "ci.yml desktop-platforms.yml package-windows.yml website.yml",
            source,
        )
        self.assertRegex(website, r"(?m)^  push:\n    branches: \[main\]$")
        self.assertIn(
            "node web/build.mjs --release-json release.json --verify-downloads",
            website,
        )
        self.assertIn("node --test web/tests/*.test.mjs", website)
        self.assertIn("contents: read", website)
        self.assertNotIn("contents: write", website)

    def test_no_legacy_collision_or_automated_publication(self):
        source = SOURCE.read_text(encoding="utf-8")
        legacy = LEGACY.read_text(encoding="utf-8")
        previous = PREVIOUS.read_text(encoding="utf-8")
        prep = PREPARE.read_text(encoding="utf-8")
        pages = PAGES.read_text(encoding="utf-8")
        policy = RELEASE_VERSION.read_text(encoding="utf-8")
        self.assertIn("tag_name != 'v1.5.2'", legacy)
        self.assertIn("tag_name == 'v1.5.1'", previous)
        self.assertNotIn("tag_name == 'v1.5.2'", previous)
        self.assertIn('elif [[ "$project" == "1.5.2" ]]', prep)
        self.assertIn('policy="--v152-source-build-assets"', prep)
        self.assertIn('"docs/releases/v1.5.2.md"', prep)
        self.assertIn("def require_v152_source_build_assets(", policy)
        self.assertIn('"' + NAME + '"', pages)
        self.assertIn("github.event.workflow_run.conclusion == 'success'", pages)
        for forbidden in ("gh release create", "git tag ", "git push", "gh release edit"):
            self.assertNotIn(forbidden, source)
        self.assertIn("release:\n    types: [published]", source)

    def test_offline_bundle_is_verified_before_upload(self):
        prepare = PREPARE.read_text(encoding="utf-8")
        verify = prepare.index("name: Independently verify offline stable candidate")
        upload = prepare.index("name: Save complete verified files")
        self.assertLess(verify, upload)
        step = prepare[verify:upload]
        for token in (
            "env.RELEASE_VERSION == '1.5.2'",
            "set -euo pipefail",
            'packaging/verify_release_assets.py --version "$VERSION" --directory release',
            "sha256sum --check SHA256SUMS.txt",
            "test -s release-notes.md",
            "result.startswith(source)",
        ):
            with self.subTest(token=token):
                self.assertIn(token, step)
        self.assertNotIn("continue-on-error", step)
        self.assertNotIn("|| true", step)
        self.assertIn("name: manual-release-assets", prepare[upload:])
        self.assertNotIn("contents: write", prepare)

    def test_draft_and_version_bump_are_separately_gated(self):
        # A release-docs-only PR cannot silently change the published program
        # version or make the candidate's draft release notes uploadable.
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        notes = ROOT / "docs/releases/v1.5.2.md"
        self.assertTrue(notes.is_file())
        self.assertTrue((ROOT / "docs/releases/v1.5.2-preflight.md").is_file())
        if "VERSION 1.5.1" in cmake:
            # This PR does not bump CMake. Its notes must remain clearly draft
            # until a separate release candidate is qualified.
            self.assertIn("NOT PUBLISHED", notes.read_text(encoding="utf-8").upper())
        else:
            # A later release-version PR may remove the draft warning only
            # after deliberately changing the CMake version.
            self.assertIn("VERSION 1.5.2", cmake)


if __name__ == "__main__":
    unittest.main()
