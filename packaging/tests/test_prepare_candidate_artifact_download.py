# SPDX-License-Identifier: GPL-3.0-or-later
"""Guard the authenticated v1.5 candidate download against unsupported CLI flags."""
from __future__ import annotations

import unittest
from pathlib import Path

WORKFLOW = (Path(__file__).resolve().parents[2]
            / ".github/workflows/prepare-v15-candidate.yml")


class CandidateArtifactDownloadTests(unittest.TestCase):
    def test_gh_api_download_uses_stdout_not_unsupported_output_option(self):
        source = WORKFLOW.read_text(encoding="utf-8")
        self.assertRegex(
            source,
            r'gh api "repos/\$GITHUB_REPOSITORY/actions/artifacts/\$id/zip" > "\$archive"',
        )
        self.assertNotRegex(source, r'gh api[^\n]*--output')
        self.assertIn('test -s "$archive"', source)

    def test_download_is_followed_by_exact_authenticated_digest_verification(self):
        source = WORKFLOW.read_text(encoding="utf-8")
        download = source.index('gh api "repos/$GITHUB_REPOSITORY/actions/artifacts/$id/zip"')
        checksum = source.index('sha256sum --check --status', download)
        extraction = source.index('python3 packaging/extract_ci_stage.py', download)
        self.assertLess(download, checksum)
        self.assertLess(checksum, extraction)
        self.assertRegex(
            source[download:extraction],
            r'printf.*digest#sha256:.*sha256sum --check --status',
        )


if __name__ == "__main__":
    unittest.main()
