# v1.5 development: independent runtime package inventory

**Inspection tooling only.** This does not produce a stable build or waive the
unresolved Linux/macOS portability, licensing, notarization and crash gates.

A Linux AppImage can contain Qt, FFmpeg and MLT binaries whose library names
appear correct but whose actual bytes differ between build runners. Similarly
a macOS .app can accidentally contain escaping symlinks or an altered framework
even if the DMG's filename matches the desired release. A package-wide inventory
makes the exact shipped file tree reviewable without writing private build paths.

## Linux experimental AppDir

After building the development AppDir:

```bash
python3 packaging/runtime_inventory.py generate \
  --kind linux-appdir --root dist/linux/AppDir \
  --manifest dist/linux/appdir-inventory.json
python3 packaging/runtime_inventory.py verify \
  --root dist/linux/AppDir --manifest dist/linux/appdir-inventory.json
```

This is an **AppDir inventory**, not an inventory of an arbitrary compressed
AppImage executable. The experimental Linux CI now creates this inventory
*after* staging and compares it with the **exact AppDir extracted from the
final hashed AppImage**, before testing real packaged code on a clean Ubuntu
container. The report and self-observed SHA-256 are saved as development CI
artifacts. This does not establish authenticated provenance or clean-host
compatibility outside the tested Ubuntu environment.

## macOS development .app

After building and signing the *development* macOS application:

```bash
python3 packaging/runtime_inventory.py generate \
  --kind macos-app \
  --root dist/macos/FrameTrellis-macos-arm64-development.app \
  --manifest dist/macos/arm64-app-inventory.json
python3 packaging/runtime_inventory.py verify \
  --root dist/macos/FrameTrellis-macos-arm64-development.app \
  --manifest dist/macos/arm64-app-inventory.json
```

Repeat separately for **native Intel and Apple Silicon** and for the actual
.app extracted from each final DMG. Never treat a staged-only manifest as
proof of the final mounted disk image. Code signing or notarization **changes
bytes**, so the final manifest must be generated *after* signing and any
stapling changes; never reuse pre-signing hashes as release evidence.

## Verifier properties

- Reads every regular file and records filename, byte size, SHA-256, and
  executable-bit state.
- Records every directory, including empty directories, and each symbolic
  link's normalized **internal** target.
- Rejects broken, escaping or absolute symlinks, platform-incompatible
  case-folded/Unicode path collisions, and special filesystem entries.
- Produces stable, sorted UTF-8 JSON without usernames, absolute build
  paths, timestamps or machine identifiers.
- Refuses to generate inside the package tree or overwrite an existing
  manifest, and detects any subsequent file, mode, symlink or path change.

**Limitations:** SHA-256 manifests do not provide a trusted signature without
a separate authenticated digest. No legal license attribution can be inferred
from a filename or checksum. The inventory does **not** verify that bundled
system libraries are appropriate, that a plugin loads, that a video renders,
that the app runs without Homebrew, or that Gatekeeper accepts the DMG.
Verify those requirements independently, as documented in the
[v1.5 preflight](../releases/v1.5.0-preflight.md).

No app files are uploaded to a service by this script. The JSON inventory
contains only package-internal relative paths and metadata.
