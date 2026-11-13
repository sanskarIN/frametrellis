# SPDX-License-Identifier: GPL-3.0-or-later
"""Offline tests for strict extraction of full production GitHub Actions stages."""
from __future__ import annotations

import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from assemble_candidate_matrix import platform_names  # noqa: E402
from extract_ci_stage import unpack  # noqa: E402

SHA = "a" * 40
VERSION = "1.5.0"
PLATFORM = "linux-x86_64"


def entries() -> list[tuple[str, bytes]]:
    names = platform_names(VERSION, PLATFORM) | {"SOURCE_COMMIT.txt"}
    return [(name, (SHA + "\n").encode() if name == "SOURCE_COMMIT.txt" else b"package")
            for name in sorted(names)]


def archive_file(dest: Path, members: list[tuple[str, bytes]]) -> Path:
    with zipfile.ZipFile(dest, "w") as archive:
        for name, content in members:
            archive.writestr(name, content)
    return dest


class ProductionStageExtractionTests(unittest.TestCase):
    def test_exact_stage_extracts_without_changing_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            file = archive_file(root / "release.zip", entries())
            original = file.read_bytes()
            result = unpack(file, root / "stage", VERSION, PLATFORM, SHA)
            self.assertEqual(set(result), {name for name, _ in entries()})
            self.assertEqual(file.read_bytes(), original)
            self.assertFalse((root / "stage" / "INCOMPLETE-DO-NOT-PUBLISH").exists())
            self.assertEqual((root / "stage" / "SOURCE_COMMIT.txt").read_text(), SHA + "\n")

    def test_mismatched_source_rejected_before_output_creation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            data = [(name, b"b" * 40 + b"\n" if name == "SOURCE_COMMIT.txt" else content)
                    for name, content in entries()]
            file = archive_file(root / "release.zip", data)
            with self.assertRaisesRegex(ValueError, "source commit"):
                unpack(file, root / "stage", VERSION, PLATFORM, SHA)
            self.assertFalse((root / "stage").exists())

    def test_missing_extra_and_traversal_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for variant in (
                entries()[:-1],
                entries() + [("unexpected.txt", b"x")],
                entries()[:-1] + [("../escape", b"bad")],
            ):
                file = archive_file(root / "release.zip", variant)
                with self.assertRaises(ValueError):
                    unpack(file, root / "stage", VERSION, PLATFORM, SHA)
                self.assertFalse((root / "stage").exists())

    def test_duplicate_member_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            member = entries()[0]
            file = archive_file(root / "dup.zip", entries() + [member])
            with self.assertRaises(ValueError):
                unpack(file, root / "stage", VERSION, PLATFORM, SHA)
            self.assertFalse((root / "stage").exists())

    def test_existing_directory_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            file = archive_file(root / "release.zip", entries())
            out = root / "stage"
            out.mkdir()
            (out / "important.txt").write_text("keep")
            with self.assertRaisesRegex(ValueError, "Refusing to replace"):
                unpack(file, out, VERSION, PLATFORM, SHA)
            self.assertEqual((out / "important.txt").read_text(), "keep")


if __name__ == "__main__":
    unittest.main()
