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
import sys
from pathlib import Path

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
    document = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or document.get("spdxVersion") != "SPDX-2.3":
        raise ValueError(f"Invalid SPDX 2.3 document: {path.name}")
    packages = document.get("packages")
    if not isinstance(packages, list) or not any(
        isinstance(package, dict)
        and package.get("name") == "FrameTrellis"
        and package.get("versionInfo") == version
        for package in packages
    ):
        raise ValueError(f"SPDX version or application package missing: {path.name}")


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
        with path.open("rb") as stream:
            if stream.read(2) != b"PK":
                raise ValueError(f"Windows ZIP lacks a ZIP signature: {name}")


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
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Candidate matrix FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"Candidate integrity matrix PASS: {len(files)} assets, three operating systems.")
    print("Physical QA, notarization/signatures, licensing and owner approval remain required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
