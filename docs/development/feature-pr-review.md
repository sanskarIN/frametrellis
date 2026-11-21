# Reviewing and integrating feature pull requests

FrameTrellis protects `main` and develops new changes through focused PRs.
**An open PR is not a released feature.** A branch may contain working source
changes while its Linux, Windows or packaging tests are still queued or failing.
Document release availability only after the PR is integrated and the release
has been audited and published.

This guide is for contributors and maintainers who review overlapping
C++/Qt/QML changes, particularly edits affecting the same QML menu, timeline
controller, project format or render path.

## Prepare a reviewable PR

1. Start from a recent `main` and define one behavior or one bug fix.
2. Keep unrelated fixes in separate branches. A PR should include the code,
   a focused regression test and the user/developer documentation it needs.
3. Explicitly state expected behavior, failure behavior and undo/redo effects.
   Identify where file data, source media, project format or export outputs
   could be affected.
4. Update `docs/releases/unreleased.md` **only** to describe changes that
   will ship after merge, without implying a release has been published.
5. Avoid commits that modify release tags, publish binaries or circumvent
   branch-protection checks in an ordinary feature PR.

## Local validation by change type

Run the project build matching your installed dependencies as described in
[building.md](building.md). Use the named CMake and CTest presets, not guessed
build-directory paths.

| Change | Minimum practical review |
|---|---|
| Core, timeline, project serialization | Unit tests for valid and invalid input; exact unchanged state on rejection |
| Undoable editing command | Undo and redo restore the entire expected timeline; invalid edits do not create partial changes |
| QML actions, shortcuts, dialogs | GUI test for actual action and disabled state; update the in-app shortcut list and user guide |
| Media import, playback, compositing | Engine/integration tests with generated media, plus a real preview test where appropriate |
| Render/export | Validate output media and dimensions, cancellation, destination safety and failed-export cleanup |
| Packaging or distribution | Package scripts, module-path checks, verification on the supported Windows runners |
| Documentation or website | Build the documentation and check internal links; identify whether statements describe stable or unreleased features |

Examples on Linux (after configuring as in the building guide):

```sh
cmake --build --preset linux-debug
ctest --preset linux-debug -L unit
ctest --preset linux-debug -L integration
ctest --preset linux-debug -L gui
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror
```

For Windows with MSYS2 UCRT64:

```sh
cmake --build --preset windows-msys2
ctest --preset windows-msys2
```

For packaging Python edits:

```sh
python3 -m unittest discover -s packaging/tests -v
ruff check
```

If a command was not run because the environment lacks Qt, MLT or Windows,
record it as **not run**; do not check a box or call the PR tested merely
because the source files were committed.

## What GitHub Actions verifies

The checked-in workflows are the source of truth; job names and rulesets
can change.

| Workflow | Relevant checks |
|---|---|
| **CI** | Formatting, Packaging scripts, Linux GCC debug with warnings as errors, Linux AddressSanitizer + UBSan, Windows MSYS2 UCRT64 |
| **Windows packages** | Build ZIP/installer and verify clean-runner behavior on Windows 2022, Windows 2025 and Windows 11 ARM64 (x64 emulation) |
| **Website** | Builds pages and verifies internal/documentation/download links; triggers only for listed doc/website paths |
| **Container image** | Container build/verification path; inspect its workflow for scope |
| **Release** | Separate publication process; a green ordinary feature PR does not itself publish a release |

A *queued* or *in-progress* check is not passing. A completed job with
`failure`, `cancelled` or `skipped` must not be represented as a successful
test. Read the failed job log and fix the cause, then verify the new **head
SHA**; an older green run on a prior commit is not evidence for the latest
branch head.

The [branch-protection guide](main-branch-protection.md) describes suggested
required checks. Always use the repository's currently configured rules.

## Integrating branches that touch the same files

Several features may independently modify `Main.qml`, `TrackHeader.qml`,
`TimelineController` or the same GUI test. They can all pass CI in isolation
yet conflict or misbehave when combined.

1. Review the diff against **current `main`**, not only the branch's original
   base. Check whether an equivalent feature is already present.
2. Merge only after required checks and review; do not bypass protection to
   meet a PR-count or release-date target.
3. Update remaining PRs against the new `main`. Resolve conflicts
   intentionally; do not blindly accept one side of a QML menu merge.
4. Confirm shortcut uniqueness and dialog guards when two PRs introduce
   menu actions. Ensure new QML files are registered in `src/ui/CMakeLists.txt`.
5. Re-run relevant unit, GUI, integration and package workflows for the
   **updated head**, including undo/redo, relinking and export paths if affected.
6. Only then update a future release checklist and consider publication.

If an opened PR is obsolete, close it with an explanation rather than
merging an unnecessary duplicate. PR count is a history metric, not proof
of feature quality.

## Release boundary

The latest published release is what users can download, not the sum of
all open PRs. Release notes and the download page must not promise new
features until a tagged, audited release exists. See
[release instructions](../releases/README.md) and the
[contribution checklist](contribution-checklist.md).

Do not include personal paths, credentials or private source media in
CI logs, screenshots or documentation examples.
