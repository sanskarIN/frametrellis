# Opacity automation acceptance matrix

Opacity keyframes are a development feature after stable v1.3.0. They use the
same source-frame clock as volume curves and add a multiplicative alpha stage
to the shared preview/export graph. Projects containing them require format v4.

## Automated coverage

| Layer | Regression coverage |
|---|---|
| Domain | Sorted upserts, held endpoints, linear interpolation, finite `[0, 1]` values, integer source positions, capacity limits, locked tracks and atomic failures |
| Commands | Slider merging, exact undo/redo, reset with audio/fade preservation, and atomic failure of multi-clip picture reset |
| Editing | Trim, split, roll, slip, duplicate and attribute paste preserve source curves; video moved to an audio track retains a dormant picture envelope |
| Persistence | Minimum format v2/v3/v4 selection, legacy reads, exact curve roundtrip, wrong-version rejection, malformed arrays, duplicate/unsorted frames and invalid values |
| Preview | Nonzero source in, zero/half/full opacity, scalar and fade multiplication, preserved source audio, PNG alpha, transparent pixels, one-frame cuts and dissolve handles |
| Export | Renderer helper receives a v4 snapshot; decoded MP4 frames match blue/purple/red composite stages |
| GUI | Real add/remove/clear clicks, zero-percent display, point navigation, independent volume, locked/outside-playhead rejection, Reset Picture undo, image support and zoom/scroll bounds |

Run the configured suite after building:

```sh
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

The focused unit target is `tst_opacity_keyframes`. `tst_engine`, `tst_render` and
`tst_gui` cover the complete rendering and interaction paths. Fixtures are generated
during tests. `FT_SCREENSHOT_DIR` enables the GUI screenshot `10-opacity-keyframes.png`.

## Manual checks before release

| Action | Expected result |
|---|---|
| Add 0% and 100% points over another video track | Smooth reveal in preview and export; the clip's sound continues |
| Animate a PNG title over footage | Transparent pixels remain transparent; text opacity changes without a rectangular background |
| Combine scalar Opacity, fades and points | All three adjustments multiply; neither curve nor fade replaces another |
| Trim away and restore a point | Hidden points remain in the project and reappear when the trim expands |
| Split, roll or slip a clip | The envelope remains anchored to source frames; undo restores the full previous state |
| Reset Picture or clear opacity points | Audio automation and fades remain intact; one undo restores picture settings and points |
| Zoom and scroll a long clip | Both curves align with source frames and paint only the visible horizontal viewport |
| Use the narrower inspector and both themes | Percentages, navigation buttons and both point shapes remain readable and usable |
| Save/reopen with opacity, volume-only, and no curves | Saved versions are respectively v4, v3 and v2; an older reader refuses unsupported versions |

Record actual manual results separately. Passing generated-media tests is not a
record of listening to a real mix or operating the editor on physical hardware.
