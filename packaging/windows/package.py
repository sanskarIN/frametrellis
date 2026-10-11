#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Assembles the portable Windows (x64) package of FrameTrellis from an MSYS2 UCRT64 build.

Run it in an MSYS2 UCRT64 shell after building the project:

    python packaging/windows/package.py --build-dir build/windows-release --output dist

It
 1. copies the executables and deploys Qt (libraries, plugins and QML modules) with windeployqt,
 2. adds the MLT modules and data files the media engine needs,
 3. copies every DLL the binaries depend on, found by reading their import tables, and stops
    if a dependency can be found neither in MSYS2 nor in Windows itself,
 4. attributes every bundled file to the MSYS2 package that installed it, copies the license
    files of those packages and writes a list of bundled components,
 5. writes an SPDX 2.3 software bill of materials and the list of source packages that make up
    the corresponding source code, and
 6. creates the portable ZIP.

External tools are always run with argument lists, never through a shell.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import uuid
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

REPOSITORY = "https://github.com/sanskarIN/frametrellis"
APP_EXECUTABLES = ["frametrellis.exe", "frametrellis-render.exe"]
# MLT modules that provide the services the engine uses (see src/engine/mlt_runtime.cpp).
MLT_MODULES = ["core", "avformat", "plus", "normalize"]
MLT_DATA_DIRS = MLT_MODULES + ["profiles", "presets"]
# Windows components that must always come from the system, even if MSYS2 has a file of that name.
SYSTEM_ONLY = {"opengl32.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "dwrite.dll",
               "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll"}
# Qt plugin types the application never uses.
SKIPPED_PLUGIN_TYPES = ["generic", "networkinformation", "qmltooling", "tls"]
SOURCES_URL = "https://repo.msys2.org/mingw/sources/"


def log(message: str) -> None:
    print(message, flush=True)


def fail(message: str) -> None:
    print(f"error: {message}", file=sys.stderr, flush=True)
    sys.exit(1)


def run(arguments: list[str], **kwargs) -> subprocess.CompletedProcess:
    log("$ " + " ".join(arguments))
    return subprocess.run(arguments, check=True, text=True, **kwargs)


# --------------------------------------------------------------------------------------------
# MSYS2 package database


@dataclass
class Package:
    name: str
    version: str
    base: str
    description: str
    url: str
    licenses: list[str]
    files: list[str] = field(default_factory=list)

    @property
    def source_archive(self) -> str:
        # Source archives replace the epoch separator ':' with '~'.
        return f"{self.base}-{self.version.replace(':', '~')}.src.tar.zst"

    @property
    def spdx_license(self) -> str:
        values = [v[len("spdx:"):] for v in self.licenses if v.startswith("spdx:")]
        if values and len(values) == len(self.licenses):
            return " AND ".join(f"({v})" if " " in v else v for v in values)
        return "NOASSERTION"


def parse_db_file(path: Path) -> dict[str, list[str]]:
    fields: dict[str, list[str]] = {}
    key = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("%") and line.endswith("%") and len(line) > 2:
            key = line[1:-1]
            fields[key] = []
        elif line and key is not None:
            fields[key].append(line)
    return fields


def first(fields: dict[str, list[str]], key: str, default: str = "") -> str:
    """The first value of a database field; fields may be present but empty."""
    values = fields.get(key)
    return values[0] if values else default


def read_package_database(msys_root: Path) -> tuple[dict[str, Package], dict[str, str]]:
    """Reads pacman's local database directly; returns packages and a file-owner map."""
    database = msys_root / "var" / "lib" / "pacman" / "local"
    if not database.is_dir():
        fail(f"pacman database not found at {database}")
    packages: dict[str, Package] = {}
    owners: dict[str, str] = {}
    for entry in sorted(database.iterdir()):
        desc = entry / "desc"
        if not desc.is_file():
            continue
        fields = parse_db_file(desc)
        name = first(fields, "NAME")
        if not name:
            continue
        package = Package(
            name=name,
            version=first(fields, "VERSION"),
            base=first(fields, "BASE", name),
            description=first(fields, "DESC"),
            url=first(fields, "URL"),
            licenses=fields.get("LICENSE", []),
        )
        files_db = entry / "files"
        if files_db.is_file():
            package.files = parse_db_file(files_db).get("FILES", [])
        for path in package.files:
            if not path.endswith("/"):
                owners[path.lower()] = name
        packages[name] = package
    return packages, owners


