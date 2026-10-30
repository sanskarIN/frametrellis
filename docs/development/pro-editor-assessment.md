# Professional-editor development assessment

Assessment date: **29 October 2026**. The source baseline is main commit
`3696293e70532e2f50c0352e7963f6d69795bbca`, after transform-animation PR #85.
Its post-merge Linux, sanitizer, Windows, package-verification and website checks passed.
The published stable version remains **v1.3.0**. This assessment separates
implemented development source and work that still needs building.

## Current state

FrameTrellis has a usable local-first desktop NLE foundation: a validated C++20
model, Qt Quick interface, shared MLT preview/render graphs, non-destructive
multi-track edits, command history, atomic persistence, autosave/recovery, and
isolated H.264/AAC exports. It does not yet offer the full workflow of a mature
professional editor.

| Area | Existing foundation | Material gap |
|---|---|---|
| Timeline editing | Move, edge trim, split, ripple delete, duplicate, batch attributes; slip and roll on main | Source-monitor In/Out workflow, insert/overwrite, linked clips and broader ripple tools |
| Audio | Waveforms, gain, mute, fades, crossfades and source-anchored volume envelopes | Live meters, channel/pan controls, loudness measurement, limiting and buses |
| Picture | Brightness, contrast, saturation, scalar opacity, opacity/transform keyframes, direct monitor handles, dissolves and transparent PNG titles | Easing/custom pivots, editable title layers, scopes and richer grading |
| Media management | Background probing, thumbnails, search, missing-media relink | Proxies, larger-project performance; audit/folder/cleanup PRs need integration |
| Review/interchange | Markers, chapter/WebVTT chapter copy, cuts-only EDL; this change adds subtitle editing, SRT and burn-in | Multiple subtitle languages/tracks, more subtitle formats and fuller interchange |
| Delivery | MP4/H.264/AAC, safe output paths, isolated helper, cancellation, output verification | More codecs/containers and measured hardware-encoding options |
| Distribution | Windows x64 installer/portable builds; Linux from source | Signing, Linux packages and a supported macOS build |

Chapter WebVTT is chapter metadata, not a subtitle editor. Raster titles are not
editable text layers. Safe encoder presets are not GPU rendering. These distinctions
must remain clear in documentation and release claims.

## This implementation: subtitles

The complete path is validated cues → undoable commands → format-v6 persistence
→ SRT import/edit/export → shared preview/render producer → native cue editor
and timeline strip. Cues retain exact milliseconds; rational frame sampling
avoids cumulative rounding at fractional rates. Import appends atomically and
refuses overlaps. Plain Unicode text remains literal. All cues survive sidecar
export, even beyond the shortened video or between sampled frames.

The editor protects unapplied drafts, separates text undo from project undo,
provides navigation and an applied preview, and controls burn-in, size and dark
background. A canonical text layout is scaled for smaller previews. Captions
do not change audio, extend video or follow clip edits. A subtitle object in a
legacy format is refused; nondefault styling alone also requires v6.

Acceptance covers malformed UTF-8/SRT/JSON, capacity and timing boundaries,
atomic failures, exact undo/redo, legacy reads, generated preview pixels and
decoded helper MP4 frames. GUI tests type into real controls, exercise invalid
timings and draft guards, and capture 1200×800 dark/light layouts. Package and
container self-tests now verify encoded caption pixels, exercising the font
runtime as well as the encoder. Fonts remain installed system dependencies;
cross-platform language shaping needs manual acceptance on target machines.

## Next work, in order

1. **Integrated release acceptance.** Run the complete Linux, sanitizer and Windows
   checks on combined main, review real footage and audio, and reconcile PR #81's
   release checklist with project formats v3/v4/v5/v6 before publishing a new version.
2. **Proxy media.** Generate proxies asynchronously with cancellation and cache
   identity; switch only preview to proxies and always use originals for export.
   Cover relinking, stale proxies, unavailable originals and changed source files.
3. **Audio finishing.** Add peak meters and clipping indicators, then pan/channel
   controls and measured loudness/limiting. Keep metering independent of gain edits.
4. **Performance and integration debt.** Establish reproducible long-timeline
   measurements before optimizing. Integrate media PRs #65 → #66 → #67 separately;
   reconcile old overlapping PRs against main instead of assuming they shipped.
5. **Subtitle refinements.** Validate long cue lists and real language/font rendering,
   then define multiple language tracks, style presets and more import formats.
6. **Animation refinements.** Add easing/interpolation choices, numeric entry,
   crop/custom pivots and independent axes after defining project-format and
   preview/export semantics. Keep today's uniform linear transforms predictable.
7. **Delivery and platform work.** Add independently tested export formats and
   hardware encoders, then packaging/signing work. Preserve software fallbacks.

## Release requirements

- Each feature must preserve save/reopen, preview/export agreement and undo/redo.
- Check the final combined source revision, including Linux debug/static analysis,
  ASan/UBSan, Windows Qt tests, packaging, container and documentation workflows.
- Record manual real-media checks separately: seek/edit responsiveness, audio
  listening, missing-media recovery, long exports and clean Windows startup.
- Keep backup copies when using development project formats v2/v3/v4/v5/v6. Do not overwrite
  stable release claims with branch-level feature lists.
- Do not publish a new stable version solely because a feature PR passes. Release
  rehearsal, package verification and the integrated acceptance matrix still apply.
