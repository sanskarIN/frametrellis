# Experimental native macOS DMG: v1.5 development only

**Not a stable or notarized release.** Use the new
[`Experimental macOS desktop packages`](../../.github/workflows/package-macos-experimental.yml)
workflow to build on both Apple Silicon and Intel. The output names explicitly
contain `UNNOTARIZED-development`. They must never be attached to a stable
GitHub release, published to Homebrew, or described as a safe Gatekeeper
download without Apple Developer ID signing and notarization.

## What the build does

1. Build the full C++20/Qt6 editor and independent render helper on a native Mac.
2. Copy the render helper into `Contents/MacOS`, beside the GUI.
3. Stage only MLT `core`, `avformat`, `plus`, and `normalize` modules,
   plus MLT data inside `Contents/PlugIns/mlt-7` and
   `Contents/Resources/mlt-7`. The editor resolves those local folders.
4. Use `macdeployqt` to deploy Qt frameworks, GUI plugins and QML imports.
   Explicitly deploy both Cocoa and **offscreen** Qt QPA plugins: macdeployqt
   commonly bundles only Cocoa, but the independently packaged renderer and
   unattended GUI screenshots require the offscreen platform. The bundle
   verifier rejects a missing platform plugin before a DMG is published.
5. Use `dylibbundler` on both executables **and** the dynamically loaded
   MLT modules to copy linked codec/media dependencies into `Contents/Frameworks`.
6. Run `packaging/macos/verify_bundle.py` to reject remaining Homebrew or
   other absolute non-system dylib links, malformed references and paths
   escaping the application bundle. Perform ad-hoc signing for integrity tests.
7. Produce a development DMG, checksum, separate version inventory and logs.
   Run real synthetic MP4 rendering and Qt Quick GUI screenshot tests from
   the bundled app, then mount and check the DMG.

From a fresh checkout with Homebrew dependencies available:

```bash
brew install cmake ninja pkg-config qt mlt ffmpeg sdl2 dylibbundler
cmake --preset macos -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix mlt)"
cmake --build --preset macos
ctest --preset macos
bash packaging/macos/build-dmg.sh build/macos dist/macos
```

## Required stable v1.5.0 blockers

Passing the workflow is **not sufficient** to publish this as a supported
macOS download. For each architecture, the **same frozen source SHA** needs:
- A clean, supported macOS machine without Homebrew, developer runtimes,
  build-tree libraries or CI environment variables.
- GUI startup and shutdown, project open/save/recovery, video+audio preview,
  MLT plugin availability, video effects, subtitle fonts, long 4K exports,
  MP4 stream FPS and actual audible AAC verified independently.
- A complete record of Qt, MLT, FFmpeg, SDL2 and transitive codecs/plugins,
  including **all required redistributed licenses**, source provenance and
  complete checksum/SBOM audit. This experimental script stages main project
  notices but **does not yet prove third-party license closure**.
- Developer ID Application signing of all nested native code, hardened
  runtime where required, Apple notarization and DMG stapling, verification
  with `spctl` and Gatekeeper quarantine. Ad-hoc signing in CI is not a
  replacement for any of these.
- A reviewed update to the guarded release workflow: current stable
  attachment is deliberately Windows-only; do not bypass this gate.
