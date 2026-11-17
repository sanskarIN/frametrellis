# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Read-only classification tests for v1.5.0 historical approval markers."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from release_publication_guard import (
    SOURCE_BUILD_MARKER,
    THREE_PLATFORM_MARKER,
    publication_decision,
)


class ReleasePublicationGuardTests(unittest.TestCase):
    @staticmethod
    def release(**overrides):
        value = {
            "tag_name": "v1.5.0",
            "draft": False,
            "prerelease": False,
            "body": "FrameTrellis 1.5.0",
        }
        value.update(overrides)
        return value

    def test_unapproved_publication_is_flagged_for_owner_review(self):
        self.assertEqual(publication_decision(self.release()), "owner-review-required")

    def test_source_build_marker_is_accepted_as_one_exact_line(self):
        release = self.release(body=f"Notes\n{SOURCE_BUILD_MARKER}\n")
        self.assertEqual(publication_decision(release), "approved-source-build")

    def test_three_platform_marker_is_accepted_as_one_exact_line(self):
        release = self.release(body=f"Notes\n{THREE_PLATFORM_MARKER}\n")
        self.assertEqual(publication_decision(release), "approved-three-platform")

    def test_missing_or_substring_only_marker_requires_owner_review(self):
        for body in (
            f"Approval requested: {SOURCE_BUILD_MARKER}",
            f"Inline marker: {THREE_PLATFORM_MARKER}",
            "No approval marker",
        ):
            with self.subTest(body=body):
                self.assertEqual(
                    publication_decision(self.release(body=body)), "owner-review-required"
                )

    def test_conflicting_markers_require_owner_review(self):
        bodies = (
            f"{SOURCE_BUILD_MARKER}\n{THREE_PLATFORM_MARKER}",
            f"{SOURCE_BUILD_MARKER}\n{THREE_PLATFORM_MARKER} extra",
        )
        for body in bodies:
            with self.subTest(body=body):
                self.assertEqual(
                    publication_decision(self.release(body=body)), "owner-review-required"
                )

    def test_repeated_selected_marker_matches_existing_exact_line_gate(self):
        release = self.release(body=f"{SOURCE_BUILD_MARKER}\n{SOURCE_BUILD_MARKER}")
        self.assertEqual(publication_decision(release), "approved-source-build")

    def test_other_tags_and_prereleases_are_ignored(self):
        self.assertEqual(
            publication_decision(self.release(tag_name="v1.5.1")), "ignore"
        )
        self.assertEqual(
            publication_decision(self.release(prerelease=True)), "ignore"
        )

    def test_existing_draft_is_never_modified(self):
        self.assertEqual(
            publication_decision(self.release(draft=True)), "already-draft"
        )

    def test_missing_draft_state_is_rejected(self):
        release = self.release()
        del release["draft"]
        with self.assertRaisesRegex(ValueError, "draft boolean"):
            publication_decision(release)

    def test_non_text_release_body_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "body must be text"):
            publication_decision(self.release(body=[]))


if __name__ == "__main__":
    unittest.main()
