# Rendering in a container

`frametrellis-render` is the renderer that the editor uses for exports. It also works on its
own, without a display, and is published as a container image in GitHub Packages:

```text
ghcr.io/sanskarin/frametrellis-render
```

Use it to render FrameTrellis projects on a server, in a CI pipeline or on any machine with
Docker or Podman (Linux x86-64), without installing FrameTrellis. Each FrameTrellis version has
an image tagged with that version (`1.0.0`); `latest` is the newest stable version.

## Rendering a project

Mount the folder that holds the project and its media at `/work`, and run the image as your
own user so that the video belongs to you:

```sh
docker run --rm --user "$(id -u):$(id -g)" -v "$PWD:/work" \
    ghcr.io/sanskarin/frametrellis-render:1.0.0 \
    --project /work/my-project.ftproj --output /work/my-video.mp4
```

FrameTrellis finds media relative to the project file, so keep the media in the mounted folder
or below it. The options are the export settings of the editor:

| Option | Meaning | Default |
|---|---|---|
| `--project <file>` | The project to render (`.ftproj`) | required |
| `--output <file>` | The MP4 file to write | required |
| `--short-side <pixels>` | Scale so that the shorter side has this many pixels; `0` keeps the project size | `0` |
| `--crf <value>` | Quality, from `0` (best) to `51` | `20` |
| `--preset <name>` | Encoder speed, from `ultrafast` to `veryslow` | `medium` |
| `--audio-bitrate <kbps>` | AAC bitrate | `192` |
| `--no-audio` | Write a video without sound | |

The output is H.264 video with AAC audio, the same as an export from the editor, and it is
checked before it is kept. If rendering fails or is cancelled, no partial file is left behind.

## Inspecting a project (unreleased development builds)

Development builds after v1.2.2 also support a **read-only**, headless inspection command:

```sh
frametrellis-render --inspect-project --project /work/my-project.ftproj
```

It emits one `@ft {…}` JSON protocol line named `project_inspection`. For example:

```text
@ft {"event":"project_inspection","name":"My Edit","trackCount":4,"videoTrackCount":2,"audioTrackCount":2,"clipCount":3,"mediaCount":4,"usedMediaCount":3,"unusedMediaCount":1,"missingMediaCount":1,"missingUsedMediaCount":0,"durationFrames":300}
```

The event's counts have precise meanings:

| Field | Meaning |
|---|---|
| `trackCount`, `videoTrackCount`, `audioTrackCount` | Total tracks and counts by track kind |
| `clipCount` | Total timeline clips across all tracks |
| `mediaCount` | Total media-bin entries |
| `usedMediaCount` | Distinct media entries referenced by at least one clip |
| `unusedMediaCount` | Media entries not referenced by any clip |
| `missingMediaCount` | Entries whose files are absent or are not regular files |
| `missingUsedMediaCount` | Missing entries currently referenced by timeline clips |
| `durationFrames` | Sequence length at the project's frame rate, in frames |
| `subtitleCueCount`, `subtitleBurnIn` | Number of subtitle cues and whether preview/video burn-in is enabled (format-v6 development builds) |

A missing *unused* file increases `missingMediaCount` but not `missingUsedMediaCount`.
The command inspects file metadata only. It does **not** start MLT, decode footage, render
video, write project files or include source-file paths in the successful summary. A structurally valid project returns exit code 0 **even if some media is missing**;
use `missingMediaCount` to determine whether relinking may be needed. A malformed or missing
project returns a nonzero exit status and a structured `error` event. Do not combine
`--inspect-project` with `--output`.

This is **not** a feature of the existing v1.2.2 release or v1.0.0 container tags. It will
become available in a future release after required checks and explicit publication.

## Input and output safety

Development builds supporting format v6 also render sequence subtitles when
`burnIn` is enabled. The image includes DejaVu/Noto Core fonts and Qt's offscreen
platform. Install additional fonts for scripts they do not cover. Use the same
fonts on editing and rendering machines to keep line wrapping consistent. Captions
do not extend the video; disabling burn-in produces video suitable for a separate
SRT sidecar. This is outside the stable image's existing feature set.

For regular exports, the render helper rejects output paths that would replace the opened
project document or any referenced source media, including canonical-path aliases. When
`--watch-stdin` is enabled, only a complete `cancel` line (LF or CRLF), or closure
of stdin, requests cancellation. Partial words and other commands are ignored.

## Checking the image

```sh
docker run --rm ghcr.io/sanskarin/frametrellis-render:1.0.0 --version
docker run --rm ghcr.io/sanskarin/frametrellis-render:1.0.0 --self-test /tmp/self-test
```

The self-test generates sample media, builds and saves a small project, renders it and checks
the result. Development self-tests also check that subtitle pixels survive encoding.
Every line should start with `PASS`. The release workflow runs the same self-test,
and renders the project it saves from a mounted folder, before an image is published.

## What is in the image

The image is based on Debian 13, with Qt, MLT and FFmpeg from Debian. Only the MLT modules the
renderer needs are loaded. `/usr/share/doc/frametrellis` contains the FrameTrellis license, the
third-party notices and the list of Debian packages with their versions; the license of each
Debian package is in `/usr/share/doc/<package>/copyright`, and its source code is available
from the Debian archive. See [Licensing](../LICENSING.md).

To build the image yourself from a FrameTrellis source tree:

```sh
docker build -f packaging/container/Dockerfile -t frametrellis-render .
```
