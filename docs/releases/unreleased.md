# Unreleased (after FrameTrellis 1.3.0)

The latest published stable release is [FrameTrellis 1.3.0](v1.3.0.md).
The [v1.3.0 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.0)
provides Windows installer and portable ZIP downloads. Development features below
are not included in those downloads. No v1.4 release is declared by these notes.

## Integrated on main for the next milestone

- [#75](https://github.com/sanskarIN/frametrellis/pull/75): persistent timeline markers,
  undoable edits, ruler indicators, navigation and snapping; project format v2.
- [#76](https://github.com/sanskarIN/frametrellis/pull/76): read-only video-description
  chapter lines and HTML5 WebVTT chapter tracks copied from timeline markers.
- [#77](https://github.com/sanskarIN/frametrellis/pull/77): cut-only CMX3600 picture EDL
  clipboard export, with black gaps, reel IDs and exact frame-rate validation.
- [#78](https://github.com/sanskarIN/frametrellis/pull/78): media-bin GridView delegate fix.
- [#79](https://github.com/sanskarIN/frametrellis/pull/79): atomic selected-clip source
  slip edits, inspector controls, undo/redo and transition-handle validation.
- [#80](https://github.com/sanskarIN/frametrellis/pull/80): separate prerelease tag names
  from base package versions in the guarded release workflow.
- [#82](https://github.com/sanskarIN/frametrellis/pull/82): roll adjacent video/audio
  cuts without changing their combined duration; preserve source handles, fades,
  locks and transitions, with inspector controls and undo/redo.
- [#83](https://github.com/sanskarIN/frametrellis/pull/83): source-anchored volume
  keyframes, inspector controls, shared preview/export automation and bounded
  timeline envelope painting. Volume points require project format v3.

## Volume automation

- Source-anchored volume keyframes from -60 to +12 dB, with linear interpolation
  in dB and held endpoints. Preview and MP4 export share the same envelope.
- Inspector add/remove/clear controls, a decibel slider, previous/next navigation,
  timeline envelope curves and keyframe diamonds. Slider drags form one undo step.
- Clip volume, fades and mute combine with automation. Trim/split/move/duplicate
  preserve the source envelope; slip reveals levels for the newly selected frames.
  Attribute paste leaves destination automation unchanged.
- Strict model and JSON validation, per-clip point limits, locked-track protection,
  undo/redo, generated-media tests and encoded-audio regression coverage.
- Projects containing points save as **format v3**, refused by older v2 readers.
  Projects without points continue to save as v2; loading v1/v2 remains supported.
  Back up projects before moving between development builds.

## Opacity automation

- Source-anchored opacity points from 0% to 100%, with linear interpolation and
  held endpoints. Video and still-image clips use the same project-frame clock.
- Multiply the curve with source alpha, scalar opacity and video fades; preserve
  PNG transparency and source audio. Dissolve handles retain the opacity filters.
- Inspector add/remove/clear and previous/next controls, percentage values and
  accent-colored square points. White volume diamonds remain independent. Both
  envelopes share viewport-bounded painting during zoom and horizontal scrolling.
- Reset Picture clears scalar picture adjustments and opacity points together,
  retaining volume points and fades. Attribute paste retains destination curves.
- Format v4 preserves opacity automation; volume-only projects remain v3 and
  projects without automation remain v2. Loading formats v1 through v4 is supported.
- Model, undo, legacy/malformed document, generated-frame, exported MP4 and GUI
  regressions cover zero opacity, still images, PNG alpha, one-frame clips,
  transitions, source edits, real controls and narrower layouts. See the
  [opacity acceptance matrix](../testing/opacity-automation.md).

These are development features after v1.3.0, not a new stable release or version bump.

## Separate open proposals

- Media audit, recursive folder import and unused-media cleanup: PRs #65, #66, #67,
  with their documented integration order and separate validation requirements.
- Timeline navigation and Zoom to Selection: PR #70.
- Preview-frame PNG snapshots and other bulk track/export workflows remain subject
  to their own PR review; open PRs are not released features.

See the [professional-editor assessment](../development/pro-editor-assessment.md),
[roadmap](../roadmap.md) and [v1.3.0 verification report](v1.3.0-verification.md).
