# Windows export crash triage

An export error and an editor crash are different failures. FrameTrellis runs the MLT/FFmpeg
encoder in `frametrellis-render.exe` so a helper crash should be reported in the Export
dialog **without closing the editor**. If the whole FrameTrellis window disappears, diagnose
the **editor process**, not just the output MP4.

## Preserve the edit first

1. On startup choose **Recover**, not **Discard backup**, if an autosave is offered.
2. Save a separate `.ftproj` copy (**File > Save As**) before retrying export.
3. Keep the source media and recovery files in place until the copy opens correctly.
4. Never treat a partially written `.part.mp4` as a finished export.

## Collect a useful, privacy-conscious report

1. In FrameTrellis open **Help > About > Open log folder** and inspect
   `frametrellis.log` around the export time. Attach a *redacted* excerpt if reporting a bug.
2. If the editor closes unexpectedly on Windows, open **Event Viewer > Windows Logs >
   Application**. Find the matching **Application Error** for `frametrellis.exe` (or
   `frametrellis-render.exe`), and record the faulting module and exception code.
   Alternatively, use **Reliability Monitor** (`perfmon /rel`) to find the matching
   application failure.
3. Record the FrameTrellis version, Windows version, source video/audio codecs, resolution,
   duration, chosen export preset, export FPS, and whether the crash occurs at a consistent
   frame or elapsed time. Do not upload private media or full crash dumps by default.
4. If the editor stays open but the helper fails, expand the Export dialog's error details.
   They now include the renderer exit code and the tail of its stderr when available.
5. Note whether the system ran out of memory or disk space. To isolate a failing source clip,
   first try an export of a **short duplicate project** using non-sensitive test media.

## Minimal regression procedure

- Use a disposable 25 fps project with a short picture and audio track.
- Export to MP4 at 30 fps, then **overwrite the same destination** at 24 fps and
  30000/1001 fps. The editor must remain open; the output must have the selected FPS.
- Repeat with the project's original frame rate and with a different export preset.
- Verify the result in a separate media player and check that no temporary `.part.mp4`
  survives successful runs.
- Cancel a long export: the editor should stay open and an existing destination should remain
  untouched.
- Run the packaged renderer's `--self-test` as described in the installation guide,
  and report whether it passes independently of the editor.

## How to interpret symptoms

| Symptom | Useful next evidence |
| --- | --- |
| Only the Export dialog shows a failure | Renderer exit code, error details, and log |
| `frametrellis-render.exe` stops but editor stays open | Helper exception code and source codec |
| Entire `frametrellis.exe` process disappears | Windows **Application Error** for the editor, memory usage, editor log |
| Export completes but MP4 cannot play | Output metadata, probing/verification error, preset and FPS |
| Failure only with altered output FPS | Exact source FPS, selected output FPS, FPS conversion test results |

A successful CI run or short sample export does **not** prove that every imported media format,
hardware configuration, or long-running Windows export is stable.
