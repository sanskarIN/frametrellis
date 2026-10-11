# Installing FrameTrellis

FrameTrellis is published for **Windows (x64)**. On Linux you can build it from source; macOS is
not supported yet.

- [Windows](#windows)
- [Linux](#linux)
- [macOS](#macos)
- [Where FrameTrellis keeps its data](#where-frametrellis-keeps-its-data)

## Windows

### Requirements

* Windows 11, or Windows 10 version 1809 or later, 64-bit.
* A graphics adapter with Direct3D 11. Without a suitable driver, Windows' built-in software
  renderer is used, which works but is slower.
* No other software is needed: the packages contain Qt, MLT, FFmpeg and everything else the
  editor uses.

Each release package is checked automatically before it is published. The check runs the
installer and the portable version on clean Windows Server 2022, Windows Server 2025 and
Windows 11 (ARM64, using x64 emulation) machines that have only Windows on the `PATH`. It runs
the engine self-test, starts the editor and checks that every library is loaded from the
package or from Windows itself, then installs and uninstalls the program.

### Download

Get the files from the [download page](https://sanskarin.github.io/frametrellis/download/) or
the [GitHub releases page](https://github.com/sanskarIN/frametrellis/releases):

| File | Use it for |
|---|---|
| `FrameTrellis-<version>-windows-x64-setup.exe` | A normal installation with a Start menu entry and the `.ftproj` file association (recommended) |
| `FrameTrellis-<version>-windows-x64.zip` | A portable copy you can run from any folder or a USB drive |
| `SHA256SUMS.txt` | Checksums to confirm the download is complete and unmodified |

To check a download, open PowerShell in the download folder and compare the output with the
matching line in `SHA256SUMS.txt`:

```powershell
Get-FileHash .\FrameTrellis-<version>-windows-x64-setup.exe -Algorithm SHA256
```

### Installer

1. Run `FrameTrellis-<version>-windows-x64-setup.exe`.
2. The packages are not code-signed, so Windows SmartScreen may show *Windows protected your
   PC*. If the checksum matches, choose **More info** → **Run anyway**.
3. Choose whether to install for yourself only (the default, no administrator rights needed;
   the program goes to `%LOCALAPPDATA%\Programs\FrameTrellis`) or for all users (needs
   administrator rights; the program goes to `C:\Program Files\FrameTrellis`).
4. Accept the license (GPL-3.0-or-later) and pick the optional desktop shortcut and the
   `.ftproj` file association.
5. Start FrameTrellis from the Start menu.

Installing a newer version over an older one updates it in place. Your projects and settings
are kept.

For unattended installation, the installer accepts the standard Inno Setup options, for example:

```powershell
.\FrameTrellis-<version>-windows-x64-setup.exe /VERYSILENT /CURRENTUSER /NORESTART
```

To uninstall, open *Settings → Apps → Installed apps*, select **FrameTrellis** and choose
**Uninstall**. Uninstalling removes the program and its shortcuts but not your projects, media,
settings or logs.

### Portable ZIP

1. Extract `FrameTrellis-<version>-windows-x64.zip` to a folder you can write to (for example
   `Documents\FrameTrellis`). Do not run it from inside the ZIP.
2. Run `frametrellis.exe` from the extracted folder.

The portable version makes no changes to Windows apart from your user settings (see below). To
remove it, delete the folder.

### Checking an installation

If FrameTrellis does not start or exports fail, run the built-in self-test from a command
prompt in the program folder:

```bat
frametrellis-render.exe --self-test "%TEMP%\frametrellis-self-test"
```

It checks the media engine, writes sample media, builds and saves a small project, exports it
to MP4 and verifies the result, printing `PASS` or `FAIL` for each step. Please include its
output when you [report a problem](https://github.com/sanskarIN/frametrellis/issues).

## Linux

There is no Linux package yet. FrameTrellis builds and passes its tests on Ubuntu 24.04; follow
the [building guide](../development/building.md#linux). A self-contained package (AppImage or
Flatpak) is planned and will be published once it has been verified.

## macOS

macOS is not supported yet. The code base is portable, and a macOS build is planned; see the
[roadmap](../roadmap.md).

## Where FrameTrellis keeps its data

| What | Windows | Linux |
|---|---|---|
| Settings (theme, recent projects) | Registry: `HKEY_CURRENT_USER\Software\FrameTrellis\FrameTrellis` | `~/.config/FrameTrellis/FrameTrellis.conf` |
| Autosave and crash recovery | `%APPDATA%\FrameTrellis\FrameTrellis\autosave` | `~/.local/share/FrameTrellis/FrameTrellis/autosave` |
| Log files | `%APPDATA%\FrameTrellis\FrameTrellis\logs` | `~/.local/share/FrameTrellis/FrameTrellis/logs` |
| Thumbnail cache | `%LOCALAPPDATA%\FrameTrellis\FrameTrellis\cache\thumbnails` | `~/.cache/FrameTrellis/FrameTrellis/thumbnails` |

Projects (`.ftproj`) and exported videos are saved wherever you choose. FrameTrellis never
moves, changes or deletes your media files.
