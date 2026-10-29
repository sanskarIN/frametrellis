# Professional-editor development assessment

Assessment date: **29 October 2026**. The source baseline is main commit
`88ff8d3fee3631b71bca8f04c2efe8c7c0cad1a2`, after opacity-keyframe PR #84.
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
| Review/interchange | Main has markers, chapter/WebVTT chapter copy and cuts-only picture EDL | Editable subtitles, SRT import/export, caption burn-in and fuller interchange |
| Delivery | MP4/H.264/AAC, safe output paths, isolated helper, cancellation, output verification | More codecs/containers and measured hardware-encoding options |
| Distribution | Windows x64 installer/portable builds; Linux from source | Signing, Linux packages and a supported macOS build |

Chapter WebVTT is chapter metadata, not a subtitle editor. Raster titles are not
editable text layers. Safe encoder presets are not GPU rendering. These distinctions
must remain clear in documentation and release claims.

## This implementation: transform animation

The complete path is model → undoable command → format-v5 persistence → shared
preview/render graph → inspector → direct monitor handles. Position uses canvas
fractions from the centre, scale is uniform and aspect-preserving, and rotation
is clockwise about the picture centre. Values are finite and bounded. One point
is a fixed transform; additional source-anchored points interpolate all coordinates
linearly with held endpoints. Angles remain unwrapped to permit deliberate turns.
Trim/split/roll/slip/duplicate preserve curves. Reset Picture clears picture settings,
opacity and transforms atomically while retaining sound and fades. The user guide
specifies these semantics and version boundaries; older readers refuse v5.

Acceptance includes strict numeric/frame/capacity validation, atomic failures,
track locks, exact undo/redo, drag merging, legacy reads and malformed documents.
Generated footage exercises nonzero source-in, rotation direction, alpha holes,
scalar/animated opacity, aspect fit, smaller previews, one-frame cuts, transitions
and decoded MP4 output. Real GUI clicks and monitor move/scale/rotate drags cover
navigation, independent audio, reset, locks and the 1200×800 dark/light layouts.
The feature screenshots are retained by CI for review.

GUI stress also exposed stale preview delivery after a newer seek. Request
generations now invalidate both in-flight and queued pictures under the worker's
command lock. A regression leaves an old image queued, seeks or replaces the
project, then verifies only the current picture is delivered. It reproduces on
the original engine. Generated-media and GUI validation do not replace real-footage
listening or physical-desktop release acceptance.

## Next work, in order

1. **Integrated release acceptance.** Run the complete Linux, sanitizer and Windows
   checks on combined main, review real footage and audio, and reconcile PR #81's
   release checklist with project formats v3/v4/v5 before publishing a new version.
2. **Subtitle workflow.** Start with a validated cue model, exact rational clock
   conversions and SRT import/edit/export; add burn-in using the same rendering
   path. Cover Unicode, overlaps, malformed input, undo and project migration.
3. **Proxy media.** Generate proxies asynchronously with cancellation and cache
   identity; switch only preview to proxies and always use originals for export.
   Cover relinking, stale proxies, unavailable originals and changed source files.
4. **Audio finishing.** Add peak meters and clipping indicators, then pan/channel
   controls and measured loudness/limiting. Keep metering independent of gain edits.
5. **Performance and integration debt.** Establish reproducible long-timeline
   measurements before optimizing. Integrate media PRs #65 → #66 → #67 separately;
   reconcile old overlapping PRs against main instead of assuming they shipped.
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
- Keep backup copies when using development project formats v2/v3/v4/v5. Do not overwrite
  stable release claims with branch-level feature lists.
- Do not publish a new stable version solely because a feature PR passes. Release
  rehearsal, package verification and the integrated acceptance matrix still apply.
