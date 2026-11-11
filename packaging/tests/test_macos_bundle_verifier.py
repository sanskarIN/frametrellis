# SPDX-License-Identifier: GPL-3.0-or-later
"""OS-independent unit tests for macOS runtime dependency audits."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / "macos" / "verify_bundle.py"
SPEC = importlib.util.spec_from_file_location("macos_bundle_verifier", MODULE)
assert SPEC and SPEC.loader
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)


class MacBundleVerifierTests(unittest.TestCase):
    def test_otool_dependencies(self):
        output = (
            "app/Contents/MacOS/frametrellis:\n"
            "\t@rpath/QtCore.framework/Versions/A/QtCore (compatibility version 6.0.0)\n"
            "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
        )
        self.assertEqual(
            verify.dependencies(output),
            [
                "@rpath/QtCore.framework/Versions/A/QtCore",
                "/usr/lib/libSystem.B.dylib",
            ],
        )

    def test_internal_framework_is_accepted_and_external_brew_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "FrameTrellis.app"
            binary = app / "Contents" / "MacOS" / "frametrellis"
            bundled = app / "Contents" / "Frameworks" / "libmlt-7.dylib"
            bundled.parent.mkdir(parents=True)
            binary.parent.mkdir(parents=True)
            bundled.write_bytes(b"fake")
            binary.write_bytes(b"fake")

            self.assertIsNone(
                verify.assert_dependency("@rpath/libmlt-7.dylib", binary, app)
            )
            self.assertIsNone(
                verify.assert_dependency(
                    "@executable_path/../Frameworks/libmlt-7.dylib", binary, app
                )
            )
            self.assertIsNone(
                verify.assert_dependency(
                    "/System/Library/Frameworks/AppKit.framework/AppKit", binary, app
                )
            )
            self.assertIn(
                "external non-system",
                verify.assert_dependency("/opt/homebrew/lib/libmlt-7.dylib", binary, app),
            )

    def test_reject_escape_and_absent_dependency(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "FrameTrellis.app"
            binary = app / "Contents" / "MacOS" / "frametrellis"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"fake")
            self.assertIn(
                "unresolved",
                verify.assert_dependency("@rpath/libmissing.dylib", binary, app),
            )
            self.assertIn(
                "unresolved",
                verify.assert_dependency("@loader_path/../../../../../etc/passwd", binary, app),
            )
            self.assertIn(
                "absolute non-system",
                verify.assert_dependency("/opt/other/libfoo.dylib", binary, app),
            )


    def test_framework_loader_path_uses_resolved_versions_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / "FrameTrellis.app"
            framework = app / "Contents/Frameworks/QtCore.framework"
            real_binary = framework / "Versions/A/QtCore"
            real_binary.parent.mkdir(parents=True)
            real_binary.write_bytes(b"fake Mach-O")
            framework_entry = framework / "QtCore"
            framework_entry.symlink_to("Versions/A/QtCore")
            dependent = app / "Contents/Frameworks/libssl.4.dylib"
            dependent.write_bytes(b"fake dylib")

            # At runtime @loader_path points to Versions/A (the actual image),
            # not the symlink's containing .framework directory.
            self.assertIsNone(
                verify.assert_dependency(
                    "@loader_path/../../../libssl.4.dylib", framework_entry, app
                )
            )


if __name__ == "__main__":
    unittest.main()
