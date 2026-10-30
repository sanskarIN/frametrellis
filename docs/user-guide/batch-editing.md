# Batch editing in FrameTrellis 1.2.0

FrameTrellis 1.2.0 adds operations for editing several clips without repeating the same
changes one clip at a time. These edits use the existing project undo history and leave
the timeline untouched if any selected clip cannot be edited.

## Duplicate a group of clips

Select clips using Ctrl+click, then press **Ctrl+D**, or choose **Edit → Duplicate
Selected Clips**.

- **One clip:** the original behavior is retained; the duplicate is appended to the
  end of its own track.
- **Two or more clips:** copies are placed after the *end of the entire timeline*.
  The earliest selected clip starts at the end, and every other copy retains its
  offset relative to that earliest selection. Audio and video copies stay aligned
  and on their original tracks.
- Media sources, source trims, clip durations, volume, picture adjustments, fades
  and clip mute are retained. Transitions connecting the original clips are not
  duplicated.
- New copies are selected so they can be moved or adjusted immediately.
- The entire group is one undo/redo action. Locked tracks, malformed selections,
  overlapping or out-of-bounds destinations abort the complete edit.

## Copy and paste clip attributes

1. Select **one** source clip and choose **Edit → Copy Clip Attributes** (**Ctrl+Alt+C**).
2. Select one or more destination clips. Use Ctrl+click for multi-selection.
3. Choose **Edit → Paste Clip Attributes** (**Ctrl+Alt+V**).

The clipboard stores the current **clip volume, audio mute, fade in/out, brightness,
contrast, saturation and opacity**. It does not copy media files, trim points,
timeline positions or transitions. The clipboard remains available while other
projects are opened within the same application session.

For destination clips shorter than the source, the copied fade lengths are
shortened to fit. Existing tracks must be unlocked. Pasting across several clips
is one undoable operation; if any destination rejects the edit, none are changed.

## Batch mute and picture reset

Select several clips and choose:

- **Edit → Mute Selected Clips** (**Ctrl+Alt+M**) or **Unmute Selected Clips**
  (**Ctrl+Alt+U**). This adjusts each clip's audio mute setting but leaves picture,
  volume and fades unchanged.
- **Edit → Reset Selected Picture Adjustments**. This resets brightness,
  contrast, saturation and opacity while preserving audio volume, mute and fades.

The batch operations are non-destructive, do not change the source files, and can
be undone or redone with **Ctrl+Z / Ctrl+Shift+Z**.

## Limits

- The selection's newest copy must fit the maximum allowed timeline duration.
- Title PNG files are external project media: duplication references the same file
  and does not generate a second PNG.
- No clipboard operation changes or copies transition relationships.
- Neither group duplication nor attribute pasting bypasses track locks.
- In this version the clipboard is *in-app*, not the system clipboard.

## Frame-accurate group nudging

Select any combination of video or audio clips. Use **Alt+Left / Alt+Right** to move them one project frame, or **Alt+Shift+Left / Alt+Shift+Right** to move ten frames. Edits retain source trims and synchronized spacing; all clips move together in one undo step. Moves are refused if a track is locked, the move overlaps an unselected clip, exceeds timeline boundaries, or would invalidate a transition. Undo restores every clip and transition.

## Align one clip edge to the playhead (v1.4 development)

Select **one** timeline clip and position the playhead on the exact destination
frame. Under **Edit → Align Selected Clip to Playhead**, choose **Align Start** to
place the selected clip's start at the playhead, or **Align End** to place its
exclusive end at that frame. The clip remains on its current track and keeps
its source in/out, duration, volume, picture adjustments and fades.

This command moves the complete clip; it does **not** trim, ripple neighboring
clips, change source media or seek the playhead. An overlap, locked track,
negative start or invalid transition rejects the operation atomically without
changing the edit. A successful move is one undoable command. The feature is
only in development builds, not the published v1.3.1 stable release.

## Insert time across tracks (v1.4 development)

Move the playhead to a safe boundary or empty interval, then use **Edit →
Insert Gap at Playhead** and choose **1 Frame**, **10 Frames**, **About 1 Second**
or **About 5 Seconds**. Every clip starting at or after that frame moves later by
that duration, across video and audio tracks. Existing source trims, clip
adjustments, automation points and fully shifted transitions remain unchanged.
The command is one undoable operation.

This operation does not split a clip. If any clip spans the playhead, an affected
track is locked, a transition would be disconnected, or the new positions exceed
the timeline limit, nothing changes. Timeline markers and subtitle cues stay at
their original sequence times; move those separately where necessary. The
approximate second choices use a rounded number of project frames. The feature
is not in the published v1.3.1 stable release.

## Remove known empty time across tracks (v1.4 development)

Choose **Edit → Remove Empty Time at Playhead**, selecting **1 Frame**,
**10 Frames**, **About 1 Second** or **About 5 Seconds**. FrameTrellis checks that
the entire time interval is empty on *every* video and audio track, then moves
all later clips earlier by that many frames in one undoable edit. Existing
source trims, effects, automation and transitions within the shifted clips
remain unchanged.

If any clip overlaps the selected time, or a locked track would need to move,
the command refuses the change without altering the project. It also rejects
invalid ranges and operations with no later clips. Subtitle cues and review
markers remain at their original absolute sequence times, so adjust those
separately. Removing an empty interval does not delete source media or trim a
clip. This workflow requires the preceding multi-track gap feature, and
neither feature is in stable v1.3.1.

## Select complete clips before or after the playhead (v1.4 development)

Use **Edit → Select Entire Clips Before Playhead** or **Select Entire Clips
After Playhead** to select complete clips across all audio/video tracks. An
earlier clip must end at or before the playhead; a later clip must start at or
after it. Clips that straddle the playhead are intentionally excluded. Locked
tracks remain selectable for inspection, but their edits are still protected.
An empty match leaves your existing selection intact. These commands never
modify the project, media files or undo history. They are development-only.

## Trim selected clip to the playhead (v1.4 development)

Select one clip, position the playhead **strictly within** that clip, and use
**Edit → Trim Selected Clip to Playhead → Trim Start** or **Trim End**.
Trimming the start advances its source In frame while leaving the source Out
frame fixed. Trimming the end leaves the source In frame fixed. Only the selected
clip is affected, in one undoable edit. This is not a ripple trim.

Locked clips, a playhead outside the selected clip, or clips participating
in a transition are rejected instead of silently changing adjacent edits.
No original media files are modified. This command is development-only
and does not appear in the v1.3.1 stable release.


## Align a group of clips to the playhead (v1.4 development)

Select one or more clips on video and/or audio tracks, then choose **Edit →
Align Selection Start to Playhead**. The earliest selected clip start moves
exactly to the playhead. Every selected clip shifts by the same signed number
of frames, preserving track assignment, relative offsets, source In/Out, fades
and automation. This is an atomic **Nudge Clips** undo step. A locked track,
collision with an unselected clip, timeline overflow or an unsafe transition
rejects the whole edit. Empty selections and no-op alignment create no undo
entry. This does not ripple, trim or move unselected clips.
