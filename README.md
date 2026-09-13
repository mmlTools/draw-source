# Instant Highlight Source Draw

An OBS Studio plugin for drawing directly on a scene in real time: freehand
ink, basic shapes, and a true pixel eraser, controlled from a dedicated dock
or by drawing straight onto the OBS preview.

## Features

- Freehand pen, plus Square, Circle, Arrow, and Heart shape tools
- Pixel-level eraser that removes ink along the stroke you draw, not whole
  shapes at once
- Adjustable line thickness (1–64 px) and eraser size (4–256 px)
- Color picker with independent opacity control
- Two release behaviors: keep ink until cleared, or fade it out over a
  configurable duration after you release the pointer
- Multi-level undo and redo
- Draw directly on the OBS preview — mouse, tablet, and touch input — without
  opening the source's Interact window
- "Add canvas" creates a full-scene drawing layer, sized to the program
  output and locked in place, in one click
- Optional mirror source: draw over another source, with the canvas
  auto-sized to match it
- Dock layout and visibility persist across OBS restarts

## How it works

The plugin registers a Draw Source. All drawing state — ink, undo history,
and tool settings — lives inside that source, so scenes stay simple: no
browser source, external overlay, or extra compositing step is required.

The Draw Tools dock selects the active Draw Source, exposes its tool,
color, thickness, opacity, and release-mode settings, and provides
undo/redo/clear. Its "Draw on preview" button lets you draw directly on the
OBS preview without switching to the Interact window; Ctrl+Z / Ctrl+Y and
Esc work while it's active.

## Installation

1. Download the archive for your platform from the [Releases page](https://github.com/mmlTools/draw-source/releases)
   or from https://obscountdown.com.
2. Extract it into your OBS Studio plugins directory:
   - Windows: `%ProgramFiles%\obs-studio\obs-plugins\`
   - macOS: `~/Library/Application Support/obs-studio/plugins/`
   - Linux: installed automatically by the `.deb` package, or extract the
     `.tar.xz` into `~/.config/obs-studio/plugins/`
3. Restart OBS Studio.

## Getting started

1. Add a Draw Source to a scene, or click **Add canvas** in the Draw Tools
   dock to create one automatically.
2. Open **View → Docks → Draw Tools**.
3. Select the source, choose a tool, color, and thickness, then either open
   **Interact** or click **Draw on preview**.
4. Draw. Use **Undo**, **Redo**, or **Clear** from the dock as needed.

## Release behavior

- **Keep until cleared** — ink persists until cleared manually or removed
  with the eraser.
- **Fade after release** — ink fades out automatically over a configurable
  duration after the pointer is released.

## Compatibility

- OBS Studio 31 or later (uses the dock-by-id frontend API)
- Windows x64, Linux x86_64, and macOS (Intel and Apple Silicon)
- Requires the OBS Frontend API and Qt6, both included in standard OBS
  Studio builds

## Building from source

Requires CMake 3.28+. `CMakePresets.json` defines a preset per platform;
Windows and macOS fetch their OBS/Qt6 build dependencies automatically via
`buildspec.json`, while Linux expects a system-installed OBS SDK and Qt6
(see `.github/scripts/utils.zsh/setup_ubuntu` for the exact packages CI
installs).

```sh
# Windows (Visual Studio 2022)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Linux
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64

# macOS
cmake --preset macos
cmake --build --preset macos
```

Pass `-DBUILD_TESTING=ON` to also configure the `tests` target, which
exercises the drawing engine (`drawing_document.cpp`) independent of OBS.

## License

MIT — see [LICENSE](LICENSE).

## Author

MML Tech
Website: https://obscountdown.com
Email: contact@obscountdown.com
