# Contributor validation checklist

Before opening a pull request:

1. Branch from the latest `main` and keep changes focused.
2. Configure and build with the documented CMake preset for your platform.
3. Run `ctest --preset linux` or `ctest --preset windows-msys2` as appropriate.
4. For C++ changes, run `clang-format --dry-run --Werror` on changed `.cpp` and `.h` files.
5. For packaging Python changes, run `python3 -m unittest discover -s packaging/tests -v` and `ruff check`.
6. For editor changes, verify project load/save, undo/redo, media relinking and export cancellation where relevant.
7. Record the commands actually executed and any unavailable checks in the PR description; do not claim unrun checks passed.
8. Check the current branch head and all required workflows, not only older green runs.
9. When other open PRs change the same C++/QML files or shortcuts, review combined behavior and rerun CI after resolving conflicts.
10. Wait for required GitHub Actions checks and review before merging. Never bypass branch protection.

For check names, risk areas, head-SHA validation and integration of overlapping branches,
see [Reviewing and integrating feature PRs](feature-pr-review.md).

## Release hygiene

Ordinary development PRs must not modify stable release tags or publish binaries. Releases require their own audited process and artifact verification. For a security vulnerability, follow `SECURITY.md` rather than publishing details in a public issue.
