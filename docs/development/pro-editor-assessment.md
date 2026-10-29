# Professional-editor development assessment

Assessment date: **29 October 2026**. The source baseline is main commit
`747d6a3a7f5714063fcaad9a061994f2ce528898`, after volume-keyframe PR #83.
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
| Picture | Brightness, contrast, saturation, scalar opacity, opacity keyframes, dissolves and transparent PNG titles | Transform keyframes, editable title layers, scopes and richer grading |
| Media management | Background probing, thumbnails, search, missing-media relink | Proxies, larger-project performance; audit/folder/cleanup PRs need integration |
| Review/interchange | Main has markers, chapter/WebVTT chapter copy and cuts-only picture EDL | Editable subtitles, SRT import/export, caption burn-in and fuller interchange |
| Delivery | MP4/H.264/AAC, safe output paths, isolated helper, cancellation, output verification | More codecs/containers and measured hardware-encoding options |
| Distribution | Windows x64 installer/portable builds; Linux from source | Signing, Linux packages and a supported macOS build |

Chapter WebVTT is chapter metadata, not a subtitle editor. Raster titles are not
editable text layers. Safe encoder presets are not GPU rendering. These distinctions
must remain clear in documentation and release claims.

## This implementation: opacity automation

The complete path is model → undoable command → project persistence → shared
preview/render graph → inspector → timeline envelope. Points use integer source
frames at the project rate and bounded, finite opacity values. Source alpha,
scalar opacity, fades and the curve multiply; volume remains independent.
Source anchoring makes trimming, splitting and rolling reversible without
resampling curves. Images use the same frame clock. Reset Picture clears both
scalar adjustments and opacity automation atomically, preserving audio and fades.
The user guide documents these semantics and the v4 format boundary.

Acceptance covers interpolation/endpoints, validation and limits, atomic failures,
locked tracks, drag merging, exact undo/redo, legacy loads, malformed documents,
trim/split/roll/slip/duplicate preservation, nonzero source-in rendering, transition
handles, zero transparency, semi-transparent PNGs, one-frame clips, scalar/fade
composition, encoded MP4 frames and real GUI interactions. Both envelopes use
bounded viewport painting. The GUI is inspected at 1200 × 800 as well as the
existing larger layout. Automated validation does not replace real-footage and
physical-desktop release acceptance.

## Next work, in order

1. **Integrated release acceptance.** Run the complete Linux, sanitizer and Windows
   checks on combined main, review real footage and audio, and reconcile PR #81's
   release checklist with project formats v3/v4 before publishing a new version.
2. **Transform keyframes.** Extend animation to position, scale and rotation with
   explicit coordinate semantics, bounded transforms, on-canvas controls and
   matched preview/export images. Define split/trim/slip behavior before coding.
3. **Subtitle workflow.** Start with a validated cue model, exact rational clock
   conversions and SRT import/edit/export; add burn-in using the same rendering
   path. Cover Unicode, overlaps, malformed input, undo and project migration.
4. **Proxy media.** Generate proxies asynchronously with cancellation and cache
   identity; switch only preview to proxies and always use originals for export.
   Cover relinking, stale proxies, unavailable originals and changed source files.
5. **Audio finishing.** Add peak meters and clipping indicators, then pan/channel
   controls and measured loudness/limiting. Keep metering independent of gain edits.
6. **Performance and integration debt.** Establish reproducible long-timeline
   measurements before optimizing. Integrate media PRs #65 → #66 → #67 separately;
   reconcile old overlapping PRs against main instead of assuming they shipped.
7. **Delivery and platform work.** Add independently tested export formats and
   hardware encoders, then packaging/signing work. Preserve software fallbacks.

## Release requirements

- Each feature must preserve save/reopen, preview/export agreement and undo/redo.
- Check the final combined source revision, including Linux debug/static analysis,
  ASan/UBSan, Windows Qt tests, packaging, container and documentation workflows.
- Record manual real-media checks separately: seek/edit responsiveness, audio
  listening, missing-media recovery, long exports and clean Windows startup.
- Keep backup copies when using development project formats v2/v3/v4. Do not overwrite
  stable release claims with branch-level feature lists.
- Do not publish a new stable version solely because a feature PR passes. Release
  rehearsal, package verification and the integrated acceptance matrix still apply.
