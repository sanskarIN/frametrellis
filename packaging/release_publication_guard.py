# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
"""Fail-closed publication policy for the v1.5.0 GitHub Release.

The release-event workflow runs after GitHub marks a release as published.
This helper decides whether that publication carries one of the two explicit
owner approval markers. It does not create, publish, tag, or edit a release;
the workflow performs only the narrowly scoped draft restoration.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

SOURCE_BUILD_MARKER = "FrameTrellis-1.5.0-SOURCE-BUILD-OWNER-APPROVED"
THREE_PLATFORM_MARKER = "FrameTrellis-1.5.0-OWNER-APPROVED"


def publication_decision(release: dict[str, Any]) -> str:
    """Return the safe action for one GitHub release API object.

    A published stable v1.5.0 release is allowed to remain public only when
    one supported approval marker appears as a complete body line. Missing,
    substring-only, or conflicting markers fail closed. Other tags,
    prereleases, and already-draft releases are never modified.
    """
    if not isinstance(release, dict):
        raise ValueError("Release API response must be a JSON object")

    tag = release.get("tag_name")
    if tag != "v1.5.0" or release.get("prerelease") is True:
        return "ignore"
    if release.get("draft") is True:
        return "already-draft"
    if release.get("draft") is not False:
        raise ValueError("Release API response has no explicit draft boolean")

    body = release.get("body")
    if body is None:
        body = ""
    if not isinstance(body, str):
        raise ValueError("Release body must be text or null")

    lines = body.splitlines()
    source_exact = SOURCE_BUILD_MARKER in lines
    matrix_exact = THREE_PLATFORM_MARKER in lines
    # Match the existing attachment workflows' substring conflict guard, but
    # require the selected marker itself to occupy a complete line.
    source_any = SOURCE_BUILD_MARKER in body
    matrix_any = THREE_PLATFORM_MARKER in body
    if source_exact and not matrix_any:
        return "approved-source-build"
    if matrix_exact and not source_any:
        return "approved-three-platform"
    return "restore-draft"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-json", required=True, type=Path)
    args = parser.parse_args()
    try:
        release = json.loads(args.release_json.read_text(encoding="utf-8"))
        decision = publication_decision(release)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        parser.error(str(exc))
    print(f"decision={decision}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
