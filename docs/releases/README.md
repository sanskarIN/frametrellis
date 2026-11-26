# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

## Next maintenance candidate — preparation only

**v1.5.3 is targeted for 26 November 2026, not yet approved or published.**
See [candidate notes](v1.5.3.md), [preflight](v1.5.3-preflight.md), and the
[owner release runbook](v1.5.3-owner-release-runbook.md).

## Latest published stable release

**[FrameTrellis v1.5.2](https://github.com/sanskarIN/frametrellis/releases/tag/v1.5.2)**
was published on **24 November 2026** with five public Windows x64 assets:
installer, portable ZIP, SPDX SBOM, corresponding third-party sources and
SHA256SUMS.txt. Linux and macOS remain source builds. Do not modify the
published release or tag during preparation for v1.5.3.

| Version | Notes |
|---|---|
| 1.5.3 | [v1.5.3.md](v1.5.3.md) — candidate, not approved |
| 1.5.2 | [v1.5.2.md](v1.5.2.md) — published stable 24 November 2026 |
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
