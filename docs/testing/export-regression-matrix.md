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

## Future automation priorities

- Add generated-media integration tests for Unicode paths and destination replacement.
- Add fault-injection coverage for encoder failure and cancellation races.
- Track render speed on consistent hardware without making brittle absolute performance assertions.
