# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify uploaded GitHub Release metadata against the exact offline candidate.

The caller must first validate all 14 candidate files with
verify_candidate_matrix.py; this checks the public GitHub asset names, byte
counts, state and SHA-256 digests against those same local verified bytes.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from verify_candidate_matrix import digest_file, required_payloads


def verify_published(assets: object, candidate: Path, version: str) -> list[str]:
    required = required_payloads(version) | {"SHA256SUMS.txt"}
    if not isinstance(assets, list):
        raise ValueError("Release assets metadata must be a JSON list")
    found: dict[str, dict] = {}
    for item in assets:
        if not isinstance(item, dict) or not isinstance(item.get("name"), str):
            raise ValueError("Malformed release asset metadata")
        name = item["name"]
        if name in found:
            raise ValueError(f"Duplicate published asset name: {name}")
        found[name] = item
    if set(found) != required:
        raise ValueError(
            "Incomplete or unexpected published release assets: "
            f"missing={sorted(required - set(found))}, "
            f"extra={sorted(set(found) - required)}"
        )
    for name in sorted(required):
        local = candidate / name
        if local.is_symlink() or not local.is_file() or local.stat().st_size == 0:
            raise ValueError(f"Missing or unsafe verified candidate file: {name}")
        remote = found[name]
        if remote.get("state") != "uploaded" or remote.get("size") != local.stat().st_size:
            raise ValueError(f"Published asset size/state mismatch: {name}")
        digest = remote.get("digest")
        if not isinstance(digest, str) or digest != "sha256:" + digest_file(local):
            raise ValueError(f"Published asset SHA-256 missing or mismatched: {name}")
    return sorted(required)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets-json", type=Path, required=True)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    try:
        metadata = json.loads(args.assets_json.read_text(encoding="utf-8"))
        files = verify_published(metadata, args.directory, args.version)
    except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as exc:
        print(f"Published candidate verification FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: {len(files)} published assets match the exact 14-file verified candidate")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
