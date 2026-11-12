# SPDX-License-Identifier: GPL-3.0-or-later
"""Regression checks for pruning optional Qt deployment plugins on macOS."""
from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MODULE = Path(__file__).resolve().parents[1] / "macos" / "prune_unused_qt.py"
SPEC = importlib.util.spec_from_file_location("prune_unused_qt", MODULE)
assert SPEC and SPEC.loader
pruner = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pruner)


class MacPruneOptionalQtTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        root = Path(self.directory.name)
        self.app = root / "FrameTrellis.app"
        self.contents = self.app / "Contents"
        self.contents.mkdir(parents=True)
        self.sources = root / "src" / "ui" / "qml"
        self.sources.mkdir(parents=True)

    def add_plugin(self, name: str) -> Path:
        path = self.contents / name
        path.parent.mkdir(parents=True, exist_ok=True)
        if name.endswith(".dylib"):
            path.write_bytes(b"test plugin")
        else:
            path.mkdir(exist_ok=True)
            (path / "qmldir").write_text("module Unused\n")
        return path

    def test_prunes_known_unused_modules_but_keeps_required_dialogs_and_controls(self):
        (self.sources / "Main.qml").write_text(
            "import QtQuick\nimport QtQuick.Dialogs\nimport QtQuick.Controls.Basic\n"
        )
        optional = self.add_plugin("Resources/qml/QtQuick/Scene3D")
        extra_pdf = self.add_plugin("PlugIns/imageformats/libqpdf.dylib")
        required = self.add_plugin("Resources/qml/QtQuick/Dialogs")
        removed = pruner.prune(self.app, self.sources)
        self.assertIn("Resources/qml/QtQuick/Scene3D", removed)
        self.assertIn("PlugIns/imageformats/libqpdf.dylib", removed)
        self.assertFalse(optional.exists())
        self.assertFalse(extra_pdf.exists())
        self.assertTrue(required.is_dir())

    def test_keeps_optional_module_when_imported_by_source(self):
        (self.sources / "Main.qml").write_text(
            "import QtQuick.VirtualKeyboard\nimport QtQuick.Pdf 1.0\n"
        )
        keyboard = self.add_plugin("Resources/qml/QtQuick/VirtualKeyboard")
        context = self.add_plugin("PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib")
        pdf = self.add_plugin("Resources/qml/QtQuick/Pdf")
        imageformat = self.add_plugin("PlugIns/imageformats/libqpdf.dylib")
        removed = pruner.prune(self.app, self.sources)
        self.assertEqual(removed, [])
        self.assertTrue(all(path.exists() for path in [keyboard, context, pdf, imageformat]))


    def test_prunes_unreferenced_native_quick_plugins_from_failed_macos_ci(self):
        """Homebrew deploys optional QML modules under PlugIns/quick as well."""
        (self.sources / "Main.qml").write_text(
            "import QtQuick\nimport QtQuick.Controls.Basic\n", encoding="utf-8"
        )
        quick_plugins = [
            "libqtvkbopenwnnplugin.dylib",
            "libqtvkbthaiplugin.dylib",
            "libvirtualkeyboardplugin.dylib",
            "libqtquickscene2dplugin.dylib",
            "libqtvkbplugin.dylib",
            "libqtvkbpinyinplugin.dylib",
            "libqtquicktimelineplugin.dylib",
            "libpdfquickplugin.dylib",
            "libqtquickscene3dplugin.dylib",
            "libqtvkbtcimeplugin.dylib",
            "libqtvkbhangulplugin.dylib",
            "libqtqmlstatemachineplugin.dylib",
            "libqtvkbsettingsplugin.dylib",
            "libqtquicktimelineblendtreesplugin.dylib",
            "libqtvkbhunspellplugin.dylib",
            "libqtvkbcomponentsplugin.dylib",
            "libqtvkbstylesplugin.dylib",
        ]
        candidates = [self.add_plugin("PlugIns/quick/" + name) for name in quick_plugins]
        required = self.add_plugin("PlugIns/quick/libqtquickcontrols2basicstyleplugin.dylib")
        removed = pruner.prune(self.app, self.sources)
        self.assertEqual(
            {path for path in removed if path.startswith("PlugIns/quick/")},
            {"PlugIns/quick/" + name for name in quick_plugins},
        )
        self.assertTrue(all(not path.exists() for path in candidates))
        self.assertTrue(required.is_file(), "Non-allowlisted plugins must survive")

    def test_imported_quick_plugins_are_preserved(self):
        (self.sources / "Main.qml").write_text(
            "import QtQuick.VirtualKeyboard\n"
            "import QtQuick.Timeline\n"
            "import QtQml.StateMachine\n"
            "import QtQuick.Pdf\n", encoding="utf-8"
        )
        installed = [
            self.add_plugin("PlugIns/quick/libqtvkbpinyinplugin.dylib"),
            self.add_plugin("PlugIns/quick/libqtquicktimelineblendtreesplugin.dylib"),
            self.add_plugin("PlugIns/quick/libqtqmlstatemachineplugin.dylib"),
            self.add_plugin("PlugIns/quick/libpdfquickplugin.dylib"),
        ]
        self.assertEqual(pruner.prune(self.app, self.sources), [])
        self.assertTrue(all(path.exists() for path in installed))

    def test_rejects_symlink_in_prunable_path(self):
        (self.sources / "Main.qml").write_text("import QtQuick\n")
        external = Path(self.directory.name) / "external"
        external.mkdir()
        path = self.contents / "Resources/qml/QtQuick/Timeline"
        path.parent.mkdir(parents=True)
        path.symlink_to(external, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "symlink"):
            pruner.prune(self.app, self.sources)
        self.assertTrue(external.exists())

    def test_requires_valid_source_and_bundle(self):
        with self.assertRaisesRegex(ValueError, "source directory"):
            pruner.prune(self.app, Path(self.directory.name) / "notfound")
        (self.sources / "Main.qml").write_text("import QtQuick\n")
        with mock.patch.object(pruner.shutil, "rmtree") as remove:
            self.assertEqual(pruner.prune(self.app, self.sources), [])
            remove.assert_not_called()


if __name__ == "__main__":
    unittest.main()
