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


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--project-version", required=True)
    args = parser.parse_args()
    try:
        release = resolve_release_version(args.tag, args.project_version)
    except ValueError as exc:
        parser.error(str(exc))
    print(f"tag={release.tag}")
    print(f"version={release.version}")
    print(f"package_version={release.package_version}")
    print(f"prerelease={str(release.prerelease).lower()}")


if __name__ == "__main__":
    main()
