# Transform animation acceptance matrix

Transform keyframes are development work after stable v1.3.0. A nonempty curve,
including one constant point, requires project format v5. The same graph renders
preview and MP4 output. Coordinates and angle semantics are specified in the
[project-format reference](../architecture/project-format.md) and
[user guide](../user-guide/README.md#transform-animation-development-preview).

## Automated coverage

| Layer | Regression coverage |
|---|---|
| Domain | Finite canvas-relative offsets, bounded positive uniform scale, bounded clockwise angles, strict source-frame ordering, held endpoints, four-coordinate interpolation and deliberate multiple turns |
| Commands | Point upsert/remove/clear, exact undo/redo, merged drags, capacity replacement, locked-track rejection and atomic failures |
| Source editing | Move/trim/split/roll/slip/duplicate preserve curves; attribute paste retains destination curves; video on an audio track keeps a dormant transform |
| Picture reset | Single/batch Reset Picture clears scalar settings, opacity and transforms atomically; audio points, gain and fades remain |
| Persistence | Exact v5 roundtrip, v1–v5 legacy reads, minimum v2/v3/v4/v5 writes, missing/wrong-type fields, invalid bounds, fractional source frames, duplicate/unsorted points and oversized arrays |
| Engine | Source offset, fixed and animated transforms, clockwise rotation, preserved source sound, PNG holes, opacity multiplication, aspect fit, reduced-resolution preview, one-frame cuts and dissolve handles |
| MP4 helper | A v5 snapshot produces decoded frames with the expected animated position, scale, rotation and composite colour |
| GUI | Real add/remove/reset/navigation clicks; move/scale/rotate mouse drags; slider wiring, exact single-step undo, locks/outside-playhead guards, independent sound/opacity, timeline badge and 1200×800 dark/light layout |
| Playback | Superseded queued images cannot reverse a newer seek or show the previous project; regression reproduced on the original engine |

Run the complete configured suite after building:

```sh
cmake --build --preset linux-debug
ctest --preset linux-debug --output-on-failure
```

Focused targets are `tst_transform_keyframes`, `tst_engine`, `tst_render`,
`tst_playback` and `tst_gui`. Tests generate their own media. With
`FT_SCREENSHOT_DIR` set, the GUI saves `11-transform-keyframes.png` and
`12-transform-keyframes-light.png`; Linux CI retains these alongside volume and
opacity screenshots. Review these images in addition to automated assertions.

## Manual checks before release

| Action | Expected result |
|---|---|
| Adjust a landscape video and a portrait image | Their aspect ratios remain intact; position/scale act consistently at each preview size and export resolution |
| Add one point, then points later in the clip | One point is constant; additional points interpolate all coordinates without abrupt jumps |
| Animate 0°→720° and 350°→10° | Two clockwise turns in the first case; direct backward interpolation in the second |
| Drag the monitor outline, square and circular handle | Move, uniform scale and clockwise rotation use the actual picture rectangle; one undo restores the entire drag |
| Use both themes, smaller windows and display scaling | Handles, values and inspector scrolling remain readable and correctly positioned |
| Combine transparent titles, scalar opacity, fades and opacity points | No opaque rectangle appears; the exposed track and source sound remain correct |
| Trim/split/roll/slip, then undo and reopen | Source anchoring and hidden points survive; undo restores exact previous states |
| Move the clip wholly outside the canvas | Inspector values and Reset all transforms can recover it |
| Lock the track or start playback | Monitor handles disappear and point edits are refused; existing animation still plays |
| Seek rapidly while editing and resizing preview | An older picture never pulls the playhead backward; the final picture belongs to the latest request |
| Clear curves and save/reopen | The minimum compatible format is selected; older readers refuse unsupported transforms |

Record actual real-footage, audio-listening and physical-desktop results separately.
Automated generated-media checks do not constitute that release acceptance.
