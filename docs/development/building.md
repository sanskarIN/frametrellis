# Building FrameTrellis

FrameTrellis is a CMake project. It needs:

| Dependency | Minimum | Notes |
|---|---|---|
| C++20 compiler | GCC 11, Clang 14, MSVC is not supported yet | MinGW-w64 GCC (MSYS2) on Windows |
| CMake | 3.25 (presets); 3.21 without presets | |
| Ninja | any recent | used by the presets |
| Qt | 6.5 (6.8 LTS recommended) | Core, Gui, Qml, Quick, Quick Controls, Quick Dialogs, Svg, Concurrent, Test |
| MLT | 7.14 | with the `avformat` (FFmpeg), `core`, `plus` and `normalize` modules |
| FFmpeg | whatever MLT was built against | must include an H.264 encoder (libx264 preferred) for MP4 export |
| SDL 2 | 2.0.18 (optional) | audio playback; without it the preview is silent |

Build options (`-D<option>=ON|OFF`):

| Option | Default | Meaning |
|---|---|---|
| `FT_BUILD_APP` | ON | the Qt Quick application (needs Qt Quick) |
| `FT_BUILD_ENGINE` | ON | the MLT engine, render helper and engine tests |
| `FT_BUILD_TESTS` | ON | the test suite |
| `FT_WITH_SDL2` | ON | use SDL 2 for audio when found |
| `FT_WARNINGS_AS_ERRORS` | OFF | treat compiler warnings as errors |
| `FT_ENABLE_SANITIZERS` | OFF | AddressSanitizer + UBSan (GCC/Clang) |

The executables are written to `build/<preset>/bin/`: `frametrellis` (the editor) and
`frametrellis-render` (the export helper, which must stay next to the editor).

## Windows 11 (MSYS2, UCRT64)

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 UCRT64** shell.
2. Install the toolchain and libraries:

   ```sh
   pacman -Syu
   pacman -S --needed git mingw-w64-ucrt-x86_64-{cc,cmake,ninja,pkgconf,qt6-base,qt6-declarative,qt6-svg,qt6-imageformats,mlt,fftw,libebur128,ffmpeg,SDL2}
   ```

   MSYS2's MLT lists its plug-in libraries only as optional dependencies, so install them
   explicitly: `ffmpeg` for the FFmpeg module, and `fftw` and `libebur128` for the "plus" module,
   which provides the `affine` transition FrameTrellis uses to layer video tracks. Without them
   FrameTrellis reports that the installation is incomplete.
3. Configure, build and test:

   ```sh
   git clone https://github.com/sanskarIN/frametrellis.git
   cd frametrellis
   cmake --preset windows-msys2
   cmake --build --preset windows-msys2
   ctest --preset windows-msys2
   ```

4. Run it from the same shell (so the MSYS2 DLLs are found):

   ```sh
   ./build/windows-msys2/bin/frametrellis.exe
   ```

