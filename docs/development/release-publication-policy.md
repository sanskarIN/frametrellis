# FrameTrellis release publication policy

## Published v1.5.0

FrameTrellis v1.5.0 was published as a **stable** GitHub release on
16 November 2026. Its published asset set is Windows x64 installer and
portable ZIP, SPDX SBOM, third-party source archive and SHA256SUMS.
Linux and macOS are supported through source builds in this distribution.
Do not silently reinterpret this release as a three-platform binary release.

## Owner controls publication state

Only a deliberate repository-owner action may publish, unpublish, convert a
release to draft, or replace release assets. GitHub Actions that audit or
prepare release candidates **must not** automatically reverse a published
release, even when an older approval-marker convention is missing.

The workflow in `.github/workflows/guard-v15-publication.yml` is now a
read-only audit. It checks an existing v1.5.0 release using a token with
`contents: read`; it must not call release-edit, release-delete or PATCH
endpoints. Its decisions are:

- `approved-source-build`: the exact historical five-file scope marker
  appears alone on one line in the release body.
- `approved-three-platform`: the exact historical three-platform scope
  marker appears alone on one line in the release body.
- `owner-review-required`: the historical markers are absent, ambiguous
  or conflicting. This produces an Actions warning **without** editing the
  publication, tag, artifacts or release description.
- `already-draft` or `ignore`: informational; no changes.

A warning is not permission for an automated workflow to alter the state of a
published release. The owner may manually review the notes and the asset set.
The current workflow deliberately does not edit release text or add approval
markers on the owner's behalf.

## Changes after v1.5.0

Continue regular code and CI development on protected `main` through
reviewed pull requests. Do not rewrite `v1.5.0`, republish it, or attach
unverified platform artifacts to make existing assets appear to cover more
platforms. For later releases, use version-specific candidate verification,
independent CI, package checksums and **separate manual approval** before
publication.

Regression coverage in
`packaging/tests/test_release_publication_workflow.py` verifies the
read-only permissions and the absence of automatic draft-restoration commands.
The classifier is exercised by
`packaging/tests/test_release_publication_guard.py`.
