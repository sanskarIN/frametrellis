# Release notes

Release notes for each tagged version live in this folder (`vX.Y.Z.md`) and are used as the
GitHub release description. Unreleased changes are collected in [unreleased.md](unreleased.md).

| Version | Notes |
|---|---|
| 1.1.0 | [v1.1.0.md](v1.1.0.md) — clip duplication, title overlays and container renderer |
| 1.0.0 | [v1.0.0.md](v1.0.0.md) — the first stable release |

## Publishing a release

1. Make sure the version in `CMakeLists.txt` is the version to release, that
   `docs/releases/vX.Y.Z.md` exists and is not marked as a draft, and that CI and the Windows
   packages workflow are green on `main`. Optionally rehearse the release first: run the
   **Release** workflow by hand from the Actions tab; it builds and verifies everything and
   keeps the release notes and checksums it would publish, without publishing.
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
