# SPDX-License-Identifier: GPL-3.0-or-later
"""Stage a complete *future* three-platform release candidate without publishing it.

Each of four isolated production-package directories must contain exactly its
release payloads and a SOURCE_COMMIT.txt with the same 40-hex commit SHA.
This checks consistency of *declared* commit identities; it is NOT a
cryptographic build attestation and must be backed by trusted CI provenance.
No production Linux/macOS files are fabricated by this script.

Usage:
  python3 packaging/assemble_candidate_matrix.py --version 1.5.0 \
    --source-sha COMMIT_SHA --windows-dir WIN_DIR --linux-dir LINUX_DIR \
    --macos-arm64-dir ARM_DIR --macos-x86_64-dir INTEL_DIR \
    --output-dir NEW_EMPTY_PATH
"""
from __future__ import annotations

import argparse
import hashlib
import re
import shutil
import sys
from pathlib import Path

from verify_candidate_matrix import VERSION, required_payloads, verify

COMMIT = re.compile(r"^[a-f0-9]{40}$")
EVIDENCE_FILE = "SOURCE_COMMIT.txt"
SUFFIXES: dict[str, tuple[str, ...]] = {
    "windows-x64": (
        "setup.exe",
        "zip",
        "spdx.json",
        "third-party-sources.tar",
    ),
    "linux-x86_64": (
        "AppImage",
        "spdx.json",
        "third-party-sources.tar",
    ),
    "macos-arm64": (
        "dmg",
        "spdx.json",
        "third-party-sources.tar",
    ),
    "macos-x86_64": (
        "dmg",
        "spdx.json",
        "third-party-sources.tar",
    ),
}


def platform_names(version: str, platform: str) -> set[str]:
    if platform not in SUFFIXES:
        raise ValueError(f"Unknown release platform: {platform}")
    result = set()
    for suffix in SUFFIXES[platform]:
        if platform == "windows-x64" and suffix == "setup.exe":
            result.add(f"FrameTrellis-{version}-{platform}-setup.exe")
        elif suffix in ("zip", "AppImage", "dmg", "spdx.json"):
            result.add(f"FrameTrellis-{version}-{platform}.{suffix}")
        else:
            result.add(f"FrameTrellis-{version}-{platform}-{suffix}")
    return result


def digest_file(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(chunk)
    return result.hexdigest()


def validate_stages(
    version: str, source_sha: str, stages: dict[str, Path]
) -> dict[str, Path]:
    """Check all inputs BEFORE creating the output folder."""
    if not VERSION.fullmatch(version) or not COMMIT.fullmatch(source_sha):
        raise ValueError("Expected numeric release version and exact lowercase 40-hex source SHA")
    if set(stages) != set(SUFFIXES):
        raise ValueError("Exactly four platform stages are required")

    outputs: dict[str, Path] = {}
    for platform, directory in sorted(stages.items()):
        if directory.is_symlink() or not directory.is_dir():
            raise ValueError(f"Unsafe or missing stage directory: {platform}")
        required = platform_names(version, platform)
        found = {entry.name for entry in directory.iterdir()}
        expected = required | {EVIDENCE_FILE}
        if found != expected:
            raise ValueError(
                f"{platform} stage has missing or unexpected files: "
                f"missing={sorted(expected - found)}, extra={sorted(found - expected)}"
            )
        evidence = directory / EVIDENCE_FILE
        if evidence.is_symlink() or not evidence.is_file():
            raise ValueError(f"Unsafe source evidence: {platform}")
        declared = evidence.read_text(encoding="ascii").strip()
        if declared != source_sha or evidence.read_bytes() != (source_sha + "\n").encode("ascii"):
            raise ValueError(f"{platform} has mismatched/noncanonical source commit evidence")
        for name in sorted(required):
            path = directory / name
            if path.is_symlink() or not path.is_file() or path.stat().st_size == 0:
                raise ValueError(f"Unsafe, missing or empty release payload: {name}")
            if name in outputs:
                raise ValueError(f"Repeated candidate payload from multiple stages: {name}")
            outputs[name] = path
    if set(outputs) != required_payloads(version):
        raise ValueError("Staged candidate does not match the 13 required platform payloads")
    return outputs


def assemble(
    version: str, source_sha: str, stages: dict[str, Path], output: Path
) -> list[str]:
    sources = validate_stages(version, source_sha, stages)
    if output.is_symlink() or output.exists():
        raise ValueError("Refusing to overwrite an existing candidate output directory")
    if not output.parent.is_dir() or output.parent.is_symlink():
        raise ValueError("Output parent must be an existing, non-symlinked directory")

    # Exclusive directory creation prevents overwriting any existing output.
    # Keep the sentinel on ANY failure: verify_candidate_matrix refuses extra
    # files, so an incomplete or failed build can never pass the upload gate.
    output.mkdir(exist_ok=False)
    sentinel = output / "INCOMPLETE-DO-NOT-PUBLISH"
    sentinel.write_text("The candidate has not passed offline verification.\n", encoding="utf-8")
    try:
        checksums: list[str] = []
        for name, source in sorted(sources.items()):
            destination = output / name
            # Preserve AppImage executable mode and metadata from verified staging.
            shutil.copy2(source, destination, follow_symlinks=False)
            expected_hash = digest_file(source)
            actual_hash = digest_file(destination)
            if expected_hash != actual_hash:
                raise ValueError(f"Source changed during copying or copied bytes differ: {name}")
            checksums.append(f"{actual_hash}  {name}")
        (output / "SHA256SUMS.txt").write_text(
            "\n".join(checksums) + "\n", encoding="utf-8"
        )
        # The final verifier rejects extra files. Remove the sentinel only
        # during verification; restore it if anything fails.
        sentinel.unlink()
        try:
            checked = verify(output, version)
        except Exception:
            sentinel.write_text(
                "Candidate verification FAILED. Do not publish.\n", encoding="utf-8"
            )
            raise
    except Exception:
        # Never erase the user's sources or delete a pre-existing directory.
        # A failed output remains visibly incomplete and invalid for upload.
        if not sentinel.exists():
            sentinel.write_text("Candidate assembly FAILED. Do not publish.\n", encoding="utf-8")
        raise
    return checked


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--source-sha", required=True)
    parser.add_argument("--windows-dir", type=Path, required=True)
    parser.add_argument("--linux-dir", type=Path, required=True)
    parser.add_argument("--macos-arm64-dir", type=Path, required=True)
    parser.add_argument("--macos-x86_64-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    stages = {
        "windows-x64": args.windows_dir,
        "linux-x86_64": args.linux_dir,
        "macos-arm64": args.macos_arm64_dir,
        "macos-x86_64": args.macos_x86_64_dir,
    }
    try:
        files = assemble(args.version, args.source_sha, stages, args.output_dir)
    except (OSError, ValueError, UnicodeError) as exc:
        print(f"Three-platform candidate assembly FAILED: {exc}", file=sys.stderr)
        return 1
    print(f"PASS: {len(files)} payloads + SHA256SUMS.txt from declared source {args.source_sha}")
    print("NOT a release: verify signed source-build provenance, real hardware,")
    print("licensing, macOS Developer ID/notarization and owner approval separately.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
