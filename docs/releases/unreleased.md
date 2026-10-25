# Unreleased

The latest published stable release is [FrameTrellis 1.2.2](v1.2.2.md).
The changes below have been **merged into `main` but are not yet released**
to users. An open PR is not a shipped feature.

## Integrated reliability and safety

- Prevent project saves over source media and reject duplicate media paths,
  including canonical/path aliases (PRs #23, #24).
- Detect same-size external changes to media sources and retain Unicode
  export-path regression coverage (PRs #25, #17).
- Validate extreme timecode values, fade totals, clip/source endpoints
  and free-range queries without signed overflow (PRs #22, #30, #71).
- Bound render-helper protocol messages, handle cancellation as an exact
  line command, and prevent exporting over the currently opened project
  document (PRs #19, #68).
- Add read-only `frametrellis-render --inspect-project --project FILE`
  diagnostics with counts for track kinds, used/unused media, missing media
  referenced by timeline clips, and duration (PR #68).
- Serialize MLT profile construction to prevent a concurrency issue
  observed in sanitizer tests (PR #63).

## Integrated editor workflow improvements

- Search media in the bin and filter by kind (PR #16).
- Jump to an exact SMPTE timecode with the Go to Timecode dialog and
  Ctrl+G shortcut (PR #26).

## Integrated documentation improvements

- Searchable documentation navigation, copyable code examples, section
  links, accessibility styling and new website tests (PR #69).

## Still under review — not integrated

- Zoom to Selection and Center on Playhead (PR #70) and additional
  timeline, media, preview and export feature PRs.

See [draft v1.3.0 notes](v1.3.0.md) and [the candidate acceptance plan](v1.3.0-plan.md).
**No v1.3.0 tag, prerelease, stable release or downloadable installer is published.**
