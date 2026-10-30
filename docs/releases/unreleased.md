# Unreleased (after FrameTrellis 1.3.1)

The latest published stable release is [FrameTrellis 1.3.1](v1.3.1.md).
The [v1.3.1 GitHub release](https://github.com/sanskarIN/frametrellis/releases/tag/v1.3.1)
provides verified Windows installer and portable ZIP downloads. It is a narrow
maintenance patch based on v1.3.0 and does not include the development features
below. No v1.4 release is declared by these notes.

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

- [#84](https://github.com/sanskarIN/frametrellis/pull/84): source-anchored opacity
  curves with PNG alpha/fade composition, inspector controls and preview/MP4 parity;
  project format v4.
- [#85](https://github.com/sanskarIN/frametrellis/pull/85): transform animation,
  direct monitor handles, format v5 and generation-checked preview delivery.

- [#65](https://github.com/sanskarIN/frametrellis/pull/65): read-only, local JSON media
  audit with online/missing/changed status, usage counts and protected atomic output.
- [#66](https://github.com/sanskarIN/frametrellis/pull/66): bounded recursive media-folder
  import with symlink avoidance, duplicate filtering and background decoder checks.
- [#67](https://github.com/sanskarIN/frametrellis/pull/67): remove unused media references in
  one undoable change; disk files remain untouched.
- Media-bin usage and changed-source filters complement the integrated audit and cleanup
  workflows; all category filtering is read-only and responds to timeline edits.

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
- Volume-only projects save as **format v3**, refused by older v2 readers.
  Projects without automation or subtitles save as v2; the reader supports v1–v6.
  Back up projects before moving between development builds.

## Opacity automation

- Source-anchored opacity points from 0% to 100%, with linear interpolation and
  held endpoints. Video and still-image clips use the same project-frame clock.
- Multiply the curve with source alpha, scalar opacity and video fades; preserve
  PNG transparency and source audio. Dissolve handles retain the opacity filters.
- Inspector add/remove/clear and previous/next controls, percentage values and
  accent-colored square points. White volume diamonds remain independent. Both
  envelopes share viewport-bounded painting during zoom and horizontal scrolling.
- Reset Picture clears scalar picture adjustments, opacity points and transform points together,
  retaining volume points and fades. Attribute paste retains destination curves.
- Opacity points require format v4; transform points raise the format to v5.
  Volume-only projects remain v3 and projects without automation or subtitles remain v2.
  Loading formats v1 through v6 is supported; subtitles require v6.
- Model, undo, legacy/malformed document, generated-frame, exported MP4 and GUI
  regressions cover zero opacity, still images, PNG alpha, one-frame clips,
  transitions, source edits, real controls and narrower layouts. See the
  [opacity acceptance matrix](../testing/opacity-automation.md).

## Transform animation and reliable preview delivery

- Position, uniform scale and clockwise rotation keyframes for video and images,
  using bounded canvas-relative coordinates, linear interpolation, unwrapped angles
  and held endpoints. One point applies a fixed transform throughout a clip.
- Inspector values and add/remove/reset/navigation controls; direct program-monitor
  move, scale and rotation handles, with each mouse drag forming one undo step.
  The timeline identifies transformed clips with a point-count badge.
- Source anchoring survives trim, split, roll, slip and duplication. Reset Picture
  clears scalar picture settings and both picture curves while preserving sound/fades.
  Attribute paste retains destination curves. Locks and atomic failures are covered.
- Format v5 is written only for transform points. Projects without them retain the
  minimum required v4/v3/v2 format; older readers refuse unsupported versions.
- Shared graph preview/export tests cover rotation direction, transparent pixels,
  combined opacity, aspect preservation, smaller previews, one-frame clips,
  dissolve handles and decoded MP4 frames. GUI tests exercise real mouse handles
  and dark/light narrow layouts. See the [transform acceptance matrix](../testing/transform-animation.md).
- Prevent an old in-flight or queued preview frame from undoing a newer seek or
  showing a superseded project snapshot. A targeted regression reproduces on the
  prior engine and passes with generation-checked publication/delivery.

These are development features after v1.3.0, not a new stable release or version bump.

## Subtitle editing and SRT delivery

- One sequence subtitle track with exact millisecond times, plain Unicode text,
  undoable cue editing, add-at-playhead, navigation and a viewport-filtered timeline strip.
- UTF-8 SRT import appends all cues in one undo step. Overlaps, malformed times,
  invalid text and capacity violations refuse the entire change. Atomic SRT export
  retains every cue independently of project history and the video's duration.
- A dedicated editor protects unapplied text drafts and includes an applied preview,
  project Undo/Redo, text size and background controls. Burn-in can be disabled
  for sidecar-only delivery. MP4 export does not create a switchable subtitle stream.
- A native caption producer shares exact frame sampling and canonical text layout
  between preview and export. Captions do not modify sound or extend the video;
  moving/slipping/rippling clips leaves their sequence times unchanged.
- Subtitle content or nondefault styling requires project format **v6**. Earlier
  project formats remain readable; older builds explicitly refuse v6.
- Headless rendering includes the Qt font platform and Linux fonts; Windows uses
  installed system fonts. Package/container self-tests inspect caption pixels in
  the encoded MP4. See the [subtitle acceptance matrix](../testing/subtitle-workflow.md)
  and [user workflow](../user-guide/README.md#subtitles-development-preview).
- Freeze the published playhead before acknowledging pause, preventing a rendered-ahead
  frame from moving a stopped playhead during precise cue timing. A dedicated
  in-flight-frame regression accompanies the existing pause/seek tests.

## Separate open proposals

- Timeline navigation and Zoom to Selection: PR #70.
- Preview-frame PNG snapshots and other bulk track/export workflows remain subject
  to their own PR review; open PRs are not released features.

See the [professional-editor assessment](../development/pro-editor-assessment.md),
[roadmap](../roadmap.md) and [v1.3.0 verification report](v1.3.0-verification.md).
