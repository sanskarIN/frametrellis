# Project file format (`.ftproj`)

A project is a UTF-8 JSON document. The **v2 development format** adds optional
sequence-level markers; this abridged example shows that shape. The **v3 development
format** additionally preserves clip volume automation. **v4** adds opacity curves,
and **v5** adds position/scale/rotation curves. The reader supports all five versions.

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
* Clips may contain `audioKeyframes` in **format v3**:
  `[{"sourceFrame":30,"decibels":0.0},{"sourceFrame":90,"decibels":-12.0}]`.
  Source frames use the project frame rate, are integers in `[0, 1000000000]`, and
  must be strictly increasing and unique. Levels must be finite numbers in
  `[-60, 12]`. At most 1000 points per clip are accepted; the clip's media must have audio.
* Keyframes are anchored to the source, independently for each clip. They remain in
  the file outside the current trim, so trimming back out restores the envelope.
  Levels interpolate linearly in dB and hold at the first/last point outside the curve.
  An absent or empty curve is neutral (0 dB). Automation combines with clip gain,
  fades and mute; it does not change track placement or media files.
* Clips may contain `opacityKeyframes` in **format v4**:
  `[{"sourceFrame":30,"opacity":0.0},{"sourceFrame":90,"opacity":1.0}]`.
  The same source-frame bounds, strict ordering, unique-frame rule and 1000-point
  limit apply. Values must be finite numbers in `[0, 1]`; the source must have video
  (including still images). Values interpolate linearly and hold at the endpoints.
  An empty curve is neutral (factor 1). The curve multiplies source alpha, scalar
  opacity and fades. Sound is independent of opacity.
* Opacity curves may remain on a video source temporarily moved to an audio track;
  they are dormant there and reappear if it returns to a video track. Creating or
  updating an opacity point requires an unlocked video track. Reset Picture clears
  scalar picture adjustments, opacity points and transform points together; audio and fades remain.
* Clips may contain `transformKeyframes` in **format v5**:
  `[{"sourceFrame":30,"x":0.0,"y":0.0,"scale":0.5,"rotation":90.0}]`.
  Each field is required and finite. `sourceFrame` follows the same integer bounds,
  strict increasing order and 1000-point limit as the other curves. `x` and `y`
  are offsets from the canvas centre in canvas widths/heights, within `[-4, 4]`;
  positive values move right/down. Uniform `scale` is within `[0.01, 10]` and applies
  to the aspect-fitted picture. Clockwise `rotation` is within `[-3600, 3600]` degrees
  about that picture's centre. All four coordinates interpolate linearly. Angles
  do not wrap: 0→720 is two full clockwise turns. Endpoints hold outside the curve;
  one point is a constant transform and an empty curve is identity. The curve is
  absolute geometry, independent of scalar picture adjustments, opacity and audio.
* Transform curves remain source-anchored through move/trim/split/roll/slip/duplicate,
  including hidden points. They may remain dormant on video sources temporarily
  moved to audio tracks. Audio-only sources cannot contain transform curves.
  Attribute paste retains destination curves. Clearing transforms restores identity;
  Reset Picture clears all picture curves atomically and preserves audio/fades.
* The writer chooses the minimum required development format: v5 if any clip has
  transform points, otherwise v4 if it has opacity points, otherwise v3 if it has
  volume points, otherwise v2. Clearing curves can lower the next save's version.
  Older readers refuse unsupported versions rather than losing automation.
  Nonempty audio curves before v3, opacity curves before v4, and transform curves
  before v5 are rejected as inconsistent.
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
