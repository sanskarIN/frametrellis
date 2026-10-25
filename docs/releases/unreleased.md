# Unreleased

Changes after [FrameTrellis 1.2.2](v1.2.2.md) under review for a future release.
Nothing on this page is tagged or published as a stable version yet.

## Rendering reliability and diagnostics (proposed)

- Protect the originally opened project file from replacement by the standalone
  `frametrellis-render` command, including a project saved under an `.mp4` filename.
- Interpret stdin cancellation as an exact line command instead of matching the word
  `cancel` inside unrelated input; bound command buffering.
- Add `frametrellis-render --inspect-project --project FILE` for read-only,
  machine-readable project diagnostics: separate video/audio track counts, unique
  referenced/unused media counts, missing media used by clips and total duration,
  without starting MLT or exporting video.
- Cover these behaviors with focused unit and render-helper integration tests.

For fast export options already released, see the
[v1.2.2 release notes](v1.2.2.md).
