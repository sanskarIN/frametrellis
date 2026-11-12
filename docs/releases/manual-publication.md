# Publish FrameTrellis from your GitHub account (automatic asset attachment)

The repository owner creates and **publishes** the stable GitHub Release through GitHub's
Releases interface. **This five-file procedure applies only to approved
Windows-only stable releases through v1.4.x**, not the upcoming three-platform
v1.5.0 featured release. Substitute the exact numeric release version for every
`X.Y.Z` placeholder only within that supported release family. The
`Attach verified release assets` workflow then **automatically uploads five
Windows files** from a successful, exact-commit `Prepare manual release` run.
Neither workflow creates a tag or GitHub Release. This is a change from the earlier
fully manual attachment process.

> IMPORTANT: A release becomes publicly visible **before** the automatic asset
> attachment finishes. Do not publish unless physical Windows editing/audio/export
> and project-backup acceptance is complete. If asset upload fails, the release
> remains published but incomplete: stop announcements and resolve the failed run.

## v1.5.0 three-platform publication remains blocked

The v1.5.0 featured release is planned to include Windows x64, Linux x86_64,
and native Apple Silicon/Intel macOS. Its [planned 14-file asset contract](../development/three-platform-release-candidate.md)
requires 13 platform payloads and one checksum manifest. **Do not follow
the five-asset Windows instructions below for v1.5.0 or later.** Both legacy
preparation and automatic attachment intentionally reject a stable v1.5.0+
tag using the `--windows-only-assets` release policy. An owner-created
public release could still appear online *without files* if published
prematurely; never treat an upload failure as release approval.

The owner must first have a separately completed, securely verified
three-platform preparation/attachment path, complete Mac notarization and
clean-host licensing/audio/export testing, and authorize publication under
the [v1.5.0 preflight](v1.5.0-preflight.md). Do not bypass this hold via
manual five-file uploads.

## Before publishing

1. Freeze `main`, record its complete commit SHA, and confirm the project version
   and matching `docs/releases/vX.Y.Z.md` correspond to the intended `vX.Y.Z` tag
   (replace `X.Y.Z` with the exact numeric CMake project version).
2. Require successful CI, Windows packages (including clean `windows-2022`,
   `windows-2025`, and `windows-11-arm` verification), and website checks on
   that exact SHA.
3. Complete the [integrated editing acceptance matrix](../testing/v1.4-integrated-editing-acceptance.md)
   on real Windows hardware. Test installer and portable startup, physical audio,
   preview, MP4 output, backups and re-opening copies of older projects. CI and
   hosted Windows GUI smoke tests cannot substitute for this.
4. Open **Actions → Prepare manual release**. Find a **successful** run on
   `main` with the exact frozen SHA (or manually dispatch it on that SHA's
   current `main` if none exists). Confirm it contains the unexpired
   `manual-release-assets` artifact with the five files below and
   `release-notes.md`.
5. Download the complete artifact and independently verify all four payload hashes
   against `SHA256SUMS.txt`. From the repository root, use
   `python3 packaging/verify_release_assets.py --version X.Y.Z --directory /path/to/release`.
   Check the corresponding-source archive, package notices, version and limits.
   Do not waive a failed integrity check simply because CI was green.

## Publish through GitHub's UI

1. Sign into [FrameTrellis Releases](https://github.com/sanskarIN/frametrellis/releases)
   as the repository owner, and choose **Draft a new release**.
2. Create the tag `vX.Y.Z` from `main` **only after checking `main` is still
   the exact tested SHA**. A pre-existing tag must point to that SHA.
3. Set title `FrameTrellis X.Y.Z` and paste the assembled
   `release-notes.md` into the description.
4. **Do not manually attach the files** when using the automatic uploader.
   Leave the release's asset picker empty.
5. After quality approval, leave **Pre-release** unchecked and choose **Latest**;
   click **Publish release**. This action is performed by the account owner,
   not by GitHub Actions.

## Verify the automatic upload

1. The `Attach verified release assets` workflow runs for a published, non-
   prerelease GitHub Release. It checks the release tag is the current `main`
   commit and matches CMake, then locates a successful preparation run at the
   **same exact commit SHA**. It downloads only that run's complete artifact.
2. The workflow verifies all four SHA-256 payloads, portable ZIP structure,
   SPDX version and third-party source TAR before attaching:
   - `FrameTrellis-X.Y.Z-windows-x64-setup.exe`
   - `FrameTrellis-X.Y.Z-windows-x64.zip`
   - `FrameTrellis-X.Y.Z-windows-x64.spdx.json`
   - `FrameTrellis-X.Y.Z-windows-x64-third-party-sources.tar`
   - `SHA256SUMS.txt`
3. Check the workflow succeeded and the published release under
   [FrameTrellis Releases](https://github.com/sanskarIN/frametrellis/releases)
   has exactly five complete assets with the expected file sizes.
4. `Publish website` starts only after the upload workflow succeeds, so the
   website download links refer to uploaded release files. Verify those links
   independently, then mark the release tracker complete.

If the uploader fails, **do not assume uploads will happen later**. Inspect the
failed workflow and the release asset list. It refuses to overwrite preexisting
assets, so an interrupted run with partial uploads needs deliberate cleanup
and a review before rerunning. If an artifact expired or the tag differs from
the tested `main` SHA, re-freeze, re-test and prepare matching assets.

For an emergency manual fallback only, upload all five independently verified
files yourself. Do not mix payloads from different commits or publish an
unverified file. `release-notes.md` is description text and is not an asset.

## Separation of responsibilities

- **Owner**: approves quality, creates the tag, and publishes the GitHub Release.
- **Prepare manual release**: builds/checks packages and retains the complete
  upload bundle as an Actions artifact; cannot publish a GitHub Release.
- **Attach verified release assets**: uploads files to an already published
  owner-approved release; cannot create a tag or release.
- **Publish website**: updates download links only after assets are attached.
