# SPDX-License-Identifier: GPL-3.0-or-later
"""Unit tests for the future three-platform release asset integrity gate."""
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

SCRIPT = Path(__file__).resolve().parents[1] / "verify_candidate_matrix.py"
SPEC = importlib.util.spec_from_file_location("candidate_asset_matrix", SCRIPT)
assert SPEC and SPEC.loader
matrix = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(matrix)


def fixture_spdx() -> dict:
    app = "SPDXRef-Package-FrameTrellis"
    third = "SPDXRef-Package-SyntheticCodec"
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": "FrameTrellis test SBOM",
        "documentNamespace": "https://example.org/test/frametrellis",
        "creationInfo": {"created": "2026-11-13T00:00:00Z",
                         "creators": ["Tool: test-fixture"]},
        "packages": [
            {"SPDXID": app, "name": "FrameTrellis", "versionInfo": "1.5.0",
             "licenseDeclared": "GPL-3.0-or-later",
             "licenseConcluded": "GPL-3.0-or-later",
             "downloadLocation": "https://github.com/sanskarIN/frametrellis"},
            {"SPDXID": third, "name": "SyntheticCodec", "versionInfo": "1.0.0",
             "licenseDeclared": "MIT", "licenseConcluded": "MIT",
             "downloadLocation": "https://example.org/synthetic-source"},
        ],
        "relationships": [
            {"spdxElementId": "SPDXRef-DOCUMENT", "relationshipType": "DESCRIBES",
             "relatedSpdxElement": app},
            {"spdxElementId": app, "relationshipType": "CONTAINS",
             "relatedSpdxElement": third},
        ],
    }


def fixture_payload(name: str) -> bytes:
    if name.endswith(".spdx.json"):
        return json.dumps(fixture_spdx()).encode("utf-8")
    if name.endswith(".AppImage"):
        return b"\x7fELFdummy"
    if name.endswith(".dmg"):
        return b"prefix" + b"0" * 506 + b"koly" + b"0" * 508
    if name.endswith("-setup.exe"):
        return b"MZinstaller"
    if name.endswith(".zip"):
        data = io.BytesIO()
        with zipfile.ZipFile(data, "w") as archive:
            archive.writestr("FrameTrellis/frametrellis.exe", b"gui executable")
            archive.writestr("FrameTrellis/frametrellis-render.exe", b"render executable")
        return data.getvalue()
    if name.endswith("-third-party-sources.tar"):
        data = io.BytesIO()
        with tarfile.open(fileobj=data, mode="w") as archive:
            content = b"synthetic test source"
            info = tarfile.TarInfo("source/README.txt")
            info.size = len(content)
            archive.addfile(info, io.BytesIO(content))
        return data.getvalue()
    return b"unexpected-fixture"


def make_candidate(root: Path) -> None:
    checksums: list[str] = []
    for name in sorted(matrix.required_payloads("1.5.0")):
        payload = fixture_payload(name)
        (root / name).write_bytes(payload)
        checksums.append(f"{hashlib.sha256(payload).hexdigest()}  {name}")
    (root / "SHA256SUMS.txt").write_text(
        "\n".join(checksums) + "\n", encoding="utf-8"
    )


def replace_payload(root: Path, name: str, value: bytes) -> None:
    """Replace a checked payload and recompute its manifest to test structure gates."""
    path = root / name
    path.write_bytes(value)
    manifest = root / "SHA256SUMS.txt"
    manifest.write_text(
        "\n".join(
            f"{hashlib.sha256(value).hexdigest()}  {name}"
            if line.endswith(f"  {name}") else line
            for line in manifest.read_text(encoding="utf-8").splitlines()
        ) + "\n",
        encoding="utf-8",
    )


def fixture_zip(entries: list[tuple[str, bytes]]) -> bytes:
    out = io.BytesIO()
    with zipfile.ZipFile(out, "w") as archive:
        for name, content in entries:
            archive.writestr(name, content)
    return out.getvalue()


