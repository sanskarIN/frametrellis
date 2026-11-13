# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the *proposed* three-platform v1.5 asset set before any publication.

This is an offline archive/integrity preflight, NOT proof of notarization,
licensing, runtime compatibility, malware scanning or owner release approval.
Use only after the Linux and Mac experimental packages have been promoted
through their separate signed, clean-host and third-party-source gates.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import sys
import tarfile
import unicodedata
import zipfile
from pathlib import Path, PurePosixPath

VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SUM_LINE = re.compile(r"^([0-9a-fA-F]{64})  ([A-Za-z0-9][A-Za-z0-9_.-]+)$")
PLATFORM_SUFFIXES = (
    "windows-x64-setup.exe",
    "windows-x64.zip",
    "windows-x64.spdx.json",
    "windows-x64-third-party-sources.tar",
    "linux-x86_64.AppImage",
    "linux-x86_64.spdx.json",
    "linux-x86_64-third-party-sources.tar",
    "macos-arm64.dmg",
    "macos-arm64.spdx.json",
    "macos-arm64-third-party-sources.tar",
    "macos-x86_64.dmg",
    "macos-x86_64.spdx.json",
    "macos-x86_64-third-party-sources.tar",
)
FORBIDDEN_LABELS = ("EXPERIMENTAL", "UNNOTARIZED", "development", "NOT-STABLE")


def required_payloads(version: str) -> set[str]:
    if not VERSION.fullmatch(version):
        raise ValueError(f"Invalid CMake version: {version!r}")
    return {f"FrameTrellis-{version}-{suffix}" for suffix in PLATFORM_SUFFIXES}


def checksums_from_text(text: str, required: set[str]) -> dict[str, str]:
    found: dict[str, str] = {}
    for line in text.splitlines():
        parsed = SUM_LINE.fullmatch(line)
        if parsed is None:
            raise ValueError(f"Invalid SHA256SUMS record: {line!r}")
        digest, name = parsed.groups()
        if name not in required or name in found:
            raise ValueError(f"Unexpected or repeated checksum record: {name}")
        found[name] = digest.lower()
    if set(found) != required:
        raise ValueError(
            f"Checksum records incomplete: missing={sorted(required - set(found))}"
        )
    return found


