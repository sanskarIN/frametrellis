# Distribution channels

FrameTrellis is published as GitHub release assets: a Windows installer, a portable ZIP,
`SHA256SUMS.txt`, an SPDX software bill of materials and an archive with the source of every
bundled component. The [release workflow](../../.github/workflows/release.yml) builds and
verifies all of them. This page records which other channels were considered, when each one
makes sense, and what it needs.

The rule for every channel: publish only a package that has been built and verified by the
release workflow, and never make a channel the only way to get a release.

## Summary

| Channel | Platform | Decision | Precondition |
|---|---|---|---|
| GitHub Releases | Windows | Used from 1.0.0 | None |
| [GitHub Packages](https://github.com/sanskarIN/frametrellis/pkgs/container/frametrellis-render) (container registry) | Linux x86-64 | Used from 1.0.0 for the headless renderer | An image built and checked in CI |
| [WinGet](https://learn.microsoft.com/windows/package-manager/) | Windows | Submit after 1.0.0 is published | A published, verified release |
| [Flathub](https://flathub.org/) | Linux | Planned | A Flatpak that is built and verified in CI |
| [Homebrew](https://brew.sh/) (cask) | macOS | Planned | A verified macOS build and `.dmg` |
| npm, NuGet, PyPI and similar registries | — | Not used | FrameTrellis is an application, not a library |

## WinGet

WinGet installs from a manifest in the community repository
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs). The manifest points to the
installer on the GitHub release and records its SHA-256 checksum, so nothing is re-hosted and the
release assets stay the single source.

The installer already meets WinGet's requirements:

- It is an Inno Setup installer (`InstallerType: inno`) with a stable `AppId`, so upgrades
  replace the previous version and the uninstaller is registered.
- It installs per user without administrator rights by default (`Scope: user`) and supports a
  machine-wide installation (`/ALLUSERS`, `Scope: machine`).
- It runs unattended with `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`, which the clean-machine
  verification exercises on every build.

What it needs:

1. A published release. The manifest needs the final download URL and checksum, which exist only
   after the release workflow has run.
2. A manifest for the version (a package identifier such as `Sanskar.FrameTrellis`, the
   version, installer URL, SHA-256, scope, license `GPL-3.0-or-later`, publisher and
   description), validated with `winget validate --manifest <folder>` and tested with
   `winget install --manifest <folder>` on a clean machine.
3. A pull request to `microsoft/winget-pkgs` from the maintainer's account. Later versions can be
   submitted with [`wingetcreate update`](https://github.com/microsoft/winget-create).

The packages are not code-signed. WinGet accepts unsigned installers; code signing (planned for
1.1) is still worth doing, because SmartScreen warns about unsigned installers that people
download directly.

## Flathub

Flathub builds the application on its own infrastructure from a manifest that lists every
dependency (Qt, MLT, FFmpeg, SDL 2) with exact source URLs and checksums. FrameTrellis does not
have a Linux package yet. The plan is to add a Flatpak (or AppImage) build to CI first, verify it
on clean Linux runners the same way the Windows packages are verified, publish it as a release
asset, and only then submit the manifest to Flathub.

## Homebrew

A Homebrew cask installs a macOS `.dmg` from the release. There is no macOS build yet: it needs a
macOS CI job, a verified `.dmg` and, in practice, notarisation, because macOS refuses to open
unsigned applications downloaded from the internet. A cask will be submitted after that.

## GitHub Packages

The renderer, `frametrellis-render`, is useful on its own: it renders FrameTrellis projects
without a display, on servers and in CI pipelines. It is published as a container image,
`ghcr.io/sanskarin/frametrellis-render`, by the
[container workflow](../../.github/workflows/container.yml) whenever a version tag is pushed or
a release is published in the web interface. Before publishing, the workflow runs the
installation self-test inside the image and renders the project it saves from a mounted folder
as an unprivileged user. Pull requests that touch the code or the image build and check it
without publishing. How to use it is described in
[Rendering in a container](../user-guide/container.md).

The image for a version that was tagged before the workflow existed (such as 1.0.0) is built
from that tag's source by running the workflow by hand with the tag as input.

## Package registries

FrameTrellis is a desktop application. It has no separately maintained library, so there is no
npm, NuGet or PyPI package, and none should be created for the application itself.
