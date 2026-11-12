# macOS Developer ID notarization gate (future v1.5.0 candidate)

**Never run this workflow as a substitute for a stable release approval.**
The experimental [macOS DMG pipeline](macos-packaging.md) uses ad-hoc code
signatures only and produces `UNNOTARIZED-development` artifacts. Such DMGs
are not approved for public release.

`packaging/macos/notarize-candidate.sh` is a **separate, opt-in** route to
build a *potentially publishable* DMG from an already audited self-contained
native `.app`. It **does not upload**, tag or create a GitHub Release.

## Prerequisites

- Native, trusted macOS Intel or Apple Silicon machine with Xcode command-line
  tools and a valid Developer ID Application signing identity in the keychain.
- An Apple notarytool keychain profile already configured locally. Never
  commit an Apple API private key, authentication password, keychain dump or
  `.p8` file, and do not put such credentials on a command line.
- All bundled Qt/MLT/FFmpeg/SDL libraries and MLT modules pass
  `packaging/macos/verify_bundle.py` on the **same source revision**.
- CMake project version is the intended `x.y.z` release version. No manual
  change to `v1.5.0` is permitted until the owner has accepted scope.
- A complete, license-reviewed third-party package and source inventory.

Example, **only when the owner approves a future candidate build**:

```bash
export FT_DEVELOPER_ID_APPLICATION="Developer ID Application: YOUR COMPANY (TEAMID)"
export FT_NOTARYTOOL_PROFILE="your-local-keychain-profile"
bash packaging/macos/notarize-candidate.sh \
  dist/macos/FrameTrellis-macos-arm64-development.app \
  dist/macos/notarized-candidates 1.5.0
```

Replace the example identity with your own, already-installed **certificate
identity**. The profile is a name, **not** a secret. The script reads
credentials from macOS Keychain through Apple `notarytool`.

It rejects missing credentials, version/architecture mismatch, invalid
bundle/runtime dependency paths and existing output filenames. It signs
nested native code, frameworks and the independent renderer, signs the outer
app with hardened runtime, creates a DMG, waits for an explicit **Accepted**
notarization result, staples the ticket, validates the ticket and DMG and
only **then** exposes the production-style candidate filename with SHA256.

Failure before all gates pass leaves no production-style output file.
Temporary signed packages are deleted; original app/project/media are
untouched.

## What this does NOT certify

- Apple notarization can require additional Qt/QML hardened-runtime
  entitlements or updated codec libraries. Any failure must be investigated
  instead of bypassed with `codesign --deep --force` hacks.
- A successful receipt and staple are still **not** a tested Finder launch on
  a *clean, quarantined* Mac without Homebrew. Perform that separately using
  `spctl`, Gatekeeper prompts, GUI/renderer startup, audio and MP4 export.
- Repeat on *both* arm64 and x86_64 from the same frozen source SHA.
- It does not generate SPDX or corresponding-source archives for third-party
  bundled libraries. Those remain required for distribution.
- No current GitHub Actions workflow has release-signing secrets; this
  script must not be wired into untrusted pull-request workflows.
- Windows and Linux still require independent, platform-appropriate
  distribution and QA gates. Owner publication remains manual.

Record the notarization request ID privately along with exact SHA256,
certificate Team ID, macOS architecture, Gatekeeper results and corresponding
source revision. Do not upload unredacted keychain or signing logs.
