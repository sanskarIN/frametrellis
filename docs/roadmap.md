# Roadmap

FrameTrellis follows semantic versioning with `v`-prefixed tags (`v1.0.0`). A milestone is only
tagged when its features work and its quality gates pass; dates are not promised.

Legend: ✅ implemented and tested · 🚧 in progress · ⏳ planned

The 0.x milestones below were development stages on the way to 1.0. They were not published as
separate releases.

## v0.1.0 — Application skeleton and workspace ✅
- ✅ CMake/C++20/Qt 6 project, presets, CI, formatting and static analysis
- ✅ Main workspace: menus, toolbar, media bin, monitor, inspector, timeline, status bar
- ✅ Original branding, dark and light themes, first-run guide

## v0.2.0 — Media import and preview ✅
- ✅ Background import with probing and real thumbnails; duplicate detection
- ✅ Real-time preview with audio, seeking and frame stepping

## v0.3.0 — Timeline model and clip operations ✅
- ✅ Tracks and clips with validated, non-destructive edits
- ✅ Select, move (also between tracks), delete, ripple delete

## v0.4.0 — Trimming, splitting, positioning ✅
- ✅ Edge trimming, split at playhead, snapping, zoom, edit-point navigation
- ✅ Drag and drop from the media bin

## v0.5.0 — Audio ✅
- ✅ Audio tracks, clip volume and mute, track mute, audio fades
- ✅ Audio waveforms on clips, computed in the background and cached

## v0.6.0 — Export ✅
- ✅ MP4 (H.264 + AAC) export in a helper process with progress, cancel and verification
- ✅ Export presets, quality and speed settings

## v0.7.0 — Persistence and recovery ✅
- ✅ Versioned project format, atomic saves, undo/redo, autosave and crash recovery
- ✅ Missing-media detection and relinking, recent projects

## v0.8.0 — Polish and robustness 🚧
- ✅ Error handling and accessibility labels throughout the UI
- 🚧 Windows installer and portable ZIP, verified automatically on clean Windows Server 2022,
  Windows Server 2025 and Windows 11 (ARM64, x64 emulation) machines
- ✅ Installation self-test (`frametrellis-render --self-test`)

## v0.9.0 — Release candidate 🚧
- ✅ Dissolve and crossfade transitions between touching clips
- ✅ Audio waveforms on clips
- 🚧 Release workflow with SHA-256 checksums, an SPDX software bill of materials and the source
  code of every bundled component

## v1.0.0 — First stable release
Acceptance criteria (all must hold):
- the documented build succeeds in the release environment, CI is green on all supported platforms;
- import → preview → edit → save → reopen → export works end to end and is tested;
- no known crash or data-loss bug; fresh-install instructions verified;
- packages contain the expected files, licenses and notices; website and links checked;
- limitations and platform support are explicit in the release notes.

## v1.1 — Next
- ⏳ Basic title overlays
- ⏳ A Linux package (AppImage or Flatpak), published once it has been verified
- ⏳ A macOS build in CI, then a `.dmg` package once it has been verified
- ⏳ Faster exports (MLT's parallel frame rendering is disabled because it corrupted audio)
- ⏳ Performance work for very long timelines (virtualised clip views, thumbnail strips)
- ⏳ Proxy media for demanding footage
- ⏳ Code-signed Windows packages
- ⏳ Evaluate WinGet, Flathub and Homebrew as additional distribution channels

## Later
- Subtitles (import, edit, burn-in and export as SRT)
- Keyframes for opacity, volume and position
- Customisable keyboard shortcuts
- More export formats (WebM/VP9, ProRes, image sequences, audio-only)
- Plugin extension points (effects and exporters) with documentation
- Translations
