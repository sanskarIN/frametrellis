# SPDX-License-Identifier: GPL-3.0-or-later
"""Platform-neutral tests for Linux AppDir dependency closure checks."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from unittest import mock
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

    def test_staged_appdir_under_runner_home_is_not_a_false_positive(self):
        appdir = Path("/home/runner/work/_temp/linux-packages/AppDir")
        bundled = str(appdir / "usr/lib/libQt6Gui.so.6")
        self.assertIsNone(verify.check_link("libQt6Gui.so.6", bundled, appdir))

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


    def test_collects_qt_plugin_elf_and_ignores_regular_assets(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            plugin = appdir / "usr/lib/qt6/plugins/platforms/libqoffscreen.so"
            plugin.parent.mkdir(parents=True)
            plugin.write_bytes(b"\x7fELF" + b"test")
            text = appdir / "usr/share/applications/frametrellis.desktop"
            text.parent.mkdir(parents=True)
            text.write_text("[Desktop Entry]")
            files, problems = verify.collect_bundled_elfs(appdir)
            self.assertEqual(problems, [])
            self.assertEqual(files, [plugin])

    def test_rejects_links_to_external_and_missing_libraries(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            libraries = appdir / "usr/lib"
            libraries.mkdir(parents=True)
            external = Path(tmp) / "outside.so"
            external.write_bytes(b"\x7fELF" + b"test")
            (libraries / "libexternal.so").symlink_to(external)
            (libraries / "libbroken.so").symlink_to(libraries / "missing.so")
            files, problems = verify.collect_bundled_elfs(appdir)
            self.assertEqual(files, [])
            self.assertEqual(len(problems), 2)
            self.assertTrue(all("symlink" in problem for problem in problems))

    def test_appdir_checks_dynamically_loaded_qt_plugin(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            root = appdir / "usr"
            required_files = [
                "bin/frametrellis",
                "bin/frametrellis-render",
                "share/applications/frametrellis.desktop",
                "share/mime/packages/frametrellis.xml",
                "share/icons/hicolor/scalable/apps/frametrellis.svg",
            ] + [
                f"lib/mlt-7/libmlt{name}.so"
                for name in ("core", "avformat", "plus", "normalize")
            ]
            for name in required_files:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"test fixture")
            (root / "share/mlt-7").mkdir(parents=True)
            plugin = root / "lib/qt6/plugins/platforms/libqoffscreen.so"
            plugin.parent.mkdir(parents=True)
            plugin.write_bytes(b"\x7fELF" + b"test fixture")

            def fake_run(command, **_kwargs):
                if command[0] == "readelf":
                    return mock.Mock(returncode=0, stdout="Dynamic section at offset 0x40\n")
                if command[0] == "ldd":
                    return mock.Mock(
                        returncode=0,
                        stdout="libQt6Core.so.6 => /usr/lib/libQt6Core.so.6 (0x123)\n",
                    )
                raise AssertionError(f"Unexpected verifier command: {command}")

            with mock.patch.object(verify.subprocess, "run", side_effect=fake_run):
                problems = verify.check_appdir(appdir)
            self.assertTrue(
                any("libqoffscreen.so" in problem and "outside AppDir" in problem
                    for problem in problems),
                problems,
            )

    def test_sdl3_runtime_must_be_bundled(self):
        with tempfile.TemporaryDirectory() as tmp:
            appdir = Path(tmp) / "AppDir"
            self.assertIn(
                "outside AppDir",
                verify.check_link("libSDL3.so.0", "/usr/lib/libSDL3.so.0", appdir),
            )


if __name__ == "__main__":
    unittest.main()
