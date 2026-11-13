# Windows renderer repetition / crash reproduction (development)

This is a **local manual diagnostic** for [native export crash #175](https://github.com/sanskarIN/frametrellis/issues/175). It does **not** diagnose the Qt editor process, does not prove the long-export issue is fixed, and is **not a release gate substitute** for real Windows editing/export testing. Do not run it on an unsaved project or important output directory.

The script is `scripts/windows/Test-FrameTrellisRepeatedExports.ps1` and uses Windows PowerShell 5.1+ and the **packaged** `frametrellis-render.exe`, not a third-party FFmpeg installation. The renderer is launched in an isolated child process on each iteration. Runs produce separate local stdout/stderr logs, optional MP4s, exit codes, hexadecimal Windows exception codes, and a JSON summary. The script does not upload data.

## Synthetic self-test repetitions

In an ordinary (not elevated) PowerShell console, from the source checkout:

```powershell
powershell -NoProfile -File .\scripts\windows\Test-FrameTrellisRepeatedExports.ps1 `
    -Renderer "C:\path\to\FrameTrellis\frametrellis-render.exe" `
    -OutputDirectory "C:\Temp\frametrellis-repro-2026-11-08" -Runs 5 -TimeoutSeconds 120
```

If your Windows or organization policy blocks local scripts, follow your normal trusted-script approval process rather than weakening system-wide execution settings.

When no `-Project` is supplied, each run uses the render helper's own
`--self-test` synthetic media and writes to a unique `run-001`, `run-002` etc.
The harness now requires a nonempty `self-test-export.mp4` inside **each**
run folder; a helper that exits 0 without producing an MP4 is reported as
`missing-output` (or `empty-output` for zero bytes), not as success.
This checks installation and repeated process lifecycles, not editor playback,
independent codec validation or a representative long video.

## Clean-host CI repeated exports (development evidence only)

The Windows package workflow now runs **three independent native helper
processes** on each clean-host verification runner (Windows Server 2022,
Windows Server 2025 and Windows 11 ARM x64 emulation), using the packaged
portable renderer and the disposable synthetic project produced by its own
self-test. Each process must create a nonempty MP4 at `30000/1001` FPS,
and any crash, timeout or missing/empty output fails the verification job.
The harness summary and outputs remain in the job's private CI artifacts.
This is short synthetic-media stress, **not** a physical Windows x64 long
export, user-media playback, native exception call stack or real audio test.

## Repeated actual project exports

Use **a copy** of a failing project, with locally available source media.
Choose a fresh output directory that contains no existing run folders:

```powershell
powershell -NoProfile -File .\scripts\windows\Test-FrameTrellisRepeatedExports.ps1 `
    -Renderer "C:\path\to\FrameTrellis\frametrellis-render.exe" `
    -Project "C:\path\to\test-copy.ftproj" `
    -OutputDirectory "D:\FrameTrellis-Test-Outputs" -Runs 3 -TimeoutSeconds 3600 -Fps "30000/1001"
```

For a valid project, each run produces a separate `export.mp4`. The script
never overwrites an existing `run-NNN` folder, and it does not delete partial
renders or generated outputs after failures. A per-run timeout terminates only
that run's child renderer. Large exports may require considerable disk space.
The output still needs manual image/audio quality and duration verification.

## Local Windows crash / storage-event correlation

The old v1.4.0 log recorded editor and renderer exits without a native stack.
In particular, `0xC0000006` is an in-page error and must be investigated
separately from the editor's `0xC0000005` access violation. A disk or storage
warning occurring near the same time would be **a lead, not proof of cause**.

The new local-only diagnostic collector reads limited Windows Application
Error / Windows Error Reporting event metadata and selected System disk,
NTFS, storage-controller and hardware warning IDs. It **does not** copy
raw Windows event messages, paths, unrelated user events or crash dumps
into the report, change registry keys, or upload anything. PowerShell
can run it without administrator privileges when the relevant event logs
are readable.

Use the actual failure time window on your own PC:

```powershell
powershell -NoProfile -File .\scripts\windows\Collect-FrameTrellisCrashEvents.ps1 `
    -Since "2026-11-07 09:35:00" -Until "2026-11-07 10:05:00"
```

The report remains in your local
`%LOCALAPPDATA%\FrameTrellis\Diagnostics` directory by default. Review
it before sharing; even event timestamps can be personally identifying.
A near-time event is correlation only; a WER minidump with private native
symbols/call stack, physical export reproduction and drive health checks
are still needed to identify the cause.

## Interpreting the evidence

- `success`: child process exit code 0; **not proof** of visual or audio correctness.
- `renderer-failed`: child exit code nonzero. The summary includes hexadecimal
  exception codes such as `0xC0000006`, which must be correlated with a native stack.
- `timeout`: test runner stopped the child after the configured timeout. This is
  not the same as a native access violation.
- `launch-failed`: the test runner could not start a process at all.

For the **editor crash** (`frametrellis.exe`, `0xC0000005`), repeat the
normal app-level export manually with the opt-in
`scripts/windows/FrameTrellis-CrashDumps.ps1` diagnostic separately. Follow
[#175](https://github.com/sanskarIN/frametrellis/issues/175) for
the required native crash-stack investigation. The two exception codes are
not interchangeable.

**Privacy:** JSON, stderr and logs can contain local paths, usernames and
media names. They stay on your computer, but review/redact them carefully
before posting any results. Do not upload proprietary footage, raw minidumps,
or personal paths to a public issue.

The proposed v1.5.0 stable release remains explicitly **on hold** pending the
owner's later authorization, resolution/disposition of native crashes and
hands-on Windows acceptance.
