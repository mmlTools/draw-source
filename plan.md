# OBS Smart Screen Implementation and Handoff

## Objective

Turn Instant Highlight Source Draw into a usable OBS smart screen with freehand
ink, partial erasing, undo/redo, and a fullscreen drawing surface. Restore Windows,
Linux, and macOS build/package workflows and run the exact pinned formatting
checks locally. Preserve source IDs, dock IDs, existing shape tool values, and
the user's existing README edits.

## Implementation Plan

- [x] Inspect source, dock, CMake presets, dependencies, and existing workflow.
- [x] Implement a portable Qt drawing document: pen, shapes, pixel eraser,
  bounded undo/redo, clear, consistent alpha, and cached rendering.
- [x] Integrate document with OBS rendering and interaction, protect shared
  state, handle focus loss, and preserve source configuration compatibility.
- [x] Add a resizable/fullscreen smart screen with mouse, touch, and tablet
  input and dock controls for pen/eraser size, color, opacity, undo/redo/clear.
- [x] Fix dock reference ownership and teardown.
- [x] Restore build and install packages for Windows x64, Linux x86_64, and
  macOS arm64/x86_64. Pin formatters and run checks on pushes and pull requests.
- [x] Format all tracked C/C++ and CMake files with CI's exact versions.
- [x] Build and run focused drawing tests locally; validate workflow structure.
- [x] Document installation, operation, compatibility, verification, limitations,
  and any remaining native-platform testing in this handoff file.

## What Was Built

**Drawing engine** (`src/drawing_document.hpp/.cpp`): a headless, OBS-agnostic
`DrawingDocument` covering Pen, Square, Circle, Arrow, Heart, and a true
pixel-erase (via `QPainter::CompositionMode_Clear` along the erase stroke's
path, not whole-shape deletion). Undo/redo is a bounded deque (128 live
actions); older persistent ink is baked into a `base_` image so undo history
never unbounds memory while ink is never lost. Fade-after-release is
alpha-interpolated per action at render time. Rendering is cached and only
recomputed when dirty or while a fade animation is in flight, tracked with a
monotonic `revision()` so `DrawSource` only re-uploads the GPU texture when
the image actually changed.

**OBS integration** (`src/draw_source.hpp/.cpp`): `DrawSource` owns a mutex
around `DrawingDocument` and never holds it while calling into another
source's `obs_source_video_render` (for the optional mirror/background
source). `focus(false)` and mouse-leave both end any in-progress stroke so a
stroke can't get stuck "drawing" after alt-tab or losing interaction focus.
Canvas size is clamped to 64..8192 per side and to 16 total megapixels
(preserving aspect ratio) to bound texture memory. All existing setting keys
(`tool`, `thickness`, `color`, `opacity`, `release_mode`, `fade_ms`) and the
source ID `instant_highlight_source_draw` are unchanged from before this
work, so existing scene collections keep working.

**Fullscreen/resizable smart screen**: per the earlier default-scope decision
in this file (OBS board, not a separate desktop overlay), the "smart screen"
is the Draw Source itself: it can be sized up to 8192x8192 or auto-sized to
match a mirrored background source, and `PreviewOverlay`
(`src/preview_overlay.hpp/.cpp`) lets the operator draw directly on top of
the live OBS Preview/Program surface — mapping preview-widget coordinates
through the active scene item's transform and crop back into source pixels —
so the draw source can be scaled to fill an entire scene and drawn on in
place. Mouse, tablet (pressure-agnostic; eraser via `QPointingDevice::Eraser`
pointer type or right-click), and touch (multi-touch tracked by point id) are
all handled through one `QEvent` filter installed on the preview widget.
Ctrl+Z/Ctrl+Y and Escape are handled while drawing is active.

**Dock** (`src/draw_dock.hpp/.cpp`): target source picker, tool selector
(including the restored Pen), thickness, eraser size, color+opacity, release
mode/fade duration, Undo/Redo/Clear, an "Add canvas" button that creates and
locks a full-scene Draw Source, and a "Draw on preview" toggle that engages
`PreviewOverlay`. The dock releases every `obs_source_t` ref it stashed in
the source combo box on destruction (`~DrawDock`), and `Draw_destroy_dock()`
unregisters the dock by id on `obs_module_unload`, so no source refs or dock
registrations leak across plugin reload.

## CI / Build Restoration

Only `.github/workflows/windows-build.yml` (a tag-triggered DLL zip) existed
at task start; the rest of the original obs-plugintemplate CI
(`build-project.yaml`, `check-format.yaml`, `push.yaml`, `pr-pull.yaml`,
`dispatch.yaml`, the composite actions under `.github/actions/`, and the
`.github/scripts/` + `build-aux/` helper scripts) had been deleted in an
earlier commit (`ce2a585`, "fixed building"). `CMakePresets.json`,
`buildspec.json`, and the per-platform `cmake/` helpers were already restored
to a working state (Windows x64, Ubuntu x86_64, macOS universal presets,
including `-ci` variants with `CMAKE_COMPILE_WARNING_AS_ERROR=ON`) before
this pass — they were simply unused without the workflow files.

This pass:
- Restored the full CI scaffolding from the last known-good commit before the
  deletion (`.github/actions/*`, `.github/scripts/*`, `.github/workflows/{build-project,check-format,push,pr-pull,dispatch}.yaml`,
  `build-aux/*`), verified against what each script/action expects (preset
  names, `buildspec.json` keys, apt/brew dependency lists) — all matched the
  CMake side unchanged, so no script edits were needed beyond the workflow
  triggers below.
