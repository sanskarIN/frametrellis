# Timeline review (development build)

Use **File → Timeline Review…** before exporting a project, or when reviewing a sequence
with another editor. The review is read-only: it does not modify the timeline, source
files, selection, project dirty state or undo history. Opening the dialog pauses preview.

## Review a sequence

1. Open a project and make the required edits.
2. Open Timeline Review from the File menu.
3. Read the sequence duration, track and clip counts, unavailable assets, uncovered
   picture/audio frame totals and finding count.
4. Choose a category from the **Show** filter. The findings are ordered by start frame,
   then end frame and category. Use **Go to** to inspect the start of a finding that
   lies inside the sequence.
5. Open **Track summary** to see occupied frames, available picture/audio placement
   frames, transitions, unavailable clips and track visibility, mute and lock state.
6. After relinking files or changing the project, open the review again. **Refresh**
   checks file availability again without decoding the sources.

## Interpret the findings

| Finding | What the review checks | Suggested review |
| --- | --- | --- |
| No available picture placement | No available video/still clip on a visible picture track covers the range. | Inspect intentional black sections, missing sources or timeline gaps. |
| No available unmuted audio placement | No available audio-bearing clip on an unmuted track with unmuted nonzero scalar gain covers the range. | Check intended silence, music and voice placements. |
| Source file unavailable | A used source is not a readable regular file at its recorded location. | Relink or restore the media. |
| Marker beyond sequence end | A marker is strictly later than the exclusive sequence end. | Move or retain the marker intentionally. An end-boundary marker is valid. |
| Subtitle extends beyond sequence | A cue has a sampled boundary outside the sequence. | Check caption timing and intended sidecar delivery. |
| Subtitle has no sampled sequence frame | The cue has no corresponding visible frame at the project rate. | Extend or retime the cue if it is intended for burn-in. |

An uncovered interval is **not necessarily an error**. The review checks structural
coverage, not rendered black frames or measured silence. It does not decode media,
verify codecs or validate the eventual exported MP4. Existing files can still be
corrupt, changed, or unplayable. Coverage does not simulate opacity, automation,
transforms, fades, filters, transitions, sound levels or the final compositor.

Global coverage counts the union of eligible half-open intervals across tracks.
Overlapping layers do not increase the sequence-wide total. Hidden video tracks can
still contribute audio. Track locking affects editing, not playback. Unused missing
media is not counted as an unavailable used source. The review checks source
availability once per referenced asset and counts unavailable placements separately.

All frame intervals are **start-inclusive and end-exclusive** in project frames.
Caption boundaries are converted from milliseconds using the exact project rate and
the same first-sampled-frame convention as subtitle handling, including NTSC rates.
A cue may contribute two distinct findings.

## Export review reports

Use **Export JSON…** for structured review data or **Export HTML…** for a standalone
browser-readable summary. The report is generated again from the current project.
The HTML file is static, uses no external scripts or assets, and can be opened offline
or printed using a browser.

The dialog displays the earliest 200 findings; exported reports retain up to 5,000.
All summary totals remain complete. Filters operate on the retained detail rows,
not the entire set when there are more findings than the display limit.

Reports contain project, track, asset display and marker names, identifiers and
timecodes. Absolute source file paths and caption text fields are omitted. Display
names themselves may contain private information; inspect reports before sharing.
Reports do not contain video, audio or image data and are not project backups.

The exporter writes atomically and rejects destinations that would overwrite the
open project or referenced source media, directories or symbolic links.

## JSON schema version 1

The root contains:

- `schemaVersion`, `projectName`, `projectId`, `width`, `height` and `frameRate`
  (the exact rational FPS as a string).
- `coverageMode`: `nominal-available-placements`.
- `summary`: sequence duration, track/media/clip counts, coverage frame totals,
  counts by category and complete finding count.
- `tracks`: per-track metrics in project order, with stable track IDs and flags.
- `findings`: ordered detail rows, with stable category codes, display title,
  subject, object/track IDs where applicable, frame and timecode bounds and
  whether the starting frame is seekable.
- `omittedFindings`: count of additional findings not represented in the array.

Consumers should use category codes rather than display titles. Frame counts are
integers; the end is exclusive. Timecode labels follow the project's automatic
drop-frame/non-drop-frame convention and should not be parsed as wall-clock seconds.
