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

## v0.8.0 — Polish and robustness ✅
- ✅ Error handling and accessibility labels throughout the UI
- ✅ Windows installer and portable ZIP, verified automatically on clean Windows Server 2022,
  Windows Server 2025 and Windows 11 (ARM64, x64 emulation) machines
- ✅ Installation self-test (`frametrellis-render --self-test`)

## v0.9.0 — Release candidate ✅
- ✅ Dissolve and crossfade transitions between touching clips
- ✅ Audio waveforms on clips
- ✅ Release workflow with SHA-256 checksums, an SPDX software bill of materials and the source
  code of every bundled component
- ✅ License texts of every bundled component in the Windows packages

## v1.0.0 — First stable release ✅
Acceptance criteria, each checked before the release:
- ✅ the documented build succeeds in the release environment, CI is green on all supported
  platforms (Linux and Windows);
- ✅ import → preview → edit → save → reopen → export works end to end and is tested;
- ✅ no known crash or data-loss bug; the installer and the portable ZIP are installed, run and
  removed on clean Windows machines;
- ✅ packages contain the expected files, licenses and notices; the website's links are checked
  on every build;
- ✅ limitations and platform support are explicit in the [release notes](releases/v1.0.0.md).

## v1.1.0 — Clip duplication and title overlays ✅
- ✅ Undoable clip duplication with Ctrl+D, preserving trims and adjustments
- ✅ Basic raster title overlays as transparent PNG images, with project-local storage
- ✅ Linux and Windows CI, transparency compositing, GUI interaction and packaging tests
- ✅ Headless renderer container image with installation self-test

## v1.1.1 — Patch release ✅
- ✅ Prevent title creation when there is no unlocked video track
- ✅ Test transparent PNG compositing through an exported MP4

## v1.2.0 — Batch editing and clip attributes ✅
- ✅ Multi-clip duplication across audio and video tracks with preserved timing
- ✅ One-to-many copy/paste of clip volume, fades, mute and picture attributes
- ✅ Batch mute/unmute and picture-reset actions with atomic undo and locked-track safety
- ✅ Unit, GUI, Linux sanitizers and Windows CI verification
- ✅ Windows installer and portable ZIP built and verified through release workflows
- ✅ Atomic frame-accurate group nudging across tracks, with undo/redo and collision validation

## v1.2.1 — Startup and package-verification maintenance 🚧
- 🚧 Investigate an intermittent Windows 11 ARM64 x64-emulation portable GUI startup crash
- 🚧 Preserve regular cleanup on QML startup failure, record Qt stderr and worker-shutdown diagnostics
- 🚧 Check two independent portable launches on ARM64; treat either failure as a failed check
- ⏳ Complete investigations and automated validation before tagging or publishing

## v1.2.2 — Potential WinGet distribution ⏳
- ⏳ Evaluate a stable Windows package identifier, installer metadata and hash-verified manifests
- ⏳ Submit only after separately confirming the process and release readiness
- ⏳ No WinGet changes belong to the v1.2.1 maintenance branch

## Next milestones
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
