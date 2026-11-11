# Verify a manual-release asset bundle

Run this gate **after** downloading and extracting the complete `manual-release-assets`
artifact, and **before** uploading anything to GitHub Releases.

From the repository root:

```sh
python3 packaging/verify_release_assets.py --version 1.4.0 --directory /path/to/extracted-assets
```

The directory must contain exactly the four publishable binary/source payloads,
`SHA256SUMS.txt`, and optionally `release-notes.md`.

The validator checks:

- Exact filenames and a four-entry SHA-256 manifest without duplicates or extra files.
- Independently streamed hashes for the installer, portable ZIP, SPDX JSON and corresponding-source TAR.
- Full portable ZIP CRC integrity, an actual `frametrellis.exe` member, and no archive path traversal or symlinks.
- SPDX 2.3 metadata containing a `FrameTrellis` package matching the requested version.
- Corresponding-source TAR member safety and the presence of a package inventory and source packages.

It **does not** establish the safety of executing the installer, the completeness
or license compliance of every corresponding-source archive, code-signing, or
real hardware/audio/video/MP4 editing quality. Continue the physical desktop
acceptance matrix and direct owner-published release procedure even if this
script succeeds. Never merge changes to the frozen release source commit without
re-running the exact-commit release acceptance gates.

For isolated local development tests:

```sh
python3 -m unittest discover -s packaging/tests -p 'test_verify_release_assets.py' -v
```
