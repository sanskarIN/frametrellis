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
- Real-time preview with SDL2 audio.
- MP4 (H.264 + AAC) export in a separate process with presets, progress, cancellation and
  verification of the output.
- Versioned project format with strict validation, atomic saves, autosave and crash recovery.
- Automated unit, integration and GUI tests; CI for Linux and Windows (MSYS2).

## Known limitations
- No transitions, titles, keyframes or audio waveforms yet.
- MP4 is the only export format.
- Windows packages are experimental; macOS is not supported yet.
