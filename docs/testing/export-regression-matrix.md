# Export regression matrix

This document defines repeatable acceptance checks for FrameTrellis MP4 export. It is a test plan, not a claim that the cases have passed.

| Case | Setup | Expected result |
| --- | --- | --- |
| Short video | Single short video clip | Valid MP4 with expected picture duration |
| Audio retention | Video and separate audio track | Audible AAC output and expected duration; missing/wrong audio stream fails verification before publication |
| Video-only output | Disable audio in Export dialog | No audio stream in the resulting MP4; an unexpected stream fails verification before publication |
| Quick profile | Supported source and Quick preset | Valid H.264/AAC output |
| Turbo preview | Supported source and Turbo preset | Valid preview-size output |
| No-upscale | Small source and no-upscale enabled | Output does not exceed permitted source dimensions |
| Custom output width (v1.5 candidate) | Choose even custom width at nondefault rational output FPS | Exact requested width, even aspect-ratio-derived height, correct AAC/FPS/duration and unchanged project; invalid width rejects before publication |
| Custom width with no-upscale | Request a width larger than project with no-upscale enabled | Keep the source canvas dimensions; batch variants retain their own preset resolutions |
| Cancellation | Cancel a long render | Previous destination remains intact |
| Failed encoder | Simulate a render-helper failure | Failure is reported and destination is not replaced |
| Destination collision | Existing output file | Replace only after a fully verified render |
| Missing media | Project with offline source | Clear failure or documented placeholder behavior; no silent success |
| Non-ASCII paths | Unicode project and output paths | Output is created and playable |

## Execution protocol

1. Generate disposable fixtures; do not commit copyrighted sample media.
2. Run unit, integration and GUI suites on supported CI platforms.
3. Probe the resulting media streams and duration independently of the editor.
4. Verify cancellation and failure cleanup, including temporary files.
5. Record OS, compiler, Qt, MLT, FFmpeg versions, preset, fixture details and command output.
6. File reproducible issues for failures. Do not treat this matrix as proof of automated coverage until corresponding tests exist.

## Implemented regression case (v1.3 development)

`tests/integration/tst_render.cpp::unicodeExportPathProducesPlayableMedia` now exercises an MP4 export to a Unicode-named directory and filename, probes the video and audio streams, and verifies temporary-part cleanup. The case must still pass on CI before it can be considered verified; this does not cover all internationalized filesystem configurations.


## Opt-in Windows resource samples (v1.5 development)

The packaged-renderer harness can optionally record **peak working-set** and
**peak private-memory** bytes per repeated export. For example, in an ordinary
PowerShell terminal, invoke `scripts/windows/Test-FrameTrellisRepeatedExports.ps1`
with `-Renderer <installed-frametrellis-render.exe>`,
`-OutputDirectory <new-local-test-folder>`, `-Project <copy-of-project.ftproj>`,
`-Runs 3`, `-SampleResources` and `-SampleIntervalMs 500`.

This mode polls **only the child renderer started by the harness**. It does not
inspect the editor or other processes and does not change any existing media.
The local JSON summary adds `resourceSamples`, `peakWorkingSetBytes` and
`peakPrivateBytes` per run, with null memory values when no sample was
obtained. The flag is **off by default**; without it, the original timeout
and output-integrity behavior remains unchanged. Peak memory is a sampled
observation, not a precise allocation trace, and cannot establish the native
root cause of crashes #175/#185. Use Windows Event Viewer and private WER
call stacks where appropriate.

Output directories, input project paths, stdout/stderr and JSON summaries can
contain private filesystem details. Nothing is uploaded automatically. Review
and redact files before sharing; never publish crash dumps with private media.
The disposable Windows fixture checks both default-off and enabled sampling,
along with output-missing/empty/nonempty classifications.

## Future automation priorities

- Add generated-media integration tests for Unicode paths and destination replacement.
- Add fault-injection coverage for encoder failure and cancellation races.
- Track render speed on consistent hardware without making brittle absolute performance assertions.
