# FrameTrellis user guide

FrameTrellis is a local-first video editor: your media stays on your computer and is never
uploaded. This guide covers everything the current version can do.

- [Installing FrameTrellis](installation.md)
- [The workspace](#the-workspace)
- [Projects](#projects)
- [Importing media](#importing-media)
- [Editing on the timeline](#editing-on-the-timeline)
- [Batch editing and clip attributes](batch-editing.md)
- [Title overlays](#title-overlays)
- [Transitions](#transitions)
- [Adjusting clips](#adjusting-clips)
- [Playback](#playback)
- [Exporting](#exporting)
- [Exporting an edit decision list (development preview)](#exporting-a-picture-edit-decision-list-development-preview)
- [Faster export profiles](faster-export.md)
- [Missing media and recovery](#missing-media-and-recovery)
- [Troubleshooting](#troubleshooting)
- [Keyboard shortcuts](keyboard-shortcuts.md)
- [Supported formats](formats.md)

## The workspace

![Workspace](../images/workspace.png)

| Area | Purpose |
|---|---|
| **Media** (left) | Files in the project. Drag a tile to the timeline or double-click it to append it. |
| **Program** (centre) | The edited sequence at the playhead, with transport controls and timecode. |
| **Clip / Project** (right) | Properties of the selected clip, or a project summary. |
| **Timeline** (bottom) | Tracks and clips. Video tracks (V1, V2…) are drawn bottom-up: V2 covers V1. Audio tracks (A1, A2…) are mixed together. |

Panels can be resized by dragging the gaps between them. *View* switches between dark and light
themes and between compact, normal and tall tracks.

## Projects

* **New project** (Ctrl+N): choose a name and a format (resolution and frame rate). The frame rate
  can only be changed while the timeline is empty; the export resolution can be chosen later.
* **Save** (Ctrl+S) writes a `.ftproj` file. Saving is atomic: if it fails (for example, the disk
  is full), the previous file is kept. FrameTrellis also refuses to save a project over
  a referenced source media file, including through an alternate path or symlink.
* **Open** (Ctrl+O) and *File → Open Recent* reopen projects. FrameTrellis asks before discarding
  unsaved changes.

Project files only reference your media; keep the media files when you move or share a project.
If you move the project folder together with the media, FrameTrellis finds them again.

## Importing media

In development builds with the **planned v1.4 folder import feature**, use
**File → Import Media Folder…** to select a directory. FrameTrellis searches
its subfolders for up to **500 supported media files** and imports them using
the normal background decoder checks. Files with unsupported suffixes and
symbolic-link file entries are ignored; directory symlinks are not traversed.
Files already in the media bin are skipped. A safety limit also stops discovery
after 10,000 scanned files, and large folders are reported as truncated.
No files are moved, copied, renamed or deleted. If the folder contains more
than the safety limit, split your import into smaller subfolders.

This is a **future v1.4 candidate**, not included in the v1.3.0 stable
release. Folder discovery is currently a bounded, synchronous step, so prefer
small batches on slow network drives.


Use *File → Import Media…* (Ctrl+I), the import button on the media panel, or drop files from your
file manager onto the media panel or the timeline. Files are checked in the background: those
FFmpeg cannot decode are reported and not added. Importing a file that is already in the project
is ignored. Media identities are checked by canonical path, so a second name or relative-path
alias to the same file cannot create a duplicate entry. Relinking a media tile to a file already
used by another tile is rejected.

Supported: most video formats FFmpeg decodes (MP4, MOV, MKV, WebM, AVI, MTS, …), audio (WAV, MP3,
AAC/M4A, FLAC, Ogg, Opus, …) and still images (PNG, JPEG, BMP, GIF, TIFF, WebP and others your Qt
installation supports). Animated GIFs are treated as video. See [Supported formats](formats.md)
for the full list.

Right-click a tile to add it to the timeline, show it in its folder, relink it or remove it from
the project (removing never deletes the file; media in use must be removed from the timeline first).

For a cleaner media bin, **File → Remove Unused Media from Project** removes
imported media references that are not used by any timeline clip. Original
files remain unchanged on disk, and **Ctrl+Z** restores the removed references
in their original order. Media in use is always preserved. This is a
**proposed v1.4 feature**, not included in the v1.3.0 stable download.


Use the **Search media** field at the top of the media bin to find files by name or path,
without changing the project. The adjacent filter shows all assets, just videos, audio,
still images or offline files. Search is case-insensitive, combines with the chosen kind,
and updates the visible tiles immediately. Select **All** and clear the search box to see
every imported file again. Filtering never removes files from the project.

## Editing on the timeline

* **Add clips:** drag a tile onto a track (a ghost shows where it lands and turns red where it
  cannot go), or double-click a tile to append it to the first suitable track. Video and image
  files go on video tracks; audio files go on audio tracks. Video files with sound can also be
  placed on audio tracks to use only their sound.
* **Select:** click a clip; Ctrl+click adds to the selection. Click an empty part of a track to
  clear the selection and move the playhead.
* **Move:** drag a clip left/right or onto another track of the same kind. Moves that would
  overlap another clip are refused — nothing is ever overwritten.
* **Trim:** drag the left or right edge of a clip. You cannot trim beyond the start or end of the
  source media.
* **Slip (v1.4 development):** select one or more video or audio clips, then open
  *Edit → Slip Selected Clips (Source)*. Move source content one frame with
  **Ctrl+Alt+Left / Right**, or ten frames with **Ctrl+Alt+Shift+Left / Right**.
  The clip stays in the same timeline position and keeps the same duration,
  fades and effects; only its source in/out points change. Every selection
  slips atomically in one undo step. There must be available source material
  before/after the current source trim. Still images, locked tracks and slips
  that would remove a transition's source handles are rejected without edits.
  Trimming the end of a clip may create room to slip it later.
  This is not yet in the v1.3.0 stable release.
* **Roll a cut (v1.4 development):** select the outgoing clip (the one before a cut)
  and choose *Edit → Roll Cut After Selected Clip*. For a touching time-based clip,
  the inspector also offers **Roll Following Cut** controls. Rolling earlier shortens
  the outgoing clip and reveals more of the incoming source; rolling later does the
  opposite. The combined duration and surrounding timeline edges stay fixed.
  A roll preserves clip properties, fades and transitions and is undoable in one step.
  An invalid roll (exhausted source media, invalid fades or transition handles,
  separated clips, still images or a locked track) fails without changing either clip.
  This is not part of the stable v1.3.0 package.
* **Split:** move the playhead and press **S**. With clips selected, only those are split;
  otherwise every unlocked clip under the playhead is split.
* **Duplicate:** select one clip and press **Ctrl+D** (or use the timeline copy button). The copy
  retains its source trim and adjustments, and is appended to the end of the same track without
  moving other clips. Transitions are not copied; undo removes the duplicate in one step.
* **Delete:** **Delete** removes the selected clips and leaves a gap; **Shift+Delete** (ripple
  delete) removes one clip and moves the following clips on that track left to close the gap.
* **Snapping** (N) aligns clip edges with other clips and the playhead.
* **Zoom** with + and −, Ctrl+mouse wheel, the zoom slider, or *Fit* to see the whole sequence.
* **Go to timecode** with Ctrl+G (Playback menu): enter a frame-accurate timecode, such as
  `00:00:12:08`. Invalid or out-of-sequence positions are rejected without moving the playhead.
* **Tracks:** add tracks with *+ Video* / *+ Audio*; each track can be hidden (video), muted or
  locked. Double-click a track name to rename it; right-click it to remove the track.

Every change can be undone (Ctrl+Z) and redone (Ctrl+Shift+Z).

Clips on audio tracks show the waveform of their sound, scaled by the clip's volume. The first
time a file is used, FrameTrellis reads its sound in the background (a few seconds for a long
file) and keeps the result in its cache, so the waveform appears instantly afterwards.

## Timeline markers (v1.4 development preview)

Use **Playback → Add Timeline Marker** (`Shift+M`) or the **+ Marker** button above the
timeline to attach a named review cue to an exact project frame. Markers are shown as diamonds
on the time ruler and remain visible as you zoom and scroll. Click one to seek to it;
double-click or right-click to open its editor. You can change its label or frame, or delete it.
Each change is undoable. Use `Ctrl+PageUp` and `Ctrl+PageDown` to jump to earlier/later markers.

To prepare video chapters for a description, add a first marker at frame **0**, then
place later markers at least one whole second apart. Use **Playback → Copy Markers as Video
Chapters** to copy lines in `HH:MM:SS Label` format. This uses the exact project frame rate
to convert frame positions into playback clock seconds, keeps Unicode names, and refuses
ambiguous timestamps. Paste the result into your video description, then review chapter
requirements for your publishing service. The action does not modify the project or files.

Markers are sequence-level notes: they do not change picture, sound, clip trims or export,
and they do **not** automatically follow clips moved by edits. The edit-point snapping
system also snaps clips to markers when snapping is on. Cues saved beyond the current end
of a shortened sequence remain in the project but cannot be navigated to until media again
covers their position.

**Compatibility:** Saving in this development version uses project format v2. FrameTrellis
v1.3 and older reject these newer project files, even if they contain no markers. Keep a
backup if you may need to return to an older editor. This feature is not in v1.3.0 stable.

### Export review markers as chapters

In this development build, choose **Playback → Copy Markers as Video Chapters** to
copy lines such as `00:01:05 Color grade` for a video description. The first
chapter must start at frame zero, and no two markers may map to the same whole second.
This action does not change the project or its edit history.

Choose **Playback → Copy Markers as WebVTT Chapters** for an HTML5 chapter track.
WebVTT chapter cues include frame-derived millisecond time boundaries, so they can
be used as chapter metadata in compatible web video players. The project must
contain a nonempty timeline, the first chapter must begin at frame zero, and all
chapter starts must precede the sequence end with strictly increasing frames.
Marker names are escaped as plain text, not interpreted as WebVTT markup.
Paste the copied output into a UTF-8 `.vtt` text file; attach it as a
`<track kind="chapters">` to a compatible HTML5 video player. This does not
burn anything into the exported video or upload data to any service.

Both chapter workflows are **v1.4 proposals**, not features of the v1.3.0
stable Windows package.

## Title overlays

Choose **File → Create Title…** after saving your project. Enter the text, choose a font size and
colour, and press **Create title**. FrameTrellis draws the text with a subtle shadow onto a
transparent PNG at the project's resolution. It stores that PNG in a **FrameTrellis Titles**
folder beside your `.ftproj` file and imports it as a normal image.

The title is initially placed at the end of the **uppermost unlocked video track**. Drag it to
the part of the sequence where it should appear, and trim its edges to control how long it is
visible. Keep titles on a track **above** footage you want visible behind the lettering.

These are **raster title overlays**, not editable text layers: create a new image if you need to
change the wording, font size or colour. Keep the title folder together with the project when
sharing it. Title creation currently supports dimensions up to 4096 × 4096 pixels.

## Transitions

A transition blends one clip into the next where they meet: a **dissolve** on video tracks and
a **crossfade** of the sound on every track.

1. Make sure the two clips touch: the second clip must start exactly where the first one ends
   (with snapping on, drag it until it snaps to the end of the first).
2. Select the first clip and press **T** (*Edit → Add Transition*), or choose *Add transition to
   next clip* in the inspector. The transition is one second long, or shorter if the media does
   not allow that.
3. Click the transition (the box with the bow tie over the cut) to select it. The inspector shows
   its length, which you can change with the slider, and a button to remove it. **Delete** also
   removes it.

![A dissolve between two clips on track V3, selected, with its length in the inspector](../images/transition.png)

A transition is centred on the cut, so both clips need unused media around it: the first clip
must continue after its end and the second must start earlier in its file. If a clip already
uses its whole file, trim it a little first. Still images can always be blended. Moving a clip
away from the cut or trimming it at the cut removes the transition; undo brings it back.

## Adjusting clips

Select one clip to see its properties:

* **Audio:** volume (−∞ to +12 dB) and mute.
* **Fades:** fade-in and fade-out length. Video fades reveal the track below (black on V1).
* **Picture:** opacity, brightness, contrast and saturation. Double-click a slider to reset it.
  Brightness, contrast and saturation need FFmpeg's `eq` filter; if your installation lacks it,
  the inspector says so.

Continuous slider changes become a single undo step.

## Playback

Space plays and pauses; Left/Right step one frame; Shift+Left/Right move one second; Up/Down jump
to the previous/next edit point; Home/End go to the start/end. Clicking or dragging on the ruler
moves the playhead. If no audio device is available, the preview plays silently.

## Media audit reports (planned v1.4 feature)

In development builds that include the v1.4 media-audit feature, choose
**File → Export Media Audit (JSON)…** to save a read-only report of the media
bin. It records the current file availability, size and modification-time
changes, clip-use counts and a [versioned schema](../architecture/media-audit-json.md)
(`schemaVersion: 1`).

The report never decodes, relocates, replaces or edits source files; it also
does not change project undo/dirty state. A file with the same byte count may
still be flagged as *changed* when its saved modification time differs. The
report does not calculate cryptographic checksums and is not a media-authenticity
guarantee. **Full local source-file paths are included** and may be sensitive:
inspect or redact the JSON before sharing it publicly.

This is a **proposed v1.4 feature**, not available in the current v1.3.0
stable download.

## Exporting

*File → Export…* (Ctrl+E):

1. Choose a preset (*Match project*, *1080p*, *720p*, *2160p*, *Fast draft*).
2. Choose where to save the `.mp4` file.
3. Optionally adjust quality (lower CRF = better quality, bigger file), encoder speed and audio.
4. Click **Export**.

The export runs in the background — you can keep editing; it uses a snapshot of the project.
The status bar shows progress. The result is written to a temporary file first and only replaces
an existing file once the export succeeded. FrameTrellis refuses to overwrite your source media
or the project file. Exports need all media to be available (relink offline files first).

Output: MP4 with H.264 video (yuv420p) and AAC audio, which plays almost everywhere.
For v1.2.2 and later, **Quick export** and **Turbo preview** optimize encoder speed.
Enable **Do not upscale smaller projects** to preserve native size instead of magnifying
small projects, and watch the measured frames/second in the export progress dialog.
See [Faster export](faster-export.md) for presets and tradeoffs.

## Exporting a picture edit decision list (development preview)

**File → Copy Picture EDL (CMX3600)** generates a cut-only picture edit
decision list and copies it to the clipboard. Paste it into a UTF-8 text file
with the `.edl` extension. An EDL describes source-in/source-out and
record-in/record-out SMPTE timecodes so an offline picture cut can be
conformed in another editing tool. Black gaps are represented explicitly as
`BL` events. Repeated uses of one source keep the same eight-character
reel identifier, with `* FROM CLIP NAME` and `* SOURCE FILE` comments
to help relink. Source file paths are metadata, not copies of your files.

The exporter deliberately **rejects** picture cuts that would misrepresent
the original timeline. There must be exactly one nonempty visible video
track; dissolve transitions, still-image clips, picture adjustments, fades,
and known source/sequence frame-rate mismatches are unsupported. Projects
must use 24, 25, 30, 23.976 or 29.97 fps. The output is limited to 999
events and 24-hour SMPTE timecodes. At 29.97 fps the exporter uses drop-frame
labels. Audio tracks and audio processing are **not represented**: this is
a *picture-only* EDL, not an audio conform, mix, or full project interchange.

EDLs cannot describe many effects, reel/timecode metadata and mixed-rate
workflows in a modern NLE. Review and verify the conformed sources in your
target editor. The export does not save over the project, change the timeline,
or render any media. **This is a v1.4 development feature, not part of
FrameTrellis v1.3.0 stable.**

## Missing media and recovery

If a media file was moved or deleted, its tile shows *Offline* and its clips show a red
placeholder. Right-click the tile and choose *Relink missing file…* to locate it. The replacement
must be the same kind of media and at least as long as the clips need.

When you choose **Edit → Check for Missing Media**, FrameTrellis also checks whether a media
file's size or recorded modification time changed since import. A changed source is flagged
in the media bin for review even if its file size is unchanged. This check does not edit
source files or mark the project as modified; changes made within one second may not be
detectable from filesystem timestamps alone.

FrameTrellis saves a backup of unsaved work every minute. If it closes unexpectedly, it offers to
recover that work the next time it starts.

## Troubleshooting

* **"The media engine could not start"** — the MLT modules were not found; reinstall FrameTrellis.
  Developers: see [building](../development/building.md#running-from-the-build-tree).
* **No sound** — check *Playback → Mute Preview* and your system's audio device. The speaker icon
  under the monitor is crossed out when no audio device is available.
* **A file is not supported** — the import message names the reason. Converting the file with
  FFmpeg (for example to H.264 MP4) usually helps.
* **Logs** — *Help → About → Open log folder*. *Copy diagnostics* copies version information for
  bug reports.
