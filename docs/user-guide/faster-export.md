# Faster MP4 exports (v1.2.2)

Use the **Quick export** or **Turbo preview** preset in File → Export to prioritize
turnaround over compression efficiency.

| Preset | Limit | Encoder speed | CRF |
|---|---|---|---|
| Quick export (project size) | Native canvas | veryfast | 22 |
| Quick export (up to 1080p) | Up to 1080p | veryfast | 22 |
| Fast draft (up to 720p) | Up to 720p | veryfast | 28 |
| Turbo preview (up to 720p) | Up to 720p | superfast | 26 |
| Low-bandwidth review (up to 480p) — v1.4 development | Up to 480p | veryfast | 27 |

A **Creator upload (up to 1440p)** preset (CRF 18, medium encoding, 256 kbit/s
AAC) is also available in v1.4 development alongside the 1080p and 4K creator
profiles. A 3840 × 2160 project becomes 2560 × 1440; a smaller 1080p project
stays native. The 480p review preset uses 96 kbit/s AAC and does not upscale
small projects; resulting file sizes still depend on the scene and encoder.

The established Match project, 1080p, 720p and 2160p modes remain available.
Output size, **export FPS**, encoder preset, video quality and audio bitrate can be customized.
Select **Export FPS → Match project** to preserve the timeline FPS or choose a common
rate such as 24, 25, 29.97, 30, 50, 59.94 or 60. Choose **Custom FPS…** to enter
an exact integer, decimal or rational (e.g. `24000/1001`, `48`). The editor
remains at the original project frame rate; only the exported MP4 is converted.
The converter adds/drops picture frames and preserves the timeline/audio duration,
within the rounding of the final output frame. Frame-rate conversion can take longer
to encode. Invalid or out-of-range values are refused before output begins.

## Custom export width (v1.5.0 development candidate)

Open **File → Export**, enable **Scaling → Custom width** and enter an even
number of pixels between 16 and 8192. A 1920×1080 project exported at 1280
pixels wide becomes 1280×720. A portrait 1080×1920 project exported at width
720 becomes 720×1280. Height is calculated from the project's own aspect
ratio, then rounded to an even pixel count for H.264 `yuv420p`.

This setting overrides the selected preset's short-side size for a **single
MP4 export**, without changing project dimensions or source media. **Do not
upscale** still keeps native dimensions when the requested width would enlarge
the project. Selecting a new preset or choosing **Reset preset** turns off
custom width. The entered value is retained across dialog reopenings in the
current session. Batch delivery always uses each variant's own preset sizing;
it intentionally ignores the single-export custom width.

The standalone render helper accepts `--width 1280` with `--project` and
`--output`. `--width 0` (default) uses the short-side preset. Odd, negative,
out-of-range or derived oversized dimensions are rejected *before* encoding.
This is a development feature, not in stable v1.4.0, and needs physical Windows
MP4 playback/audio QA before v1.5.0 can ship.

## Fine-tuning and reviewing export failures (v1.5.0 development)

In post-v1.4.0 development builds, reopening the Export dialog preserves
changes to its quality, encoder-speed, audio-bitrate, scaling and FPS controls
for the current editor session. Choosing another preset deliberately applies
its defaults. **Reset preset** reapplies the currently selected preset without
changing the output path or project timeline. Settings are not currently saved
between application restarts.

If the renderer fails, choose **Copy error details** in the Export dialog.
This copies the current failure message, helper diagnostics and output location
to the clipboard *locally*, without uploading any files. The report can contain
private paths or source filenames: review and redact them before posting.
This diagnostic action does not enable crash dumps or treat a renderer failure
as a successful export.

These conveniences are **not included in stable v1.4.0**.

## Export just part of the timeline (v1.5.0 development only)

Choose **File → Export → Timeline range** to render:

- **Entire timeline** — the existing default; every visible track is exported.
- **Selected clips' time span** — the earliest selected clip start through the
  latest selected clip's exclusive end, **including gaps and other tracks**
  during that interval. Make a clip selection before opening Export.
- **Custom frame interval** — enter a project-frame **Start** (inclusive)
  and **End** (exclusive). For example, start 250, end 500 exports 250
  frames at the **project's original timebase** before any requested output-FPS conversion.

The project, source trims, subtitles, video effects, keyframes, clip positions,
playhead and Undo/Redo history do not change. The helper constructs the full
sequence graph and encodes only the selected absolute frame interval, so audio
and visuals stay synchronised. Output FPS can still be Match project, a common
preset or a custom rational such as `24000/1001`. The selected interval is
converted using exact rational frame arithmetic, rounding its absolute
boundaries up; a range that would contain no output frames is rejected.

Frame interval values must be non-negative integers within the timeline and
the exclusive end must exceed the start. Do not confuse these values with
source-clip frame positions. The headless helper supports the equivalent
`--start-frame <frame>` and `--end-frame <exclusive-end>` options
(`--end-frame -1` means the entire remaining timeline).

The renderer still writes a temporary file, checks the output dimensions,
FPS and **selected segment duration**, and only then replaces the destination.
It never truncates or rearranges the saved project. These features remain
unreleased development work pending full native Windows and export safety QA.

## No-upscale setting

The **Do not upscale smaller projects (faster)** option is on by default in
performance presets. A 854 × 480 project exported with Quick 1080p stays at
854 × 480, not a magnified 1920 × 1080. If a project is larger than 1080p,
it is still scaled down to the target. Uncheck the toggle when a higher
delivery resolution is explicitly needed.

The policy applies to the *project canvas*, not each underlying media clip.
The headless render helper supports the equivalent command-line option
--no-upscale, used with --short-side.

## Check your performance

The export progress label displays average rendered frames/second after the
first second, along with elapsed and estimated remaining time. For an
apples-to-apples test, use the same project and output dimensions, compare
Match project (medium) against Quick project-size (veryfast), and record
elapsed time, frames/sec, final file size and playback quality. Benchmark
audio and titles as well. Results depend on CPU, effects, decoding, storage
and available encoders; no fixed speed gain is guaranteed.

## Reliability and limitations

MLT parallel frame rendering is *not* enabled: tested versions previously
produced invalid audio. Video encoder internal threading remains enabled.
The helper verifies both final frame dimensions and duration, cleans up after
cancellation, and replaces a destination only after a verified successful render.
Resolution requests must be 0 (native) or between 1 and 8192 pixels on the
shorter side. Oversized or negative requests are rejected before scaling.

Quick export is not stream copy or GPU acceleration. Output is still
MP4/H.264 with AAC (or no audio).
