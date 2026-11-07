# Development history and unreleased changes

**Latest stable: [FrameTrellis v1.4.0](https://github.com/sanskarIN/frametrellis/releases/tag/v1.4.0),
published on 2 November 2026.** Its exact shipped features are listed in
[v1.4.0.md](v1.4.0.md). The integrated PR list below is a **historical
pre-release development log** accumulated after v1.3.1; it is not an
up-to-date list of still-unreleased features.

## Future work after v1.4.0

Priorities include source monitoring/three-point editing, verified proxy
generation, audio metering, long-timeline performance profiling, and
further cross-platform packaging. See the [professional-editor assessment](../development/pro-editor-assessment.md)
and [roadmap](../roadmap.md). No features are claimed as released until
their actual tag and packages are verified.

## Post-v1.4.0 development changes (not in the stable download)

- **Atomic ripple source insert:** the Source Monitor's **Ripple Insert**
  places a marked video/audio excerpt at the Program playhead on an exact
  or automatic unlocked destination track, shifting all later clips on
  affected tracks by its duration in one Undo/Redo operation. End-of-timeline
  placement works without later clips. Cuts through clips, locked affected
  tracks, incompatible/offline sources, overflow and invalidated transitions
  reject the entire edit. Existing clip IDs, trims, properties and transitions
  are retained. The sequence-aware addition in [#171](https://github.com/sanskarIN/frametrellis/pull/171)
  also shifts markers at/after the cut and eligible subtitle cues at rational
  frame-rate boundaries in the same undo step. Subtitles spanning the cut must
  be adjusted or split before insertion; marker and subtitle time overflow is
  rejected without a partial edit. This is not a linked V+A ripple or a
  destructive overwrite.

- **Match Source Monitor excerpt to selected clip length:** one click sets
  exclusive Out to the selected timeline clip's duration after the existing
  source In. It never moves In, touches the timeline or adds an undo step.
  Locked, incompatible or too-short sources disable the action. This helps
  prepare an equal-duration **Replace selected** edit without frame arithmetic.

- **Replace the selected clip's media without moving a cut:** Source Monitor
  **Replace selected** uses an equal-duration source excerpt for one unlocked
  timeline clip. It preserves clip ID, track, start/end and scalar adjustments,
  clears source-anchored automation, and refuses insufficient source range,
  incompatible media or transitions lacking safe handles. One Undo/Redo
  restores/reapplies the complete change without touching neighbouring clips.

- **Atomic synchronized source picture/audio placement:** for a source containing
  both video and audio, **Place V+A** adds identical source excerpts to distinct
  video/audio tracks at the Program playhead as one undoable edit. Honors a
  selected destination and chooses the other compatible free lane. A collision,
  lock, invalid range or missing lane rolls back the complete operation.
  The clips remain independently editable, not persistently linked; there is
  no *paired* V+A ripple insertion, overwrite or source audio playback.

- **Append a source excerpt to the chosen track:** Source Monitor **Append range**
  now honors its Target track picker; when automatic, it skips compatible
  tracks that have no timeline capacity. Explicit locked, missing, incompatible
  or full lanes are refused instead of silently changing the destination.
  The original video/audio source remains untouched, and one undo reverses
  the appended clip.

- **Choose an exact Source Monitor destination track:** the source placement
  picker lists compatible video and audio tracks, marks locked destinations,
  and preserves the chosen track across timeline edits. Users can retain
  automatic first-free placement or explicitly target one track. Explicit
  placement never falls back to another track when the chosen destination is
  locked, removed or occupied; an audio-bearing video source may also be
  placed on a compatible audio track without linking streams.

- **Source range placement at playhead:** use Source In/Out plus the Program
  playhead to place an excerpt without overwriting or rippling clips. The
  first compatible unlocked track with enough free space receives one
  undoable clip; occupied ranges, invalid source boundaries and locked tracks
  are rejected without changing existing edits. Video/audio are not linked
  automatically. This is development-only, not in v1.4.0 installers.

- **Independent Source Monitor:** a separate Source tab opens original video
  or audio from the Media Bin and keeps its preview position and exact
  In/Out marks separate from the Program playhead. Frame stepping, scrubbing
  and a deliberate undoable **Append range** action are available. The
  viewer is silent, not yet full three-point insert/overwrite editing.

- **Frame-accurate source excerpt preview:** scrub video before appending a
  non-destructive clip range. Preview frames are decoded asynchronously at the
  exact project rate, with Mark In / Mark Out controls. Audio-only excerpts
  retain numeric range editing; the Program monitor and export are unchanged.

- **Find timeline uses from Media Bin:** right-click a used source to select all
  matching timeline clips or jump the playhead to its earliest clip. This works
  across tracks without changing source files, project edits, or Undo history.

- **Media Properties inspector:** inspect a source's technical metadata, disk
  status, file path and timeline usage from the media-bin context menu. Copy a
  plain-text diagnostic report without modifying project or media files.

- **Media-bin sorting:** choose longest/shortest source duration or most-used
  media in addition to import/name order. Unknown durations and stills come
  last in duration modes; equal values retain import order. Source assets
  and project files are not changed by the sort.

- **Loop Preview:** the program monitor and Playback menu can repeat the
  entire sequence during playback. It restarts at frame zero with a correctly
  paced playback clock while retaining already-queued tail audio for continuity.
  Turning it off restores normal stop-at-end behavior. **Ctrl+Alt+L** toggles
  the mode in post-v1.4.0 development builds; this option neither changes
  project files nor affects MP4 exports. Regression tests cover the audio-free
  loop boundary, single-frame pacing and the real QML transport control.
  **Only available after v1.4.0**, pending integration into a future release.

## Historical integration log for v1.4.0

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
- [#95](https://github.com/sanskarIN/frametrellis/pull/95): asynchronous folder
  discovery keeps the GUI responsive; stale results are discarded on project switches.
- [#67](https://github.com/sanskarIN/frametrellis/pull/67): remove unused media references in
  one undoable change; disk files remain untouched.
- [#90](https://github.com/sanskarIN/frametrellis/pull/90): live Used, Unused and Changed
  media-bin filters, combined with name/path search and offline-media keyboard relinking.
- [#15](https://github.com/sanskarIN/frametrellis/pull/15): 1080p/4K creator-quality
  H.264/AAC upload profiles with no-upscale safeguards.
- [#91](https://github.com/sanskarIN/frametrellis/pull/91): read-only renderer
  `--list-presets` JSON discovery, including the creator upload profiles.
- [#92](https://github.com/sanskarIN/frametrellis/pull/92): protected PNG
  snapshots of the currently displayed preview frame with atomic writes.
- [#94](https://github.com/sanskarIN/frametrellis/pull/94): Zoom to Selection and
  Center on Playhead shortcuts with finite zoom guards, without changing timeline edits.

- [#96](https://github.com/sanskarIN/frametrellis/pull/96): read-only timeline clip
  list CSV export with SMPTE timecodes, source paths, UTF-8 encoding, formula-safe cells,
  protected atomic writes and unchanged undo/modified state.

- [#99](https://github.com/sanskarIN/frametrellis/pull/99): Lock All Tracks and
  Unlock All Tracks protect every lane in one undoable operation without changing
  mute, visibility, clip timing or source references.
- [#98](https://github.com/sanskarIN/frametrellis/pull/98): select clips beneath
  the playhead, every use of a source, or all clips on a selected track.
- [#100](https://github.com/sanskarIN/frametrellis/pull/100): undoable same-kind
  adjacent track reordering from track headers, preserving clip contents and media.

- [#103](https://github.com/sanskarIN/frametrellis/pull/103) and
  [#104](https://github.com/sanskarIN/frametrellis/pull/104): undoable multi-track
  insertion of empty time and removal of verified empty intervals, retaining
  source trims, automation and transition safety.
- [#105](https://github.com/sanskarIN/frametrellis/pull/105): append frame-accurate
  media excerpts from a chosen source range.
- [#106](https://github.com/sanskarIN/frametrellis/pull/106) and
  [#107](https://github.com/sanskarIN/frametrellis/pull/107): trim a single selected
  clip to the playhead, and select whole clips before or after the cursor.
- [#108](https://github.com/sanskarIN/frametrellis/pull/108): v1.4 development
  acceptance audit and additional manual release gates.
- [#109](https://github.com/sanskarIN/frametrellis/pull/109): formula-safe CSV
  media inventory export.
- [#110](https://github.com/sanskarIN/frametrellis/pull/110): align the earliest
  selected clip start to the playhead while keeping multi-track offsets.
- [#111](https://github.com/sanskarIN/frametrellis/pull/111): exact frame-count
  gap insertion/removal dialog and close-empty-gap-to-next-clip command.
- [#112](https://github.com/sanskarIN/frametrellis/pull/112): seek to the first or
  final visible frame of a selected clip range without modifying edit history.
- [#114](https://github.com/sanskarIN/frametrellis/pull/114): atomic selected
  video/audio clip fade presets and fade clearing, with per-clip duration clamping.
- [#115](https://github.com/sanskarIN/frametrellis/pull/115): undoable batch
  audio-gain presets, with range validation and locked-track safety.
- [#116](https://github.com/sanskarIN/frametrellis/pull/116): atomic video-opacity
  presets across selected video clips, preserving source-anchored automation.
- [#117](https://github.com/sanskarIN/frametrellis/pull/117): align a multi-track
  selection's latest exclusive end to the playhead with undo/redo.
- [#119](https://github.com/sanskarIN/frametrellis/pull/119): undoable multi-video
  brightness, contrast and saturation presets, with atomic validation and
  preservation of opacity, automation, fades and source trims.
- [#120](https://github.com/sanskarIN/frametrellis/pull/120): manual v1.4
  editing, preview/export, format-compatibility and clean-package acceptance
  checklist. The checklist does **not** assert that manual testing has passed.
- [#122](https://github.com/sanskarIN/frametrellis/pull/122): duplicate selected
  video/audio clips at the playhead, preserving inter-clip timing and track
  assignments with collision-safe atomic undo/redo.

- [#123](https://github.com/sanskarIN/frametrellis/pull/123):
  center selected multi-track clips around the playhead.
- [#125](https://github.com/sanskarIN/frametrellis/pull/125):
  select clips overlapping or contained within a time selection.
- [#126](https://github.com/sanskarIN/frametrellis/pull/126):
  navigate each selected clip's exact first/last visible frame.
- [#128](https://github.com/sanskarIN/frametrellis/pull/128):
  set precise custom numeric brightness, contrast and saturation adjustments.
- [#129](https://github.com/sanskarIN/frametrellis/pull/129):
  safe UTF-8 subtitle WebVTT sidecar export.
- [#130](https://github.com/sanskarIN/frametrellis/pull/130):
  shift all subtitle cues atomically for audio sync.
- [#131](https://github.com/sanskarIN/frametrellis/pull/131):
  protected timestamped Unicode TXT transcript export.
- [#132](https://github.com/sanskarIN/frametrellis/pull/132):
  seek to the earlier midpoint visible frame of a clip selection.
- [#133](https://github.com/sanskarIN/frametrellis/pull/133):
  1440p creator upload and 480p review MP4 presets.

The integrated candidate subsequently shipped as stable v1.4.0.
Its [publication record](v1.4.0-preflight.md) links successful exact-commit
CI, Windows packaging and automatic asset attachment. This historical
log does not by itself establish inclusion of every PR: use the release
notes and verified tag for that.

- [#136](https://github.com/sanskarIN/frametrellis/pull/136):
  exact five/ten-second playback jumps and whole-sequence midpoint with
  real shortcut integration, frame-rate-aware clamping, and read-only seeks.
- [#135](https://github.com/sanskarIN/frametrellis/pull/135):
  refuse whitespace-only media paths, invalid project/track names and
  cross-kind timeline ID collisions; keep prior project bytes unchanged
  when invalid in-memory state is rejected on save.
- [#137](https://github.com/sanskarIN/frametrellis/pull/137):
  copy SMPTE selection start, exclusive end, last visible frame, duration
  and rational frame rate to the OS clipboard without editing.
- [#139](https://github.com/sanskarIN/frametrellis/pull/139):
  consistently bound media paths to 4,096 characters and display names to
  512 characters in project edits, validation and loading, with round-trip
  coverage preventing files the project reader cannot reopen.
- [#140](https://github.com/sanskarIN/frametrellis/pull/140):
  omit oversized optional relocation hints when serializing media paths,
  preserving absolute source references and saved-project reopenability.

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
  and [user workflow](../user-guide/README.md#subtitles).
- Freeze the published playhead before acknowledging pause, preventing a rendered-ahead
  frame from moving a stopped playhead during precise cue timing. A dedicated
  in-flight-frame regression accompanies the existing pause/seek tests.

## Separate open proposals

- Additional bulk track operations and export workflows remain under review;
  open PRs are not released features.

See the [professional-editor assessment](../development/pro-editor-assessment.md),
[roadmap](../roadmap.md) and [v1.3.0 verification report](v1.3.0-verification.md).
