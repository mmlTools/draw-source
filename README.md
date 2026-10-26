# Instant Highlight Source Draw

An OBS Studio plugin for drawing directly on a scene in real time: freehand
ink, markers, spray paint, shapes, and a true pixel eraser, controlled from a
compact Draw Tools dock that looks and behaves like the built-in Sources dock,
or by drawing straight onto the OBS preview.

## Features

- Eleven tools: Pen, Highlighter, Spray paint, Line, Arrow, Eraser, Square,
  Circle, Triangle, Star, and Heart
- Pixel-level eraser that removes ink along the stroke you draw, not whole
  shapes at once, with a live preview ring showing exactly what it will clear
- Adjustable stroke size (1–64 px), eraser size (4–256 px), and opacity
- Color picker in the dock footer
- Two release behaviors: keep ink until cleared, or fade it out over a
  configurable duration after you release the pointer
- Multi-level undo and redo
- Draw directly on the OBS preview — mouse, tablet, and touch input — without
  opening the source's Interact window
- A drawing window that mirrors the live preview (program output, or the
  preview scene in Studio Mode) with your ink composited in; drag it to a
  tablet or second screen and draw there with pen, touch, or mouse
- "Add canvas" creates a full-scene drawing layer, sized to the program
  output and locked in place, in one click
- Optional mirror source: draw over another source, with the canvas
  auto-sized to match it
- Native look: the dock uses OBS's own theme icons, toolbar, borders and
  background, so it blends in with every bundled theme (dark and light)
- Dock layout and visibility persist across OBS restarts

## How it works

The plugin registers a Draw Source. All drawing state — ink, undo history,
and tool settings — lives inside that source, so scenes stay simple: no
browser source, external overlay, or extra compositing step is required.

The Draw Tools dock selects the active Draw Source, exposes a tool palette,
size, eraser size, opacity and release-mode settings, and carries a footer
toolbar with the drawing window, Add canvas, Draw on preview, Undo, Redo,
Clear and the color swatch. "Draw on preview" lets you draw directly on the
OBS preview; Ctrl+Z / Ctrl+Y and Esc work while it's active, and right-click
(or a tablet pen's eraser end) erases with any tool selected.

### Drawing window

The first footer button opens the plugin's own drawing window. Unlike OBS's
stock Interact window, which only shows the bare source, it renders the live
preview — the program output, or the preview scene when Studio Mode is on —
scaled to fit, with the draw source's ink already composited. Input is mapped
through the source's scene-item transform, so what you draw lands exactly
where you see it. Move the window to a drawing tablet or a touch screen and
it behaves like a dedicated telestrator surface. A collapsible sidebar on the
right carries the same tool palette, sliders, release mode, undo/redo/clear
and color picker as the dock, so everything can be driven from the tablet
without touching the main OBS window; the thin handle between canvas and
sidebar (or the Tab key) hides and shows it, and both panels stay in sync.
The window remembers its size, position and sidebar state. If the selected
Draw Source is not placed in the current scene, the window falls back to
showing the source alone. Esc closes it; Ctrl+Z / Ctrl+Y undo and redo.

### Eraser preview

With the Eraser tool selected, the preview cursor becomes a ring the exact
size of the eraser at the current preview zoom, so you can see what a stroke
will clear before you click, both on the OBS preview and in the drawing
window. In OBS's stock Interact window the same ring is drawn by the source
itself while the pointer hovers over it.

## Installation

1. Download the archive for your platform from the [Releases page](https://github.com/mmlTools/draw-source/releases)
   or from https://streamrsc.com.
2. Extract it into your OBS Studio plugins directory:
   - Windows: `%ProgramData%\obs-studio\plugins\` (the archive contains an
     `instant-highlight-source-draw` folder with the DLL and its `data`
     folder, the layout OBS Studio 33 requires). Portable installs use
     `<OBS folder>\plugins\` instead. Remove any older copy from
     `%ProgramFiles%\obs-studio\obs-plugins\64bit\`; that location is flagged
     Legacy in OBS 33 and stops loading in OBS 34.
   - macOS: `~/Library/Application Support/obs-studio/plugins/`
   - Linux: installed automatically by the `.deb` package, or extract the
     `.tar.xz` into `~/.config/obs-studio/plugins/`
3. Restart OBS Studio.

## Getting started

1. Open **View → Docks → Draw Tools**.
2. Click **Add canvas** (the plus icon in the dock footer) to create a
   full-scene Draw Source, or add one to a scene yourself and select it in
   the dock.
3. Pick a tool, color, and size, then either click the brush icon to
   **Draw on preview** or open the **drawing window** (first footer button).
4. Draw. Use **Undo**, **Redo**, or **Clear** from the footer as needed.

## Release behavior

- **Keep until cleared** — ink persists until cleared manually or removed
  with the eraser.
- **Fade after release** — ink fades out automatically over a configurable
  duration after the pointer is released.

## Compatibility

- Built and tested against OBS Studio 33.0.0; uses the dock-by-id frontend
  API introduced in OBS 30
- Windows x64, Linux x86_64, and macOS (Intel and Apple Silicon)
- Requires the OBS Frontend API and Qt6, both included in standard OBS
  Studio builds

## Building from source

Requires CMake 3.28+. `CMakePresets.json` defines a preset per platform;
Windows and macOS fetch the OBS 33 sources and matching obs-deps/Qt6 builds
automatically via `buildspec.json`, while Linux expects a system-installed
OBS SDK and Qt6 (see `.github/scripts/utils.zsh/setup_ubuntu` for the exact
packages CI installs).

```sh
# Windows (Visual Studio 2026, matching OBS 33's own toolchain)
cmake --preset windows-x64
cmake --build --preset windows-x64

# Linux
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64

# macOS (Xcode 26.5 or newer, required by the OBS 33 sources)
cmake --preset macos
cmake --build --preset macos
```

Pass `-DBUILD_TESTING=ON` to also configure the `tests` target, which
exercises the drawing engine (`drawing_document.cpp`) independent of OBS.

## Releasing

Builds are never triggered by pushes or tags; they are run on demand and
reused for the release:

1. **Actions → Build → Run workflow** builds and packages Windows, macOS and
   Ubuntu artifacts for the selected commit (choose `Release` and keep
   *package* enabled).
2. Bump `version` in `buildspec.json` if you have not already, then push a
   tag such as `2.1.0`.
3. The **Release** workflow picks the newest successful Build run for the
   tagged commit (falling back to the newest successful Build run overall,
   with a warning), downloads its artifacts, generates checksums, and creates
   a draft GitHub release with them. Nothing is recompiled.
4. Review the draft release and publish it.

The Release workflow can also be dispatched manually with a tag name and,
optionally, the id of the Build run to take the artifacts from.

## License

MIT — see [LICENSE](LICENSE).

## Author

MML Tech
Website: https://streamrsc.com
Email: contact@streamrsc.com
