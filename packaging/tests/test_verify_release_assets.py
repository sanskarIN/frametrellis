# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Network-free positive and negative checks for manual-release asset validation."""

from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "verify_release_assets.py"
spec = importlib.util.spec_from_file_location("verify_release_assets", SOURCE)
assert spec and spec.loader
verifier = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verifier)


class ReleaseAssetVerificationTests(unittest.TestCase):
    VERSION = "1.4.0"

    def setUp(self):
        self.work = tempfile.TemporaryDirectory()
        self.addCleanup(self.work.cleanup)
        self.root = Path(self.work.name)
        prefix = f"FrameTrellis-{self.VERSION}-windows-x64"
        self.installer = self.root / f"{prefix}-setup.exe"
        self.portable = self.root / f"{prefix}.zip"
        self.spdx = self.root / f"{prefix}.spdx.json"
        self.sources = self.root / f"{prefix}-third-party-sources.tar"
        self.installer.write_bytes(b"synthetic test installer")
        with zipfile.ZipFile(self.portable, "w") as archive:
            archive.writestr("FrameTrellis/frametrellis.exe", b"synthetic test executable")
        self.spdx.write_text(
            json.dumps(
                {
                    "spdxVersion": "SPDX-2.3",
                    "packages": [{"name": "FrameTrellis", "versionInfo": self.VERSION}],
                }
            ),
            encoding="utf-8",
        )
        with tarfile.open(self.sources, "w") as archive:
            for name in (
                "pkg/FrameTrellis-1.4.0-windows-x64-sources.json",
                "pkg/mingw-w64-test-1.0-1.src.tar.zst",
            ):
                data = b"test"
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        self.refresh_manifest()

    def refresh_manifest(self):
        self.manifest = self.root / "SHA256SUMS.txt"
        files = (self.installer, self.portable, self.spdx, self.sources)
        self.manifest.write_text(
            "".join(
                f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n"
                for file in sorted(files)
            ),
            encoding="utf-8",
        )

    def test_valid_release_with_optional_notes(self):
        (self.root / "release-notes.md").write_text("Release notes", encoding="utf-8")
        self.assertEqual(len(verifier.verify(self.root, self.VERSION)), 4)

    def test_rejects_modified_payload(self):
        self.installer.write_bytes(b"tampered file")
        with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_duplicate_manifest_entries(self):
        line = self.manifest.read_text(encoding="utf-8").splitlines()[0] + "\n"
        self.manifest.write_text(line * 4, encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "duplicate"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_missing_payload(self):
        self.sources.unlink()
        with self.assertRaisesRegex(ValueError, "missing="):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_unexpected_release_asset(self):
        (self.root / "extra.exe").write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "unexpected="):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_malformed_version(self):
        with self.assertRaisesRegex(ValueError, "Invalid numeric"):
            verifier.verify(self.root, "../1.4.0")

    def test_rejects_missing_portable_executable(self):
        with zipfile.ZipFile(self.portable, "w") as archive:
            archive.writestr("README.txt", b"no executable")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "frametrellis.exe"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_unsafe_portable_member(self):
        with zipfile.ZipFile(self.portable, "w") as archive:
            archive.writestr("../frametrellis.exe", b"unsafe")
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "Unsafe portable ZIP"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_wrong_spdx_version(self):
        self.spdx.write_text(
            json.dumps(
                {
                    "spdxVersion": "SPDX-2.3",
                    "packages": [{"name": "FrameTrellis", "versionInfo": "1.3.1"}],
                }
            ),
            encoding="utf-8",
        )
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "does not describe"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_tar_without_source_packages(self):
        with tarfile.open(self.sources, "w") as archive:
            info = tarfile.TarInfo("pkg/FrameTrellis-1.4.0-windows-x64-sources.json")
            info.size = 0
            archive.addfile(info, io.BytesIO())
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "missing source packages"):
            verifier.verify(self.root, self.VERSION)

    def test_rejects_tar_path_traversal(self):
        with tarfile.open(self.sources, "w") as archive:
            for name in ("pkg/sources.json", "../evil.src.tar.zst"):
                info = tarfile.TarInfo(name)
                info.size = 0
                archive.addfile(info, io.BytesIO())
        self.refresh_manifest()
        with self.assertRaisesRegex(ValueError, "Unsafe corresponding-source"):
            verifier.verify(self.root, self.VERSION)


if __name__ == "__main__":
    unittest.main()