To produce a self-contained folder that runs outside MSYS2, see [Packaging](#packaging).

## Linux

### Ubuntu 24.04 LTS

Ubuntu 24.04 ships Qt 6.4, which is older than FrameTrellis needs, so install Qt 6.8 from the
[Qt online installer](https://www.qt.io/download-open-source) or with
[aqtinstall](https://github.com/miurahr/aqtinstall):

```sh
sudo apt install build-essential cmake ninja-build pkg-config git \
  libavformat-dev libavcodec-dev libavutil-dev libavfilter-dev libavdevice-dev \
  libswscale-dev libswresample-dev libsdl2-dev libgl1-mesa-dev libxkbcommon-x11-0 libxcb-cursor0
pipx install aqtinstall
aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O "$HOME/Qt"

# Ubuntu's MLT packages bring in their own Qt-based modules, so build the few MLT modules
# FrameTrellis needs against the system FFmpeg instead (takes about a minute):
scripts/build-mlt.sh v7.22.0 "$HOME/.local/mlt"

cmake --preset linux -DCMAKE_PREFIX_PATH="$HOME/Qt/6.8.3/gcc_64;$HOME/.local/mlt"
cmake --build --preset linux
ctest --preset linux
./build/linux/bin/frametrellis
```

### Fedora, Arch Linux and other distributions with Qt ≥ 6.5

Install Qt 6 (base, declarative, svg), MLT 7 development files, FFmpeg and SDL 2 from your
distribution, for example on Arch Linux:

```sh
sudo pacman -S --needed base-devel cmake ninja qt6-base qt6-declarative qt6-svg mlt ffmpeg sdl2
cmake --preset linux && cmake --build --preset linux && ctest --preset linux
```

If your distribution's MLT pulls in a Qt-based MLT module linked against a *different* Qt
version than the one you build with, point FrameTrellis at a module folder without it
(`FRAMETRELLIS_MLT_REPOSITORY=/path/to/mlt-7`) or use `scripts/build-mlt.sh`.

## macOS (experimental)

macOS is not officially supported yet. With [Homebrew](https://brew.sh/):

```sh
brew install cmake ninja qt mlt sdl2
cmake --preset macos -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix mlt)"
cmake --build --preset macos
ctest --preset macos
```

## Running from the build tree

FrameTrellis finds MLT's modules in this order: the `FRAMETRELLIS_MLT_REPOSITORY` or
`MLT_REPOSITORY` environment variables, a `lib/mlt…` folder next to (or one level above) the
executable, the MLT used at build time, and finally MLT's built-in default. `MLT_DATA` is set
to the matching `share/mlt…` folder automatically.

Useful environment variables while developing:

| Variable | Effect |
|---|---|
| `QT_LOGGING_RULES="ft.*.debug=true"` | verbose FrameTrellis logs (`ft.engine`, `ft.playback`, …) |
| `QT_QPA_PLATFORM=offscreen` | run without a display (tests, screenshots) |
| `QT_QUICK_BACKEND=software` | software rendering for Qt Quick |
| `SDL_AUDIODRIVER=dummy` | silent audio device |

Logs are also written to `frametrellis.log` in the application data folder
(*Help → About → Open log folder*).

## Development tools

```sh
cmake --preset linux-debug     # Debug, warnings as errors
cmake --preset linux-asan      # AddressSanitizer + UBSan
git ls-files '*.cpp' '*.h' | xargs clang-format -i                  # format
git ls-files 'src/*.cpp' | xargs clang-tidy -p build/linux-debug    # static analysis
```

CI runs the same formatting, clang-tidy, sanitizer and test steps; see
[`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).

## Packaging

### Windows

Build the `windows-msys2-release` preset in the MSYS2 UCRT64 shell (with the `python` package
installed as well), then assemble the portable folder and ZIP:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-python
cmake --preset windows-msys2-release
cmake --build --preset windows-msys2-release
ctest --preset windows-msys2-release
python packaging/windows/package.py --build-dir build/windows-msys2-release --output dist
```

[`packaging/windows/package.py`](../../packaging/windows/package.py) copies the executables,
deploys Qt (libraries, plug-ins and QML modules) with `windeployqt`, adds the MLT modules and
data, and copies every DLL the binaries depend on, found by reading their import tables. It
stops if a dependency can be found neither in MSYS2 nor in Windows. Every bundled file is
attributed to the MSYS2 package that installed it; the script copies those packages' license
files and writes `BUNDLED_COMPONENTS.txt`, `SOURCES.txt` and an SPDX 2.3 software bill of
materials (`dist/FrameTrellis-<version>-windows-x64.spdx.json`).

Some MSYS2 packages (FFmpeg, x264, x265, ICU and about twenty others) install no license file.
For those, the script downloads the package's MSYS2 source package and copies the license and
notice files (`COPYING*`, `LICENSE*`, `NOTICE*` and similar) from the upstream source, reading
Git sources at the revision that was built. This needs `curl`, `zstd` and `git` in the MSYS2
shell (`pacman -S --needed curl git zstd`); downloads are cached in
`<build-dir>/msys2-sources`. Packaging stops if any bundled component is left without a license
text. The script's unit tests run on any platform:

```sh
python3 -m unittest discover -s packaging/tests -v
```

The installer is built from that folder with [Inno Setup](https://jrsoftware.org/isinfo.php) 6:

```powershell
ISCC.exe /DAppVersion=<version> /DStageDir=dist\FrameTrellis-<version>-windows-x64 `
  /DOutputDir=dist /DRepoDir=. packaging\windows\frametrellis.iss
```

[`packaging/windows/verify.ps1`](../../packaging/windows/verify.ps1) checks both packages with
nothing but Windows on the `PATH`. It runs the engine self-test (`frametrellis-render
--self-test <folder>`), starts the editor, checks that every loaded library comes from the
package or from Windows, then installs, tests and uninstalls the installer.

The MLT modules live in `lib/mlt`, but their dependencies (FFmpeg and the other libraries) sit
next to the executables. MLT loads modules with an altered DLL search path that skips the
application folder, so the engine adds that folder with `SetDllDirectory` before it starts MLT.
Development builds do not notice this because MSYS2 is on their `PATH`; the clean-machine
check is what catches it.

### Linux

`cmake --install build/linux --prefix <dir>` installs the binaries, the desktop entry and the
icon. A self-contained Linux package (AppImage or Flatpak) is planned. It will be published
only after it has been verified.

### Automation

The [Windows packages workflow](../../.github/workflows/package-windows.yml) builds the
packages on every pull request and verifies them on clean Windows Server 2022, Windows
Server 2025 and Windows 11 (ARM64, x64 emulation) runners. The
[release workflow](../../.github/workflows/release.yml) runs on version tags. It publishes a
GitHub release only when every build, test and verification step has passed, and it attaches
the installer, the portable ZIP, the SBOM, the third-party source archive and `SHA256SUMS.txt`.

Which other channels (WinGet, Flathub, Homebrew) are planned, and what each one needs, is
described in [Distribution channels](distribution.md).
