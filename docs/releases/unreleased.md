# FrameTrellis — Unreleased changes after 1.3.0

Stable release: [v1.3.0](v1.3.0.md) (last published stable).
The versioned 1.4 changes described below are **integrated into main** but
have not been promoted to a verified stable 1.4.0 release. The development
build may identify as 1.4.0 before any GitHub prerelease is published.

## Integrated development work

- [PR #78](https://github.com/sanskarIN/frametrellis/pull/78):
  media-bin delegate GridView focus/reference fix.
- [PR #79](https://github.com/sanskarIN/frametrellis/pull/79):
  frame-accurate source slip editing for multiple clips, source-handle and
  transition protection, undo/redo, inspector and shortcuts.
- [PR #75](https://github.com/sanskarIN/frametrellis/pull/75):
  persistent timeline review markers with undo, ruler, snapping and navigation;
  project document version **v2** (reads v1; not backward readable by 1.3).
- [PR #76](https://github.com/sanskarIN/frametrellis/pull/76):
  clipboard chapter timestamp and WebVTT chapter-track generation.
- [PR #77](https://github.com/sanskarIN/frametrellis/pull/77):
  cut-only CMX3600 picture EDL clipboard export with strict validation.

See [1.4.0 release notes](v1.4.0.md) for release scope and compatibility
limits and [the v1.4 smoke checklist](../development/v1.4.0-smoke-checks.md)
for manual testing not yet claimed as completed.

## Pending release work

- Reverify full Linux/Windows/ASan/format/packaging CI on **one final integrated
  commit** before publishing a prerelease.
- Verify tagged Windows binaries, checksums, third-party sources and SBOM.
- Record real desktop smoke checks on Windows and review marker project saves,
  v1-to-v2 compatibility, source slip, WebVTT and EDL import.
- Keep GitHub prerelease classification separate from stable 1.3.0 until
  promotion is justified. Open PRs remain proposals until merged.

## Other proposals not yet shipped

- Zoom to Selection and timeline navigation improvements (PR #70).
- Preview-frame PNG export (PR #27).
- Media inventory export, recursive media import, additional organization
  workflows and proxy playback need separate design/validation.

Merging a PR does not by itself create a published release.
