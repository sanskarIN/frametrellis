# Unreleased

Changes since [FrameTrellis 1.0.0](v1.0.0.md) are collected here and become the notes of the next
release.

## Added
- Duplicate a selected timeline clip with Ctrl+D, the Edit menu or timeline toolbar. The copy
  retains source trims and adjustments, is appended without overwriting clips, and can be undone
  in one step.
- A container image of the headless renderer, `ghcr.io/sanskarin/frametrellis-render`, in
  GitHub Packages: it renders FrameTrellis projects to MP4 on servers and in CI pipelines. Each
  image is checked with the installation self-test and a real render before it is published.
