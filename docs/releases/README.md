# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

| Version | Notes |
|---|---|
| 1.3.1 | [v1.3.1.md](v1.3.1.md) — maintenance fixes for media-bin interaction and offline keyboard relink |
| 1.3.0 | [v1.3.0.md](v1.3.0.md) — source safety, searchable media bin, timecode navigation and render diagnostics (published stable) |
| 1.2.2 | [v1.2.2.md](v1.2.2.md) — faster MP4 export presets and verified resolution safety |
| 1.2.1 | [v1.2.1.md](v1.2.1.md) — startup diagnostics and Windows package verification |
| 1.2.0 | [v1.2.0.md](v1.2.0.md) — multi-clip editing, clip attributes clipboard and batch adjustments |
| 1.1.1 | [v1.1.1.md](v1.1.1.md) — title creation guard and export regression tests |
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — clip duplication, title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — the first stable release |

The 1.3.x maintenance branch is based on the published 1.3.0 tag. The 1.3.1
release is published only after exact-SHA CI and clean-Windows package checks
pass. Post-1.3.0 development from `main` is deliberately excluded.

## Publishing a release

1. Make sure the version in `CMakeLists.txt` is the version to release, that
   `docs/releases/vX.Y.Z.md` exists and is not marked as a draft, and that CI and the Windows
   packages workflow are green on `main`. Optionally rehearse the release first: run the
   **Release** workflow by hand from the Actions tab with **publish=false**; it builds and
   verifies everything and keeps the release notes and checksums without publishing.
   A maintainer may instead choose **publish=true** to create or update the release only
   after the verified packages and source archive have been produced.
2. Publish the release, in either of two ways:
   * **In the web interface:** *Releases → Draft a new release*, choose a new tag `vX.Y.Z` on
     `main`, title it `FrameTrellis X.Y.Z`, and either paste the notes from `vX.Y.Z.md` or leave
     the description empty, then *Publish release*.
   * **From the command line:** `git tag -a vX.Y.Z -m "FrameTrellis X.Y.Z"` on `main`, then
     `git push origin vX.Y.Z`. The workflow creates the release.
3. The **Release** workflow builds the packages, verifies them on clean Windows machines and
   attaches them to the release, usually within 30 minutes. If the release has no description,
   it uses `vX.Y.Z.md`; otherwise it keeps the description and adds the download list and the
   checksums. If any check fails, nothing is attached.
4. The website's download page is updated automatically when the packages have been published.
