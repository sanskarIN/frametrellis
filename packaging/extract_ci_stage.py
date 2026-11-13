# SPDX-License-Identifier: GPL-3.0-or-later
"""Safely unpack a production GitHub Actions artifact into an isolated stage.

A separate trusted workflow first checks the artifact ZIP's SHA-256 against
GitHub's API digest and checks the run's exact main-branch commit. This
extractor rejects unsafe ZIP paths, extra/duplicate files and a mismatched
SOURCE_COMMIT.txt before touching the output directory. Nothing is published.
"""
from __future__ import annotations

import argparse
import re
import shutil
import stat
import sys
import unicodedata
import zipfile
from pathlib import Path

from assemble_candidate_matrix import COMMIT, EVIDENCE_FILE, platform_names

MAX_TOTAL_SIZE = 16 * 1024 * 1024 * 1024  # release artifact budget, not a legal audit


def unpack(archive_path: Path, output: Path, version: str,
           platform: str, expected_sha: str) -> list[str]:
    if not COMMIT.fullmatch(expected_sha):
        raise ValueError("Expected an exact lowercase 40-character source SHA")
    expected = platform_names(version, platform) | {EVIDENCE_FILE}
    if archive_path.is_symlink() or not archive_path.is_file():
        raise ValueError("Artifact ZIP must be a real file")
    if output.is_symlink() or output.exists():
        raise ValueError("Refusing to replace an existing output stage")
    if not output.parent.is_dir() or output.parent.is_symlink():
        raise ValueError("Stage parent must already exist and not be a symlink")

    with zipfile.ZipFile(archive_path) as archive:
        entries = archive.infolist()
        if len(entries) != len(expected):
            raise ValueError("Production stage has missing or unexpected ZIP members")
        seen: set[str] = set()
        total = 0
        for item in entries:
            name = item.filename
            # An uploaded production stage must be a flat archive. No path
            # traversal, unicode/case aliases, directories or symlink members.
            if (name != Path(name).name or "/" in name or "\\" in name
                    or ":" in name or name in (".", "..") or item.is_dir()
                    or stat.S_ISLNK(item.external_attr >> 16)):
                raise ValueError(f"Unsafe stage ZIP member: {name}")
            folded = unicodedata.normalize("NFC", name).casefold()
            if folded in seen:
                raise ValueError(f"Duplicate stage ZIP member: {name}")
            seen.add(folded)
            total += item.file_size
            if item.file_size == 0 or total > MAX_TOTAL_SIZE:
                raise ValueError(f"Empty or oversized stage ZIP member: {name}")
        names = {item.filename for item in entries}
        if names != expected:
            raise ValueError(
                f"Unexpected stage payloads: missing={sorted(expected - names)}, "
                f"unexpected={sorted(names - expected)}"
            )
        with archive.open(EVIDENCE_FILE) as stream:
            declared = stream.read(128)
        if declared != (expected_sha + "\n").encode("ascii"):
            raise ValueError("Artifact source commit does not match the trusted main SHA")
        if archive.testzip() is not None:
            raise ValueError("Production stage ZIP has corrupt contents")
        output.mkdir(exist_ok=False)
        sentinel = output / "INCOMPLETE-DO-NOT-PUBLISH"
        sentinel.write_text("Stage extraction not finished.\n", encoding="utf-8")
        try:
            for item in entries:
                dest = output / item.filename
                with archive.open(item) as source, dest.open("xb") as target:
                    shutil.copyfileobj(source, target, length=1024 * 1024)
                if dest.stat().st_size != item.file_size:
                    raise ValueError(f"Extracted file length mismatch: {item.filename}")
            sentinel.unlink()
        except Exception:
            # Never convert partial or corrupt extraction into a publishable stage.
            raise
    return sorted(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--source-sha", required=True)
    args = parser.parse_args()
    try:
        unpack(args.archive, args.output, args.version, args.platform, args.source_sha)
    except (OSError, ValueError, UnicodeError, zipfile.BadZipFile) as exc:
        print(f"Production stage rejected: {exc}", file=sys.stderr)
        return 1
    print("Production stage extracted; external licensing and hardware gates remain required.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
