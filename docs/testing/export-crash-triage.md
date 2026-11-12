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
3. Record the FrameTrellis version **and the build revision displayed in Help → About**
   (also in **Copy diagnostics**), Windows version, source video/audio codecs, resolution,
   duration, chosen export preset, export FPS, and whether the crash occurs at a consistent
   frame or elapsed time. Do not upload private media or full crash dumps by default.
4. If the editor stays open but the helper fails, expand the Export dialog's error details.
   They now include the renderer exit code and the tail of its stderr when available.
5. Note whether the system ran out of memory or disk space. To isolate a failing source clip,
   first try an export of a **short duplicate project** using non-sensitive test media.

## Partial MP4 files after an interrupted export

A render job now gives its helper a unique, randomly generated temporary-output token.
If **only the renderer** stops or is cancelled while the editor remains running, the
editor can remove that job's specific `.part.mp4` after the child process exits. It must
not remove similarly named files belonging to other exports or other editor instances.

If **the whole editor process also crashes**, it cannot run cleanup code. A stale hidden
`.part.mp4` may still exist in the destination folder and is **not** a successfully
verified output. The program must not automatically delete an unfamiliar partial MP4
when it launches; first preserve the project backup and verify file ownership before
manually cleaning up abandoned files.

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

## Repeatable local Windows renderer check

Use a **saved copy** of a test project, an already installed/portable
`frametrellis-render.exe`, and a **new, empty** output directory:

```powershell
.\scripts\windows\Test-FrameTrellisRepeatedExports.ps1 `
    -Renderer "C:\FrameTrellis\frametrellis-render.exe" `
    -Project "C:\QA\disposable-copy.ftproj" `
    -OutputDirectory "C:\QA\ft-repro-001" -Fps "30000/1001" -Runs 3
```

The script runs each export in an independent process with a timeout and
retains local stdout/stderr, output files, exit-code hex values and a JSON
summary for fault correlation. In **both** real-project and synthetic
`--self-test` modes, a zero exit code is **not enough**: a missing or
zero-byte MP4 now fails with `missing-output` or `empty-output`.
The synthetic mode requires its actual `self-test-export.mp4` while project
mode requires `export.mp4`; `outputBytes` records that run's file size.

A harness `success` means only that the renderer exited cleanly and a
nonempty file was present in the expected path. It **does not verify** that the
MP4 plays, contains AAC, or has the expected duration, FPS or picture; probe
it independently and listen to the audio. This harness does not run or
diagnose the GUI, so it cannot rule out the separate editor crash. Do not
publish unredacted local summaries, media or crash dumps.

## How to interpret symptoms

| Symptom | Useful next evidence |
| --- | --- |
| Only the Export dialog shows a failure | Renderer exit code, error details, and log |
| `frametrellis-render.exe` stops but editor stays open | Helper exception code and source codec |
| Entire `frametrellis.exe` process disappears | Windows **Application Error** for the editor, memory usage, editor log |
| Export completes but MP4 cannot play | Output metadata, probing/verification error, preset and FPS |
| Failure only with altered output FPS | Exact source FPS, selected output FPS, FPS conversion test results |

For sequential batch delivery, a **real helper failure after a cancellation request**
must remain a failed batch, with the error code and local diagnostic report
available to the user. A cooperative cancellation stops the batch and starts
no more variants. A failed item must not be silently marked "cancelled" merely
because the user also clicked Cancel. The regression uses a disposable
helper fixture that exits with code 42 after reading the cancellation request;
it does not simulate or resolve a native crash.

A successful CI run or short sample export does **not** prove that every imported media format,
hardware configuration, or long-running Windows export is stable.

## Optional Windows minidumps (for native crash root-cause work)

The Qt application log cannot capture a native stack trace once the process terminates with
an access violation. Windows Error Reporting can capture a local **mini**dump when either
`frametrellis.exe` or `frametrellis-render.exe` crashes. This is opt-in, and the bundled script
**never** starts, uploads, deletes, or reads dump files.

Use an elevated PowerShell session from the repository root only if you are comfortable with
local crash diagnostics:

```powershell
.\scripts\windows\FrameTrellis-CrashDumps.ps1 -Enable
.\scripts\windows\FrameTrellis-CrashDumps.ps1 -Status
# Reproduce the crash using a separate saved test project; then turn it off:
.\scripts\windows\FrameTrellis-CrashDumps.ps1 -Disable
```

The script configures only the two executable-specific keys below
`HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps`, not global crash
collection. It sets `DumpType=1` (minidump), `DumpCount=3`, and the default destination
`%LOCALAPPDATA%\FrameTrellis\CrashDumps` (expanded for the current account).
Administrator rights are required. The script refuses to overwrite pre-existing per-executable
settings not created by this helper, and `-Disable` removes only settings it manages.
A small number of older files may still persist: delete them separately when no longer needed.
It does not override Windows crash-reporting policies or automatically enable dumps if a
system debugger or policy takes precedence.

Minidumps can contain user identifiers, file paths, application memory, and media-related
metadata. **Do not upload a dump publicly or attach it directly to a GitHub issue.** Consult a
trusted debugger, collect a minimal/redacted native stack trace, and share only relevant module
names, exception codes and call frames. For the 2026-11-07 report:
- `frametrellis.exe`: `0xC0000005` (memory access violation)
- `frametrellis-render.exe`: `0xC0000006` (in-page error)

These are distinct faults, even if Windows lists `ntdll.dll` for both. Check disk and
paging-related event records for the in-page error, but do **not** assume a defective drive
without evidence. Continue using a copy of the project and never risk its only recovery file.

Reference: [Microsoft — Collecting User-Mode Dumps](https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps).
