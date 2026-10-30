# FrameTrellis professional-editor gap assessment — 30 October 2026

This is a development assessment, **not a release announcement**. The audited
source baseline is `main` at `df587712851d8061445e8f3f84c7dbe0634f53c7` following the merge of PR #103.
The published stable release is **v1.3.1**. Do not describe development
features as shipped in its installer.

## What is integrated on main

| Workflow | Verified implementation | Important limits |
|---|---|---|
| Timeline | Non-destructive cut, move, trim, split, roll/slip, group nudge, duplication, same-kind track reordering, whole-track duplication, selection tools and undo/redo | No persistent clip linking, source In/Out monitor, generalized multi-lane insert/overwrite |
| Gap edits | PR #103 inserts frame-accurate time across affected video/audio tracks as a single atomic command | Requires a cut or empty interval; no clip may cross the insertion frame; markers/subtitle cues do not move |
| Picture | Transform and opacity keyframes, scalar picture adjustments, fades, picture dissolves and PNG title assets | No editable text title layers, optical flow, color scopes or full color-grade pipeline |
| Audio | Waveforms, mute, gain, fades/crossfades and source-anchored automation | No real-time program peak meter, pan/mixer buses, LUFS measurement, limiting or surround workflow |
| Media | Background import/probing, filtered bin, folder discovery, missing-media relinking, media audit and cleanup | No preview proxies, cache lifecycle or source monitor |
| Review | Timeline markers, chapters, SRT import/edit/export and subtitle burn-in, cuts-only EDL and clip-list CSV | No multicamera sync, OTIO/FCPXML interchange, multiple subtitle tracks or review collaboration |
| Output | Isolated FFmpeg/MLT rendering, H.264/AAC creator presets, preview-frame PNG export and safe file writes | Wider container/codec matrix and hardware-encoder profiling are unverified |
| Platforms | Windows verification and release assets, development Linux builds | No verified signed release chain, official macOS build or cross-platform package matrix |

The GUI and renderer share the sequence model. Save/reopen and rendering are
core acceptance gates for any feature that adds persisted properties. A new
label in the UI is not evidence of preview/export parity.

## Development PRs awaiting acceptance

These proposals are **not merged in this assessment baseline**:

- [#104](https://github.com/sanskarIN/frametrellis/pull/104):
  remove a confirmed empty interval from all tracks, with exact undo and guards
  against clips inside the range; it builds on integrated #103.
- [#105](https://github.com/sanskarIN/frametrellis/pull/105):
  source excerpt In/Out frame selection from the media bin. The proposal
  appends on a compatible unlocked track; it does not yet implement a full
  source preview/player or automatic linked audio/video insertion.
- [#106](https://github.com/sanskarIN/frametrellis/pull/106):
  trim one selected clip edge to the playhead while refusing locked-track
  operations and silently destructive transition changes.
- [#107](https://github.com/sanskarIN/frametrellis/pull/107):
  whole-clip selection before or after the playhead without editing the project.

Every PR needs all required checks on its final head SHA; mergeable is not
synonymous with tested. Inspect the integrated `main` CI after successive
merges, especially changes sharing QML and TimelineController files.

## Highest-value next feature programs

### 1. Preview-only proxies and cache validation

Start with an explicit interface between source assets and optional preview
media. A proxy must be identified by a canonical source path, source file
metadata, project rate and transform/proxy settings, not only its filename.
Generate proxies on a worker with observable progress and cancellation.
Reject stale outputs when the source is replaced or relinked. Keep original
media mandatory for final export; a proxy-only project must not render an
apparently successful final master. Verify seeking, clip trims and audio
synchronization against both proxy and original at fractional frame rates.

### 2. Source-monitor and true three-point editing

Add an independently seekable source viewer with accurate In/Out marks and
source-frame/timecode controls. Define insert vs overwrite semantics across
video and audio lanes, locked tracks and linked streams. Avoid silently
replacing clips; edits spanning several tracks need one atomic undo command.
Selection, drag previews and persistence must agree on half-open frame ranges.
The proposed media-bin excerpt workflow in #105 is only a precursor.

### 3. Audio finishing and objective meters

Provide live per-channel peaks after the same gain/fade/keyframe graph used
for preview; include clip indicators and explicit dBFS reference. Test silence,
peak at 0 dBFS, muted tracks, long-running playback and exports with differing
sample rates. Next add channel routing/pan and measured LUFS with reportable
filter and decoder versions. Do not claim mastering or broadcast compliance
without measured acceptance.

### 4. Timeline scale and performance

Establish repeatable profiles for 100, 1,000 and 10,000 clips with edits,
waveforms, transforms, subtitles and long-GOP sources. Measure median/p95
scrub latency, first-frame preview delay, timeline model updates, memory and
cold/warm cache effects before optimization. Keep undo/redo exact and bound
work to changed tracks where safe. Test long timelines on Windows and Linux.

### 5. Delivery, interoperability and platform confidence

Expand export formats one at a time with real decode-and-compare tests,
audio sync inspection, metadata checks and a safe software fallback.
Implement interchange only against published schemas with correct
timebases, track/source references, transition semantics and round-trip
fixtures. Keep Windows x64/ARM and Linux verification separate from macOS
until macOS builds have actually run. Signing, installers and distribution
manifests are independent release gates.

## Release acceptance gates for v1.4.0

- Freeze feature scope and enumerate merged SHAs, project format versions,
  supported upgrades/downgrades and data-migration limitations.
- Run Linux warnings-as-errors, unit/GUI tests, ASan/UBSan, Windows MSYS2,
  clean Windows package verification, website checks, packaging scripts and
  renderer-container checks **on the final combined commit**.
- Test with actual imported camera video, variable-frame-rate media, music,
  silent sources, missing media and long timelines. Distinguish automated
  fixtures from manual listening/viewing.
- Confirm that preview and export yield the same frame ordering, transition
  timing, clip automation and subtitles. Check cancellation, output-path
  safety, temporary-file cleanup and recovery from a failed export.
- Validate project save/reopen and undo/redo across all newly integrated
  operations, including a mixed track sequence with locks and transitions.
- Confirm release artifacts, SHA256 checksums, SPDX/third-party notices,
  Windows installation/portable smoke tests and user-facing documentation.
- Publish a new stable tag **only after** combined CI and release acceptance.
  Development PR merges alone do not advance the published version.

## Notes on current semantics

Timeline gap editing uses exact integer project frames. Inserting a gap is not
a ripple split: it refuses any clip intersecting the insertion point. Markers
and subtitles are sequence-anchored and currently stay at their original
absolute positions; editors must reposition them intentionally. Frames used
in source excerpts are project-rate frames, not original timecode from a
variable-frame-rate recording. Always document these semantics rather than
promising broader professional workflows that have not been implemented.
