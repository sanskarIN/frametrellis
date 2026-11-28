# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Validate release tags and separate advertised release from binary package versions.

CMake's project(VERSION) supports numeric versions only. The verified Windows
packaging workflows therefore produce base-version binaries (e.g. 1.4.0)
for the prerelease tag v1.4.0-beta.1. Do not mistake the suffix for a
different package filename when collecting assets.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass

VERSION_PATTERN = re.compile(
    r"^v(?P<base>[0-9]+\.[0-9]+\.[0-9]+)"
    r"(?P<suffix>-(?:[0-9A-Za-z-]+)(?:\.[0-9A-Za-z-]+)*)?$"
)


@dataclass(frozen=True)
class ReleaseVersion:
    tag: str
    version: str
    package_version: str
    prerelease: bool


def resolve_release_version(tag: str, project_version: str) -> ReleaseVersion:
    """Reject malformed, cross-version or unexpectedly qualified release tags."""
    match = VERSION_PATTERN.fullmatch(tag)
    if not match:
        raise ValueError(
            "Expected vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-SUFFIX release tag."
        )
    base = match.group("base")
    if base != project_version:
        raise ValueError(
            f"Release {tag} does not match CMake project version {project_version}."
        )
    return ReleaseVersion(
        tag=tag,
        version=tag[1:],
        package_version=base,
        prerelease=match.group("suffix") is not None,
    )


def require_windows_only_assets(release: ReleaseVersion) -> None:
    """Block new stable multi-platform versions from the legacy Windows uploader.

    The existing owner-operated workflow prepares and attaches only Windows
    files. v1.5.0+ requires the planned three-platform verified artifact set;
    success from this helper must never be mistaken for acceptance of a partial
    stable release. Prerelease preparations are development-only.
    """
    version = tuple(int(part) for part in release.package_version.split("."))
    if not release.prerelease and version >= (1, 5, 0):
        raise ValueError(
            f"Stable {release.tag} requires the full three-platform release "
            "matrix. The Windows-only preparation/uploader is not authorized; "
            "complete the 14-file publishing migration before release."
        )


def require_v15_source_build_assets(release: ReleaseVersion) -> None:
    """Authorize the *five-file* v1.5.0 option only via explicit scope selection.

    This does not approve publication. Linux and macOS are advertised as
    supported from source; it must not be described as a three-OS installer.
    The legacy windows-only policy remains unchanged for all other releases.
    """
    if release.prerelease or release.tag != "v1.5.0":
        raise ValueError(
            "The explicit source-build distribution scope is permitted only "
            "for stable v1.5.0; do not silently allow other release tags."
        )



def require_v151_source_build_assets(release: ReleaseVersion) -> None:
    """Allow the explicit five-file Windows+source-build route for v1.5.1 only.

    This validates *distribution scope*, not release readiness or permission to
    publish. The v1.5.0 historic approval remains separate and unchanged.
    """
    if release.prerelease or release.tag != "v1.5.1":
        raise ValueError(
            "The v1.5.1 source-build distribution scope is permitted only "
            "for stable v1.5.1; a separate owner approval is required."
        )



def require_v152_source_build_assets(release: ReleaseVersion) -> None:
    """Allow the separate five-file Windows+source-build route for v1.5.2 only.

    The exact opt-in guards asset naming and scope, NOT publication approval.
    A distinct owner marker, same-SHA checks and physical QA remain required.
    """
    if release.prerelease or release.tag != "v1.5.2":
        raise ValueError(
            "The v1.5.2 source-build distribution scope is permitted only "
            "for stable v1.5.2; a separate owner approval is required."
        )


def require_v153_source_build_assets(release: ReleaseVersion) -> None:
    """Allow only stable v1.5.3 five-file preparation, never publication."""
    if release.prerelease or release.tag != "v1.5.3":
        raise ValueError(
            "The v1.5.3 source-build distribution scope is permitted only "
            "for stable v1.5.3; a separate owner approval is required."
        )


def require_v154_source_build_assets(release: ReleaseVersion) -> None:
    """Allow only stable v1.5.4 five-file preparation, never publication."""
    if release.prerelease or release.tag != "v1.5.4":
        raise ValueError(
            "The v1.5.4 source-build distribution scope is permitted only "
            "for stable v1.5.4; a separate owner approval is required."
        )


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--project-version", required=True)
    policies = parser.add_mutually_exclusive_group()
    policies.add_argument(
        "--windows-only-assets", action="store_true",
        help="Legacy release policy: refuse stable v1.5.0+ five-file assets.",
    )
    policies.add_argument(
        "--v15-source-build-assets", action="store_true",
        help=(
            "Explicit alternative: five Windows assets with Linux/macOS source "
            "builds (stable v1.5.0 only)."
        ),
    )
    policies.add_argument(
        "--v151-source-build-assets", action="store_true",
        help=(
            "Explicit five-asset Windows binary + Linux/macOS "
            "source-build scope for stable v1.5.1 only."
        ),
    )
    policies.add_argument(
        "--v152-source-build-assets", action="store_true",
        help=(
            "Explicit five-asset Windows binary + Linux/macOS "
            "source-build scope for stable v1.5.2 only."
        ),
    )
    policies.add_argument(
        "--v153-source-build-assets", action="store_true",
        help="Explicit five-file Windows + Linux/macOS source scope for stable v1.5.3.",
    )
    policies.add_argument(
        "--v154-source-build-assets", action="store_true",
        help="Explicit five-file Windows + Linux/macOS source scope for stable v1.5.4.",
    )
    args = parser.parse_args()
    try:
        release = resolve_release_version(args.tag, args.project_version)
        if args.windows_only_assets:
            require_windows_only_assets(release)
        if args.v15_source_build_assets:
            require_v15_source_build_assets(release)
        if args.v151_source_build_assets:
            require_v151_source_build_assets(release)
        if args.v152_source_build_assets:
            require_v152_source_build_assets(release)
        if args.v153_source_build_assets:
            require_v153_source_build_assets(release)
        if args.v154_source_build_assets:
            require_v154_source_build_assets(release)
    except ValueError as exc:
        parser.error(str(exc))
    print(f"tag={release.tag}")
    print(f"version={release.version}")
    print(f"package_version={release.package_version}")
    print(f"prerelease={str(release.prerelease).lower()}")


if __name__ == "__main__":
    main()
