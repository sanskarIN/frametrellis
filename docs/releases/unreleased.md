# Unreleased — v1.2.1 maintenance (in progress)

Changes made after [FrameTrellis 1.2.0](v1.2.0.md) are collected here for the next
maintenance release. The published [v1.2.0 release](v1.2.0.md) is unchanged.

## Work started

- Ensure failure to load the QML root window follows the normal controller, thread-pool
  and MLT shutdown path instead of returning before cleanup.
- Log startup and shutdown checkpoints to help distinguish initialization, rendering
  and teardown failures.
- Capture portable/installed editor stderr and retain app logs on crashes during Windows
  package verification.
- Exercise the portable x64 editor twice independently on Windows 11 ARM64. Each run must
  produce a valid screenshot and succeed; a successful rerun never erases a failure.
- Record the implementation phases in the [v1.2.1 development plan](../development/v1.2.1-plan.md).

## Still required before a release

Root-cause investigation, successful CI on the maintenance branch, verification of startup
stability across environments, and explicit release approval. An intermittent Qt6Core.dll
access violation was observed on Windows ARM64 during v1.2.0 packaging checks but was not
reproduced on the immediate job rerun. The changes above **do not prove this crash is fixed**.

WinGet packaging and updates are deferred to a possible v1.2.2 milestone, not v1.2.1.
