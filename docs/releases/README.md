# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

## Next maintenance candidate — preparation only

**v1.5.2 is proposed but not tagged, packaged under that version or published.**
See the [candidate notes](v1.5.2.md),
[preflight/physical-QA gate](v1.5.2-preflight.md), and
[release tracker #317](https://github.com/sanskarIN/frametrellis/issues/317).
The source project still reports v1.5.1 until the separately reviewed
v1.5.2 version bump. Green CI and draft notes are not owner release approval.

## Latest published stable release

**[FrameTrellis v1.5.1](https://github.com/sanskarIN/frametrellis/releases/tag/v1.5.1)**
was published on **20 November 2026** from
[commit `bb491caa`](https://github.com/sanskarIN/frametrellis/commit/bb491caada9b037c82f07c4c40b575e05ce3e0f2).
It has five public assets: the unsigned Windows x64 installer, portable ZIP,
SPDX SBOM, corresponding third-party source archive and SHA256SUMS.txt.
Linux and macOS remain supported from source, not by production AppImage or
notarized DMG assets. See the [published maintenance notes](v1.5.1.md)
and the [physical QA still outstanding](v1.5.1-preflight.md).
The v1.5.1 release and tag must not be modified by post-release development.

| Version | Notes |
|---|---|
| 1.5.2 | [v1.5.2.md](v1.5.2.md) — **candidate notes only; not published** |
| 1.5.1 | [v1.5.1.md](v1.5.1.md) — published stable 20 November 2026 |
| 1.5.0 | [v1.5.0.md](v1.5.0.md) — published featured stable 16 November 2026 |
| 1.4.0 | [v1.4.0.md](v1.4.0.md) — published stable 2 November 2026 |
| 1.3.1 | [v1.3.1.md](v1.3.1.md) — maintenance corrections |
| 1.3.0 | [v1.3.0.md](v1.3.0.md) — media bin, timecode and render diagnostics |
| 1.2.2 | [v1.2.2.md](v1.2.2.md) — MP4 presets and resolution validation |
| 1.2.1 | [v1.2.1.md](v1.2.1.md) — Windows packaging verification |
| 1.2.0 | [v1.2.0.md](v1.2.0.md) — multi-clip editing and batch adjustments |
| 1.1.1 | [v1.1.1.md](v1.1.1.md) — title creation guard |
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — first stable release |

The earlier stable [v1.4.0 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.4.0)
was published on **2 November 2026** from commit
[`1ba161ed`](https://github.com/sanskarIN/frametrellis/commit/1ba161ed8b7d7f8a0a38494e535e3ac8644d797f).
Its Windows installer, portable ZIP, SPDX bill of materials, corresponding
source archive and SHA-256 checksums are attached. The exact release commit
passed [main CI](https://github.com/sanskarIN/frametrellis/actions/runs/41466264193),
[Windows packages](https://github.com/sanskarIN/frametrellis/actions/runs/41466264022)
and [release-asset attachment](https://github.com/sanskarIN/frametrellis/actions/runs/41470759563).
These automated results do **not** establish physical-device QA.
The older [v1.3.1 release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.1)
remains available for projects that do not need v1.4 functionality.
The [v1.3.0 verification report](v1.3.0-verification.md) documents
that earlier release's automated/manual testing distinction.

## Publishing a release as the repository owner

For v1.4.0 and subsequent releases, consult the
[manual release checklist](manual-publication.md) and the release-specific
runbook. The repository owner publishes releases through the GitHub UI;
preparation workflows must not create a GitHub tag or publish a release.
Attachment workflows depend on the version and its separately approved
distribution scope. Verify each release's actual published asset list and
checksums rather than assuming every workflow will upload files.

The `Windows packages` CI workflow runs independently of publication.
Website downloads must point to the actual published artifacts. Release
publication and unpublication are manual owner decisions; the
[v1.5.0 publication audit](../development/release-publication-policy.md)
is **read-only** and cannot send a published release back to Draft.
