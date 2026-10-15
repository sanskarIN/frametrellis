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
