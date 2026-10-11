# Licensing

FrameTrellis is licensed under the **GNU General Public License, version 3 or (at your
option) any later version** (SPDX: `GPL-3.0-or-later`). The complete license text is in
[`LICENSE`](../LICENSE). Every source file carries an `SPDX-License-Identifier` header.

This document records how the license was chosen and what it means for people who build and
redistribute FrameTrellis. It is a careful engineering review, **not legal advice**; if you plan
to distribute FrameTrellis commercially or in large volumes, have the points under
[Open questions](#open-questions) reviewed by a specialist.

## Why GPL-3.0-or-later

The project is a community-driven editor whose maintainer wants modified, distributed versions
to keep the same freedoms. A strong copyleft license does that. The choice was checked against
the actual dependencies and build configurations rather than assumed:

| Component | Used for | License of the parts we use | Compatible with GPL-3.0-or-later? |
|---|---|---|---|
| Qt 6 (Core, Gui, Qml, Quick, Quick Controls, Quick Dialogs, Svg, Concurrent; Test for tests) | UI, application framework | `LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only` (verified in the Qt 6.8.3 sources) — we use it under LGPL-3.0 / GPL-3.0 | Yes |
| Qt Shader Tools | Build-time tool for Qt Quick only; not linked | GPL-3.0 with Qt exception / LGPL-3.0 | Not distributed with the app |
| MLT framework, `core`, `avformat`, `plus` modules | Timeline graph, playback, encoding | `LGPL-2.1-or-later` | Yes |
| MLT `normalize` module (the `volume` filter) | Clip volume and audio fades | `GPL-2.0-or-later` | Yes ("or later" allows GPL-3.0) |
| FFmpeg (through MLT) | Decoding and encoding | `LGPL-2.1-or-later` by default; `GPL-2.0-or-later` with `--enable-gpl`; `GPL-3.0-or-later` with `--enable-gpl --enable-version3` | Yes, for every configuration **except** `--enable-nonfree` builds, which may not be redistributed at all |
| x264 (inside GPL FFmpeg builds) | H.264 encoding | `GPL-2.0-or-later` | Yes |
| SDL 2 | Audio output (optional) | `Zlib` | Yes |
| FrameTrellis artwork and icons | Logo, icons | Original work, `GPL-3.0-or-later` | — |

Because the H.264 encoder that makes the documented MP4 export possible (x264) is only available
in GPL builds of FFmpeg, and because MLT's `volume` filter is GPL-licensed, a binary FrameTrellis
distribution is a GPL work in practice. Choosing a permissive license for FrameTrellis would not
change that for binaries and would weaken the protection the maintainer asked for. Nothing in the
stack is `GPL-2.0-only`, which would be the one incompatibility to watch for: Qt is also offered
under `GPL-2.0-only`, but we use its LGPL-3.0/GPL-3.0 options.

## The project license versus dependency licenses

* The FrameTrellis source code, documentation, website and artwork in this repository are
  `GPL-3.0-or-later` unless a file says otherwise.
* Dependencies keep their own licenses. They are listed in
  [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md). Using them under GPL-3.0-or-later does
  not relicense them.
* Media you edit and the files you export belong to you. The license of the software does not
  apply to your footage or output.

## Building from source

When you build FrameTrellis yourself, you link against whatever Qt, MLT and FFmpeg your system
provides. All common distribution packages (Ubuntu/Debian, Fedora, Arch, MSYS2, Homebrew) are
GPL-compatible. Do **not** link FrameTrellis against an FFmpeg built with `--enable-nonfree`
if you intend to redistribute the result: such builds are not redistributable.

You can check an FFmpeg build with `ffmpeg -buildconf` (look for `--enable-nonfree`).

## Redistributing binaries

If you distribute FrameTrellis binaries (including the project's own release packages), the
GPL and LGPL require you to:

1. **Ship the license texts.** Packages include `LICENSE`, `THIRD_PARTY_NOTICES.md` and, for the
   Windows package, the license files of the bundled libraries.
2. **Provide the Corresponding Source** for FrameTrellis *and* for every bundled GPL/LGPL library
   (Qt, MLT, FFmpeg and FFmpeg's GPL dependencies such as x264), for the exact versions shipped.
   For FrameTrellis this is the tagged source in this repository. Each Windows package contains
   `BUNDLED_COMPONENTS.txt` (every bundled MSYS2 package with its version and license) and
   `SOURCES.txt` (the matching MSYS2 source packages). The release workflow downloads those
   source packages (build recipe, patches and upstream source) and attaches them to the release
   as `FrameTrellis-<version>-windows-x64-third-party-sources.tar`, so the project itself keeps
   the Corresponding Source available next to the binaries.
3. **Allow relinking of LGPL libraries.** Qt and the LGPL parts of MLT/FFmpeg are shipped as
   separate shared libraries (DLLs), so users can replace them. Static linking is not used.
4. **Not add restrictions.** No DRM, no additional terms, no "non-commercial" conditions.

### Windows packages

The Windows package is built with the MSYS2 UCRT64 toolchain and bundles MSYS2's Qt 6, MLT and
FFmpeg DLLs. As verified in the MSYS2 package recipes used at the time of writing:

* FFmpeg is configured with `--enable-gpl --enable-version3` and **without** `--enable-nonfree`,
  so its effective license is `GPL-3.0-or-later` and it is redistributable.
* MLT is built with its default `GPL=ON`/`GPL3=ON` options, so its GPL modules are present.
* SDL 2 is `Zlib`.

The bundled set is determined at packaging time, so the manifest generated for each release is
the authoritative list for that release.

### Patent-encumbered codecs

Some formats (notably H.264/AVC, H.265/HEVC and AAC) may be covered by patents in some
countries. Copyright licenses such as the GPL do not grant patent licenses from third parties.
FrameTrellis does not charge for the software and does not include any patent license. If you
distribute FrameTrellis commercially, check the rules that apply in your country.

## Open questions

These points have been considered but would benefit from specialist review before large-scale
commercial redistribution:

* Whether the MSYS2 source packages attached to each release are complete for every
  component. They are what MSYS2 publishes as the source of its binaries, but a component
  whose recipe fetches additional sources at build time could need extra archives.
* Patent licensing for H.264/AAC in jurisdictions where those patents are still enforceable.

## Contributions

By submitting a contribution you agree that it is licensed under GPL-3.0-or-later, the same as
the project (inbound = outbound). There is no contributor license agreement and no copyright
assignment; contributors keep their copyright. Please add an SPDX header to new files.