def digest_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def validate_spdx(path: Path, version: str) -> None:
    """Reject placeholder SBOMs; the declared inventory still needs a legal audit."""
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or document.get("spdxVersion") != "SPDX-2.3":
        raise ValueError(f"Invalid SPDX 2.3 document: {path.name}")
    if document.get("SPDXID") != "SPDXRef-DOCUMENT" or document.get("dataLicense") != "CC0-1.0":
        raise ValueError(f"Missing SPDX document identity/license: {path.name}")
    namespace = document.get("documentNamespace")
    if not isinstance(namespace, str) or not namespace.startswith("https://"):
        raise ValueError(f"Invalid SPDX document namespace: {path.name}")
    creation = document.get("creationInfo")
    if (not isinstance(creation, dict)
            or not isinstance(creation.get("created"), str)
            or not re.fullmatch(r"\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z",
                                creation["created"])
            or not isinstance(creation.get("creators"), list)
            or not creation["creators"]
            or not all(isinstance(s, str) and s.strip() for s in creation["creators"])):
        raise ValueError(f"Missing SPDX creation information: {path.name}")
    packages = document.get("packages")
    if not isinstance(packages, list) or len(packages) < 2:
        raise ValueError(f"SPDX needs an application and third-party inventory: {path.name}")
    app_id: str | None = None
    identifiers: set[str] = set()
    dependent_ids: set[str] = set()
    for package in packages:
        if not isinstance(package, dict):
            raise ValueError(f"Malformed SPDX package: {path.name}")
        ident = package.get("SPDXID")
        if (not isinstance(ident, str)
                or not re.fullmatch(r"SPDXRef-[A-Za-z0-9.-]+", ident)
                or ident == "SPDXRef-DOCUMENT" or ident in identifiers):
            raise ValueError(f"Missing or duplicate SPDX package ID: {path.name}")
        identifiers.add(ident)
        for field in ("name", "versionInfo", "licenseDeclared", "licenseConcluded",
                      "downloadLocation"):
            val = package.get(field)
            if not isinstance(val, str) or not val.strip():
                raise ValueError(f"Missing SPDX package {field}: {path.name}")
        if package["name"] == "FrameTrellis":
            if app_id is not None or package["versionInfo"] != version:
                raise ValueError(f"SPDX version or application package mismatch: {path.name}")
            if package["licenseDeclared"] != "GPL-3.0-or-later":
                raise ValueError(f"SPDX application license mismatch: {path.name}")
            app_id = ident
        else:
            if package["downloadLocation"] in ("NOASSERTION", "NONE"):
                raise ValueError(f"Third-party SPDX package lacks source location: {path.name}")
            dependent_ids.add(ident)
    if app_id is None or not dependent_ids:
        raise ValueError(f"SPDX version or third-party packages missing: {path.name}")
    relations = document.get("relationships")
    if not isinstance(relations, list):
        raise ValueError(f"SPDX document relationships missing: {path.name}")
    described = False
    contained: set[str] = set()
    for relation in relations:
        if not isinstance(relation, dict):
            raise ValueError(f"Malformed SPDX relationship: {path.name}")
        origin = relation.get("spdxElementId")
        related = relation.get("relatedSpdxElement")
        kind = relation.get("relationshipType")
        if kind == "DESCRIBES" and origin == "SPDXRef-DOCUMENT" and related == app_id:
            described = True
        if kind == "CONTAINS" and origin == app_id and related in dependent_ids:
            contained.add(related)
    if not described or contained != dependent_ids:
        raise ValueError(f"SPDX missing app/third-party DESCRIBES or CONTAINS links: {path.name}")

def safe_archive_path(name: str) -> bool:
    """Reject absolute, traversing and platform-ambiguous archive members."""
    if not name or "\\" in name or ":" in name or name.startswith("/"):
        return False
    parts = name.rstrip("/").split("/")
    return all(part not in ("", ".", "..") for part in parts)


def validate_windows_portable_zip(path: Path) -> None:
    """Verify the editor and subprocess helper are unique adjacent ZIP entries."""
    with zipfile.ZipFile(path, "r") as archive:
        entries = archive.infolist()
        names = [item.filename.rstrip("/") for item in entries]
        if len(names) != len(set(unicodedata.normalize("NFC", name).casefold()
                                 for name in names)):
            raise ValueError("Portable ZIP has duplicate or case-folded duplicate paths")
        for item in entries:
            if not safe_archive_path(item.filename):
                raise ValueError(f"Unsafe portable ZIP path: {item.filename}")
            if stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError(f"Portable ZIP contains a symbolic link: {item.filename}")

        executables: dict[str, list[PurePosixPath]] = {
            name: [
                PurePosixPath(item.filename)
                for item in entries
                if not item.is_dir()
                and PurePosixPath(item.filename).name.casefold() == name
            ]
            for name in ("frametrellis.exe", "frametrellis-render.exe")
        }
        if len(executables["frametrellis.exe"]) != 1:
            raise ValueError("Portable ZIP must contain exactly one frametrellis.exe")
        if len(executables["frametrellis-render.exe"]) != 1:
            raise ValueError("Portable ZIP must contain exactly one frametrellis-render.exe")
        if executables["frametrellis.exe"][0].parent != executables[
            "frametrellis-render.exe"
        ][0].parent:
            raise ValueError("Portable ZIP editor and render helper must be adjacent")
        bad_file = archive.testzip()
        if bad_file:
            raise ValueError(f"Corrupt portable ZIP member: {bad_file}")


