# Architecture overview

FrameTrellis is a C++20 application with a Qt Quick (QML) interface and an MLT/FFmpeg media
engine. The code is split into small libraries with one-way dependencies, so the editing logic
can be tested without a GUI and the GUI never talks to MLT directly.

```
                 ┌──────────────────────────────────────────────┐
                 │ app  (main.cpp)                              │
                 └───────────────┬──────────────────────────────┘
                                 │
                 ┌───────────────▼──────────────────────────────┐
                 │ ui   QML workspace + C++ controllers/models  │  Qt Quick
                 └──┬──────────┬───────────┬───────────┬────────┘
                    │          │           │           │
            ┌───────▼──┐ ┌─────▼────┐ ┌────▼─────┐ ┌───▼──────────────┐
            │ editing  │ │ media    │ │ playback │ │ rendering        │
            │ commands │ │ probe,   │ │ preview  │ │ export settings, │
            │ history, │ │ import,  │ │ engine   │ │ render job ──────┼──► frametrellis-render
            │ document │ │ thumbs   │ │          │ │ (helper process) │    (render runner)
            └───┬──────┘ └────┬─────┘ └────┬─────┘ └────────┬─────────┘
                │             └──────┬─────┘                │
            ┌───▼──────┐      ┌──────▼──────────────┐       │
            │ project  │◄─────┤ engine              │◄──────┘
            │ timeline │      │ MLT runtime, graph  │
            │ core     │      │ builder, stills     │  MLT ─► FFmpeg
            └──────────┘      └─────────────────────┘
```

| Library | Folder | Responsibility | Depends on |
|---|---|---|---|
| `ft_core` | `src/core` | `Rational`, `FrameRate`, timecode, `Result`/`Error`, ids, logging categories | Qt Core |
| `ft_timeline` | `src/timeline` | tracks, clips and validated edit operations, snapping | core |
| `ft_project` | `src/project` | media assets, project settings, the `Project` aggregate, the `.ftproj` format | timeline |
| `ft_editing` | `src/editing` | undoable commands, `CommandHistory`, `ProjectDocument` | project |
| `ft_rendering` | `src/rendering` | export settings and validation, helper protocol, `RenderJob` | project |
| `ft_engine` | `src/engine` | MLT lifetime and capabilities, profiles, project → MLT graph, still images | project, MLT |
| `ft_media` | `src/media` | probing, background import, thumbnail cache | engine |
| `ft_playback` | `src/playback` | threaded preview engine, SDL2 audio output | engine |
| `ft_render_engine` | `src/rendering` | `RenderRunner` and the `frametrellis-render` executable | rendering, media |
| `ft_ui` | `src/ui` | QML module `FrameTrellis`: controllers, models, `FrameView`, QML files | everything above |
| `frametrellis` | `src/app` | entry point and shutdown order | ui |

## Principles

* **Domain logic has no UI and no MLT.** `core`, `timeline`, `project` and `editing` only use
  Qt Core and are covered by fast unit tests.
* **Integer time.** Positions and durations are integer frame counts at the project frame
  rate. Frame rates are exact rationals (30000/1001, not 29.97), and conversions to
  microseconds use overflow-checked rational arithmetic. Clip identity never depends on
  floating point.
* **Validated edits.** Every timeline operation checks its preconditions (overlaps, source
  length, track kind, locks) and leaves the timeline unchanged when it fails.
* **Undo by snapshots of changed tracks.** `TimelineCommand` runs an edit on a copy, then stores
  only the tracks that changed (or the whole track list for structural edits). Undo and redo
  restore those exact states, so they cannot drift.
* **Errors are values.** Functions return `Result<T>`/`Status` with a user-facing message and
  technical detail. Exceptions are not used across module boundaries.
* **Untrusted input.** Project files and media are treated as hostile: strict JSON
  validation with size limits, media opened only with the FFmpeg producer (`avformat:` prefix),
  no shell commands, no evaluation of document content.
* **Never touch user media.** Media are referenced by path; FrameTrellis never moves, edits or
  deletes them. Exports refuse to overwrite source files or the project.

## Threads

| Thread | Owns |
|---|---|
| GUI thread | QML, controllers, `ProjectDocument`, `RenderJob` (QProcess) |
| Playback worker | the preview's MLT profile, graph builder and tractor; renders frames and audio |
| Thread pool | media probing and thumbnails (each task creates its own MLT objects) |
| Render helper process | the export's MLT graph and encoder |

Rules: MLT objects never cross threads; the playback engine receives immutable `Project`
snapshots; results from background tasks carry a generation number or are delivered through
`QFutureWatcher` children so they are dropped if their owner is gone. On shutdown the QML engine
is destroyed first, then the controllers (stopping playback and the render helper), then the
thread pool is drained, and only then is MLT closed.

See also: [project format](project-format.md), [playback](playback.md),
[rendering](rendering.md).
