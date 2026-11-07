# Troubleshooting and recovery

This page covers the **released FrameTrellis 1.2.2** editor. Before changing
projects or reinstalling software, make a backup of the `.ftproj` and any
generated title images. Do not send private footage with public bug reports.

| Symptom | What to check |
|---|---|
| A clip says *Offline* | Right-click its media-bin tile and choose **Relink missing file…**; select the same media type with enough source duration |
| A file will not import | Check the error shown during import; the active MLT/FFmpeg build may not decode its container or codec |
| Preview is silent | Check **Playback → Mute Preview**, the audio device and whether SDL audio support is present |
| A clip will not move or trim | Check that its track is unlocked, the destination is free of overlaps and the source has enough frames |
| Transitions cannot be added | The clips must touch on the same unlocked track and have unused source handles around the cut |
| Exports fail | Confirm every referenced media file is online, the destination is writable and the selected MP4 encoder is available |
| Titles disappear after moving a project | Keep the generated **FrameTrellis Titles** PNG folder with the project; relink its images if moved |
| Waveforms take time to appear | Audio analysis runs in the background and is cached; wait for the first computation to complete |

## Recover an unsaved edit

FrameTrellis saves an autosave copy of modified projects approximately once per
minute. After an abnormal shutdown it can offer recovery at startup.

1. Reopen the editor and review any recovery prompt before opening another
   project.
2. If recovered, inspect the timeline, media links and generated title overlays.
3. Choose **Save As…** to preserve a separate copy of the recovered project.
4. If there is no recovery prompt, do not assume every edit was autosaved.
   Look for the last manually saved `.ftproj` and any backups you made.

Autosave is *not* a substitute for manual saves or media backups.

## Run the installation self-test (Windows)

From a terminal in the installed or extracted program directory:

```bat
frametrellis-render.exe --self-test "%TEMP%\frametrellis-self-test"
```

The helper checks the engine and creates a small sample render. Save its
`PASS`/`FAIL` output if you file an issue. Linux users developing from source
can consult the [build instructions](../development/building.md) and
[testing guide](../testing.md).

## Collect useful diagnostics

1. Open **Help → About → Open log folder** to locate the application log.
2. Open **Help → About → Copy diagnostics** for version and runtime information.
3. Note your FrameTrellis version, Windows/Linux version, project frame rate,
   selected export preset, and what you expected to happen.
4. If the issue concerns import or export, describe the file type, codecs,
   resolution and duration **without uploading private media**.
5. Attach a minimal reproduction using generated or non-sensitive media where
   possible, and report it through
   [GitHub Issues](https://github.com/sanskarIN/frametrellis/issues).

If you suspect a security vulnerability, **do not** post exploit details in a
public issue; follow the [security policy](../../SECURITY.md).

## Known platform limits

- Official Windows x64 packages are not code-signed; check the published
  checksums and the [installation guide](installation.md).
- Linux is build-from-source; macOS does not have an officially supported
  package.
- A successful automated Windows ARM64 emulation run is not a substitute for
  physical ARM64 testing.
- Hardware GPU acceleration, keyframed effects and proxy-media workflows are
  not advertised as available in v1.2.2.
