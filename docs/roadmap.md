# Roadmap

FrameTrellis follows semantic versioning with `v`-prefixed tags (`v1.0.0`). A milestone is only
tagged when its features work and its quality gates pass; dates are not promised.

Legend: ✅ implemented and tested on the development branch · 🚧 in progress · ⏳ planned

No version has been tagged yet: the milestones below describe the order of work, and a tag will
be created only after the corresponding acceptance checks have passed in CI.

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

## v0.5.0 — Audio ✅ (waveforms 🚧)
- ✅ Audio tracks, clip volume and mute, track mute, audio fades
- 🚧 Audio waveforms on clips

## v0.6.0 — Export ✅
- ✅ MP4 (H.264 + AAC) export in a helper process with progress, cancel and verification
- ✅ Export presets, quality and speed settings

## v0.7.0 — Persistence and recovery ✅
- ✅ Versioned project format, atomic saves, undo/redo, autosave and crash recovery
- ✅ Missing-media detection and relinking, recent projects

## v0.8.0 — Polish and robustness 🚧
- ✅ Error handling and accessibility labels throughout the UI
- 🚧 Windows packages verified on physical Windows 11 machines
- ⏳ Performance work for very long timelines (virtualised clip views, thumbnail strips)
- ⏳ Proxy media for demanding footage

## v0.9.0 — Release candidate ⏳
- ⏳ Crossfade transitions, basic title overlays
- ⏳ Linux portable package; macOS build in CI
- ⏳ Public beta and bug fixing

## v1.0.0 — First stable release ⏳
Acceptance criteria (all must hold):
- the documented build succeeds in the release environment, CI is green on all supported platforms;
- import → preview → edit → save → reopen → export works end to end and is tested;
- no known crash or data-loss bug; fresh-install instructions verified;
- packages contain the expected files, licenses and notices; website and links checked;
- limitations and platform support are explicit in the release notes.

## Later
- Subtitles (import, edit, burn-in and export as SRT)
- Keyframes for opacity, volume and position
- Customisable keyboard shortcuts
- More export formats (WebM/VP9, ProRes, image sequences, audio-only)
- Plugin extension points (effects and exporters) with documentation
- Translations
