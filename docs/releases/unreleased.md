# Unreleased — preparing FrameTrellis 1.2.3

Changes after [FrameTrellis 1.2.2](v1.2.2.md) are recorded here until the
next stable release is verified and published.

## What's new (implemented on the development branch)

- New **Help → What's New** dialog in the desktop editor, readable entirely
  offline. It summarizes shipped changes, includes a link to full release
  notes, and can be dismissed with Escape.
- The website home page now has a **What's new** section that links to release
  history and clearly distinguishes existing features from 1.2.3 work.
- Added a GUI regression test for opening and closing the dialog.

The dialog initially documents confirmed 1.2.2 improvements. A 1.2.3-specific
highlight is shown when the application version reaches 1.2.3, and additional
1.2.3 changes should be added **only after their PRs have merged and tests pass**.

## Before publishing 1.2.3

- Reconcile these highlights and the website section with the final merged changes.
- Write `docs/releases/v1.2.3.md` from verified implementation and known limitations.
- Bump the CMake version and confirm all GUI, website and packaging checks pass.
- Keep the release unpublished until a maintainer explicitly approves publishing.
