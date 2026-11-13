# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests of strict exact-byte published GitHub Release asset validation."""
from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_candidate_matrix import required_payloads
from verify_published_candidate import verify_published

VERSION = "1.5.0"


def assets(root: Path) -> list[dict]:
    result = []
    for name in sorted(required_payloads(VERSION) | {"SHA256SUMS.txt"}):
        data = (name + "\n").encode("ascii")
        (root / name).write_bytes(data)
        result.append({
            "name": name,
            "size": len(data),
            "state": "uploaded",
            "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
        })
    return result


class PublishedCandidateTests(unittest.TestCase):
    def test_all_fourteen_exact_files_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            expected = assets(root)
            self.assertEqual(len(verify_published(expected, root, VERSION)), 14)

    def test_missing_and_unexpected_assets_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            items = assets(root)
            with self.assertRaisesRegex(ValueError, "Incomplete"):
                verify_published(items[:-1], root, VERSION)
            with self.assertRaisesRegex(ValueError, "unexpected"):
                verify_published(items + [{"name": "mystery", "size": 1}], root, VERSION)

    def test_matching_name_size_but_wrong_digest_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            items = assets(root)
            items[0]["digest"] = "sha256:" + "0" * 64
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                verify_published(items, root, VERSION)
            items[0].pop("digest")
            with self.assertRaisesRegex(ValueError, "SHA-256"):
                verify_published(items, root, VERSION)

    def test_incomplete_upload_state_or_wrong_size_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            items = assets(root)
            items[0]["state"] = "starter"
            with self.assertRaisesRegex(ValueError, "size/state"):
                verify_published(items, root, VERSION)
            items[0]["state"] = "uploaded"
            items[0]["size"] += 1
            with self.assertRaisesRegex(ValueError, "size/state"):
                verify_published(items, root, VERSION)

    def test_duplicate_public_asset_names_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            items = assets(root)
            with self.assertRaisesRegex(ValueError, "Duplicate"):
                verify_published(items + [dict(items[0])], root, VERSION)


if __name__ == "__main__":
    unittest.main()
