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

    def test_unsupported_version_cannot_silently_change(self):
        for version in ("1.4.0", "1.5.3", "1.5.2-rc.1", "v1.5.2", "../1.5.2"):
            with self.subTest(version=version), self.assertRaisesRegex(
                ValueError, "supports only"
            ):
                verify(self.assets, self.root, version)

    def test_v152_requires_exact_assets_and_digests(self):
        version = "1.5.2"
        names = {
            "FrameTrellis-1.5.2-windows-x64-setup.exe",
            "FrameTrellis-1.5.2-windows-x64.zip",
            "FrameTrellis-1.5.2-windows-x64.spdx.json",
            "FrameTrellis-1.5.2-windows-x64-third-party-sources.tar",
            "SHA256SUMS.txt",
        }
        self.assertEqual(expected_names(version), names)
        candidate = []
        for name in sorted(names):
            data = b"v152 fixture/" + name.encode("utf-8")
            (self.root / name).write_bytes(data)
            candidate.append({
                "name": name, "size": len(data),
                "state": "uploaded",
                "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
            })
        self.assertEqual(set(verify(candidate, self.root, version)), names)
        for previous in ("1.5.0", "1.5.1"):
            with self.subTest(previous=previous), self.assertRaises(ValueError):
                verify(candidate, self.root, previous)
        for field, value in (
            ("digest", "sha256:" + "0" * 64),
            ("state", "processing"),
            ("size", 0),
            ("name", "FrameTrellis-1.5.1-windows-x64.zip"),
        ):
            changed = [dict(x) for x in candidate]
            changed[0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                verify(changed, self.root, version)

    def test_v151_requires_its_own_exact_verified_asset_set(self):
        version = "1.5.1"
        candidate = []
        for name in sorted(expected_names(version)):
            data = b"v151 test fixture/" + name.encode("utf-8")
            (self.root / name).write_bytes(data)
            candidate.append({
                "name": name, "size": len(data), "state": "uploaded",
                "digest": "sha256:" + hashlib.sha256(data).hexdigest(),
            })
        self.assertEqual(len(verify(candidate, self.root, version)), 5)
        with self.assertRaises(ValueError):
            verify(self.assets, self.root, version)

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
