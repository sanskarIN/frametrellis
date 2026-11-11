# Rendering and export

Exports run in a separate process, `frametrellis-render`, so that a crash in an encoder can never
take down the editor or lose unsaved work.

```
Export dialog ─► ExportController ─► RenderJob ──QProcess──► frametrellis-render
                                         ▲    (argument list,      │  loads the snapshot,
                                         │     no shell)           │  RenderRunner (MLT)
                                         └──── "@ft {json}" lines ◄┘  progress / result
```

## Development: sequential export presets

The development batch controller runs **one RenderJob at a time** using a copy
of the Project captured at batch initiation. It accepts two to five distinct
known preset identifiers, validates each output and refuses any existing
destination. The base filename is suffixed with the preset ID to avoid
collisions. FPS, project-frame range and audio-enable choice are shared;
resolution, CRF, encoder speed, upscaling and bitrate come from each preset.
Every item uses the existing helper, output probing and atomic replace protocol.
The next item starts on the Qt event loop **after** the prior job has emitted
its terminal signals; never delete a RenderJob from its own stateChanged
handler. Before each queued launch the output path is checked again. The helper also
uses an explicit refuse-existing-output mode at final publication: Windows
`MoveFileExW` without replace-existing, or a same-directory atomic hard-link
followed by removal of the temporary file on POSIX. If the destination is
created during rendering, the verified new file is *not* allowed to replace it.
On a filesystem that cannot create hard links, no-replace publication fails
safely rather than falling back to a non-atomic copy.

Failures and cancellations stop subsequent launches. Already completed outputs
are never deleted. The queue is an in-memory UI operation, not crash-persistent
background scheduling. The editor autosaves recent changes before capturing
the batch, while further edits do not enter already queued items. Aggregate
progress is (completed jobs + current-job progress) / total jobs, with an
independent terminal batch state. These changes are development-only pending
full integrated Windows testing; they do not resolve the native export crashes.

## RenderJob state machine

```
Idle ─► Starting ─► Running ─► Succeeded
            │          │  └──► Failed
            │          └─────► Cancelling ─► Cancelled / Succeeded / Failed
            └────► Failed / Cancelling
Succeeded, Failed, Cancelled ─► Starting (retry)
```

`RenderJob::canTransition` enforces these transitions and is unit-tested.

## What the runner does

1. Validates the settings again (empty timeline, offline media, output path rules).
2. Picks the first available encoder from `libx264`, `libopenh264`, `h264_mf`,
   `h264_videotoolbox`, `mpeg4`.
3. Computes the output canvas, respecting an optional no-upscale policy so a quick 1080p
   preset does not enlarge a smaller project. The timeline graph is always built at the
   **original project FPS**, preserving clip positions, source trims, keyframes, transitions,
   audio and subtitles.
4. When Export FPS differs, a second MLT profile and a `timeremap` link convert the
   graph by timestamp to the output FPS. The link duplicates or drops picture frames
   as needed and resamples audio frame boundaries without speeding up or slowing down
   the edit. Rational NTSC rates (e.g. 30000/1001) remain exact. Progress counts use
   the output FPS and rounded-up frame count; the project snapshot remains unchanged.
5. Encodes with MLT's `avformat` consumer to a hidden `.<name>.<token>.part.mp4` next to the
   destination (H.264 `yuv420p` with CRF and speed preset, AAC audio, `+faststart`).
6. Verifies the result by probing it: it must be a video with the **expected width,
   height, FPS and original timeline duration** before the existing destination is changed.
7. Atomically replaces the destination (`rename` / `MoveFileExW`). An existing file is never
   touched unless the export succeeded.

Cancellation: the job writes `cancel` to the helper's stdin (the helper also stops when stdin is
closed, for example because the editor crashed, and on SIGINT/SIGTERM). The helper stops the
consumer and deletes the partial file. If it does not exit within the grace period it is killed.
A cooperating helper exits with the dedicated cancellation code (3); the editor also records
its **own** watchdog-initiated kill as cancellation. A helper that instead reports a nonzero
failure or crashes during cancellation is **Failed**, not silently marked Cancelled: its exit
code and bounded stderr remain available for debugging. Tests exercise both paths without
using real media or intentionally generating a native crash dump.

The render helper supports `--fps match` (default), whole-number rates, decimals
such as `29.97` (normalised to 30000/1001) and exact fractions such as
`--fps 24000/1001`. Requests outside 1–240 fps fail validation. No output FPS
change can modify the saved project frame rate.

## Protocol

One JSON object per line on stdout, each prefixed with `@ft ` so that unrelated output from
libraries cannot be mistaken for an event:

```
@ft {"event":"started","encoder":"libx264","total":250}
@ft {"event":"progress","frame":120,"total":250}
@ft {"event":"finished","output":"/path/out.mp4"}
@ft {"event":"error","message":"…","detail":"…"}
@ft {"event":"cancelled"}
```

Exit codes: 0 success, 1 failure, 2 bad arguments, 3 cancelled.

## Read-only project inspection

The render helper can inspect a saved project without starting MLT, decoding media,
rendering an export or modifying the project:

```sh
frametrellis-render --inspect-project --project /path/to/edit.ftproj
```

It emits a single `@ft` JSON `project_inspection` event containing track,
clip and media availability counts, the sequence dimensions (`sequenceWidth`,
`sequenceHeight`), the exact rational `frameRate` (for example `30000/1001`),
`durationFrames`, and a formatted `durationTimecode`. The timecode uses the
same automatic drop-frame convention as the editor at 29.97/59.94 fps.
Selected fields from a 160x90, 25 fps, 40-frame sequence are shown below;
the actual event also includes the remaining project summary fields:

```text
@ft {"event":"project_inspection","sequenceWidth":160,"sequenceHeight":90,"frameRate":"25/1","durationFrames":40,"durationTimecode":"00:00:01:15"}
```

The event is additive and intended for local tools and preflight scripts. It
contains no source file paths or imported media names. It reports saved project
state, not whether a future export will succeed; offline/used media counts can
help identify missing sources before rendering.

## Known engine quirks handled here

* MLT's parallel frame rendering in the avformat consumer (`real_time < 0`) produced corrupt
  (NaN) audio with MLT 7.22 on Linux and MLT 7.40 on Windows, which made the AAC encoder fail.
  FrameTrellis therefore renders frames sequentially (`real_time = 0`); the video encoder still
  uses several threads. The v1.2.2 quick presets use FFmpeg/libx264's faster
  encoder modes, not parallel frame rendering.
* MSYS2's MLT lists the libraries of its "plus" module (FFTW, libebur128) as optional. Without
  them the module does not load and the `affine` transition is missing, so the Windows build
  installs and bundles them. MLT's `composite` transition is not used as a fallback because it
  draws black frames with current releases.
* MLT 7.42 with FFmpeg 6.1 registers `avfilter.eq` but cannot run it; the engine runs a one-frame
  self-test at startup and hides picture adjustments when it fails.
* MLT's FFmpeg producer returns blank frames for repeated seeks in still images, so stills use
  FrameTrellis' own Qt-based MLT producer.
