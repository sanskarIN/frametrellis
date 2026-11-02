# Publish FrameTrellis manually from your GitHub account

This process creates the **stable release through the repository owner's GitHub Releases UI**.
GitHub Actions **does not create a tag, publish a release or upload release assets**.
The optional **Prepare manual release** workflow only prepares verified files so that a
maintainer can inspect and attach them by hand.

> Never publish a draft, unverified package or an asset set with missing corresponding
> third-party sources. The final release source SHA must match the tested `main` commit.

## Prerequisites

1. Freeze `main` and record `git rev-parse origin/main` (or the SHA in the GitHub
   `main` commit view). Confirm CMake `VERSION 1.4.0` and
   [v1.4.0 notes](v1.4.0.md) are final.
2. Confirm **CI**, **Windows packages**, and **Publish website** are green at that
   exact SHA. Clean package verification on `windows-2022`, `windows-2025`, and
   `windows-11-arm` must all pass.
3. Finish and record the [integrated editing acceptance matrix](../testing/v1.4-integrated-editing-acceptance.md)
   on a physical Windows device; verify installer and portable ZIP, real audio and
   encoded MP4 output, project backups and reopening. CI does not replace this.
4. Review the known limitations in the release notes. Avoid uploading a release if
   a data-loss, startup or media-corruption defect remains unresolved.

## Prepare all five release attachments (without publishing)

1. A **Prepare manual release** run starts when changes to the preparation
   workflow itself are merged into `main`; otherwise open **Actions → Prepare
   manual release → Run workflow**, select **main** and run it. Verify the run's
   **head SHA exactly matches** the frozen, tested `main` commit (the UI selects
   a branch/ref rather than letting you pin an arbitrary commit). This is a
   release *rehearsal and packaging* workflow, not a publisher.
2. Require the entire workflow to succeed: version/notes check, Windows packages,
   clean-host package verification, matching third-party corresponding sources,
   and the final `Assemble manual upload bundle` job.
3. Open the completed run and download the **manual-release-assets** artifact.
   Extract the artifact ZIP locally. The upload directory must contain:
   - `FrameTrellis-1.4.0-windows-x64-setup.exe`
   - `FrameTrellis-1.4.0-windows-x64.zip`
   - `FrameTrellis-1.4.0-windows-x64.spdx.json`
   - `FrameTrellis-1.4.0-windows-x64-third-party-sources.tar`
   - `SHA256SUMS.txt`

   The included `release-notes.md` is **release body text**, not an attachment.
   The Windows package workflow alone does *not* generate the third-party source
   archive; the preparation workflow does.
4. Check that none of these files is zero bytes and all SHA-256 hashes match
   `SHA256SUMS.txt`. On Windows, use PowerShell:

   ```powershell
   Get-FileHash .\FrameTrellis-1.4.0-windows-x64-setup.exe -Algorithm SHA256
   Get-FileHash .\FrameTrellis-1.4.0-windows-x64.zip -Algorithm SHA256
   Get-FileHash .\FrameTrellis-1.4.0-windows-x64.spdx.json -Algorithm SHA256
   Get-FileHash .\FrameTrellis-1.4.0-windows-x64-third-party-sources.tar -Algorithm SHA256
   ```

   Match each value against the entry for that filename. Do not trust a hash
   from an older candidate or an unrelated Actions run.
   For a repeatable four-payload hash and archive-integrity gate, run the
   [independent asset validator](asset-verification.md) from the repository root:

   ```sh
   python3 packaging/verify_release_assets.py --version 1.4.0 --directory /path/to/extracted-assets
   ```

   This checks filenames, SHA-256, portable ZIP CRC/path safety, the matching
   SPDX package version and corresponding-source TAR structure. It does **not**
   replace real Windows/audio/export acceptance tests.
5. Inspect `release-notes.md`, the source archive inventory, and the package
   manifest before publication. Use real hardware test results in release sign-off.

## Publish **yourself** in GitHub Releases (NOT GitHub Actions)

1. Open [FrameTrellis Releases](https://github.com/sanskarIN/frametrellis/releases)
   while signed in as a maintainer of `sanskarIN/frametrellis`.
2. Click **Draft a new release**.
3. Select **Choose a tag → Create new tag**, enter `v1.4.0`; target **main**
   **only if** it is still at the frozen, successfully tested SHA. If the tag
   already exists, independently verify its target SHA first. Do not retag an
   existing stable release or publish from an untested newer `main`.
4. Use the title `FrameTrellis 1.4.0` and paste the complete
   `release-notes.md` into the description.
5. Drag the **five** validated files above into the release asset uploader.
   Wait until every upload finishes and each filename is correct.
6. **Uncheck** “Set as a pre-release”. Select latest-release status when offered.
   Publish the release directly with **Publish release**.
7. Verify the live [v1.4.0 release page](https://github.com/sanskarIN/frametrellis/releases/tag/v1.4.0):
   `prerelease=false`, release tag/target SHA match, exactly expected downloads,
   valid SHA256SUMS, correct license/source archive and no truncated assets.
   Then confirm the website's download page has refreshed; publishing the release
   triggers **Publish website** but **not** an Actions release publisher.
8. Only after publication, change [release index](README.md), release tracker and
   website-facing announcements to label v1.4.0 as the latest stable.

**If any step fails:** keep it a draft or do not create it; repair the issue,
re-test from the final source commit and rebuild the matching asset bundle.

## Why release preparation can use Actions

Actions is used here for *building* and *verifying* binaries on clean runners,
as it is for ordinary CI. The publish action is performed only by the repository
owner in the GitHub Releases interface. A tag push or GitHub release publication
does **not** start the packaging/release-preparation workflow.
