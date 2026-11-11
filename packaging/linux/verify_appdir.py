# SPDX-License-Identifier: GPL-3.0-or-later
"""Check an experimental Linux AppDir before packaging it as AppImage.

This verifies deployment, not broad distro compatibility or licensing.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

# These must *not* resolve to the builder's Qt/MLT/FFmpeg install directories.
MEDIA_PREFIXES = (
    "libQt6",
    "libmlt",
    "libavcodec",
    "libavformat",
    "libavfilter",
    "libavutil",
    "libavdevice",
    "libswscale",
    "libswresample",
    "libSDL2",
    "libx264",
    "libx265",
)
DANGEROUS_BUILDER_PATHS = (
    "/home/runner/",
    "/opt/hostedtoolcache/",
    "/usr/local/Qt",
    "/usr/local/lib/qt",
)


def parse_ldd_output(text: str) -> list[tuple[str, str | None]]:
    """Return SONAME and resolved path; None means a missing library."""
    results: list[tuple[str, str | None]] = []
    for line in text.splitlines():
        line = line.strip()
        if "=>" not in line:
            continue
        soname, rest = line.split("=>", 1)
        resolved = rest.strip().split(" (", 1)[0]
        results.append((soname.strip(), None if resolved == "not found" else resolved))
    return results


def check_link(soname: str, resolved: str | None, appdir: Path) -> str | None:
    if resolved is None:
        return f"{soname}: unresolved dynamic library"
    if any(resolved.startswith(prefix) for prefix in DANGEROUS_BUILDER_PATHS):
        return f"{soname}: references build-machine library {resolved}"
    if soname.startswith(MEDIA_PREFIXES) and not Path(resolved).resolve().is_relative_to(appdir):
        return f"{soname}: media/Qt dependency is outside AppDir: {resolved}"
    return None


def check_appdir(appdir: Path) -> list[str]:
    appdir = appdir.resolve()
    root = appdir / "usr"
    required = [
        root / "bin/frametrellis",
        root / "bin/frametrellis-render",
        root / "share/applications/frametrellis.desktop",
        root / "share/mime/packages/frametrellis.xml",
        root / "share/icons/hicolor/scalable/apps/frametrellis.svg",
        root / "share/mlt-7",
        root / "lib/mlt-7",
    ]
    problems = [f"Missing installed desktop asset: {p}" for p in required if not p.exists()]
    modules = [root / "lib/mlt-7" / f"libmlt{name}.so" for name in ("core", "avformat", "plus", "normalize")]
    problems.extend(f"Missing required MLT module: {p}" for p in modules if not p.exists())
    if problems:
        return problems

    libs = [root / "lib", root / "lib/x86_64-linux-gnu"]
    library_path = ":".join(str(path) for path in libs if path.is_dir())
    env = {key: value for key, value in os.environ.items() if key not in {
        "QT_PLUGIN_PATH", "QML2_IMPORT_PATH", "FRAMETRELLIS_MLT_REPOSITORY",
        "MLT_REPOSITORY", "LD_LIBRARY_PATH",
    }}
    env["LD_LIBRARY_PATH"] = library_path
    files = required[:2] + modules
    scanned = 0
    for file in files:
        result = subprocess.run(
            ["ldd", str(file)], capture_output=True, text=True, env=env, check=False
        )
        if result.returncode:
            problems.append(f"ldd failed for {file}: {result.stderr.strip()}")
            continue
        scanned += 1
        for soname, resolved in parse_ldd_output(result.stdout):
            problem = check_link(soname, resolved, appdir)
            if problem:
                problems.append(f"{file}: {problem}")
        elf = subprocess.run(
            ["readelf", "-d", str(file)],
            capture_output=True,
            text=True,
            check=False,
        )
        if elf.returncode:
            problems.append(f"readelf failed for {file}")
            continue
        for line in elf.stdout.splitlines():
            if "(RUNPATH)" in line or "(RPATH)" in line:
                match = re.search(r"\[(.+?)\]", line)
                if match and any(bad in match.group(1) for bad in DANGEROUS_BUILDER_PATHS):
                    problems.append(f"{file}: external build-tree ELF RPATH {match.group(1)}")
    print(f"Scanned {scanned} ELF programs and MLT service modules")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appdir", type=Path)
    args = parser.parse_args()
    problems = check_appdir(args.appdir)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}", file=sys.stderr)
        return 1
    print("PASS: Linux AppDir Qt/media libraries resolve from its own deployed directory")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
