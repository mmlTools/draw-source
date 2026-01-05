/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

// NOTE:
// Do not include OBS internal headers here.
// This header is used by the standalone generated plugin-support.c compilation unit,
// which in this template may not inherit OBS include directories.
// Keep this header self-contained.

// Provided by the plugin bootstrap (configured from config.hpp.in).
// Note: in this template they are variables, not string-literal macros.
extern const char *PLUGIN_NAME;
extern const char *PLUGIN_VERSION;

// Logging is performed via OBS' `blog()` inside the generated plugin-support.c.
// We intentionally avoid referencing `blogva()` here to prevent missing-include
// failures in plugin-support.c and linkage conflicts in C++ translation units.

#ifdef __cplusplus
}
#endif
