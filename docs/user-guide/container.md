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

## Checking the image

```sh
docker run --rm ghcr.io/sanskarin/frametrellis-render:1.0.0 --version
docker run --rm ghcr.io/sanskarin/frametrellis-render:1.0.0 --self-test /tmp/self-test
```

The self-test generates sample media, builds and saves a small project, renders it and checks
the result. Every line should start with `PASS`. The release workflow runs the same self-test,
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
