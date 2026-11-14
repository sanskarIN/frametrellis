# SPDX-License-Identifier: GPL-3.0-or-later
"""Fail closed when public five-file Windows release assets differ from candidate.

The local candidate must first pass verify_release_assets.py. GitHub's uploaded
asset metadata must preserve the full name set, byte counts, state and digest.
This is for the approved Windows-binary + Linux/macOS-source-build v1.5 route,
not the unrelated fourteen-file three-platform binary distribution.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from verify_candidate_matrix import digest_file


def expected_names(version: str) -> set[str]:
    if version != "1.5.0":
        raise ValueError("Five-file source-build distribution is v1.5.0 only")
    return {
        f"FrameTrellis-{version}-windows-x64-setup.exe",
        f"FrameTrellis-{version}-windows-x64.zip",
        f"FrameTrellis-{version}-windows-x64.spdx.json",
        f"FrameTrellis-{version}-windows-x64-third-party-sources.tar",
        "SHA256SUMS.txt",
    }


def verify(assets: object, directory: Path, version: str) -> list[str]:
    expected = expected_names(version)
    if not isinstance(assets, list):
        raise ValueError("Release assets metadata must be a list")
    by_name: dict[str, dict] = {}
    for asset in assets:
        if not isinstance(asset, dict) or not isinstance(asset.get("name"), str):
            raise ValueError("Malformed public release asset metadata")
        if asset["name"] in by_name:
            raise ValueError("Duplicate release asset name")
        by_name[asset["name"]] = asset
    if set(by_name) != expected:
        raise ValueError(
            f"Wrong release assets: missing={sorted(expected - set(by_name))}, "
            f"extra={sorted(set(by_name) - expected)}"
        )
    for name in sorted(expected):
        local = directory / name
        if local.is_symlink() or not local.is_file() or local.stat().st_size == 0:
            raise ValueError(f"Unsafe, empty or missing local candidate: {name}")
        actual = by_name[name]
        if actual.get("size") != local.stat().st_size:
            raise ValueError(f"Uploaded size differs from verified candidate: {name}")
        if actual.get("state") != "uploaded":
            raise ValueError(f"Asset not uploaded: {name}")
        if actual.get("digest") != "sha256:" + digest_file(local):
            raise ValueError(f"Uploaded checksum differs or absent: {name}")
    return sorted(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-json", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        files = verify(
            json.loads(args.assets_json.read_text(encoding="utf-8")),
            args.directory,
            args.version,
        )
    except (ValueError, OSError) as exc:
        print(f"Five-file publication verification FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: all {len(files)} public files match their verified release candidate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
