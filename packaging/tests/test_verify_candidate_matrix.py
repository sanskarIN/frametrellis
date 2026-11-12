# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the future three-platform release asset integrity gate."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "verify_candidate_matrix.py"
SPEC = importlib.util.spec_from_file_location("candidate_asset_matrix", SCRIPT)
assert SPEC and SPEC.loader
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


def fixture_payload(name: str) -> bytes:
    if name.endswith(".spdx.json"):
        return json.dumps({
            "spdxVersion": "SPDX-2.3",
            "packages": [{"name": "FrameTrellis", "versionInfo": "1.5.0"}],
        }).encode("utf-8")
    if name.endswith(".AppImage"):
        return b"\x7fELFdummy"
    if name.endswith(".dmg"):
        return b"prefix" + b"0" * 506 + b"koly" + b"0" * 508
    if name.endswith("-setup.exe"):
        return b"MZinstaller"
    if name.endswith(".zip"):
        return b"PKfake"
    return b"third-party-source-archive-placeholder"


def make_candidate(root: Path) -> None:
    checksums: list[str] = []
    for name in sorted(matrix.required_payloads("1.5.0")):
        payload = fixture_payload(name)
        (root / name).write_bytes(payload)
        checksums.append(f"{hashlib.sha256(payload).hexdigest()}  {name}")
    (root / "SHA256SUMS.txt").write_text(
        "\n".join(checksums) + "\n", encoding="utf-8"
    )


class CandidateMatrixTests(unittest.TestCase):
    def test_complete_three_platform_candidate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            self.assertEqual(len(matrix.verify(root, "1.5.0")), 13)

    def test_missing_platform_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            (root / "FrameTrellis-1.5.0-macos-arm64.dmg").unlink()
            with self.assertRaisesRegex(ValueError, "incomplete"):
                matrix.verify(root, "1.5.0")

    def test_experimental_file_is_rejected_even_if_valid_files_exist(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            (root / "FrameTrellis-linux-EXPERIMENTAL.AppImage").write_bytes(b"ELF")
            with self.assertRaisesRegex(ValueError, "unexpected"):
                matrix.verify(root, "1.5.0")

    def test_modified_payload_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            with (root / "FrameTrellis-1.5.0-linux-x86_64.AppImage").open("ab") as stream:
                stream.write(b"tampered")
            with self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                matrix.verify(root, "1.5.0")

    def test_duplicate_checksum_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            manifest = root / "SHA256SUMS.txt"
            lines = manifest.read_text(encoding="utf-8").splitlines()
            manifest.write_text("\n".join(lines + [lines[0]]) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "repeated"):
                matrix.verify(root, "1.5.0")

    def test_wrong_spdx_version_is_rejected_even_with_matching_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-macos-arm64.spdx.json"
            path = root / name
            document = {"spdxVersion": "SPDX-2.3", "packages": [
                {"name": "FrameTrellis", "versionInfo": "1.4.0"}
            ]}
            path.write_text(json.dumps(document), encoding="utf-8")
            manifest = root / "SHA256SUMS.txt"
            lines = manifest.read_text(encoding="utf-8").splitlines()
            updated = [
                f"{matrix.digest_file(path)}  {name}" if line.endswith(f"  {name}") else line
                for line in lines
            ]
            manifest.write_text("\n".join(updated) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "SPDX version"):
                matrix.verify(root, "1.5.0")

    def test_broken_dmg_footer_is_rejected_even_with_matching_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-macos-arm64.dmg"
            path = root / name
            path.write_bytes(b"not-a-valid-udif" * 50)
            manifest = root / "SHA256SUMS.txt"
            lines = manifest.read_text(encoding="utf-8").splitlines()
            lines = [
                f"{matrix.digest_file(path)}  {name}" if line.endswith(f"  {name}") else line
                for line in lines
            ]
            manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "UDIF trailer"):
                matrix.verify(root, "1.5.0")

    def test_bad_version_and_path_records_rejected(self):
        with self.assertRaises(ValueError):
            matrix.required_payloads("1.5.0-rc1")
        required = matrix.required_payloads("1.5.0")
        with self.assertRaisesRegex(ValueError, "Invalid SHA256SUMS"):
            matrix.checksums_from_text("a" * 64 + "  ../evil", required)

    def test_symlinked_payload_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-linux-x86_64.AppImage"
            path = root / name
            path.unlink()
            path.symlink_to("SHA256SUMS.txt")
            with self.assertRaisesRegex(ValueError, "symlinked"):
                matrix.verify(root, "1.5.0")


if __name__ == "__main__":
    unittest.main()
