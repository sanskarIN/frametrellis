# SPDX-License-Identifier: GPL-3.0-or-later
"""Cross-platform deterministic runtime packaging inventory regression tests."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

MODULE = Path(__file__).resolve().parents[1] / "runtime_inventory.py"
SPEC = importlib.util.spec_from_file_location("runtime_inventory", MODULE)
assert SPEC and SPEC.loader
inventory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(inventory)


def sample_bundle(root: Path) -> None:
    (root / "usr/lib").mkdir(parents=True)
    (root / "bin").mkdir()
    (root / "usr/lib/libmedia.so").write_bytes(b"\x7fELFexample")
    (root / "bin/frametrellis").write_bytes(b"binary")
    (root / "bin/frametrellis").chmod(0o755)
    (root / "usr/lib/libmedia-link.so").symlink_to("libmedia.so")


class RuntimeInventoryTests(unittest.TestCase):
    def test_complete_bundle_verifies_after_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            generated = inventory.write_manifest(root, "linux-appdir", manifest)
            self.assertEqual(generated["counts"]["file"], 2)
            self.assertEqual(generated["counts"]["symlink"], 1)
            self.assertGreater(generated["regularFileBytes"], 0)
            self.assertEqual(inventory.verify_manifest(root, manifest), generated)
            symlink = next(e for e in generated["entries"] if e["type"] == "symlink")
            self.assertEqual(symlink["target"], "usr/lib/libmedia.so")
            self.assertNotIn(str(Path(tmp)), manifest.read_text(encoding="utf-8"))

    def test_identical_bytes_create_identical_report_in_different_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            package_a = base / "first" / "AppDir"
            package_b = base / "second" / "AppDir"
            for package in (package_a, package_b):
                sample_bundle(package)
            report_a = inventory.inventory(package_a, "linux-appdir")
            report_b = inventory.inventory(package_b, "linux-appdir")
            self.assertEqual(report_a, report_b)
            self.assertEqual(report_a["entries"][0]["path"], "bin")

    def test_modified_payload_is_detected_without_absolute_host_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "FrameTrellis.app"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            inventory.write_manifest(root, "macos-app", manifest)
            (root / "usr/lib/libmedia.so").write_bytes(b"different binary")
            with self.assertRaisesRegex(ValueError, "does not match package bytes"):
                inventory.verify_manifest(root, manifest)

    def test_executable_permission_changes_are_detected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            inventory.write_manifest(root, "linux-appdir", manifest)
            (root / "bin/frametrellis").chmod(0o644)
            with self.assertRaisesRegex(ValueError, "does not match package bytes"):
                inventory.verify_manifest(root, manifest)

    def test_escape_symlink_rejected_before_hashing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            (root / "usr/lib/escape").symlink_to("../../../../outside")
            with self.assertRaisesRegex(ValueError, "Escaping or broken"):
                inventory.inventory(root, "linux-appdir")

    def test_absolute_symlink_rejected_even_when_target_inside(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            (root / "usr/lib/absolute").symlink_to(
                (root / "usr/lib/libmedia.so").resolve()
            )
            with self.assertRaisesRegex(ValueError, "Non-portable absolute"):
                inventory.inventory(root, "linux-appdir")

    def test_broken_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            (root / "usr/lib/missing").symlink_to("not-here")
            with self.assertRaisesRegex(ValueError, "Escaping or broken"):
                inventory.inventory(root, "linux-appdir")

    def test_case_insensitive_collisions_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            first = root / "usr/lib/MyMedia.txt"
            other = root / "usr/lib/mymedia.txt"
            first.write_text("first", encoding="utf-8")
            other.write_text("second", encoding="utf-8")
            if first.read_text(encoding="utf-8") == other.read_text(encoding="utf-8"):
                self.skipTest("Filesystem is case-insensitive")
            with self.assertRaisesRegex(ValueError, "Case-insensitive"):
                inventory.inventory(root, "linux-appdir")

    def test_reject_manifest_inside_bundle_and_preexisting_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            with self.assertRaisesRegex(ValueError, "OUTSIDE"):
                inventory.write_manifest(root, "linux-appdir", root / "inventory.json")
            inventory.write_manifest(root, "linux-appdir", manifest)
            with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
                inventory.write_manifest(root, "linux-appdir", manifest)

    def test_concurrent_exclusive_writer_manifest_is_not_deleted(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            real_inventory = inventory.inventory

            def competing_writer(package_root: Path, kind: str) -> dict:
                # The competing file appears after the initial exists() check
                # but before our exclusive open("x"). Its bytes are not ours.
                manifest.write_text("concurrent writer data", encoding="utf-8")
                return real_inventory(package_root, kind)

            with (
                patch.object(inventory, "inventory", side_effect=competing_writer),
                self.assertRaises(FileExistsError),
            ):
                inventory.write_manifest(root, "linux-appdir", manifest)
            self.assertEqual(manifest.read_text(encoding="utf-8"),
                             "concurrent writer data")

    def test_invalid_manifest_schema_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            sample_bundle(root)
            manifest = Path(tmp) / "inventory.json"
            inventory.write_manifest(root, "linux-appdir", manifest)
            data = json.loads(manifest.read_text(encoding="utf-8"))
            data["schemaVersion"] = 999
            manifest.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unsupported runtime inventory schema"):
                inventory.verify_manifest(root, manifest)

    def test_unsupported_kind_and_empty_bundle_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "AppDir"
            root.mkdir()
            with self.assertRaisesRegex(ValueError, "Unrecognized"):
                inventory.inventory(root, "windows-exe")
            with self.assertRaisesRegex(ValueError, "empty"):
                inventory.inventory(root, "linux-appdir")


if __name__ == "__main__":
    unittest.main()
