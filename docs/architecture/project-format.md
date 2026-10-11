# Project file format (`.ftproj`)

A project is a UTF-8 JSON document. Version 1 looks like this (abridged):

```json
{
  "format": "org.frametrellis.project",
  "version": 1,
  "generator": "FrameTrellis 0.1.0",
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
    "tracks": [
      { "id": "t1…", "kind": "video", "name": "V1", "muted": false, "hidden": false, "locked": false,
        "clips": [
          { "id": "c1…", "media": "a1…", "start": 0, "in": 30, "duration": 150,
            "gain": 1.0, "muted": false, "fadeIn": 15, "fadeOut": 0,
            "brightness": 0.0, "contrast": 1.0, "saturation": 1.0, "opacity": 1.0 } ] }
    ]
  }
}
```

## Rules

* `start`, `in`, `duration`, `fadeIn` and `fadeOut` are **integer frames at the project frame
  rate**; `duration ≥ 1`. Rational values (`frameRate`) are strings like `"30000/1001"`.
* Tracks are listed bottom-up for compositing: later video tracks are drawn on top.
* Clips on a track are sorted by `start` and never overlap.
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
