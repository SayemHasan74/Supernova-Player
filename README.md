# Supernova

Supernova is a native Windows video and music player inspired by IINA and
powered by libmpv. It is being built in C++20 with Qt 6.

The current foundation includes the single-instance Qt application shell,
libmpv lifecycle management, OpenGL video rendering, and a `PlayerCore` state
engine that owns playback orchestration for the UI, CLI, and IPC paths.

## Prerequisites

- Windows 10 or Windows 11 (x64)
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.24 or newer
- Ninja
- Python 3 and `aqtinstall`
- Qt 6.8.2 for MSVC 2022 x64
- 7-Zip

Install Qt non-interactively:

```powershell
python -m pip install aqtinstall
python -m aqt install-qt windows desktop 6.8.2 win64_msvc2022_64 -O E:\Applications\Qt
```

This machine uses `E:\Applications\Qt` because its system drive has limited
free space. If Qt is installed elsewhere, update `CMAKE_PREFIX_PATH` in
`CMakePresets.json`.

## Build

Run these commands from a **Developer PowerShell for VS 2022**:

```powershell
git submodule update --init --recursive
.\vcpkg\bootstrap-vcpkg.bat
.\scripts\fetch_mpv.ps1
cmake --preset debug
cmake --build --preset debug
```

The default mpv package is pinned and checksum-verified. To deliberately use
the newest published community build, run:

```powershell
.\scripts\fetch_mpv.ps1 -Latest
```

Use `-Variant lgpl` to select the reduced LGPL mpv build. The chosen variant
and exact release are recorded in `deps/mpv/metadata.json`.

Create an install tree containing the Qt runtime, compiler runtime, and mpv:

```powershell
cmake --install build/release --prefix "$PWD/build/install"
```

## Architecture invariants

- Windowed and fullscreen playback use one window and one persistent widget
  tree.
- The libmpv OpenGL render API is hosted by `QOpenGLWidget` on Qt's GUI thread,
  matching Qt's context ownership model and mpv's official Qt example.
- Playback controls and sidebars will overlay the video surface without
  becoming layout siblings.
- Drag and fade interactions use Qt's animation/compositor facilities.
- Only `src/Mpv` may call mpv APIs directly.
- Application features talk to `PlayerCore`; `MpvCore` is an implementation
  detail except where the video surface needs its render handle.

## Current playback controls

- Use **File → Open File…** (<kbd>Ctrl</kbd>+<kbd>O</kbd>) to choose one
  or more media files.
- Use **File → Open Folder…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>)
  to recursively open supported media in a folder.
- Drag files, media folders, playlists, or media URLs onto the player.
- Use Windows **Open with**, or pass one or more media paths/URLs on the
  command line. A second launch forwards all items to the running player.
- Press <kbd>Space</kbd> to pause or resume.
- Press <kbd>Left</kbd> or <kbd>Right</kbd> to seek backward or forward five
  seconds.

## Licensing

Supernova is licensed under GPL-3.0. The mpv build variant is a separate
dependency choice. The default fetcher selects the GPL build; selecting the
LGPL build does not change Supernova's own license.
