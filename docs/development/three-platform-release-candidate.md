# v1.5.0 three-platform release candidate: planned asset contract

**Planning and validation only — v1.5.0 has NOT been approved, tagged or published.**
The existing stable publication workflows remain **Windows-only**.
The old preparation/uploader now explicitly **reject stable v1.5.0 or newer**
through the `--windows-only-assets` release-version gate. This prevents
accidentally publishing a three-platform featured release with only five
Windows artifacts; it is a safeguard, **not** the missing three-platform
release pipeline. Experimental Linux AppImages and UNNOTARIZED macOS DMGs
are **not** valid production release assets.

## Proposed complete artifact set

Use exactly the following *future production* names after all runtime, legal,
distribution and platform-specific gates pass (replace `1.5.0` with the frozen
CMake version if reviewing another candidate):

| Platform | Required production artifacts |
| --- | --- |
| Windows x64 | `FrameTrellis-1.5.0-windows-x64-setup.exe`, `FrameTrellis-1.5.0-windows-x64.zip`, `FrameTrellis-1.5.0-windows-x64.spdx.json`, `FrameTrellis-1.5.0-windows-x64-third-party-sources.tar` |
| Linux x86_64 | `FrameTrellis-1.5.0-linux-x86_64.AppImage`, `FrameTrellis-1.5.0-linux-x86_64.spdx.json`, `FrameTrellis-1.5.0-linux-x86_64-third-party-sources.tar` |
| macOS arm64 | `FrameTrellis-1.5.0-macos-arm64.dmg`, `FrameTrellis-1.5.0-macos-arm64.spdx.json`, `FrameTrellis-1.5.0-macos-arm64-third-party-sources.tar` |
| macOS Intel | `FrameTrellis-1.5.0-macos-x86_64.dmg`, `FrameTrellis-1.5.0-macos-x86_64.spdx.json`, `FrameTrellis-1.5.0-macos-x86_64-third-party-sources.tar` |

Plus one `SHA256SUMS.txt` containing **exactly one checksum per payload**.
These are *desired outputs*, not claims that the corresponding source archives,
SPDX inventories, production AppImage, or notarized DMGs exist yet. An
`EXPERIMENTAL`, `UNNOTARIZED` or `development` artifact is automatically
unacceptable as a stable candidate.

## Offline assembly of a complete candidate (not a publication workflow)

When the **real production** Windows, Linux, Apple Silicon and Intel macOS
packaging jobs have produced ALL of their per-platform payloads and complete
SPDX/source archives, collect each platform in a distinct staging folder
(`win/`, `linux/`, `mac-arm/`, `mac-intel/`). Each folder must contain
**exactly** the required files in the platform row above, plus one
`SOURCE_COMMIT.txt` with the exact 40-character lowercase Git SHA followed
by one newline. The SHA must refer to the **same frozen main commit** for
all four packages. No experimental/development files are accepted.

```bash
frozen_sha="$(git rev-parse HEAD)"
python3 packaging/assemble_candidate_matrix.py \
  --version 1.5.0 --source-sha "$frozen_sha" \
  --windows-dir /path/to/win --linux-dir /path/to/linux \
  --macos-arm64-dir /path/to/mac-arm --macos-x86_64-dir /path/to/mac-intel \
  --output-dir /path/to/NEW-release-candidate
```

The destination **must not already exist or be nested inside any input
stage**; use a separate output folder so the four declared source inventories
remain unchanged. It is created without overwriting other files. The tool
verifies stage completeness, refuses divergent
source-SHA declarations and unexpected/symlinked/empty inputs, copies exactly
13 production payloads, checks copied SHA-256 values, writes basename-only
`SHA256SUMS.txt`, then runs the full cross-platform archive/SPDX/structure
verifier. If anything fails after staging begins, a conspicuous
`INCOMPLETE-DO-NOT-PUBLISH` marker remains and the candidate verifier will
reject that directory. The tool never tags, signs, notarizes or uploads.

**Security limit:** `SOURCE_COMMIT.txt` is a self-declared, unsigned
consistency marker, **not proof of source-build identity**. Independently
authenticate every stage against trusted protected GitHub Actions run IDs,
exact checkout SHAs, uploaded artifact digests and source dependency records
before approving the frozen commit. Do not create placeholder SPDX documents,
empty source archives or renamed development binaries to satisfy this tool.

## Local offline integrity gate

After producing all 13 files from the **same frozen main SHA**, run:

```bash
python3 packaging/verify_candidate_matrix.py \
  --version 1.5.0 --directory /path/to/candidate-assets
```

The verifier refuses incomplete sets, unexpected files, symlinked payloads,
duplicate/missing checksums and changes in file bytes. Each SPDX 2.3 document
must declare a correctly versioned FrameTrellis application, a non-placeholder
third-party package inventory with source locations, and matching
DESCRIBES/CONTAINS relationships. An application-only placeholder SBOM now
fails even with a valid checksum. This is a **structural** check, not proof
that all actual shipped libraries or their full corresponding sources are
accounted for. It also rejects non-ELF AppImages, non-MZ Windows installers
and DMGs without an expected UDIF trailer. The proposed Windows portable ZIP must be a **real,
CRC-verified archive** with unique safe paths and adjacent editor/renderer
executables; all four corresponding-source TARs must be parseable, contain
nonempty files, and reject traversal, symlinks, special entries and duplicate
paths. This is *structural safety*, not proof that the source archives contain
all required upstream sources. It **does not execute or trust any packaged code**.
Continue to run the Windows-specific `verify_release_assets.py` with the
Windows-only set in a separate directory for full ZIP/archive verification.

**This check is necessary but nowhere near sufficient.** It does not itself
validate DMG signatures, notarization tickets, code-signing identities,
Gatekeeper/quarantine, audio playback, Glibc compatibility, exact executable
architecture, whether SPDX inventories and source archives account for *every*
bundled component, or whether binaries match the indicated source SHA. **GitHub workflow provenance
must independently establish that every platform was built from the exact same
frozen commit**; SHA256SUMS alone cannot establish source-build identity.

## Publication hold

1. Freeze all four builds at the exact same approved Git commit.
2. Windows: pass clean package verification on Server 2022/2025 and Windows 11
   ARM, plus real native x64 export, preview and recovery acceptance.
3. Linux: pass an isolated desktop with no developer Qt/MLT, independent real
   MP4/audio decode, clean-target tests, and complete codec/license provenance.
4. macOS: pass clean Intel/Apple Silicon Macs without Homebrew, Developer ID
   signing, hardened runtime as needed, successful Apple notarization and
   stapling, and execute
   `packaging/macos/verify-notarized-candidate.sh` separately on **each native
   architecture**. The script validates checksum, ticket, Developer ID and
   quarantined Gatekeeper acceptance; real Finder launch, physical audio and
   long media-render verification still require independent manual evidence.
5. Generate complete source archives and SPDX per platform; verify all checksums.
6. Only then extend the guarded owner-operated stable release preparation and
   five-file uploader to the **approved** full 14-file candidate matrix. New
   workflows must fail closed on an absent or failing platform.
7. Owner must separately approve `v1.5.0` creation and publication. Do **not**
   auto-tag, auto-publish, or attach experimental artifacts.

The version and asset naming scheme remain a **candidate contract** until the
actual production packaging pipelines and maintainer review are complete.
