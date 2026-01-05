#include <obs-module.h>
#include <obs.h>

#include "plugin-support.h"
#include "draw_source.hpp"

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#ifdef ENABLE_QT
#include "draw_dock.hpp"
#include <QTimer>
#endif

static inline void log_infof(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	blogva(LOG_INFO, fmt, args);
	va_end(args);
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

MODULE_EXPORT const char *obs_module_name(void)
{
	return PLUGIN_NAME;
}

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Instant highlight drawing source (Square/Circle/Arrow/Heart/Eraser) with an interaction dock.";
}

bool obs_module_load(void)
{
	log_infof("[%s][main] Plugin loaded (version %s)", PLUGIN_NAME, PLUGIN_VERSION);

	// Source
	obs_register_source(drawsrc::draw_source_get_info());

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	// IMPORTANT:
	// Create the dock NOW (like Smart Lower Thirds), not at FINISHED_LOADING,
	// otherwise OBS cannot restore its saved visibility/layout state.
	Draw_create_dock();
#endif

	return true;
}

void obs_module_post_load(void)
{
#if defined(ENABLE_QT)
	// After OBS finishes creating all UI, refresh sources a bit later.
	if (auto *dock = Draw_get_dock()) {
		QTimer::singleShot(250, dock, [dock]() { dock->refreshSources(); });
		QTimer::singleShot(1000, dock, [dock]() { dock->refreshSources(); });
	}
#endif
}

void obs_module_unload(void)
{
	log_infof("[%s][main] Unloading plugin %s", PLUGIN_NAME, PLUGIN_NAME);

#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	// SLT-style teardown
	Draw_destroy_dock();
#endif

	log_infof("[%s][main] Plugin %s unloaded", PLUGIN_NAME, PLUGIN_NAME);
}
