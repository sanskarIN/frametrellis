# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic contents audit for proposed Linux/macOS desktop bundles.

This is an integrity/provenance aid, NOT a declaration of licensing,
notarization, runtime compatibility, or a release. No private absolute
build-machine paths are written into the manifest.

Usage:
  python3 packaging/runtime_inventory.py generate --kind linux-appdir \
    --root dist/linux/AppDir --manifest dist/linux/appdir-inventory.json
  python3 packaging/runtime_inventory.py verify --root dist/linux/AppDir \
    --manifest dist/linux/appdir-inventory.json
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import stat
import sys
import unicodedata
from pathlib import Path

SCHEMA_VERSION = 1
KINDS = {"linux-appdir", "macos-app"}
HASH_CHUNK_BYTES = 1024 * 1024


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(HASH_CHUNK_BYTES), b""):
            result.update(chunk)
    return result.hexdigest()


def inventory(root: Path, kind: str) -> dict:
    if kind not in KINDS:
        raise ValueError(f"Unrecognized bundle kind: {kind}")
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Package root must be an existing real directory")
    root = root.resolve(strict=True)
    entries = []
    normalized_paths: set[str] = set()
    byte_total = 0
    counts = {"file": 0, "symlink": 0, "directory": 0}
    for candidate in sorted(root.rglob("*"), key=lambda p: p.relative_to(root).as_posix()):
        relative = candidate.relative_to(root).as_posix()
        normalized = unicodedata.normalize("NFC", relative).casefold()
        if normalized in normalized_paths:
            raise ValueError(f"Case-insensitive or Unicode path collision: {relative}")
        normalized_paths.add(normalized)
        info = candidate.lstat()
        if stat.S_ISLNK(info.st_mode):
            # Even links pointing back inside the package must be relative so
            # moving AppDir/.app cannot break them on another user's machine.
            target = os.readlink(candidate)
            if os.path.isabs(target):
                raise ValueError(f"Non-portable absolute symbolic link: {relative}")
            resolved = candidate.resolve(strict=False)
            if not resolved.is_relative_to(root) or not resolved.exists():
                raise ValueError(f"Escaping or broken symbolic link: {relative}")
            entries.append(
                {"path": relative, "type": "symlink",
                 "target": resolved.relative_to(root).as_posix()}
            )
            counts["symlink"] += 1
        elif stat.S_ISDIR(info.st_mode):
            entries.append({"path": relative, "type": "directory"})
            counts["directory"] += 1
        elif stat.S_ISREG(info.st_mode):
            entries.append(
                {"path": relative, "type": "file", "bytes": info.st_size,
                 "sha256": sha256(candidate),
                 "executable": bool(info.st_mode & 0o111)}
            )
            byte_total += info.st_size
            counts["file"] += 1
        else:
            raise ValueError(f"Unsupported device/socket/special package entry: {relative}")
    if not counts["file"]:
        raise ValueError("Package is empty: no regular files were found")
    return {
        "schemaVersion": SCHEMA_VERSION,
        "kind": kind,
        "algorithm": "sha256",
        "counts": counts,
        "regularFileBytes": byte_total,
        "entries": entries,
    }


def write_manifest(root: Path, kind: str, target: Path) -> dict:
    if target.exists() or target.is_symlink():
        raise ValueError(f"Refusing to overwrite package manifest: {target}")
    if target.resolve(strict=False).is_relative_to(root.resolve(strict=True)):
        raise ValueError("Place the manifest OUTSIDE the package being inventoried")
    report = inventory(root, kind)
    target.parent.mkdir(parents=True, exist_ok=True)
    # Write once; never include the host's path, timestamp, username, or
    # environment variables, so unchanged package bytes get identical JSON.
    try:
        with target.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, ensure_ascii=False, indent=2, sort_keys=True)
            handle.write("\n")
    except BaseException:
        target.unlink(missing_ok=True)
        raise
    return report


def verify_manifest(root: Path, manifest: Path) -> dict:
    if manifest.is_symlink() or not manifest.is_file():
        raise ValueError("Manifest must be a real regular file")
    expected = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(expected, dict) or expected.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError("Unsupported runtime inventory schema")
    if expected.get("kind") not in KINDS or expected.get("algorithm") != "sha256":
        raise ValueError("Unsupported runtime inventory format")
    if manifest.resolve(strict=True).is_relative_to(root.resolve(strict=True)):
        raise ValueError("Manifest is inside the package being inspected")
    actual = inventory(root, expected["kind"])
    if expected != actual:
        # Never print source absolute paths or complete manifest contents.
        previous = {e.get("path"): e for e in expected.get("entries", [])
                    if isinstance(e, dict)} if isinstance(expected.get("entries"), list) else {}
        current = {e["path"]: e for e in actual["entries"]}
        changed = sorted(key for key in previous.keys() | current.keys()
                         if previous.get(key) != current.get(key))
        example = changed[0] if changed else "summary counts or format"
        raise ValueError(f"Runtime manifest does not match package bytes: {example}")
    return actual


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("generate", "verify"))
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--kind", choices=sorted(KINDS))
    args = parser.parse_args()
    if args.action == "generate" and not args.kind:
        parser.error("--kind is required for generate")
    try:
        if args.action == "generate":
            report = write_manifest(args.root, args.kind, args.manifest)
        else:
            report = verify_manifest(args.root, args.manifest)
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"Runtime package inventory FAILED: {exc}", file=sys.stderr)
        return 1
    print(
        f"Runtime package inventory PASS: {report['kind']}, "
        f"{report['counts']['file']} files, {report['counts']['symlink']} links, "
        f"{report['regularFileBytes']} bytes"
    )
    print("License completeness, source provenance and clean-host QA are separate gates.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
