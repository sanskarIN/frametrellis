# Third-party notices

FrameTrellis is licensed under GPL-3.0-or-later (see [`LICENSE`](LICENSE)). It is built on the
following projects, which are licensed separately by their authors. Their licenses are not
changed by FrameTrellis. See [`docs/LICENSING.md`](docs/LICENSING.md) for the compatibility
analysis.

Binary packages contain shared libraries from these projects. Each release also includes a
generated `BUNDLED_COMPONENTS.txt` with the exact libraries, versions, licenses and
source-package locations for that release.

## Qt

* Website: <https://www.qt.io/> · Source: <https://code.qt.io/> (mirror: <https://github.com/qt>)
* Copyright © The Qt Company Ltd. and other contributors.
* Modules used: Qt Core, Qt Gui, Qt Qml, Qt Quick, Qt Quick Controls, Qt Quick Dialogs, Qt SVG,
  Qt Concurrent (and Qt Test for the test suite).
* License: GNU Lesser General Public License v3 (`LGPL-3.0-only`), also available under
  `GPL-2.0-only`/`GPL-3.0-only`. FrameTrellis uses Qt under LGPL-3.0/GPL-3.0.
  Qt bundles third-party code (for example HarfBuzz, FreeType, libpng, PCRE2, zlib); see
  Qt's own "Licenses Used in Qt" documentation for the version you install.
* Reference version used in development: Qt 6.8.3. Minimum supported: Qt 6.5.

## MLT Multimedia Framework

* Website: <https://www.mltframework.org/> · Source: <https://github.com/mltframework/mlt>
* Copyright © Meltytech, LLC and contributors.
* License: `LGPL-2.1-or-later` for the framework and the `core`, `avformat` and `plus`
  modules; `GPL-2.0-or-later` for GPL modules such as `normalize` (whose `volume` filter
  FrameTrellis uses).
* Reference versions: 7.22.0 (Ubuntu 24.04 packages and local development), 7.40/7.42 (MSYS2 and
  newer systems). Minimum supported: 7.14.

## FFmpeg

* Website: <https://ffmpeg.org/> · Source: <https://ffmpeg.org/download.html>
  (mirror: <https://github.com/FFmpeg/FFmpeg>)
* Copyright © the FFmpeg developers.
* License: `LGPL-2.1-or-later` by default; `GPL-2.0-or-later` when configured with
  `--enable-gpl`; `GPL-3.0-or-later` with `--enable-gpl --enable-version3`. FrameTrellis uses
  FFmpeg through MLT. The Windows package bundles a GPL-3.0-or-later build (configured with
  `--enable-gpl --enable-version3`, without `--enable-nonfree`).
* FFmpeg builds include further libraries with their own licenses, for example x264
  (`GPL-2.0-or-later`), x265 (`GPL-2.0-or-later`), libvpx (`BSD-3-Clause`), libopus
  (`BSD-3-Clause`), dav1d (`BSD-2-Clause`), LAME (`LGPL-2.0-or-later`), and
  opencore-amr (`Apache-2.0`). The release manifest lists the ones actually bundled.

## SDL 2 (Simple DirectMedia Layer)

* Website: <https://www.libsdl.org/> · Source: <https://github.com/libsdl-org/SDL>
* Copyright © Sam Lantinga and contributors.
* License: `Zlib`.
* Used for audio playback when available at build time.

## Toolchain runtime (Windows packages)

* GCC runtime libraries (`libgcc`, `libstdc++`): `GPL-3.0-or-later WITH GCC-exception-3.1`.
* MinGW-w64 runtime and winpthreads: `ZPL-2.1`, `MIT` and `BSD`-style licenses.

## Fonts, icons and images

FrameTrellis does not bundle fonts; it uses the system's fonts. All icons, the logo and other
artwork in this repository were created for FrameTrellis and are licensed under
GPL-3.0-or-later. The website uses no third-party images, fonts or scripts.

## Web tooling

The browser edition and website have no third-party runtime dependencies. Development uses
TypeScript (`Apache-2.0`) to compile the browser edition and Playwright (`Apache-2.0`) to test it;
neither is shipped to users.
