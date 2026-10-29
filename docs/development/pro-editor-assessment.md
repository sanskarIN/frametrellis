# Professional-editor development assessment

Assessment date: **28 October 2026**. The source baseline is main commit
`72f517309e6941d0a77f2b565009bca699285814`, after roll-edit PR #82.
The published stable version remains **v1.3.0**. This assessment separates
implemented source, this branch's candidate, and work that still needs building.

## Current state

FrameTrellis has a usable local-first desktop NLE foundation: a validated C++20
model, Qt Quick interface, shared MLT preview/render graphs, non-destructive
multi-track edits, command history, atomic persistence, autosave/recovery, and
isolated H.264/AAC exports. It does not yet offer the full workflow of a mature
professional editor.

| Area | Existing foundation | Material gap |
|---|---|---|
| Timeline editing | Move, edge trim, split, ripple delete, duplicate, batch attributes; slip and roll on main | Source-monitor In/Out workflow, insert/overwrite, linked clips and broader ripple tools |
| Audio | Waveforms, gain, mute, fades, crossfades; this branch adds volume envelopes | Live meters, channel/pan controls, loudness measurement, limiting and buses |
| Picture | Brightness, contrast, saturation, opacity, dissolves and transparent PNG titles | Transform/opacity keyframes, editable title layers, scopes and richer grading |
| Media management | Background probing, thumbnails, search, missing-media relink | Proxies, larger-project performance; audit/folder/cleanup PRs need integration |
| Review/interchange | Main has markers, chapter/WebVTT chapter copy and cuts-only picture EDL | Editable subtitles, SRT import/export, caption burn-in and fuller interchange |
| Delivery | MP4/H.264/AAC, safe output paths, isolated helper, cancellation, output verification | More codecs/containers and measured hardware-encoding options |
| Distribution | Windows x64 installer/portable builds; Linux from source | Signing, Linux packages and a supported macOS build |

Chapter WebVTT is chapter metadata, not a subtitle editor. Raster titles are not
editable text layers. Safe encoder presets are not GPU rendering. These distinctions
must remain clear in documentation and release claims.

## This implementation: volume automation

The complete path is model → undoable command → project persistence → shared
preview/render graph → inspector → timeline envelope. Points use integer source
frames at the project rate and finite decibel levels. Existing scalar attributes
remain independent. Source anchoring makes trimming and splitting reversible
without resampling curves; slip follows source audio. The user guide documents
these semantics and the v3 format boundary.

Acceptance covers interpolation/endpoints, validation and limits, atomic failures,
locked tracks, drag merging, exact undo/redo, legacy loads, malformed documents,
trim/split/slip/duplicate preservation, nonzero source-in rendering, transition
handles, combined gain/mute, encoded MP4 audio and GUI wiring/navigation. Automated
validation does not replace listening to real mixes or testing an actual Windows
editing session.

## Next work, in order

1. **Finish this candidate's verification and integration.** Review the inspector
   at narrow widths, listen to ramps/fades/crossfades, run the complete Linux,
   sanitizer and Windows checks, and validate final combined main before release.
2. **Opacity and transform keyframes.** Reuse the validated envelope approach with
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
- Keep backup copies when using development project formats v2/v3. Do not overwrite
  stable release claims with branch-level feature lists.
- Do not publish a new stable version solely because a feature PR passes. Release
  rehearsal, package verification and the integrated acceptance matrix still apply.
