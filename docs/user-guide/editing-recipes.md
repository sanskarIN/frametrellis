# Editing recipes (FrameTrellis 1.2.2)

These workflows describe features in the **released v1.2.2 editor**, not proposed pull
requests. The timeline uses integer project frames: a clip's end is **exclusive**
(the last displayed frame is one frame before its end). Nothing here alters your
original video, audio or image files.

## Make a simple video with a music bed

1. Create a project (**Ctrl+N**) and choose the intended aspect ratio and frame rate.
2. Import footage and a music file (**Ctrl+I**). Video and still images go on video
   tracks; audio can go on audio tracks. A video source with sound can also be
   placed on an audio track for its audio alone.
3. Drag clips from the media bin to the timeline. Keep sequential clips touching if
   you want a continuous cut, or leave gaps for intentional silence/black.
4. Place music on an audio track underneath the video. Select the music clip in the
   timeline, then use the inspector to adjust volume or mute.
5. To fade music in/out, set the clip's fade lengths in the inspector. Their total
   must fit within that clip's duration.
6. Preview with **Space**. Step by one project frame with **Left/Right**.
7. Save the project (**Ctrl+S**) and export a review MP4 (**Ctrl+E**). The export
   runs in a separate process using a snapshot, so subsequent editing does not
   change that in-progress render.

## Make the same edit to several clips

1. Click one clip; **Ctrl+click** other clips to add them to the selection.
2. **Alt+Left/Right** nudges every selected clip by one frame.
   **Alt+Shift+Left/Right** nudges by ten frames.
3. **Ctrl+Alt+M/U** mutes or unmutes selected clips, without changing their picture
   adjustments. **Edit → Reset Selected Picture Adjustments** affects the picture
   controls only.
4. For consistent looks, select one source clip, press **Ctrl+Alt+C**, select
   destination clips, then press **Ctrl+Alt+V**. Volume, mute, fades and picture
   adjustments are copied; source files, trims and positions are not.
5. **Ctrl+D** duplicates the selection. One clip is appended to its own track;
   two or more clips are appended after the sequence while retaining relative
   timing between audio and video.
6. **Ctrl+Z** undoes an accepted edit as a unit. If one selected destination is
   on a locked track, or an operation would overlap existing clips, the whole
   batch is rejected rather than partially applied.

The clip-attribute clipboard is internal to FrameTrellis, not your operating
system's clipboard. Duplicate clips still refer to the original media; transitions
are not automatically duplicated.

## Make a title over footage

1. Save the project as a `.ftproj` first.
2. Choose **File → Create Title…**, configure text and colour, and create it.
3. FrameTrellis produces a transparent PNG in a **FrameTrellis Titles** folder
   beside the project, imports it and appends it to an unlocked video track.
4. Position the title above the footage in the video-track stack and trim its
   edges to change how long it appears.
5. Keep the generated PNG with the project when moving it to another computer.
   To change title text, create a new overlay: titles are *images*, not editable
   text layers in v1.2.2.

## Join cuts with transitions

1. Position the incoming clip so its start touches the outgoing clip's end on
   the **same** track.
2. Ensure both media files have unused frames around the cut. Still images do
   not have a fixed source-length limit.
3. Select the outgoing clip and press **T**, then use the inspector to adjust
   the resulting dissolve or crossfade duration.
4. If adding the transition is refused, trim for more source handles, confirm
   the clips are adjacent, and check the track is unlocked.

## Save safely and send a project

- The `.ftproj` file stores project metadata and *references* to media, not
  embedded source files. Move or share the footage together with the project;
  relative references can be resolved again when the folder is moved.
- Saving writes through an atomic replacement so an unsuccessful save should
  leave the previous project file intact. Keep separate backups of important edits.
- Use **File → Export…** to create an independent H.264/AAC MP4. Exports refuse
  to replace source media or the project file and only adopt verified output.
- If an imported file is moved, use the media tile's **Relink missing file…**
  command. The replacement must have a compatible type and sufficient duration.

For advanced group operations see [batch editing](batch-editing.md); for encoder
choices see [faster export](faster-export.md). For errors or lost file locations
see [troubleshooting](troubleshooting.md).
