# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

| Version | Notes |
|---|---|
| 1.4.0 | [v1.4.0.md](v1.4.0.md) — **candidate under preparation, not published**; track remaining gates in [preflight](v1.4.0-preflight.md) |
| 1.3.1 | [v1.3.1.md](v1.3.1.md) — maintenance fixes for media-bin selection, offline keyboard relink and regression coverage (published stable) |
| 1.3.0 | [v1.3.0.md](v1.3.0.md) — source safety, searchable media bin, timecode navigation and render diagnostics |
| 1.2.2 | [v1.2.2.md](v1.2.2.md) — faster MP4 export presets and verified resolution safety |
| 1.2.1 | [v1.2.1.md](v1.2.1.md) — startup diagnostics and Windows package verification |
| 1.2.0 | [v1.2.0.md](v1.2.0.md) — multi-clip editing, clip attributes clipboard and batch adjustments |
| 1.1.1 | [v1.1.1.md](v1.1.1.md) — title creation guard and export regression tests |
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — clip duplication, title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — the first stable release |

The stable [v1.3.1 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.1)
contains verified Windows installer and portable ZIP downloads, SPDX bill of
materials, corresponding source archive and SHA-256 checksums. It was built
from a separate maintenance branch based on the v1.3.0 tag, and does not
include the newer project format or features being developed on `main`.
The [v1.3.0 verification report](v1.3.0-verification.md) documents the earlier
stable release's automated checks and manual testing distinction.

## Publishing a release as the repository owner

For v1.4.0 and subsequent releases, use the [direct GitHub release publishing
checklist](manual-publication.md). The **Prepare manual release** workflow only
rebuilds, verifies and assembles files for the maintainer. It **never creates a
GitHub tag, release or uploads release attachments**. A maintainer must attach
the installer, portable ZIP, SPDX SBOM, third-party corresponding source archive
and SHA-256 sums directly through the GitHub Releases interface after recording
physical-device acceptance results.

The `Windows packages` CI workflow can still run automatically, and publishing
a release triggers `Publish website` to update the download page. **No
Actions workflow automatically publishes a GitHub release.**
