# Unreleased (after FrameTrellis 1.3.0)

The latest published stable release is [FrameTrellis 1.3.0](v1.3.0.md).
The [v1.3.0 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.0)
provides verified Windows installer and portable ZIP downloads.
All changes already delivered in v1.3.0 belong in the
[1.3.0 release notes](v1.3.0.md), not here.

## In-progress proposals (not yet merged or released)

- Cut-only CMX3600 EDL (stacked follow-up): safe picture interchange for one visible
  video track, including explicit black gaps, stable reel IDs, exact timecodes,
  input validation and unit tests. Clipboard-only; proposed for v1.4.
- Chapter handoff (stacked follow-up PR): read-only clipboard export of named review cues
  into video-description chapter timestamps, including rational frame-rate handling.
- Video chapter copy (stacked PR #76): description-ready timestamps and WebVTT
  chapter tracks with frame-derived millisecond cue boundaries; both are read-only.
- Timeline markers: named, frame-accurate review cues, undoable editing and playback
  navigation, ruler indicators and snapping. This proposal uses project format v2 and
  requires Linux/Windows GUI validation before merge. **Not part of v1.3.0.**

No subsequent features are claimed as integrated by this file yet.
Newly merged changes after the v1.3.0 release tag should be recorded here
with their PRs, behavior, tests and any known limitations.

## In-progress development (not yet merged or released)

- Clip slip editing: shift source in/out points of selected video/audio clips
  while keeping exact timeline boundaries, duration, track and clip properties;
  atomic selection edits, undo/redo, transition-handle validation and tests.
  This remains a v1.4 proposal until CI and desktop review pass.

- Roll-cut editing (feature PR in progress): a selected outgoing clip can move its
  shared cut with the next touching video/audio clip by one or ten frames,
  preserving the combined timeline length and validating source handles, fades,
  track locks and transitions. Includes inspector controls, undo/redo and tests.
  This is not shipped in the stable v1.3.0 release.

## Proposals not yet shipped

- Timeline navigation enhancements and Zoom to Selection (PR #70).
- Preview-frame PNG snapshots (PR #27).
- Proposed bulk track operations, creator export presets, and additional
  media organization workflows remain subject to separate PR review.

An open pull request is **not** a released feature. See the
[roadmap](../roadmap.md) and [v1.3.0 verification report](v1.3.0-verification.md).
