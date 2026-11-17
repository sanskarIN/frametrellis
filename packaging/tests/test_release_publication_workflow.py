# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Prevent reintroducing automatic edits to published GitHub release state."""

import unittest
from pathlib import Path


WORKFLOW = (
    Path(__file__).resolve().parents[2]
    / ".github"
    / "workflows"
    / "guard-v15-publication.yml"
)


class ReadOnlyPublicationWorkflowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = WORKFLOW.read_text(encoding="utf-8")

    def test_audit_uses_only_read_permissions(self):
        self.assertIn("contents: read", self.workflow)
        self.assertNotRegex(self.workflow, r"(?m)^\s*contents:\s*write\s*$")
        self.assertNotRegex(self.workflow, r"(?m)^\s*actions:\s*write\s*$")

    def test_automatic_draft_restoration_is_forbidden(self):
        for prohibited in (
            "gh api --method PATCH",
            "gh release edit",
            "gh release delete",
            "draft=true",
            "-F draft=",
        ):
            with self.subTest(prohibited=prohibited):
                self.assertNotIn(prohibited, self.workflow)

    def test_unapproved_release_is_warning_not_mutation(self):
        self.assertIn("owner-review-required", self.workflow)
        self.assertIn("::warning::Published v1.5.0", self.workflow)
        self.assertIn(
            "Publication and unpublication require an explicit owner action",
            self.workflow,
        )

    def test_remains_triggered_on_main_and_release_changes(self):
        self.assertRegex(self.workflow, r"(?m)^\s*branches: \[main\]$")
        self.assertIn("types: [published, edited]", self.workflow)
        self.assertRegex(self.workflow, r"(?m)^  workflow_dispatch:$")


if __name__ == "__main__":
    unittest.main()
