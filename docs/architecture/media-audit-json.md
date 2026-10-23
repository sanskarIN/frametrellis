# Media audit JSON: schema v1 (proposed for FrameTrellis 1.4)

This is a **local, read-only report**. It is not the FrameTrellis `.ftproj`
project format and does not represent a stable feature until its PR is
integrated and released.

## Purpose

The **File → Export Media Audit (JSON)…** action reports the actual
availability of source files at the moment of export and their use in the
timeline. It does **not** probe, decode, hash, upload, relink or modify media.

```json
{
  "schemaVersion": 1,
  "projectName": "Example edit",
  "projectId": "example-id",
  "checkedAtUtc": "2026-10-23T08:00:00Z",
  "summary": {
    "total": 2,
    "online": 1,
    "missing": 1,
    "changed": 0,
    "unused": 1,
    "clipUses": 2
  },
  "assets": [
    {
      "assetId": "example-video",
      "name": "first clip.mp4",
      "sourcePath": "/project/first clip.mp4",
      "kind": "video",
      "status": "online",
      "clipUses": 2,
      "storedBytes": "12345678",
      "currentBytes": "12345678"
    },
    {
      "assetId": "example-missing",
      "name": "lost clip.wav",
      "sourcePath": "/project/lost clip.wav",
      "kind": "audio",
      "status": "missing",
      "clipUses": 0,
      "storedBytes": "12000"
    }
  ]
}
```

The example is illustrative. Asset ordering follows the current project
media-bin order; use `assetId`, not an array index, as an identity key.

## Semantics

- `schemaVersion`: integer, currently **1**. Importers should reject
  unsupported schema versions instead of guessing field semantics.
- `checkedAtUtc`: UTC timestamp when the report was assembled.
- `summary.total`: number of imported media assets, including unused ones.
- `summary.online`: sources that exist as regular files and match the stored
  positive file size and recorded modification time (second resolution).
- `summary.missing`: source path does not currently resolve to a regular file.
- `summary.changed`: a present file has a different size or stored timestamp.
  **This is a heuristic, not a cryptographic integrity check.** Files can
  change without changes to either attribute.
- `summary.unused`: imported sources with no clip referencing them.
- `summary.clipUses`: number of clips across all tracks, not number of unique
  source files. Each asset's `clipUses` counts clips referencing its ID.
- `kind`: `video`, `audio` or `image` as recorded during import.
- `storedBytes` and `currentBytes`: decimal **strings**, not floating-point
  JSON numbers, to preserve 64-bit sizes. `currentBytes` is omitted if missing.
- `storedModifiedUtc` and `currentModifiedUtc`: UTC timestamps from the
  saved media probe and live file system; the live field is absent for
  missing sources and the stored field is absent if unknown.
- `changeReasons`: array of `"size"` and/or `"modified"` when a live
  source differs from its recorded size or timestamp; an empty array means
  no such discrepancy was detected. A missing file reports `status: "missing"`
  and has no file-size/timestamp difference to compare.
- `sourcePath`: locally stored absolute source path. **Treat this field as
  sensitive** and redact it before publishing reports externally.

The report does not mark a project as modified, alter the undo history, or
copy any source media. Output is UTF-8 and committed atomically. It refuses
to overwrite a referenced project or source-media path, including resolved
symbolic-link aliases on supported platforms.

## Compatibility

An exporter may add optional descriptive fields in the future, but any
breaking change in field meaning or type must increment `schemaVersion`.
This schema does not change existing project-file structure or increase
the required format version.

See [project-format](project-format.md) for editable project documents,
the [user guide](../user-guide/README.md) for the UI, and the
[post-v1.3 planning PR](https://github.com/sanskarIN/frametrellis/pull/64).
