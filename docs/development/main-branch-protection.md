# Protecting FrameTrellis `main`

Repository: https://github.com/sanskarIN/frametrellis

Branch protection must be enforced by GitHub's repository settings/rulesets, not by
a CI-only check. A workflow can report an error **after** an unwanted direct push,
but cannot retroactively stop the push. Verify the "Protected" status in the
GitHub Branches page before reporting this task as complete.

## Recommended GitHub ruleset

Using a repository administrator account:

1. Open **Settings → Rules → Rulesets → New branch ruleset** (or the classic
   **Settings → Branches → Add branch protection rule**, when appropriate).
2. Name the ruleset `Protect main`, make it **Active**, and target
   `refs/heads/main` (default branch). Do not apply it to release tags.
3. Require a pull request before merging. For a sole maintainer, zero required
   approvals avoids an impossible self-review requirement; for a team, consider
   one independent review and dismiss stale reviews.
4. Require status checks to pass **before merging**. Suggested checks from the
   always-running pull-request workflows:
   - `Formatting`
   - `Packaging scripts`
   - `Linux (GCC, debug, warnings as errors)`
   - `Linux (AddressSanitizer + UBSan)`
   - `Windows (MSYS2 UCRT64)`
   - `Build packages`
   - `Verify on windows-2022`
   - `Verify on windows-2025`
   - `Verify on windows-11-arm`
5. Block force pushes and branch deletion. Do not create a permanent
   administrator bypass for ordinary development. Require resolution of
   review conversations where this fits the contributor workflow.
6. Enable the rule and verify in **Settings → Rules → Rulesets**, then confirm
   that the **Branches** page reports `main` protected.

Select the checks as GitHub offers them: display names and job contexts may
differ, so always pick contexts that are actually reported by the repository's
CI and Windows package workflows. The website workflow is path-filtered and
should not be made universally required unless it runs for every PR.

## Ongoing release workflow

Contributions should be made on feature/release branches, validated by GitHub
Actions, and merged through pull requests. Publish release tags from an audited
commit on `main`. Do not bypass checks just to publish a version. Branch
protection cannot substitute for release/package verification and does not
prevent developers with administrative ruleset-editing privileges from changing
repository rules themselves.

**Status:** This file describes the intended policy. It does **not** activate
branch protection. That requires GitHub administration access through an
authorized settings interface or an API credential with Administration:write.
