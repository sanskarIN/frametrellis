# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Verify the complete, extracted manual GitHub Release upload set.

This does NOT publish, sign or perform physical Windows media QA.
Run: python3 packaging/verify_release_assets.py --version 1.4.0 --directory release
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import tarfile
import zipfile
from pathlib import Path, PurePosixPath

VERSION = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+$")
SUM_LINE = re.compile(r"^([0-9a-fA-F]{64})  (\S+)$")
SUFFIXES = (
    "windows-x64-setup.exe",
    "windows-x64.zip",
    "windows-x64.spdx.json",
    "windows-x64-third-party-sources.tar",
)


def safe_archive_name(name: str) -> bool:
    path = PurePosixPath(name)
    return (
        bool(name)
        and "\\" not in name
        and not path.is_absolute()
        and ".." not in path.parts
        and not any(part == "" for part in name.split("/")[:-1])
    )


def verify(directory: Path, version: str) -> list[str]:
    """Raise ValueError for mismatched assets or structurally unsafe archives."""
    if not VERSION.fullmatch(version):
        raise ValueError(f"Invalid numeric project version: {version!r}")
    if not directory.is_dir():
        raise ValueError(f"Release asset directory does not exist: {directory}")
    payloads = {f"FrameTrellis-{version}-{suffix}" for suffix in SUFFIXES}
    required = payloads | {"SHA256SUMS.txt"}
    found = {item.name for item in directory.iterdir()}
    unexpected = found - required - {"release-notes.md"}
    missing = required - found
    if missing or unexpected:
        raise ValueError(
            f"Release assets mismatch: missing={sorted(missing)}, unexpected={sorted(unexpected)}"
        )

    manifest = directory / "SHA256SUMS.txt"
    if manifest.is_symlink():
        raise ValueError("Checksum manifest must not be a symbolic link")
    checksum_lines = manifest.read_text(encoding="utf-8").splitlines()
    checksums: dict[str, str] = {}
    for line in checksum_lines:
        match = SUM_LINE.fullmatch(line)
        if match is None:
            raise ValueError(f"Invalid checksum record: {line!r}")
        digest, name = match.groups()
        if name not in payloads or name in checksums:
            raise ValueError(f"Unexpected or duplicate manifest entry: {name}")
        checksums[name] = digest.lower()
    if set(checksums) != payloads:
        raise ValueError(
            f"Checksum manifest does not list exactly four payloads: {sorted(checksums)}"
        )

    for name in sorted(payloads):
        file = directory / name
        if file.is_symlink() or not file.is_file() or file.stat().st_size == 0:
            raise ValueError(f"Missing, empty, or unsafe payload: {name}")
        digest = hashlib.sha256()
        with file.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != checksums[name]:
            raise ValueError(f"SHA-256 mismatch: {name}")

    portable = directory / f"FrameTrellis-{version}-windows-x64.zip"
    with zipfile.ZipFile(portable) as archive:
        names = [item.filename for item in archive.infolist()]
        if len(names) != len(set(names)):
            raise ValueError("Portable ZIP contains duplicate entry names")
        for item in archive.infolist():
            if not safe_archive_name(item.filename):
                raise ValueError(f"Unsafe portable ZIP entry: {item.filename}")
            if stat.S_ISLNK(item.external_attr >> 16):
                raise ValueError(f"Portable ZIP contains a symlink: {item.filename}")
        if not any(PurePosixPath(name).name.lower() == "frametrellis.exe" for name in names):
            raise ValueError("Portable ZIP does not contain frametrellis.exe")
        bad_member = archive.testzip()
        if bad_member:
            raise ValueError(f"Corrupt portable ZIP entry: {bad_member}")

    spdx = directory / f"FrameTrellis-{version}-windows-x64.spdx.json"
    document = json.loads(spdx.read_text(encoding="utf-8"))
    if not isinstance(document, dict) or document.get("spdxVersion") != "SPDX-2.3":
        raise ValueError("Missing or unsupported SPDX 2.3 document")
    packages = document.get("packages")
    if not isinstance(packages, list) or not any(
        isinstance(item, dict) and item.get("name") == "FrameTrellis"
        and item.get("versionInfo") == version for item in packages
    ):
        raise ValueError("SPDX document does not describe this FrameTrellis version")

    sources = directory / f"FrameTrellis-{version}-windows-x64-third-party-sources.tar"
    files_in_tar: set[str] = set()
    with tarfile.open(sources, mode="r:") as archive:
        for item in archive:
            if not safe_archive_name(item.name):
                raise ValueError(f"Unsafe corresponding-source TAR member: {item.name}")
            if not (item.isdir() or item.isfile()):
                raise ValueError(
                    f"Corresponding-source TAR contains a link or special file: {item.name}"
                )
            if item.name in files_in_tar:
                raise ValueError(f"Duplicate corresponding-source TAR member: {item.name}")
            files_in_tar.add(item.name)
    if not any(name.endswith("-sources.json") for name in files_in_tar):
        raise ValueError("Corresponding-source TAR missing package inventory")
    if not any(name.endswith(".src.tar.zst") for name in files_in_tar):
        raise ValueError("Corresponding-source TAR missing source packages")
    return sorted(payloads)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True, help="Exact numeric CMake version, e.g. 1.4.0")
    parser.add_argument("--directory", type=Path, required=True, help="Extracted release asset folder")
    args = parser.parse_args()
    try:
        assets = verify(args.directory, args.version)
    except (OSError, ValueError, tarfile.TarError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"Release verification FAILED: {exc}\n")
    print(
        f"Release verification PASSED: {len(assets)} SHA-256 matches; portable ZIP, "
        "SPDX document, corresponding-source TAR and filenames checked."
    )


if __name__ == "__main__":
    main()
