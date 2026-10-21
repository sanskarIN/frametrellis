# Faster MP4 exports (v1.2.2)

Use the **Quick export** or **Turbo preview** preset in File → Export to prioritize
turnaround over compression efficiency.

| Preset | Limit | Encoder speed | CRF |
|---|---|---|---|
| Quick export (project size) | Native canvas | veryfast | 22 |
| Quick export (up to 1080p) | Up to 1080p | veryfast | 22 |
| Fast draft (up to 720p) | Up to 720p | veryfast | 28 |
| Turbo preview (up to 720p) | Up to 720p | superfast | 26 |

The established Match project, 1080p, 720p and 2160p modes remain available.
Output size, encoder preset, video quality and audio bitrate can be customized.

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
