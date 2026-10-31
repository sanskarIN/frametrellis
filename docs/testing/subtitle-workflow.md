# Subtitle workflow acceptance

This is development source after v1.3.0. Subtitle cues or nondefault appearance
require project format v6. The stable Windows downloads do not include this workflow.

| Area | Automated evidence |
|---|---|
| Cue model | Unique IDs, exact integer milliseconds, sorted non-overlapping half-open intervals, bounded count/text/time, valid Unicode and no embedded blank lines |
| SRT | Hindi/emoji/literal markup, UTF-8 BOM, CRLF/CR/LF, unordered counters, canonical export; malformed/truncated UTF-8, timestamps, missing fields, overlaps and capacity limits refuse the whole input |
| Clock | Exact rational conversions including 29.97 fps and high rates; boundaries, exclusive ends and cues between sampled frames |
| Commands | Exact undo/redo for add/update/remove/append/clear/style and whole-track time shifting; failed operations leave track/history unchanged and preserve the timeline |
| Persistence | Exact v6 roundtrip, v1–v6 reads, required fields/types/bounds, pre-v6 subtitle rejection and minimum-version selection after clearing/resetting |
| Preview graph | Active/inactive boundary frames, one-frame clips, unchanged audio/duration, burn-in disabled, canonical wrapping across smaller previews |
| MP4 helper | Render a v6 snapshot; decoded frames show text only in the expected interval; sidecar-only mode removes it; future cues do not lengthen the video |
| Controller | Local URL/path handling, atomic overlapping imports, Unicode/time sidecar roundtrip, project path protection, failed writes, selection/navigation, outside/unsampled cues and viewport filtering |
| Real QML | Type text/times, Apply, project undo, invalid timing rejection, Cancel/Discard draft guard, timeline selection; verify clips and transport remain unchanged while typing |
| Pause stability | Freeze the published frame when playback has already rendered the next frame; pause acknowledgement must not precede a later playhead advance |
| Distribution | Render helper uses QGuiApplication for fonts; Windows includes the offscreen plugin; Linux/container installs fonts; self-test checks text pixels in the encoded MP4 |

Focused local checks:

```sh
ctest --preset linux-debug -R 'tst_(subtitles|subtitle_controller|engine|render|gui)$' --output-on-failure
FT_SCREENSHOT_DIR=/tmp/subtitle-shots ctest --preset linux-debug -R '^tst_gui$'
frametrellis-render --self-test /tmp/subtitle-package-test
```

CI retains `13-subtitles.png` and `14-subtitles-light.png` as temporary feature
artifacts, alongside earlier animation screenshots. They are real 1200×800 editor
captures. Generated-media assertions remain the acceptance evidence for timing
and pixel composition.

Manual release acceptance still needs real footage, language/font checks on the
target machine, long cue lists, playback/listening and clean physical Windows
startup. Test mixed scripts and emoji with installed fonts, multiline and long
text, add/import/edit/undo/save/reopen, shortened sequences and sidecar-only
delivery. Fonts are not embedded in projects; substitution can alter wrapping.
No speech recognition, multiple language tracks, rich-text markup, subtitle
stream muxing or automatic following of clip edits is claimed.
