# Unreleased

First public development snapshot of FrameTrellis.

## Added
- Multi-track editing workspace in Qt Quick with media bin, program monitor, clip inspector and
  timeline; dark and light themes; first-run guide; keyboard shortcuts.
- Media import (video, audio, stills) with background probing, thumbnails and duplicate detection;
  missing-media detection, offline placeholders and relinking.
- Timeline editing: add, move (across tracks), trim, split, delete, ripple delete, snapping, zoom,
  track mute/hide/lock, unlimited-style undo/redo (500 steps).
- Clip volume, mute, fades, opacity, brightness, contrast and saturation.
- Audio waveforms on clips on audio tracks, computed once per file in the background and cached.
- Transitions: a dissolve (video) and crossfade (audio) centred on the cut between two touching
  clips, added with T or from the inspector, with an adjustable length; saved in the project
  file.
- Real-time preview with SDL2 audio.
- MP4 (H.264 + AAC) export in a separate process with presets, progress, cancellation and
  verification of the output.
- Versioned project format with strict validation, atomic saves, autosave and crash recovery.
- `frametrellis-render --self-test <folder>` checks an installation end to end (engine, sample
  media, project round trip, export and verification).
- Windows packaging: a portable ZIP and a per-user installer that bundle Qt, MLT and FFmpeg with
  their license files, a component list and an SPDX software bill of materials; both are verified
  automatically on clean Windows runners.
- Automated unit, integration and GUI tests; CI for Linux and Windows (MSYS2).

## Fixed
- A cancel request that arrived while a short export was finishing was ignored and the file was
  kept; the export is now discarded.

## Known limitations
- No titles or keyframes yet; dissolve/crossfade is the only transition.
- MP4 is the only export format.
- Windows packages are experimental; macOS is not supported yet.