# --------------------------------------------------------------------------------------------
# Package assembly


class Packager:
    def __init__(self, args: argparse.Namespace):
        self.source_dir = Path(args.source_dir).resolve()
        self.build_dir = Path(args.build_dir).resolve()
        self.output = Path(args.output).resolve()
        self.msys_root = Path(args.msys_root).resolve()
        self.prefix = self.msys_root / args.msystem
        self.version = args.version or read_version(self.source_dir)
        self.name = f"FrameTrellis-{self.version}-windows-x64"
        self.stage = self.output / self.name
        self.system_dir = Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32"
        self.packages, self.owners = read_package_database(self.msys_root)
        # Every file in the stage that came from MSYS2: stage-relative path -> source path.
        self.origins: dict[str, Path] = {}

    # -- helpers -------------------------------------------------------------------------------

    def qt_path(self, variable: str) -> Path:
        result = run(["qtpaths6", "--query", variable], capture_output=True)
        return Path(result.stdout.strip())

    def copy(self, source: Path, target: Path) -> None:
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        self.origins[target.relative_to(self.stage).as_posix()] = source

    def owner_of(self, source: Path) -> str | None:
        try:
            key = source.resolve().relative_to(self.msys_root).as_posix().lower()
        except ValueError:
            return None
        return self.owners.get(key)

    # -- steps ---------------------------------------------------------------------------------

    def prepare(self) -> None:
        if self.stage.exists():
            shutil.rmtree(self.stage)
        self.stage.mkdir(parents=True)
        for exe in APP_EXECUTABLES:
            built = self.build_dir / "bin" / exe
            if not built.is_file():
                fail(f"{built} does not exist; build the project first")
            shutil.copy2(built, self.stage / exe)

    def deploy_qt(self) -> None:
        plugin_dir = self.stage / "plugins"
        qml_dir = self.stage / "qml"
        run([
            "windeployqt6",
            "--release",
            "--dir", str(self.stage),
            "--plugindir", str(plugin_dir),
            "--qml-deploy-dir", str(qml_dir),
            "--qmldir", str(self.source_dir / "src" / "ui" / "qml"),
            "--no-translations",
            "--no-system-d3d-compiler",
            "--no-system-dxc-compiler",
            "--no-opengl-sw",
            "--no-compiler-runtime",
            "--skip-plugin-types", ",".join(SKIPPED_PLUGIN_TYPES),
            "--verbose", "1",
            str(self.stage / "frametrellis.exe"),
        ])
        # Record where windeployqt took each file from.
        bin_dir = self.prefix / "bin"
        qt_plugins = self.qt_path("QT_INSTALL_PLUGINS")
        qt_qml = self.qt_path("QT_INSTALL_QML")
        for path in sorted(self.stage.rglob("*")):
            if not path.is_file() or path.name in APP_EXECUTABLES:
                continue
            relative = path.relative_to(self.stage)
            if relative.parts[0] == "plugins":
                source = qt_plugins.joinpath(*relative.parts[1:])
            elif relative.parts[0] == "qml":
                source = qt_qml.joinpath(*relative.parts[1:])
            else:
                source = bin_dir / relative
            if not source.is_file():
                fail(f"cannot tell where windeployqt took {relative} from")
            self.origins[relative.as_posix()] = source
        (self.stage / "qt.conf").write_text(
            "[Paths]\nPrefix = .\nPlugins = plugins\nQmlImports = qml\n", encoding="utf-8")

    def deploy_mlt(self) -> None:
        module_dir = self.prefix / "lib" / "mlt"
        data_dir = self.prefix / "share" / "mlt"
        for module in MLT_MODULES:
            source = module_dir / f"libmlt{module}.dll"
            if not source.is_file():
                fail(f"MLT module {source} is missing")
            self.copy(source, self.stage / "lib" / "mlt" / source.name)
        for item in sorted(data_dir.iterdir()):
            if item.is_file():
                self.copy(item, self.stage / "share" / "mlt" / item.name)
        for name in MLT_DATA_DIRS:
            folder = data_dir / name
            if not folder.is_dir():
                fail(f"MLT data folder {folder} is missing")
            for item in sorted(folder.rglob("*")):
                if item.is_file():
                    self.copy(item, self.stage / "share" / "mlt" / item.relative_to(data_dir))

    def imports_of(self, binary: Path) -> list[str]:
        result = subprocess.run(["objdump", "-p", str(binary)], check=True, text=True,
                                capture_output=True)
        return re.findall(r"DLL Name:\s*(\S+)", result.stdout)

    def is_system_library(self, name: str) -> bool:
        lower = name.lower()
        return (lower.startswith("api-ms-win-") or lower.startswith("ext-ms-")
                or (self.system_dir / name).is_file())

    def deploy_dependencies(self) -> None:
        """Copies the transitive DLL closure of every binary into the package root.

        A dependency that MSYS2 provides is always bundled, even if a file of the same name
        exists on this machine: libraries such as vulkan-1.dll or OpenCL.dll come with graphics
        drivers and are missing on many computers. Only genuine Windows components are taken
        from the system.
        """
        msys_dlls = {p.name.lower(): p for p in (self.prefix / "bin").glob("*.dll")}
        root_dlls = {p.name.lower() for p in self.stage.glob("*.dll")}
        pending = [p for p in self.stage.rglob("*") if p.suffix.lower() in (".exe", ".dll")]
        scanned: set[Path] = set()
        unresolved: list[str] = []
        while pending:
            binary = pending.pop()
            if binary in scanned:
                continue
            scanned.add(binary)
            for name in self.imports_of(binary):
                lower = name.lower()
                if lower in root_dlls:
                    continue
                source = msys_dlls.get(lower)
                if source is not None and lower not in SYSTEM_ONLY:
                    target = self.stage / source.name
                    self.copy(source, target)
                    root_dlls.add(lower)
                    pending.append(target)
                elif not self.is_system_library(name):
                    unresolved.append(f"{name} (needed by {binary.relative_to(self.stage)})")
        if unresolved:
            fail("unresolved dependencies:\n  " + "\n  ".join(sorted(set(unresolved))))
        log(f"Scanned {len(scanned)} binaries; {len(root_dlls)} DLLs in the package root")

    def bundled_packages(self) -> dict[str, Package]:
        used: dict[str, Package] = {}
        unknown = []
        for relative, source in sorted(self.origins.items()):
            owner = self.owner_of(source)
            if owner is None:
                unknown.append(relative)
            else:
                used[owner] = self.packages[owner]
        if unknown:
            fail("files without a known MSYS2 package:\n  " + "\n  ".join(unknown))
        return dict(sorted(used.items()))

    def write_licenses(self, used: dict[str, Package]) -> list[str]:
        licenses_dir = self.stage / "licenses"
        shutil.copy2(self.source_dir / "LICENSE", self.stage / "LICENSE.txt")
        shutil.copy2(self.source_dir / "THIRD_PARTY_NOTICES.md",
                     self.stage / "THIRD_PARTY_NOTICES.md")
        without_files = []
        for package in used.values():
            files = [f for f in package.files
                     if "/share/licenses/" in f and not f.endswith("/")]
            if not files:
                without_files.append(package.name)
                continue
            for f in files:
                # ucrt64/share/licenses/<name>/<file...> -> licenses/<package>/<file...>
                parts = Path(f.split("/share/licenses/", 1)[1]).parts
                relative = Path(*parts[1:]) if len(parts) > 1 else Path(parts[0])
                target = licenses_dir / package.name / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(self.msys_root / f, target)
        return without_files

    def write_component_list(self, used: dict[str, Package], without_files: list[str]) -> None:
        lines = [
            f"FrameTrellis {self.version} for Windows (x64) - bundled components",
            "",
            "FrameTrellis is licensed under the GNU General Public License, version 3 or (at your",
            "option) any later version; see LICENSE.txt. The package also contains the following",
            "third-party components, built by the MSYS2 project (https://www.msys2.org/). Their",
            "license texts are in the licenses folder, one folder per component.",
            "",
            f"{'Component':44} {'Version':28} License",
            f"{'-' * 44} {'-' * 28} {'-' * 40}",
        ]
        for package in used.values():
            licenses = ", ".join(v.removeprefix("spdx:") for v in package.licenses) or "unknown"
            lines.append(f"{package.name:44} {package.version:28} {licenses}")
        lines += [
            "",
            "Source code",
            "-----------",
            f"FrameTrellis: {REPOSITORY}/tree/v{self.version}",
            "Third-party components: the release page of this version provides an archive with",
            "the MSYS2 source package (build recipe, patches and upstream source) of every",
            f"component listed above. They are also available from {SOURCES_URL}",
            "under the file names listed in SOURCES.txt.",
        ]
        if without_files:
            lines += ["", "Components whose package ships no separate license file "
                          "(see the license column):"]
            lines += [f"  {name}" for name in without_files]
        (self.stage / "BUNDLED_COMPONENTS.txt").write_text("\n".join(lines) + "\n",
                                                           encoding="utf-8")
        sources = [package.source_archive for package in used.values()]
        (self.stage / "SOURCES.txt").write_text(
            "MSYS2 source packages of the bundled components (" + SOURCES_URL + "):\n"
            + "\n".join(sorted(set(sources))) + "\n", encoding="utf-8")
        (self.output / f"{self.name}-sources.json").write_text(json.dumps({
            "version": self.version,
            "base_url": SOURCES_URL,
            "archives": sorted(set(sources)),
        }, indent=2) + "\n", encoding="utf-8")

    def write_readme(self) -> None:
        text = f"""FrameTrellis {self.version} for Windows (x64)
{'=' * (len(self.version) + 32)}

FrameTrellis is a local-first, open-source desktop video editor.
Website and documentation: https://sanskarin.github.io/frametrellis/

Running
-------
Double-click frametrellis.exe. Nothing needs to be installed; you can keep this folder
anywhere you like, for example on a USB drive. Settings are stored in your Windows user
profile. To remove FrameTrellis, delete this folder.

Checking the installation
-------------------------
Open a command prompt in this folder and run:

    frametrellis-render.exe --self-test "%TEMP%\\frametrellis-self-test"

It generates sample media, builds a small project, exports it to MP4 and checks the result.
Every line should start with PASS.

License
-------
FrameTrellis is free software under the GNU General Public License, version 3 or later
(LICENSE.txt). It comes with ABSOLUTELY NO WARRANTY. The bundled third-party components and
their licenses are listed in BUNDLED_COMPONENTS.txt and the licenses folder.

Source code: {REPOSITORY}
"""
        (self.stage / "README.txt").write_text(text, encoding="utf-8")

    def write_sbom(self, used: dict[str, Package]) -> Path:
        now = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
        app_id = "SPDXRef-Package-FrameTrellis"
        packages = [{
            "SPDXID": app_id,
            "name": "FrameTrellis",
            "versionInfo": self.version,
            "supplier": "Person: Sanskar",
            "downloadLocation": f"git+{REPOSITORY}.git@v{self.version}",
            "homepage": "https://sanskarin.github.io/frametrellis/",
            "filesAnalyzed": False,
            "licenseConcluded": "GPL-3.0-or-later",
            "licenseDeclared": "GPL-3.0-or-later",
            "copyrightText": "Copyright (C) 2026 Sanskar and FrameTrellis contributors",
            "primaryPackagePurpose": "APPLICATION",
        }]
        relationships = [{
            "spdxElementId": "SPDXRef-DOCUMENT",
            "relationshipType": "DESCRIBES",
            "relatedSpdxElement": app_id,
        }]
        for package in used.values():
            spdx_id = "SPDXRef-Package-" + re.sub(r"[^A-Za-z0-9.-]", "-", package.name)
            entry = {
                "SPDXID": spdx_id,
                "name": package.name,
                "versionInfo": package.version,
                "supplier": "Organization: MSYS2",
                "downloadLocation": SOURCES_URL + package.source_archive,
                "filesAnalyzed": False,
                "licenseConcluded": package.spdx_license,
                "licenseDeclared": package.spdx_license,
                "copyrightText": "NOASSERTION",
                "description": package.description,
            }
            if package.url:
                entry["homepage"] = package.url
            if package.spdx_license == "NOASSERTION" and package.licenses:
                entry["licenseComments"] = "MSYS2 license field: " + ", ".join(package.licenses)
            packages.append(entry)
            relationships.append({
                "spdxElementId": app_id,
                "relationshipType": "CONTAINS",
                "relatedSpdxElement": spdx_id,
            })
        document = {
            "spdxVersion": "SPDX-2.3",
            "dataLicense": "CC0-1.0",
            "SPDXID": "SPDXRef-DOCUMENT",
            "name": f"{self.name}",
            "documentNamespace": f"{REPOSITORY}/releases/v{self.version}/{self.name}-{uuid.uuid4()}",
            "creationInfo": {
                "created": now,
                "creators": ["Tool: frametrellis-packaging/windows/package.py",
                             "Organization: FrameTrellis"],
            },
            "packages": packages,
            "relationships": relationships,
        }
        target = self.output / f"{self.name}.spdx.json"
        target.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
        return target

    def make_zip(self) -> Path:
        target = self.output / f"{self.name}.zip"
        if target.exists():
            target.unlink()
        with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED,
                             compresslevel=6) as archive:
            for path in sorted(self.stage.rglob("*")):
                if path.is_file():
                    archive.write(path, Path(self.name) / path.relative_to(self.stage))
        return target

    def run(self) -> None:
        log(f"Packaging FrameTrellis {self.version} into {self.stage}")
        self.prepare()
        self.deploy_qt()
        self.deploy_mlt()
        self.deploy_dependencies()
        used = self.bundled_packages()
        without_files = self.write_licenses(used)
        self.write_component_list(used, without_files)
        self.write_readme()
        sbom = self.write_sbom(used)
        archive = self.make_zip()
        size = sum(p.stat().st_size for p in self.stage.rglob("*") if p.is_file())
        log(f"Bundled {len(used)} MSYS2 packages; package size {size / 2**20:.1f} MiB")
        if without_files:
            log("Packages without a license file: " + ", ".join(without_files))
        log(f"Wrote {archive} ({archive.stat().st_size / 2**20:.1f} MiB) and {sbom}")


def read_version(source_dir: Path) -> str:
    text = (source_dir / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(FrameTrellis\s+VERSION\s+([0-9.]+)", text)
    if not match:
        fail("cannot read the project version from CMakeLists.txt")
    return match.group(1)


def msys_root_default() -> str:
    try:
        return subprocess.run(["cygpath", "-m", "/"], check=True, text=True,
                              capture_output=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", required=True, help="CMake build directory")
    parser.add_argument("--output", required=True, help="directory for the package")
    parser.add_argument("--source-dir", default=str(Path(__file__).resolve().parents[2]))
    parser.add_argument("--msys-root", default=msys_root_default(),
                        help="MSYS2 installation root (default: from cygpath)")
    parser.add_argument("--msystem", default=os.environ.get("MSYSTEM", "UCRT64").lower())
    parser.add_argument("--version", help="version to package (default: from CMakeLists.txt)")
    args = parser.parse_args()
    if not args.msys_root:
        fail("cannot find the MSYS2 root; pass --msys-root")
    Packager(args).run()


if __name__ == "__main__":
    main()
