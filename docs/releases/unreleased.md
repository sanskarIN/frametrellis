# Unreleased

Changes after [FrameTrellis 1.2.2](v1.2.2.md) will be collected here
for a future release. For the latest fast-export work, see the
[v1.2.2 release notes](v1.2.2.md).

## Timeline workflow improvements (in review)

- Add **View → Zoom to Selection** (Ctrl+Shift+0) and a selection-aware timeline button
  that frames all selected clips without moving media or changing the project.
- Reject invalid, non-finite timeline zoom values instead of allowing NaN/Inf to damage the view.
- Add a real QML GUI regression test for selection bounds, viewport placement, keyboard
  shortcuts and unchanged project/undo state.

These are development changes; they are not yet part of any stable release.
