# v1.5.0 three-platform release candidate: planned asset contract

**Planning and validation only — v1.5.0 has NOT been approved, tagged or published.**
The legacy stable preparation/uploader remain **Windows-only** and reject
v1.5.0 or newer through the `--windows-only-assets` release-version gate.
Separate, fail-closed three-platform **candidate collection** and
**post-approval release attachment** workflows are now integrated (#265,
#266, with artifact retrieval fixed in #269). These workflows do **not**
produce the missing production Linux/macOS packages, licenses or SBOMs.
Experimental Linux AppImages and UNNOTARIZED macOS DMGs are **not** valid
production release assets.

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

## Future authenticated GitHub Actions candidate preparation

The owner-dispatched, **non-publishing** workflow
[Prepare v1.5 three-platform candidate](../../.github/workflows/prepare-v15-candidate.yml)
accepts one frozen protected-main commit and four trusted production workflow
run IDs (Windows, Linux, Apple Silicon and Intel). It refuses the current
1.4.0 CMake version, DRAFT v1.5.0 notes, run results from PR branches,
mismatched SHAs, non-successful runs, missing production artifacts and
unverified artifact ZIP bytes. It checks each downloaded archive's SHA-256
against GitHub's own artifact digest **before** safe flat-stage extraction.
The expected artifact names are
`production-v1.5.0-windows-x64`,
`production-v1.5.0-linux-x86_64`,
`production-v1.5.0-macos-arm64` and
`production-v1.5.0-macos-x86_64`; each must contain only that platform's
full 1.5.0 production payloads and exact `SOURCE_COMMIT.txt` evidence.
The workflow then calls the existing fail-closed assembler/verifier and
uploads **only a retained, clearly NOT-APPROVED candidate artifact**.

This route is intentionally unusable until separately approved production
packaging workflows exist, real source archives and SPDX inventories are
complete, the version and draft notes are finalized, and hardware/signing
acceptance is recorded. Even a successful candidate artifact is **not**
permission to create a release or use the old five-file uploader. The
14-file attachment workflow exists but it cannot compensate for absent
production packages, signing, licensing, physical QA or owner approval.

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

## Exact-SHA approved release attachment (future; no release created here)

Only after every hardware, licensing, source-provenance, Developer ID,
notarization and 14-file candidate gate is completed and the owner separately
approves publication, the owner publishes `v1.5.0` from the frozen
protected-main SHA using GitHub Releases UI. The release description must
also contain a **separate exact line**:

```text
FrameTrellis-1.5.0-OWNER-APPROVED
```

The new [v1.5.0-specific attachment workflow](../../.github/workflows/release-assets-v15.yml)
does not create tags/releases. It verifies that explicit marker, current
protected main SHA equals the published tag, CMake version 1.5.0, final
non-DRAFT notes and a successful same-SHA manual 14-file preparation run.
It downloads only that run's complete retained candidate, independently
checks all 13 platform payloads and SHA256SUMS.txt, refuses any existing
release assets and uploads the 14 files. It then checks names, sizes and
GitHub's SHA-256 published asset digests against the verified local bytes.
A partial uploader failure must be investigated manually; it cannot
safely mix a second run's files or overwrite an existing release.

The legacy five-file uploader is skipped for v1.5.0 but retains its
separate Windows-only version restriction for other versions. None of these
workflows supplies missing production Linux/macOS archives, notarization
credentials, physical QA, or grants approval without owner action.

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
6. Use the separate 14-file nonpublishing candidate preparation and
   owner-approval-gated uploader; the legacy five-file path does **not**
   accept v1.5.0. Never proceed when any platform stage is missing.
7. Owner must separately approve `v1.5.0` creation and publication, following
   [the manual owner runbook](../releases/v1.5.0-owner-release-runbook.md).
   Do **not** auto-tag, auto-publish, or attach experimental artifacts.

The version and asset naming scheme remain a **candidate contract** until the
actual production packaging pipelines and maintainer review are complete.
