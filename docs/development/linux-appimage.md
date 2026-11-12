# Experimental Linux x64 AppImage — v1.5 development only

The [experimental Linux AppImage workflow](../../.github/workflows/package-linux-experimental.yml)
builds the actual Qt Quick editor, MLT render helper and integration tests
before attempting a self-contained AppDir/AppImage. The output is named
`EXPERIMENTAL`, is not automatically published or installed, and **must not**
be attached to a stable GitHub release until the checks below pass.

## Packaging contents

- `cmake --install` stages the editor, separate `frametrellis-render`,
  Linux desktop icon, launcher, `.ftproj` MIME registration and license notices.
- Essential MLT 7 `core`, `avformat`, `plus`, `normalize` service modules
  and their data are copied under `usr/lib/mlt-7` and `usr/share/mlt-7`.
  FrameTrellis already resolves these paths relative to the executable.
- The `linuxdeploy` build is pinned to a known release and SHA256. Its Qt 6
  plug-in, Qt Quick QML scanner and media dependencies copy the required
  runtime libraries and QML modules. The plug-in's upstream release does
  **not** publish an API checksum; its downloaded SHA256 is recorded for
  auditing and must be fixed/pinned before final production approval.
- CI checks the final AppImage hash and starts the AppImage with only its own
  Qt/MLT dependencies, generating a synthetic project with the real renderer.
  No externally supplied footage is used or uploaded.
- An additional **experimental isolation gate** launches the exact packaged
  AppImage inside a fresh Ubuntu 24.04 container with common desktop system
  libraries but **without Qt/MLT/FFmpeg installed by the image**. The runtime
  test uses `--network none` and exercises both an offscreen GUI screenshot and
  the packaged render helper's self-test. It must pass on the PR's latest
  commit before counting it as verified. Container isolation on one distribution
  does **not** replace physical GUI/sound testing or older-glibc verification.

Development command after native dependencies are installed and built:

```bash
cmake --preset linux -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR;$HOME/mlt"
cmake --build --preset linux
ctest --preset linux
bash packaging/linux/build-appimage.sh build/linux dist/linux "$HOME/mlt"
```

## Release blockers (not satisfied by generating one AppImage)

1. Independently verify on clean, supported Linux distributions/desktop
   environments with no Qt, MLT, FFmpeg, developer tools or Homebrew/conda
   runtimes installed. Check X11/Wayland, sound devices and sandboxed systems.
2. Confirm *actual* MP4 H.264/AAC bitstreams, FPS, duration, non-silent
   preview, subtitles, graphics/driver operation, 4K and long repeated exports.
3. Freeze exact upstream tool versions, content hashes and transitive source
   provenance. Audit **every redistributed Qt, MLT, FFmpeg, SDL2 and codec
   license** and corresponding source. The experimental AppImage does not yet
   satisfy the complete release SBOM/source-license gate.
4. Verify glibc baseline across supported distributions, directory traversal,
   embedded QML/plugins, update/install/uninstall instructions, and privacy.
5. Update the guarded release workflow to independently verify and attach
   approved, checksum-pinned Linux packages **only after** the owner approves
   the v1.5.0 release. Do not repurpose development artifacts as stable assets.
