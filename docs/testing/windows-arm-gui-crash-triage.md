# Windows 11 ARM repeated GUI startup diagnostics

An x64 portable FrameTrellis process running under Windows 11 ARM emulation exited with
`0xC0000005` on the **second independent GUI launch** of a packaged-build smoke
test on 7 November 2026. The first launch and the later installed-app launch
completed normally. The logs ended after the Qt Quick main window was created;
they did not identify a faulting native module or stack trace.

- [Failing combined-main Windows package run](https://github.com/sanskarIN/frametrellis/actions/runs/42371917086)
- [ARM-specific tracking issue #185](https://github.com/sanskarIN/frametrellis/issues/185)
- [General Windows export crash issue #175](https://github.com/sanskarIN/frametrellis/issues/175)

The second startup crash **shares the access-violation exception code** with the
reported desktop-export crash but is not proven to have the same cause. The GUI
test does not export a long user project, and the ARM runner uses x64 emulation.

## Reproducibility and comparison

`packaging/windows/verify.ps1` accepts `-SoftwareBackendDiagnostics`.
With the normal x64 portable ZIP and installer, it first performs the existing
default-backend launches (two independent attempts on ARM), then launches the
same synthetic test project **twice more** with `QT_QUICK_BACKEND=software`.
Each run saves its own screenshot, stdout/stderr and log artifacts with separate
names. The setting is restored before installer verification. A previous
`QT_QUICK_BACKEND` or `QSG_RHI_BACKEND` inherited from a developer shell is
cleared at verification startup so the baseline is the actual default backend.

The Windows packages CI enables these extra diagnostic launches only on
`windows-11-arm`. All check results accumulate: **a successful software run
does not erase a failed default run or make the job green.**

For local investigation using a disposable copy of the project:

```powershell
./packaging/windows/verify.ps1 -Zip ./dist/FrameTrellis-1.4.0-windows-x64.zip `
  -Installer ./dist/FrameTrellis-1.4.0-windows-x64-setup.exe `
  -WorkDir "$env:TEMP/frametrellis-arm-verify" `
  -PortableGuiRuns 2 -SoftwareBackendDiagnostics
```

Use paths matching the actual downloaded build; these names are illustrative.
The script temporarily changes PATH/environment for verification, so run it
in a dedicated terminal. It does not change an installed application's
default rendering backend or enable Windows crash dumps.

## Interpretation

| Result | Next investigation |
| --- | --- |
| Default GUI fails, software GUI passes repeatedly | Investigate Qt Quick graphics API, GPU driver and x64-on-ARM interaction; not proof yet |
| Both default and software GUI fail | Investigate preview/media/SDL worker startup, native module lifetime and timing |
| Both pass | Treat the crash as intermittent; repeat with diagnostic evidence, not as a root-cause fix |
| App exits before the timed screenshot | Preserve the process exit code, application log and Windows event details |

For native-call-stack investigation, see [export crash triage](export-crash-triage.md)
and the optional, local Windows Error Reporting tooling. Avoid publicly sharing
unreviewed minidumps (they can contain private paths and program memory).
No software-backend fallback is enabled for end users based on a single result.
