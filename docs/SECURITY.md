# Security design

FrameTrellis edits local files; it does not run a server and makes no network connections. The
main attack surface is **untrusted input**: project files and media that a user receives from
someone else.

## Project files

* Documents larger than 64 MiB are rejected before parsing.
* Every field is type- and range-checked; identifiers must be short alphanumeric strings.
  Invalid documents are refused with the JSON path of the first problem (see
  [project format](architecture/project-format.md)).
* Nothing in a document is executed or interpreted as code or as an MLT service description.
* Media paths are only ever *read*. A project can reference any readable file, as in every
  editor, but FrameTrellis never writes to, moves or deletes media paths.

## Media

* Files are opened explicitly with MLT's FFmpeg producer (`avformat:` + path), so a crafted file
  name cannot select another MLT service (for example an XML or "consumer" producer).
* Still images are decoded with Qt's image readers.
* Decoding runs in the editor process (preview, thumbnails) and in the render helper (export).
  FFmpeg vulnerabilities are therefore relevant; keep FFmpeg up to date. Exports run in a
  separate process, which limits the impact of encoder crashes on the editor.

## Processes

* The render helper is started with `QProcess` using an explicit program path next to the editor
  and an argument list — never through a shell — so paths with spaces or special characters
  cannot inject commands.
* The helper receives a snapshot of the project in a private temporary folder.

## Output

* Exports are written to a temporary file in the destination folder and verified before they
  replace the destination; they refuse to overwrite source media or the project file.
* Project saves are atomic (`QSaveFile`).

## Links

The application only opens `https:` links and local folders through the desktop's default
handler (*Help* menu, About dialog).

## Privacy

FrameTrellis collects no telemetry and has no accounts. Logs (in the application data folder)
contain file paths and technical messages and stay on your computer.
