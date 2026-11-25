# Project checkpoints (development build)

Manual checkpoints preserve named versions of a project's editing state locally.
They complement regular project saves and crash-recovery autosaves. A checkpoint
never embeds source video, audio or images. Keep your original media and regular
project backups.

## Create a version

1. Open a project (it may be unsaved).
2. Choose **File → Project Checkpoints…**.
3. Enter a descriptive name, such as *Before rearranging the introduction*.
4. Choose **Create checkpoint**.

The snapshot includes the current in-memory project, including unsaved clips,
tracks, markers, subtitles, keyframes and settings. Creating a checkpoint does
not mark the open document saved, change its file location or alter Undo history.
Names contain 1–120 characters on a single line.

## Compare before restoring

Select a version and choose **Compare with current**. The summary lists added,
removed, changed and unchanged objects in nine categories: project metadata,
sequence settings, media, tracks, clips, transitions, markers, subtitle cues
and subtitle style. The detail view can filter by category or action. It
describes changes from the selected older version to the current edit.

Objects are matched by stable IDs. Moving a clip between tracks is a change,
not a deletion plus an addition. Changing a track's position can also change
the displayed order index of other tracks. Media source-location changes are
flagged without displaying raw paths. Subtitle text changes are flagged without
copying caption contents into the comparison. Runtime media availability and
project creation timestamps are ignored.

The comparison retains up to 1,000 detail rows in deterministic category/ID
order, while all category counts remain complete. The comparison does not
decode or compare media file contents; it compares stored project state.

## Open a version

Select the checkpoint and choose **Open as unsaved version**. FrameTrellis uses
the usual unsaved-changes prompt before replacing the current editor session.
A restored document retains the project identity so its version history remains
available, but it has no saved document path. Use **Save As** to choose where
to keep the restored edit. The original project is not overwritten by opening
a checkpoint.

If restoration fails because a version is damaged or unsupported, the current
document remains open. A normal crash-recovery autosave is attempted after
restoration, but the immutable checkpoint remains available regardless.

## Storage and limits

The local application data directory contains a `checkpoints` subdirectory,
with one folder per stable project ID and one `.ftcheckpoint` file per version.
Opening a different project ID shows that project's own history. Renaming or
saving the same project under a different filename does not change its ID.

- A maximum of 100 versions per project is supported. The application never
  silently deletes old versions to make room for a new one.
- History discovery reads bounded metadata headers rather than decoding every
  project document. A valid header does not guarantee that the payload is intact;
  the payload is fully checked when comparing or restoring.
- Metadata is bounded to 16 KiB and the project payload to the normal 64 MiB
  project document limit. Writes use a single atomic replacement operation.
- The application rejects path traversal, symbolic links and nonregular
  checkpoint entries. A per-project lock coordinates creation and deletion.

To delete a version, select **Delete** and confirm. Only that local checkpoint
file is removed. No source media or regular project files are deleted. Damaged
headers can be listed and explicitly removed too. Checkpoints are not a cloud
backup, and deleting the application data directory removes them. Retain
independent copies of important project files and media.

## Development format

Version 1 uses a UTF-8 JSON metadata line terminated by a newline, followed by a
normal UTF-8 FrameTrellis project document. The metadata includes the format
identifier `org.frametrellis.checkpoint`, version, checkpoint ID, project ID,
label, project display name, original project path (if known) and UTC timestamp.

The file is local history data, not an interchange format. Do not rename or edit
its identifiers manually: the reader requires header and payload identity to
match the containing project and checkpoint filenames.
