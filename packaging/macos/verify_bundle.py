# SPDX-License-Identifier: GPL-3.0-or-later
"""Conservative native macOS development-bundle runtime-closure audit.

This is a *gate*, not a complete notarization or licensing check. The package
is never promoted to a stable release merely because this script succeeds.
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

SYSTEM_PREFIXES = ("/usr/lib/", "/System/Library/")
FORBIDDEN_PREFIXES = (
    "/opt/homebrew/",
    "/usr/local/",
    "/Users/",
    "/Library/Frameworks/",
    "/private/var/",
)


def dependencies(text: str) -> list[str]:
    """Parse otool -L output, which starts with an executable header line."""
    return [line.strip().split(" (", 1)[0] for line in text.splitlines()[1:] if line.strip()]


def assert_dependency(
    dependency: str, file: Path, bundle: Path
) -> str | None:
    """Return an actionable error if a dependency is not bundled/system-owned."""
    if dependency.startswith(FORBIDDEN_PREFIXES):
        return f"{file}: external non-system dependency: {dependency}"
    if dependency.startswith(SYSTEM_PREFIXES):
        return None
    contents = bundle / "Contents"
    paths: list[Path] = []
    if dependency.startswith("@executable_path/"):
        paths = [contents / "MacOS" / dependency.removeprefix("@executable_path/")]
    elif dependency.startswith("@loader_path/"):
        paths = [file.parent / dependency.removeprefix("@loader_path/")]
    elif dependency.startswith("@rpath/"):
        rest = dependency.removeprefix("@rpath/")
        paths = [
            contents / "Frameworks" / rest,
            contents / "MacOS" / rest,
            contents / "PlugIns" / rest,
        ]
    elif dependency.startswith("/"):
        return f"{file}: absolute non-system dependency: {dependency}"
    else:
        return f"{file}: unknown dynamic dependency syntax: {dependency}"
    for candidate in paths:
        # The path may contain ../ components, but must not leave the bundle.
        resolved = candidate.resolve(strict=False)
        if resolved.is_relative_to(bundle) and resolved.is_file():
            return None
    return f"{file}: unresolved bundled dependency: {dependency}"


def check_bundle(app: Path) -> list[str]:
    app = app.resolve()
    contents = app / "Contents"
    required = [
        contents / "MacOS/frametrellis",
        contents / "MacOS/frametrellis-render",
        contents / "PlugIns/mlt-7",
        contents / "Resources/mlt-7",
        contents / "PlugIns/platforms/libqcocoa.dylib",
    ]
    problems = [f"Missing required bundle component: {p}" for p in required if not p.exists()]
    if problems:
        return problems

    macho_files: list[Path] = []
    for path in contents.rglob("*"):
        if path.is_symlink():
            resolved = path.resolve(strict=False)
            if not resolved.is_relative_to(app):
                problems.append(f"Symlink escapes application bundle: {path} -> {resolved}")
        if not path.is_file():
            continue
        # Only inspect loadable machine code; do not parse QML, fonts, images.
        file_result = subprocess.run(
            ["/usr/bin/file", "-b", str(path)],
            text=True,
            capture_output=True,
            check=False,
        )
        if "Mach-O" in file_result.stdout:
            macho_files.append(path)

    if not macho_files:
        return problems + ["No Mach-O binaries were found in application bundle"]

    seen: set[Path] = set()
    for file in macho_files:
        # Symlinks may point at the same framework binary; inspect it once.
        resolved = file.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        result = subprocess.run(
            ["otool", "-L", str(file)],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode:
            problems.append(f"otool failed on {file}: {result.stderr.strip()}")
            continue
        for dependency in dependencies(result.stdout):
            issue = assert_dependency(dependency, file, app)
            if issue:
                problems.append(issue)

        # Disallow absolute Homebrew/runner runpaths even when -L dependencies
        # have already been rewritten; they could resolve unbundled @rpath.
        rpaths = subprocess.run(
            ["otool", "-l", str(file)],
            text=True,
            capture_output=True,
            check=False,
        )
        if rpaths.returncode:
            problems.append(f"otool -l failed on {file}")
            continue
        lines = rpaths.stdout.splitlines()
        for index, line in enumerate(lines):
            if line.strip() == "cmd LC_RPATH":
                for next_line in lines[index + 1 : index + 5]:
                    item = next_line.strip()
                    if item.startswith("path "):
                        path_value = item.removeprefix("path ").split(" (offset", 1)[0]
                        if path_value.startswith(FORBIDDEN_PREFIXES):
                            problems.append(f"{file}: external LC_RPATH {path_value}")
                        break
    print(f"Checked {len(seen)} native binaries and libraries in {app}")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, help="macOS .app to verify")
    args = parser.parse_args()
    problems = check_bundle(args.app)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    print("PASS: bundled macOS dependencies have no detected external Homebrew references")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
