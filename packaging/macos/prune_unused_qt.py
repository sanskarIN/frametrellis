# SPDX-License-Identifier: GPL-3.0-or-later
"""Remove known-unused macdeployqt plugins from a development .app.

Homebrew macdeployqt currently copies optional QML modules (Qt 3D, PDF,
VirtualKeyboard, Timeline, etc.) into the bundle even though FrameTrellis
never imports them. Those plugins retain unbundled Qt dependencies. Only
remove a module when no project QML file imports it. All other bundled Mach-O
libraries still undergo the fail-closed verifier audit.
"""
from __future__ import annotations

import argparse
import re
import shutil
from pathlib import Path

# Mapping between optional modules and their macdeployqt bundle locations.
# Do not prune anything that FrameTrellis's source QML imports directly.
OPTIONAL_MODULES = {
    "QtQuick.Pdf": ("Resources/qml/QtQuick/Pdf", "PlugIns/imageformats/libqpdf.dylib"),
    "QtQuick.Scene2D": ("Resources/qml/QtQuick/Scene2D",),
    "QtQuick.Scene3D": ("Resources/qml/QtQuick/Scene3D",),
    "QtQuick.Timeline": ("Resources/qml/QtQuick/Timeline",),
    "QtQuick.VirtualKeyboard": (
        "Resources/qml/QtQuick/VirtualKeyboard",
        "PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib",
    ),
    "QtQml.StateMachine": ("Resources/qml/QtQml/StateMachine",),
}
IMPORT_RE = re.compile(r"^\s*import\s+([A-Za-z][\w.]*)\b", re.MULTILINE)


def qml_imports(source: Path) -> set[str]:
    """Find static QML module imports; do not attempt to guess dynamic dependencies."""
    if not source.is_dir():
        raise ValueError(f"FrameTrellis QML source directory does not exist: {source}")
    imports: set[str] = set()
    for file in source.rglob("*.qml"):
        imports.update(IMPORT_RE.findall(file.read_text(encoding="utf-8")))
    return imports


def prune(app: Path, source: Path) -> list[str]:
    """Prune explicitly unused Qt modules; fail closed on unsafe bundle paths."""
    if app.is_symlink() or not (app / "Contents").is_dir():
        raise ValueError(f"Invalid .app bundle: {app}")
    contents = (app / "Contents").resolve(strict=True)
    imports = qml_imports(source)
    removed: list[str] = []
    for module, paths in OPTIONAL_MODULES.items():
        if any(name == module or name.startswith(module + ".") for name in imports):
            continue
        for path in paths:
            candidate = contents / path
            if candidate.is_symlink():
                raise ValueError(f"Refusing to prune a symlink: {candidate}")
            if not candidate.exists():
                continue
            if not candidate.resolve().is_relative_to(contents):
                raise ValueError(f"Refusing to prune outside the bundle: {candidate}")
            if candidate.is_dir():
                shutil.rmtree(candidate)
            elif candidate.is_file():
                candidate.unlink()
            else:
                raise ValueError(f"Unexpected bundle object: {candidate}")
            removed.append(path)
    return removed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    parser.add_argument("qml_source", type=Path)
    args = parser.parse_args()
    for path in prune(args.app, args.qml_source):
        print(f"Removed unused macOS Qt plugin: {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
