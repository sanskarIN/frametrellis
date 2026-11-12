# SPDX-License-Identifier: GPL-3.0-or-later
"""Network-free acceptance and rejection tests for future 14-file candidate assembly."""
from __future__ import annotations

import hashlib
import io
import json
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "assemble_candidate_matrix.py"
sys.path.insert(0, str(SCRIPT.parent))
from assemble_candidate_matrix import assemble, platform_names  # noqa: E402
from verify_candidate_matrix import verify  # noqa: E402

VERSION = "1.5.0"
COMMIT = "a" * 40


def valid_payload(name: str) -> bytes:
    if name.endswith(".spdx.json"):
        return json.dumps({
            "spdxVersion": "SPDX-2.3",
            "packages": [{"name": "FrameTrellis", "versionInfo": VERSION}],
        }).encode("utf-8")
    if name.endswith(".zip"):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("FrameTrellis/frametrellis.exe", b"gui")
            archive.writestr("FrameTrellis/frametrellis-render.exe", b"render")
        return data.getvalue()
    if name.endswith("-third-party-sources.tar"):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w") as archive:
            info = tarfile.TarInfo("source/README.txt")
            info.size = 12
            archive.addfile(info, io.BytesIO(b"test sources"))
        return data.getvalue()
    if name.endswith(".AppImage"):
        return b"\x7fELF" + b"sample appimage"
    if name.endswith(".dmg"):
        return b"sample dmg" + b"0" * 502 + b"koly" + b"0" * 508
    if name.endswith("-setup.exe"):
        return b"MZ" + b"sample installer"
    raise AssertionError(f"unknown fixture: {name}")


def create_stages(root: Path) -> dict[str, Path]:
    stages: dict[str, Path] = {}
    for platform in ("windows-x64", "linux-x86_64", "macos-arm64", "macos-x86_64"):
        directory = root / platform
        directory.mkdir()
        (directory / "SOURCE_COMMIT.txt").write_text(COMMIT + "\n", encoding="ascii")
        for filename in platform_names(VERSION, platform):
            (directory / filename).write_bytes(valid_payload(filename))
        stages[platform] = directory
    return stages


class CandidateAssemblerTests(unittest.TestCase):
    def test_complete_four_stage_bundle_is_verified(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            out = root / "final"
            self.assertEqual(len(assemble(VERSION, COMMIT, stages, out)), 13)
            self.assertEqual(len(verify(out, VERSION)), 13)
            self.assertEqual(len(list(out.iterdir())), 14)
            self.assertFalse((out / "SOURCE_COMMIT.txt").exists())
            self.assertFalse((out / "INCOMPLETE-DO-NOT-PUBLISH").exists())
            lines = (out / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
            self.assertEqual(len(lines), 13)
            for line in lines:
                digest, filename = line.split("  ", 1)
                self.assertEqual(digest, hashlib.sha256((out / filename).read_bytes()).hexdigest())

    def test_cross_arch_source_mismatch_rejected_before_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            (stages["macos-x86_64"] / "SOURCE_COMMIT.txt").write_text(
                "b" * 40 + "\n", encoding="ascii"
            )
            with self.assertRaisesRegex(ValueError, "mismatched"):
                assemble(VERSION, COMMIT, stages, root / "final")
            self.assertFalse((root / "final").exists())

    def test_missing_component_and_experimental_payload_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            name = f"FrameTrellis-{VERSION}-linux-x86_64.AppImage"
            (stages["linux-x86_64"] / name).unlink()
            (stages["linux-x86_64"] / "FrameTrellis-linux-EXPERIMENTAL.AppImage").write_bytes(
                b"experiment"
            )
            with self.assertRaisesRegex(ValueError, "missing or unexpected"):
                assemble(VERSION, COMMIT, stages, root / "final")
            self.assertFalse((root / "final").exists())

    def test_source_evidence_needs_exact_newline(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            (stages["windows-x64"] / "SOURCE_COMMIT.txt").write_text(COMMIT, encoding="ascii")
            with self.assertRaisesRegex(ValueError, "noncanonical"):
                assemble(VERSION, COMMIT, stages, root / "final")

    def test_existing_candidate_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            output = root / "final"
            output.mkdir()
            (output / "private.txt").write_text("keep this", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
                assemble(VERSION, COMMIT, stages, output)
            self.assertEqual((output / "private.txt").read_text(), "keep this")

    def test_rejects_invalid_matching_hash_zip_and_marks_incomplete(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            name = f"FrameTrellis-{VERSION}-windows-x64.zip"
            (stages["windows-x64"] / name).write_bytes(b"PKfake-not-a-real-zip")
            output = root / "final"
            with self.assertRaises(zipfile.BadZipFile):
                assemble(VERSION, COMMIT, stages, output)
            self.assertTrue((output / "INCOMPLETE-DO-NOT-PUBLISH").is_file())
            with self.assertRaisesRegex(ValueError, "unexpected"):
                verify(output, VERSION)

    def test_symlinked_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stages = create_stages(root)
            name = f"FrameTrellis-{VERSION}-linux-x86_64.AppImage"
            file = stages["linux-x86_64"] / name
            file.unlink()
            file.symlink_to("SOURCE_COMMIT.txt")
            with self.assertRaisesRegex(ValueError, "Unsafe"):
                assemble(VERSION, COMMIT, stages, root / "final")


if __name__ == "__main__":
    unittest.main()
