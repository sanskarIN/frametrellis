# Preview playback

`PlaybackEngine` (`src/playback`) shows the timeline in real time.

* **One worker thread owns every MLT object** used for preview: an MLT profile at preview
  resolution (the project's aspect ratio, at most the monitor's size), a `TimelineGraphBuilder`
  and the current tractor. The GUI posts commands (set project, play, pause, seek, step, preview
  size, volume) to a queue.
* **Snapshots, not shared state.** `setProject` copies the project. Edits are debounced by
  30 ms in the UI and then rebuilt into a new tractor; source producers are cached per asset and
  track, so rebuilding does not reopen files.
* **Clock.** With an audio device (SDL 2 in push mode), the engine keeps about 120 ms of audio
  queued and shows each picture when its audio starts playing. Without one, frames are paced by
  a monotonic clock; if rendering falls far behind, the clock resets instead of racing to catch up.
* **No backlog.** Seeks are coalesced (only the latest pending seek is kept) and the UI receives
  only the most recent frame through a single pending notification, so scrubbing or a slow
  repaint cannot pile up work.
* **Display.** `FrameView` uploads the frame as a texture through the scene graph and draws it
  aspect-fitted. It works with every Qt Quick backend, including the software renderer.

Offline media appear as dark red placeholder frames during preview and are listed as warnings.
