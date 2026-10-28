# Project file format (`.ftproj`)

A project is a UTF-8 JSON document. The **v2 development format** adds optional
sequence-level markers; this abridged example shows the new shape. v1 files still load.

```json
{
  "format": "org.frametrellis.project",
  "version": 2,
  "generator": "FrameTrellis 1.0.0",
  "project": {
    "id": "6d0f…",
    "name": "Holiday",
    "created": "2026-10-11T08:00:00Z",
    "settings": { "width": 1920, "height": 1080, "frameRate": "30000/1001",
                  "sampleRate": 48000, "audioChannels": 2 },
    "media": [
      { "id": "a1…", "name": "beach.mp4", "path": "C:/Footage/beach.mp4",
        "relativePath": "../Footage/beach.mp4",
        "info": { "kind": "video", "durationUs": 12012000, "hasVideo": true, "hasAudio": true,
                  "width": 3840, "height": 2160, "frameRate": "30000/1001",
                  "audioChannels": 2, "audioSampleRate": 48000, "videoCodec": "h264",
                  "audioCodec": "aac", "container": "mp4", "fileSize": 123456789,
                  "lastModified": "2026-09-30T17:12:00Z" } }
    ],
    "markers": [
      { "id": "mark1", "frame": 120, "label": "Check color grade" }
    ],
    "tracks": [
      { "id": "t1…", "kind": "video", "name": "V1", "muted": false, "hidden": false, "locked": false,
        "clips": [
          { "id": "c1…", "media": "a1…", "start": 0, "in": 30, "duration": 150,
            "gain": 1.0, "muted": false, "fadeIn": 15, "fadeOut": 0,
            "brightness": 0.0, "contrast": 1.0, "saturation": 1.0, "opacity": 1.0 },
          { "id": "c2…", "media": "a1…", "start": 150, "in": 300, "duration": 90, … } ],
        "transitions": [
          { "id": "x1…", "from": "c1…", "to": "c2…", "duration": 24 } ] }
    ]
  }
}
```

## Rules

* `start`, `in`, `duration`, `fadeIn` and `fadeOut` are **integer frames at the project frame
  rate**; `duration ≥ 1`. Rational values (`frameRate`) are strings like `"30000/1001"`.
* Tracks are listed bottom-up for compositing: later video tracks are drawn on top.
* Clips on a track are sorted by `start` and never overlap.
* `transitions` is optional. Each entry blends the clip `from` into the clip `to`, which must
  start exactly where `from` ends. The transition is centred on that cut: `duration / 2` frames
  (rounded down) before it and the rest after it. `duration ≥ 2`, and both clips need that much
  media beyond the cut. There is at most one transition per cut, and transitions at the two
  ends of a clip may not overlap.
* `markers` is optional for legacy files. Each marker has a unique identifier, a nonblank
  UTF-8 label (at most 120 UTF-16 code units), and an integer sequence frame in
  `[0, 1000000000]`. No more than 1000 markers are allowed. Markers are sorted by
  frame, then identifier, and are not attached to clips or rendered into exports.
* Project **format v2** is written by the development version to avoid silently losing
  markers when opened by v1 editors; the v1.3.0 editor cannot open these files.
  Back up projects before upgrading. Loading format v1 remains supported.
* `relativePath` is relative to the folder containing the project file. When `path` does not
  exist but the relative location does, the relative one is used (projects can be moved
  together with their media).
* Unknown fields are ignored, so newer minor additions do not break older readers of the same
  version. A document with a **higher** `version` is refused with a clear message.

## Validation

Loading treats the document as untrusted input:

* at most 64 MiB; every field is type- and range-checked;
* identifiers must be short alphanumeric strings and unique;
* every clip must reference an existing media entry, fit inside its source and be compatible
  with its track kind;
* every marker has a unique id, valid bounded frame and nonblank bounded label;
* every transition must connect two touching clips of its track and fit their media;
* the first problem is reported with its JSON path (for example
  `$.project.tracks[0].clips[3].duration: expected an integer between 1 and 1000000000`).

## Saving

Projects are written with `QSaveFile`: the new content goes to a temporary file that atomically
replaces the old one only after it was written completely. If the disk is full or the folder is
not writable, the previous file is untouched and the user sees an error.

## Autosave

While a project has unsaved changes, a copy is written every minute to the application data
folder (`autosave/<project id>.ftproj` plus a small JSON with the original path). It is removed
on a successful save and on a normal exit; after a crash, FrameTrellis offers to recover it.

## Versioning policy

The format version only increases when an older FrameTrellis could not interpret a document
correctly. Migrations from older versions are applied in `project_format::fromJson` before
validation.
