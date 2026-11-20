# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

## Next patch candidate

[v1.5.1 candidate notes](v1.5.1.md) and its [release gate](v1.5.1-preflight.md)
track post-v1.5.0 reliability changes. **v1.5.1 is not tagged or published.**
A versioned offline candidate build does not grant publication approval.
The uploader is limited to that exact version and requires separate manual owner
approval, frozen-main build evidence and complete binary verification.

## Latest stable release

**[FrameTrellis v1.5.0](https://github.com/sanskarIN/frametrellis/releases/tag/v1.5.0)**
was published on **16 November 2026** from
[commit `4edbf6cb`](https://github.com/sanskarIN/frametrellis/commit/4edbf6cb3c5eecfd9ec92993689de8943620ddfd).
It includes five public files: the Windows x64 installer, portable ZIP, SPDX
SBOM, corresponding-source archive, and SHA-256 checksums. Linux and macOS
are supported from source; no stable standalone packages are available.
The [v1.5.0 preflight](v1.5.0-preflight.md) is an archived planning checklist,
**not** a statement that the release remains unpublished. See the
[published notes](v1.5.0.md) for exact features and limitations.

| Version | Notes |
|---|---|
| 1.5.0 | [v1.5.0.md](v1.5.0.md) — **published featured stable 16 November 2026**; Windows x64 binaries and Linux/macOS source-build instructions |
| 1.4.0 | [v1.4.0.md](v1.4.0.md) — **published stable 2 November 2026**; see the [publication record](v1.4.0-preflight.md) |
| 1.3.1 | [v1.3.1.md](v1.3.1.md) — maintenance fixes for media-bin selection, offline keyboard relink and regression coverage (published stable) |
| 1.3.0 | [v1.3.0.md](v1.3.0.md) — source safety, searchable media bin, timecode navigation and render diagnostics |
| 1.2.2 | [v1.2.2.md](v1.2.2.md) — faster MP4 export presets and verified resolution safety |
| 1.2.1 | [v1.2.1.md](v1.2.1.md) — startup diagnostics and Windows package verification |
| 1.2.0 | [v1.2.0.md](v1.2.0.md) — multi-clip editing, clip attributes clipboard and batch adjustments |
| 1.1.1 | [v1.1.1.md](v1.1.1.md) — title creation guard and export regression tests |
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — clip duplication, title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — the first stable release |

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
