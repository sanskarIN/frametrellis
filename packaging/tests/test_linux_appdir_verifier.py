# SPDX-License-Identifier: GPL-3.0-or-later
"""Platform-neutral tests for Linux AppDir dependency closure checks."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / "linux" / "verify_appdir.py"
SPEC = importlib.util.spec_from_file_location("linux_appdir_verifier", MODULE)
assert SPEC and SPEC.loader
verify = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verify)


class LinuxAppDirVerifierTests(unittest.TestCase):
    def test_parse_ldd_missing_and_resolved_dependencies(self):
        self.assertEqual(
            verify.parse_ldd_output(
                "\tlibQt6Core.so.6 => /app/usr/lib/libQt6Core.so.6 (0xabc)\n"
                "\tlibmlt++-7.so.7 => not found\n"
                "\tlinux-vdso.so.1 (0xdef)\n"
            ),
            [
                ("libQt6Core.so.6", "/app/usr/lib/libQt6Core.so.6"),
                ("libmlt++-7.so.7", None),
            ],
        )

    def test_non_system_media_lib_must_be_bundled(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            local = appdir / "usr" / "lib" / "libavcodec.so.61"
            local.parent.mkdir(parents=True)
            local.write_bytes(b"fixture")
            self.assertIsNone(verify.check_link("libavcodec.so.61", str(local), appdir))
            self.assertIn(
                "outside AppDir",
                verify.check_link(
                    "libQt6Core.so.6", "/usr/lib/libQt6Core.so.6", appdir
                ),
            )
            self.assertIsNone(verify.check_link("libc.so.6", "/usr/lib/libc.so.6", appdir))

    def test_missing_and_build_machine_libraries_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            self.assertIn(
                "unresolved", verify.check_link("libQt6Gui.so.6", None, appdir)
            )
            self.assertIn(
                "build-machine",
                verify.check_link(
                    "libmlt++-7.so.7", "/home/runner/mlt/lib/libmlt++-7.so.7", appdir
                ),
            )


if __name__ == "__main__":
    unittest.main()
