#pragma once

#include "drawing_document.hpp"

#include <QColor>
#include <QIcon>

namespace drawsrc {

// Monochrome tool glyphs painted at runtime in the given color, so they follow
// whichever OBS theme is active instead of shipping fixed bitmaps.
QIcon tool_icon(Tool tool, const QColor &color);

// Loads one of OBS Studio's own theme icons ("plus.svg", "sources/brush.svg",
// ...) from the Dark or Light set via the "theme:" search path OBS registers.
QIcon theme_icon(const char *relative, bool dark);

// Horizontal mirror of an icon, used to derive a redo glyph from OBS's revert icon.
QIcon mirrored_icon(const QIcon &icon);

// Rounded swatch showing the current ink color.
QIcon color_swatch_icon(const QColor &fill, const QColor &outline);

} // namespace drawsrc