- Re-enabled the `check-format` job in `push.yaml` and `pr-pull.yaml` (it had
  been unhooked, though the reusable `check-format.yaml` workflow itself
  still existed at that point) so clang-format/gersemi actually gate pushes
  and PRs again, per this plan's objective. Formatter versions are pinned in
  the actions themselves: `clang-format@19` (>=19.1.1, <19.2.0) via the
  `obsproject/tools` Homebrew tap, and `gersemi` (>=0.12.0) via the same tap,
  both invoked through `build-aux/run-clang-format` / `run-gersemi`.
- Added `development` to the push/PR branch triggers alongside `main`/`master`,
  since that's this repo's active branch.
- Retired `.github/workflows/windows-build.yml`: it duplicated (with a
  narrower, DLL-only zip) what `push.yaml` + `build-project.yaml` +
  `Package-Windows.ps1` now do properly (installed-layout zip, plus Linux
  .deb/.ddeb and macOS .pkg on the same tag). If the old artifact naming or
  the tag format `v*` (vs. the restored pipeline's bare `MAJOR.MINOR.PATCH`)
  is load-bearing for an existing release process, this is the one change in
  this pass worth double-checking before the next tag push.
- Left codesigning/notarization untouched: `setup-macos-codesigning` degrades
  to ad-hoc signing (`haveCodesignIdent=false`) when the `MACOS_SIGNING_*`
  secrets aren't configured, which is fine for unsigned local builds but
  means macOS artifacts won't be Gatekeeper-notarized until those secrets are
  added to the repo.
- Removed the CMake-side warnings suppression that was masking CI's
  `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` request (`set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)`
  and the MSVC `/WX-` override) — see Validation below for proof the build is
  actually clean under that flag now.

## Formatting

Ran the exact pinned tool versions locally against every tracked (plus
newly-added) C/C++ and CMake file, not just this change's diff:
- `clang-format 19.1.7` (matches CI's `clang-format@19` pin) with
  `-style=file -fallback-style=none`, in place, on all of `src/*.cpp/.hpp`
  and `tests/drawing_tests.cpp`. `draw_dock.*` and `draw_source.cpp` were
  reformatted from a mixed/space-indent style to the project's tab-indent
  `.clang-format`; the new `drawing_document.*`/`preview_overlay.*` files
  were already conformant.
- `gersemi 0.22.2` (satisfies the `>=0.12.0` floor the CI check enforces) with
  the repo's `.gersemirc`, in place, on `CMakeLists.txt` and
  `tests/CMakeLists.txt` — the only two files gersemi flagged as
  non-conformant out of every tracked CMake file.

## Validation Status

Host: Windows, MSVC (VS 18 2026 Insiders toolset), CMake 4.2.0. A local
build tree (`build_smart/`, gitignored) was already configured against the
downloaded OBS 31.1.1 + obs-deps + Qt6 dependency set under `.deps/`
(gitignored) from earlier work in this session.

Verified locally, this pass:
- `cmake --build build_smart --target drawing-tests --config RelWithDebInfo`
  builds clean; `ctest -C RelWithDebInfo --output-on-failure` in
  `build_smart/tests` passes `drawing-document` (pen coverage, partial pixel
  erase + undo/redo of the erase, clear + undo, redo-history invalidation on
  new ink, alpha-correctness of translucent joined strokes, fade timing at
  midpoint and after expiry, visible-pixel checks for every shape tool, and
  bounded-undo-history eviction with baked ink surviving it).
- `cmake --build build_smart --target instant-highlight-source-draw --config RelWithDebInfo`
  builds the full plugin DLL clean.
- Reconfigured the same tree with `-DCMAKE_COMPILE_WARNING_AS_ERROR=ON` (what
  the `-ci` presets set) and rebuilt both targets from clean — zero warnings,
  zero errors, tests still pass. This is the flag CI actually enforces, so
  this is the meaningful local proxy for "CI will pass the build step."

Not verified (no Linux/macOS runner available in this environment):
- Actual GitHub Actions execution of the restored `build-project.yaml` on
  Ubuntu or macOS runners. The scripts/presets were cross-checked by reading
  (preset names, `buildspec.json` dependency keys, apt package lists, the
  `obsproject/obs-studio` PPA path for Linux's system libobs) but not
  dispatched — do not claim Linux/macOS CI passes until a workflow run
  actually completes on GitHub.
- The restored `check-format.yaml` / `run-clang-format` / `run-gersemi`
  actions were not run through actual GitHub Actions; local runs used the
  same clang-format major version and a newer-but-compatible gersemi, against
  the same `.clang-format`/`.gersemirc`, as the closest available proxy.
- Manual mouse/touch/tablet interaction in a running OBS Studio instance
  (dock UI, "Draw on preview" preview-overlay mapping, Interact-window path)
  — the plugin was compiled and loads the expected symbols, but was not
  exercised inside a live OBS process in this pass.

## Known Limitations / Follow-ups

- macOS packages are unsigned/unnotarized until `MACOS_SIGNING_*` /
  `MACOS_NOTARIZATION_*` secrets are added to the repo.
- The retired `windows-build.yml` used tag pattern `v*`; the restored release
  path (`push.yaml`) expects bare `MAJOR.MINOR.PATCH[-rc/beta N]` tags. Adjust
  tagging convention accordingly for the next release.
- README.md's feature list/compatibility section predates this pass (still
  says "Windows (64-bit)" only, no Pen/mirror-source/on-preview drawing) and
  was intentionally left untouched beyond the user's pre-existing "To build"
  addition, per this task's instruction to preserve their README edits — flag
  to the user as a candidate for a follow-up documentation refresh.