def validate_corresponding_source_tar(path: Path) -> None:
    """Reject unsafe/empty source archives; legal source completeness is separate."""
    seen: set[str] = set()
    nonempty_files = 0
    with tarfile.open(path, "r:") as archive:
        for item in archive:
            if not safe_archive_path(item.name):
                raise ValueError(f"Unsafe corresponding-source TAR path: {item.name}")
            normalized = unicodedata.normalize("NFC", item.name.rstrip("/")).casefold()
            if normalized in seen:
                raise ValueError(f"Duplicate corresponding-source TAR path: {item.name}")
            seen.add(normalized)
            if not (item.isfile() or item.isdir()):
                raise ValueError(f"Corresponding-source TAR has a link/special entry: {item.name}")
            if item.isfile() and item.size > 0:
                nonempty_files += 1
    if not nonempty_files:
        raise ValueError(f"Corresponding-source TAR has no nonempty files: {path.name}")


def validate_structure(path: Path, version: str) -> None:
    name = path.name
    if name.endswith(".spdx.json"):
        validate_spdx(path, version)
    elif name.endswith(".AppImage"):
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                raise ValueError(f"Linux AppImage lacks an ELF header: {name}")
    elif name.endswith(".dmg"):
        # UDIF (the hdiutil UDZO image used for development) has a "koly"
        # footer at the beginning of its final 512-byte trailer. Native Mac
        # hdiutil verification and notarization remain separate requirements.
        with path.open("rb") as stream:
            if path.stat().st_size < 512:
                raise ValueError(f"macOS DMG has no UDIF trailer: {name}")
            stream.seek(-512, 2)
            if stream.read(4) != b"koly":
                raise ValueError(f"macOS DMG lacks its UDIF trailer: {name}")
    elif name.endswith("-setup.exe"):
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                raise ValueError(f"Windows installer lacks an MZ header: {name}")
    elif name.endswith(".zip"):
        validate_windows_portable_zip(path)
    elif name.endswith("-third-party-sources.tar"):
        validate_corresponding_source_tar(path)


def verify(directory: Path, version: str) -> list[str]:
    required = required_payloads(version)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError("Candidate asset directory must be a real directory")
    present = {entry.name for entry in directory.iterdir()}
    permitted = required | {"SHA256SUMS.txt", "release-notes.md"}
    if present - permitted or required - present or "SHA256SUMS.txt" not in present:
        raise ValueError(
            f"Candidate asset set incomplete or unexpected: "
            f"missing={sorted((required | {'SHA256SUMS.txt'}) - present)}, "
            f"unexpected={sorted(present - permitted)}"
        )
    if any(any(label.casefold() in name.casefold() for label in FORBIDDEN_LABELS)
           for name in present):
        raise ValueError("Experimental/unnotarized assets are not valid stable candidates")
    manifest = directory / "SHA256SUMS.txt"
    if manifest.is_symlink() or not manifest.is_file():
        raise ValueError("Unsafe checksum manifest")
    notes = directory / "release-notes.md"
    if notes.is_symlink() or (notes.exists() and
                              (not notes.is_file() or notes.stat().st_size == 0)):
        raise ValueError("Unsafe or empty optional release notes")
    records = checksums_from_text(manifest.read_text(encoding="utf-8"), required)
    for name in sorted(required):
        path = directory / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing, symlinked or empty candidate payload: {name}")
        if digest_file(path) != records[name]:
            raise ValueError(f"SHA-256 mismatch: {name}")
        validate_structure(path, version)
    return sorted(required)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Exact numeric CMake version")
    parser.add_argument("--directory", type=Path, required=True)
    args = parser.parse_args()
    try:
        files = verify(args.directory, args.version)
    except (OSError, ValueError, json.JSONDecodeError,
            tarfile.TarError, zipfile.BadZipFile) as exc:
        print(f"Candidate matrix FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"Candidate integrity matrix PASS: {len(files)} assets, three operating systems.")
    print("Physical QA, notarization/signatures, licensing and owner approval remain required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
