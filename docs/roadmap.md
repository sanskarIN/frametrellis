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

## v1.2.1 — Startup and package-verification maintenance ✅
- ✅ Preserve normal cleanup when QML window initialization fails; log startup and shutdown checkpoints
- ✅ Save Qt stderr and available crash logs during portable/installed Windows package verification
- ✅ Require two independent portable GUI launches on Windows 11 ARM64; reject failures and stale artifacts
- ✅ Harden DLL-module boundary checks and cover them with Windows regression tests
- ✅ Pass CI, sanitizer, website, container and Windows packaging checks on the development branch
- ℹ️ The earlier intermittent Qt6Core.dll ARM64 incident was not reproduced or root-caused; see
  [v1.2.1 known limitations](releases/v1.2.1.md). Do not claim a confirmed crash fix.

## v1.2.2 — Faster MP4 exports and distribution preparation ✅
- ✅ Add Quick project-size, Quick 1080p and Turbo preview H.264 profiles
- ✅ Avoid unnecessary video upscaling and expose the setting through GUI and CLI
- ✅ Display live average render frames/sec; expand export regression tests
- ✅ Reject invalid scale requests and verify rendered MP4 dimensions before output replacement
- ✅ Expanded regression coverage and passed branch-head quality checks
- ✅ Maintainer approved stable v1.2.2 publication on 2026-10-21
- ⏳ WinGet submission remains a separately verified distribution step
- ⏳ Evaluate WinGet package identifier, installer metadata and hash-verified manifests;
  do not claim a WinGet listing without confirmed upstream acceptance

## v1.3.0 — Editing safeguards, media search and diagnostics ✅
- ✅ Reject project saves or exports that overwrite referenced media or the opened project
- ✅ Detect same-size external media changes and reject duplicate canonical paths
- ✅ Make timecode, fade and clip-end validation resilient to signed overflow
- ✅ Protect against concurrent MLT profile initialization and invalid render commands
- ✅ Search and category-filter the media bin; navigate directly to a SMPTE timecode
- ✅ Inspect project track/media counts with a read-only render-helper command
- ✅ Upgrade documentation search, section navigation and code copy controls
- ✅ Passed integrated Linux/Windows CI and automated clean-machine Windows package checks
- ✅ Stable v1.3.0 published from the verified `e86cab7d7eef7c287716abedc6721fa1bd76e719` release commit
- ℹ️ [Automated release verification](releases/v1.3.0-verification.md) is recorded separately
  from the still-unrecorded manual desktop smoke-test checklist

## Next milestones
- ⏳ A Linux package (AppImage or Flatpak), published once it has been verified
- ⏳ A macOS build in CI, then a `.dmg` package once it has been verified
- ⏳ Further export acceleration beyond safe encoder speed presets (MLT parallel frame
  rendering is still disabled because it previously corrupted audio)
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