def fixture_tar(name: str, content: bytes, *, symlink: bool = False) -> bytes:
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode="w") as archive:
        info = tarfile.TarInfo(name)
        if symlink:
            info.type = tarfile.SYMTYPE
            info.linkname = "../../escape"
        else:
            info.size = len(content)
        archive.addfile(info, None if symlink else io.BytesIO(content))
    return out.getvalue()


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

    def test_placeholder_application_only_spdx_does_not_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            filename = "FrameTrellis-1.5.0-linux-x86_64.spdx.json"
            replace_payload(root, filename, json.dumps({
                "spdxVersion": "SPDX-2.3",
                "packages": [{"name": "FrameTrellis", "versionInfo": "1.5.0"}],
            }).encode("utf-8"))
            with self.assertRaisesRegex(ValueError, "identity/license"):
                matrix.verify(root, "1.5.0")

    def test_spdx_missing_describes_or_contains_fails(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            filename = "FrameTrellis-1.5.0-macos-arm64.spdx.json"
            for relation in fixture_spdx()["relationships"]:
                sbom = fixture_spdx()
                sbom["relationships"].remove(relation)
                replace_payload(root, filename, json.dumps(sbom).encode("utf-8"))
                with self.assertRaisesRegex(ValueError, "DESCRIBES or CONTAINS"):
                    matrix.verify(root, "1.5.0")

    def test_spdx_missing_third_party_source_and_duplicate_ids_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            filename = "FrameTrellis-1.5.0-windows-x64.spdx.json"
            sbom = fixture_spdx()
            sbom["packages"][1]["downloadLocation"] = "NOASSERTION"
            replace_payload(root, filename, json.dumps(sbom).encode("utf-8"))
            with self.assertRaisesRegex(ValueError, "source location"):
                matrix.verify(root, "1.5.0")
            sbom["packages"][1]["downloadLocation"] = "https://example.org/source"
            sbom["packages"][1]["SPDXID"] = sbom["packages"][0]["SPDXID"]
            replace_payload(root, filename, json.dumps(sbom).encode("utf-8"))
            with self.assertRaisesRegex(ValueError, "duplicate"):
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

    def test_windows_portable_requires_adjacent_renderer(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-windows-x64.zip"
            replace_payload(root, name, fixture_zip([
                ("FrameTrellis/frametrellis.exe", b"gui"),
            ]))
            with self.assertRaisesRegex(ValueError, "frametrellis-render.exe"):
                matrix.verify(root, "1.5.0")

            replace_payload(root, name, fixture_zip([
                ("FrameTrellis/frametrellis.exe", b"gui"),
                ("other/frametrellis-render.exe", b"renderer"),
            ]))
            with self.assertRaisesRegex(ValueError, "adjacent"):
                matrix.verify(root, "1.5.0")

    def test_windows_portable_rejects_case_collisions_and_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-windows-x64.zip"
            replace_payload(root, name, fixture_zip([
                ("FrameTrellis/frametrellis.exe", b"gui"),
                ("FrameTrellis/frametrellis-render.exe", b"render"),
                ("frametrellis/FRAMETRELLIS.EXE", b"collision"),
            ]))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                matrix.verify(root, "1.5.0")

            replace_payload(root, name, fixture_zip([
                ("../escape.exe", b"unsafe"),
                ("FrameTrellis/frametrellis.exe", b"gui"),
                ("FrameTrellis/frametrellis-render.exe", b"render"),
            ]))
            with self.assertRaisesRegex(ValueError, "Unsafe portable ZIP"):
                matrix.verify(root, "1.5.0")

    def test_windows_portable_rejects_invalid_zip_even_with_matching_checksum(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-windows-x64.zip"
            replace_payload(root, name, b"PKnot-really-a-ZIP")
            with self.assertRaises(zipfile.BadZipFile):
                matrix.verify(root, "1.5.0")

    def test_all_platform_source_tars_reject_unsafe_entries(self):
        for suffix in (
            "windows-x64", "linux-x86_64", "macos-arm64", "macos-x86_64"
        ):
            with self.subTest(platform=suffix), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                make_candidate(root)
                name = f"FrameTrellis-1.5.0-{suffix}-third-party-sources.tar"
                replace_payload(root, name, fixture_tar("../escape", b"unsafe"))
                with self.assertRaisesRegex(ValueError, "Unsafe corresponding-source TAR"):
                    matrix.verify(root, "1.5.0")

                replace_payload(root, name, fixture_tar("source/link", b"", symlink=True))
                with self.assertRaisesRegex(ValueError, "link/special"):
                    matrix.verify(root, "1.5.0")

                replace_payload(root, name, fixture_tar("source/empty.txt", b""))
                with self.assertRaisesRegex(ValueError, "no nonempty files"):
                    matrix.verify(root, "1.5.0")

    def test_unicode_normalized_zip_names_cannot_collide(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-windows-x64.zip"
            replace_payload(root, name, fixture_zip([
                ("FrameTrellis/frametrellis.exe", b"gui"),
                ("FrameTrellis/frametrellis-render.exe", b"render"),
                ("FrameTrellis/caf\u00e9.txt", b"first"),
                ("FrameTrellis/cafe\u0301.txt", b"second"),
            ]))
            with self.assertRaisesRegex(ValueError, "duplicate"):
                matrix.verify(root, "1.5.0")

    def test_unicode_normalized_tar_names_cannot_collide(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            name = "FrameTrellis-1.5.0-macos-arm64-third-party-sources.tar"
            archive_bytes = io.BytesIO()
            with tarfile.open(fileobj=archive_bytes, mode="w") as archive:
                for path in ("src/caf\u00e9.txt", "src/cafe\u0301.txt"):
                    content = b"data"
                    info = tarfile.TarInfo(path)
                    info.size = len(content)
                    archive.addfile(info, io.BytesIO(content))
            replace_payload(root, name, archive_bytes.getvalue())
            with self.assertRaisesRegex(ValueError, "Duplicate corresponding-source TAR"):
                matrix.verify(root, "1.5.0")

    def test_empty_optional_release_notes_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            make_candidate(root)
            (root / "release-notes.md").write_bytes(b"")
            with self.assertRaisesRegex(ValueError, "Unsafe or empty optional release notes"):
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
