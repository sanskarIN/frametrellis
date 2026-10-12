# Supported formats

FrameTrellis reads media through [FFmpeg](https://ffmpeg.org/) (video and audio) and Qt (still
images), and exports MP4 files with H.264 video and AAC audio.

## Importing

Every file is decoded once in the background before it is added, so a file is only accepted if
FrameTrellis can actually read it. Files that cannot be decoded are reported and left out.

| Kind | File types offered by the import dialog | Notes |
|---|---|---|
| Video | `.mp4` `.m4v` `.mov` `.mkv` `.webm` `.avi` `.mts` `.m2ts` `.mpg` `.mpeg` `.ts` `.mxf` `.ogv` `.wmv` `.flv` `.3gp` | Any codec the bundled FFmpeg decodes, for example H.264, H.265/HEVC, VP8, VP9, AV1, MPEG-2, MPEG-4, ProRes and DNxHD. Clips with sound can be placed on video and audio tracks. |
| Audio | `.wav` `.mp3` `.m4a` `.aac` `.flac` `.ogg` `.opus` `.aiff` `.aif` `.wma` | Sources are converted to the project's sample rate and channel count. |
| Still image | `.png` `.jpg` `.jpeg` `.bmp` `.gif` `.tif` `.tiff` `.webp` `.tga` | Read with Qt's image plug-ins; TIFF, WebP and TGA need the Qt Image Formats module, which the Windows packages include. EXIF rotation is applied. Animated GIFs are treated as video. |

The automated tests check H.264/AAC MP4, WAV and PNG files on every change. Other formats depend
on the FFmpeg and Qt libraries in use: the Windows packages include FFmpeg and Qt built by
[MSYS2](https://www.msys2.org/) (see `BUNDLED_COMPONENTS.txt` in the package for the exact
versions). On Linux, the system's FFmpeg and Qt are used.

Footage whose frame rate differs from the project's is played at the project frame rate:
frames are repeated or skipped as needed, and the clip keeps its real-time duration.

## Exporting

| Setting | Value |
|---|---|
| Container | MP4 (`.mp4`), with the index at the start of the file for fast playback start (`+faststart`) |
| Video | H.264 (libx264 when available, otherwise another H.264 or MPEG-4 encoder FFmpeg provides), 8-bit 4:2:0 (`yuv420p`) |
| Audio | AAC at the project's sample rate (48 kHz by default) and channel count, 32–512 kbit/s, or no audio |
| Resolution | The project's size, or scaled so the shorter side is 720, 1080 or 2160 pixels |
| Frame rate | The project's frame rate |
| Quality | Constant quality (CRF 0–51) with encoder speed presets from *ultrafast* to *veryslow* |

Presets: *Match project*, *1080p*, *720p*, *2160p (4K)* and *Fast draft* (720p, quick, lower
quality). Other containers and codecs (WebM/VP9, ProRes, image sequences, audio only) are
planned; see the [roadmap](../roadmap.md).

## Project files

Projects are saved as `.ftproj` files: UTF-8 JSON in a versioned format that is documented in
[the project file format](../architecture/project-format.md). Project files reference media by
path and never contain the media itself.

## Patents

H.264, H.265 and AAC may be covered by patents in some countries. FrameTrellis is distributed
free of charge and does not include a patent license; see [Licensing](../LICENSING.md#patent-encumbered-codecs).
