# Contributing to FrameTrellis

Thank you for helping! Bug reports, documentation, tests, translations and code are all welcome.
By participating you agree to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Reporting bugs and requesting features

* Search [existing issues](https://github.com/sanskarIN/frametrellis/issues) first.
* Use the issue templates. For bugs, include *Help → About → Copy diagnostics*, the steps to
  reproduce, and what you expected. Never attach private footage; describe the file instead
  (container, codecs, resolution — FrameTrellis' media tooltip shows these).
* Security problems: do **not** open a public issue; see [SECURITY.md](SECURITY.md).

## Development setup

Follow [docs/development/building.md](docs/development/building.md). The quickest loop on Linux:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

## Making changes

1. Fork the repository and create a branch from the default branch.
2. Keep each pull request focused on one change. Large changes: open an issue first to discuss
   the design.
3. Follow the existing structure (see [docs/architecture/overview.md](docs/architecture/overview.md)):
   domain logic in `core`/`timeline`/`project`/`editing` (no UI, no MLT), MLT code in
   `engine`/`media`/`playback`/`rendering`, QML and view models in `ui`.
4. Add or update tests. Bug fixes should come with a test that fails without the fix. Do not
   commit media files; generate fixtures in tests (see `tests/support/media_fixtures.h`).
5. Update documentation (user guide, roadmap, release notes in `docs/releases/unreleased.md`).
   Distinguish implemented, merged and actually released capabilities; do not describe an open
   feature PR as already available in the stable download. See the
   [feature PR review guide](docs/development/feature-pr-review.md) for validation and
   multi-branch integration.
6. Make sure these pass locally:

   ```sh
   git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run --Werror
   ctest --preset linux-debug
   ```

## Code style

* C++20, formatted with the repository's `.clang-format`; `clang-tidy` must be clean.
* Prefer value types and RAII; use `std::unique_ptr` for ownership and Qt parent ownership for
  QObjects. No raw `new` without an owner, no global mutable state.
* Report failures with `ft::Result`/`ft::Status` and a user-facing message; never swallow errors.
* Time on the timeline is integer frames (`ft::FrameCount`); use `Rational`/`FrameRate` for
  conversions, never floating point for clip positions.
* Every edit to a project goes through an undoable command.
* User-visible text is written in plain, friendly English and wrapped in `qsTr()` in QML.
* New files start with `// SPDX-License-Identifier: GPL-3.0-or-later` and a copyright line.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/) style with a clear scope:

```
feat(timeline): support ripple trimming
fix(export): report encoder initialisation failures
test(project): cover documents saved by a newer version
docs: explain proxy media
```

Write the body to explain *why*. Keep commits small and logical; each commit should build.

## Licensing of contributions

FrameTrellis is GPL-3.0-or-later. By submitting a contribution you agree that it is licensed
under the same terms. You keep your copyright; there is no CLA. Only submit work you have the
right to contribute, and do not copy code, artwork or text from other projects unless their
license is compatible and the origin is noted.
