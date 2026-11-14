# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests for the fail-closed public v1.5 source-build release verifier."""
from __future__ import annotations

import hashlib
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from verify_published_windows_v15 import expected_names, verify


class PublishedWindowsV15Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.assets = []
        for name in sorted(expected_names("1.5.0")):
            data = b"not actual release media: test fixture/" + name.encode("utf-8")
            (self.root / name).write_bytes(data)
            self.assets.append({
                "name": name, "size": len(data),
                "state": "uploaded",
                "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
            })

    def test_exact_set_succeeds(self):
        self.assertEqual(len(verify(self.assets, self.root, "1.5.0")), 5)

    def test_version_cannot_silently_change(self):
        with self.assertRaisesRegex(ValueError, "v1.5.0 only"):
            verify(self.assets, self.root, "1.5.1")

    def test_missing_extra_and_duplicate_rejected(self):
        for changed in (
            self.assets[:-1],
            self.assets + [{"name": "foreign.exe"}],
            self.assets + [dict(self.assets[0])],
        ):
            with self.subTest(count=len(changed)), self.assertRaises(ValueError):
                verify(changed, self.root, "1.5.0")

    def test_changed_hash_or_metadata_rejected(self):
        for field, value in (
            ("digest", "sha256:" + "0" * 64),
            ("digest", None),
            ("state", "processing"),
            ("size", 0),
        ):
            changed = [dict(x) for x in self.assets]
            changed[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify(changed, self.root, "1.5.0")

    def test_modified_file_or_symlink_rejected(self):
        name = self.assets[0]["name"]
        (self.root / name).write_bytes(b"changed")
        with self.assertRaises(ValueError):
            verify(self.assets, self.root, "1.5.0")
        (self.root / name).unlink()
        (self.root / name).symlink_to(self.root / self.assets[1]["name"])
        with self.assertRaises(ValueError):
            verify(self.assets, self.root, "1.5.0")

    def test_malformed_json_shape_rejected(self):
        for sample in (None, {}, "hello", [None]):
            with self.subTest(sample=sample), self.assertRaises(ValueError):
                verify(sample, self.root, "1.5.0")


if __name__ == "__main__":
    unittest.main()
