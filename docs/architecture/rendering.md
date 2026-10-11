# Rendering and export

Exports run in a separate process, `frametrellis-render`, so that a crash in an encoder can never
take down the editor or lose unsaved work.

```
Export dialog ─► ExportController ─► RenderJob ──QProcess──► frametrellis-render
                                         ▲    (argument list,      │  loads the snapshot,
                                         │     no shell)           │  RenderRunner (MLT)
                                         └──── "@ft {json}" lines ◄┘  progress / result
```

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
3. Builds the MLT graph at the export resolution with offline media treated as an error.
4. Encodes with MLT's `avformat` consumer to a hidden `.<name>.<token>.part.mp4` next to the
   destination (H.264 `yuv420p` with CRF and speed preset, AAC audio, `+faststart`).
5. Verifies the result by probing it: it must be a video of the expected duration.
6. Atomically replaces the destination (`rename` / `MoveFileExW`). An existing file is never
   touched unless the export succeeded.

Cancellation: the job writes `cancel` to the helper's stdin (the helper also stops when stdin is
closed, for example because the editor crashed, and on SIGINT/SIGTERM). The helper stops the
consumer and deletes the partial file. If it does not exit within the grace period it is killed.

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

## Known engine quirks handled here

* MLT's parallel frame rendering in the avformat consumer (`real_time < 0`) produced corrupt
  (NaN) audio with MLT 7.22 on Linux and MLT 7.40 on Windows, which made the AAC encoder fail.
  FrameTrellis therefore renders frames sequentially (`real_time = 0`); the video encoder still
  uses several threads.
* MSYS2's MLT lists the libraries of its "plus" module (FFTW, libebur128) as optional. Without
  them the module does not load and the `affine` transition is missing, so the Windows build
  installs and bundles them. MLT's `composite` transition is not used as a fallback because it
  draws black frames with current releases.
* MLT 7.42 with FFmpeg 6.1 registers `avfilter.eq` but cannot run it; the engine runs a one-frame
  self-test at startup and hides picture adjustments when it fails.
* MLT's FFmpeg producer returns blank frames for repeated seeks in still images, so stills use
  FrameTrellis' own Qt-based MLT producer.
