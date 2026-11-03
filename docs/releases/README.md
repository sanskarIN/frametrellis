# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

| Version | Notes |
|---|---|
| 1.4.0 | [v1.4.0.md](v1.4.0.md) — **published stable 2 November 2026**; see the [publication record](v1.4.0-preflight.md) |
| 1.3.1 | [v1.3.1.md](v1.3.1.md) — maintenance fixes for media-bin selection, offline keyboard relink and regression coverage (published stable) |
| 1.3.0 | [v1.3.0.md](v1.3.0.md) — source safety, searchable media bin, timecode navigation and render diagnostics |
| 1.2.2 | [v1.2.2.md](v1.2.2.md) — faster MP4 export presets and verified resolution safety |
| 1.2.1 | [v1.2.1.md](v1.2.1.md) — startup diagnostics and Windows package verification |
| 1.2.0 | [v1.2.0.md](v1.2.0.md) — multi-clip editing, clip attributes clipboard and batch adjustments |
| 1.1.1 | [v1.1.1.md](v1.1.1.md) — title creation guard and export regression tests |
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — clip duplication, title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — the first stable release |

The stable [v1.4.0 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.4.0)
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

For v1.4.0 and subsequent releases, use the [direct GitHub release publishing
checklist](manual-publication.md). The **Prepare manual release** workflow only
rebuilds, verifies and assembles files; it never creates a GitHub tag or
release. Once physical-device acceptance is recorded and the owner publishes
the release in the GitHub UI, the **Attach verified release assets** workflow
automatically verifies and uploads all five matching assets. A failed job
means the publicly visible release may be missing files: inspect the run.

The `Windows packages` CI workflow can still run automatically. After asset
attachment succeeds, `Publish website` refreshes the download page. **No
Actions workflow creates or publishes a GitHub release.**
