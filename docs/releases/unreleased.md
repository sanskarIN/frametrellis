# Unreleased (after FrameTrellis 1.3.0)

The latest published stable release is [FrameTrellis 1.3.0](v1.3.0.md).
The [v1.3.0 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.0)
provides verified Windows installer and portable ZIP downloads.
All changes already delivered in v1.3.0 belong in the
[1.3.0 release notes](v1.3.0.md), not here.

No subsequent features are claimed as integrated by this file yet.
Newly merged changes after the v1.3.0 release tag should be recorded here
with their PRs, behavior, tests and any known limitations.

## In-progress development (not yet merged or released)

- Clip slip editing: shift source in/out points of selected video/audio clips
  while keeping exact timeline boundaries, duration, track and clip properties;
  atomic selection edits, undo/redo, transition-handle validation and tests.
  This remains a v1.4 proposal until CI and desktop review pass.

## Proposals not yet shipped

- Timeline navigation enhancements and Zoom to Selection (PR #70).
- Preview-frame PNG snapshots (PR #27).
- Proposed bulk track operations, creator export presets, and additional
  media organization workflows remain subject to separate PR review.

An open pull request is **not** a released feature. See the
[roadmap](../roadmap.md) and [v1.3.0 verification report](v1.3.0-verification.md).
