# FrameTrellis professional-editor gap assessment — 30 October 2026

This is a **historical development assessment**, not a release announcement.
Its audited source baseline is `main` at
`eb4eb18a23482f92edfea332af9b768ad6fbed34`, following PRs #103 and #104.
The published stable release **at the time of this 30 October audit** was
v1.3.1. Its tables and PR status refer to that historical snapshot, not
necessarily the current repository.

## Status addendum — 4 November 2026

The current published stable release is
[**v1.4.0 (2 November 2026)**](https://github.com/sanskarIN/frametrellis/releases/tag/v1.4.0),
with a Windows installer, portable ZIP, SHA256 checksums, SPDX manifest and
third-party source notice archive. The source-excerpt In/Out workflow discussed
below is now part of v1.4.0, but this does **not** mean a standalone source
monitor, three-point insert/overwrite workflow or proxies have shipped.

Subsequent **development-only**, post-v1.4.0 improvements merged into main
include media duration/usage sorting
([#153](https://github.com/sanskarIN/frametrellis/pull/153)),
QML fade repaint reliability
([#154](https://github.com/sanskarIN/frametrellis/pull/154)),
Media Bin timeline-use navigation
([#155](https://github.com/sanskarIN/frametrellis/pull/155)) and source
technical properties
([#156](https://github.com/sanskarIN/frametrellis/pull/156)).
The source-excerpt picture preview
([#157](https://github.com/sanskarIN/frametrellis/pull/157)) merged into
main as **post-v1.4.0 development**. The independent, silent Source Monitor
([#159](https://github.com/sanskarIN/frametrellis/pull/159)), collision-safe
range placement at the Program playhead
([#162](https://github.com/sanskarIN/frametrellis/pull/162)) and explicit
destination-track selection
([#163](https://github.com/sanskarIN/frametrellis/pull/163)) subsequently
merged as further development milestones. Atomic two-track video/audio
placement is being developed independently of these single-clip workflows.
These improvements are **not** included in the v1.4.0 installers, and the
Source Monitor remains a silent viewer, not a source-audio transport player.
Full ripple insertion, destructive overwrite, persistent clip linking and
proxy media are still absent. Always distinguish a merged development feature
from one present in a tagged release.

The original audit follows unchanged as a record of the gaps and engineering
acceptance criteria at that time.

## What was integrated on main at the 30 October audit

| Workflow | Verified implementation | Important limits |
|---|---|---|
| Timeline | Non-destructive cut, move, trim, split, roll/slip, group nudge, duplication, same-kind track reordering, whole-track duplication, selection tools and undo/redo | No persistent clip linking, source In/Out monitor, generalized multi-lane insert/overwrite |
| Gap edits | PRs #103 and #104 insert or remove empty time across affected video/audio tracks as one atomic undoable operation | Insertion refuses straddling clips; removal requires a wholly empty interval. Markers/subtitles remain at their original sequence times |
| Picture | Transform and opacity keyframes, scalar picture adjustments, fades, picture dissolves and PNG title assets | No editable text title layers, optical flow, color scopes or full color-grade pipeline |
| Audio | Waveforms, mute, gain, fades/crossfades and source-anchored automation | No real-time program peak meter, pan/mixer buses, LUFS measurement, limiting or surround workflow |
| Media | Background import/probing, filtered bin, folder discovery, missing-media relinking, media audit and cleanup | No preview proxies, cache lifecycle or source monitor |
| Review | Timeline markers, chapters, SRT import/edit/export and subtitle burn-in, cuts-only EDL and clip-list CSV | No multicamera sync, OTIO/FCPXML interchange, multiple subtitle tracks or review collaboration |
| Output | Isolated FFmpeg/MLT rendering, H.264/AAC creator presets, preview-frame PNG export and safe file writes | Wider container/codec matrix and hardware-encoder profiling are unverified |
| Platforms | Windows verification and release assets, development Linux builds | No verified signed release chain, official macOS build or cross-platform package matrix |

The GUI and renderer share the sequence model. Save/reopen and rendering are
core acceptance gates for any feature that adds persisted properties. A new
label in the UI is not evidence of preview/export parity.

## Historical PRs awaiting acceptance on 30 October

These proposals were **not merged at the original assessment baseline**.
Consult their GitHub status for subsequent integration; do not assume the
following list describes the current open PR queue:

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

## Historical release acceptance gates for v1.4.0

v1.4.0 has since been published. The following engineering gates document the
review criteria of the original audit. Apply equivalent final-commit and
installer checks to future release candidates; do not infer that a PR merge
alone constitutes a release.

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
