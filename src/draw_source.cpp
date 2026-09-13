#include "draw_source.hpp"

#include <util/platform.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace drawsrc {

static constexpr const char *kSourceId = "instant_highlight_source_draw";

DrawSource::DrawSource(obs_data_t *settings, obs_source_t *source) : source_(source)
{
	update(settings);
}

DrawSource::~DrawSource()
{
	if (mirror_) {
		obs_source_remove_active_child(source_, mirror_);
		obs_source_release(mirror_);
	}
	obs_enter_graphics();
	gs_texture_destroy(texture_);
	obs_leave_graphics();
}

void DrawSource::update(obs_data_t *settings)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (obs_data_get_bool(settings, "_do_clear"))
		document_.clear();
	if (obs_data_get_bool(settings, "_do_undo"))
		document_.undo();
	if (obs_data_get_bool(settings, "_do_redo"))
		document_.redo();
	for (const char *key : {"_do_clear", "_do_undo", "_do_redo"})
		obs_data_unset_user_value(settings, key);

	obs_source_t *candidate = obs_get_source_by_name(obs_data_get_string(settings, "mirror_source"));
	if (candidate != mirror_) {
		if (mirror_) {
			obs_source_remove_active_child(source_, mirror_);
			obs_source_release(mirror_);
			mirror_ = nullptr;
		}
		// OBS validates the active-child graph, including indirect scene cycles.
		if (candidate && candidate != source_ && std::strcmp(obs_source_get_id(candidate), kSourceId) != 0 &&
		    obs_source_add_active_child(source_, candidate))
			mirror_ = obs_source_get_ref(candidate);
	}
	obs_source_release(candidate);

	int w = int(obs_data_get_int(settings, "width"));
	int h = int(obs_data_get_int(settings, "height"));
	if (mirror_ && obs_data_get_bool(settings, "mirror_autosize")) {
		w = int(obs_source_get_width(mirror_));
		h = int(obs_source_get_height(mirror_));
	}
	w = std::clamp(w > 0 ? w : 1280, 64, 8192);
	h = std::clamp(h > 0 ? h : 720, 64, 8192);
	// Bound raster allocation to 16 megapixels while preserving aspect ratio.
	if (int64_t(w) * h > 16777216) {
		const double scale = std::sqrt(16777216.0 / (double(w) * h));
		w = int(w * scale);
		h = int(h * scale);
	}
	width_ = uint32_t(w);
	height_ = uint32_t(h);
	document_.resize({w, h});
	tool_ = Tool(std::clamp(int(obs_data_get_int(settings, "tool")), 0, int(Tool::Pen)));
	thickness_ = std::clamp(obs_data_get_double(settings, "thickness"), 1.0, 64.0);
	eraserSize_ = std::clamp(obs_data_get_double(settings, "eraser_size"), 4.0, 256.0);
	const auto rgba = uint32_t(obs_data_get_int(settings, "color"));
	// Preserve the dock's historical ARGB setting representation.
	color_ = QColor::fromRgba(rgba | ((rgba & 0xff000000) ? 0 : 0xff000000));
	color_.setAlphaF(color_.alphaF() *
			 double(std::clamp<long long>(obs_data_get_int(settings, "opacity"), 0, 100)) / 100.0);
	fadeMs_ = obs_data_get_int(settings, "release_mode") == 1
			  ? uint32_t(std::clamp<long long>(obs_data_get_int(settings, "fade_ms"), 50, 10000))
			  : 0;
}

void DrawSource::mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t)
{
	if (!event || (type != MOUSE_LEFT && type != MOUSE_RIGHT))
		return;
	std::lock_guard<std::mutex> lock(mutex_);
	const QPointF point(event->x, event->y);
	if (mouse_up) {
		document_.move(point);
		document_.end(os_gettime_ns());
	} else {
		const Tool tool = type == MOUSE_RIGHT ? Tool::Eraser : tool_;
		document_.begin(point, tool, tool == Tool::Eraser ? eraserSize_ : thickness_, color_, fadeMs_);
	}
}

void DrawSource::mouse_move(const obs_mouse_event *event, bool mouse_leave)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (mouse_leave)
		document_.end(os_gettime_ns());
	else if (event)
		document_.move({double(event->x), double(event->y)});
}

void DrawSource::focus(bool focused)
{
	if (!focused) {
		std::lock_guard<std::mutex> lock(mutex_);
		document_.end(os_gettime_ns());
	}
}

void DrawSource::render()
{
	// Do not hold document state while calling another source's renderer.
	obs_source_t *mirror;
	QImage image;
	uint64_t revision;
	{
		std::lock_guard<std::mutex> lock(mutex_);
		mirror = obs_source_get_ref(mirror_);
		image = document_.image(os_gettime_ns());
		revision = document_.revision();
	}
	if (mirror) {
		const uint32_t w = obs_source_get_width(mirror);
		const uint32_t h = obs_source_get_height(mirror);
		if (w && h) {
			gs_matrix_push();
			gs_matrix_scale3f(float(image.width()) / float(w), float(image.height()) / float(h), 1);
			obs_source_video_render(mirror);
			gs_matrix_pop();
		}
		obs_source_release(mirror);
	}
	if (image.isNull())
		return;
	if (texture_ && (gs_texture_get_width(texture_) != uint32_t(image.width()) ||
			 gs_texture_get_height(texture_) != uint32_t(image.height()))) {
		gs_texture_destroy(texture_);
		texture_ = nullptr;
	}
	if (!texture_) {
		texture_ = gs_texture_create(uint32_t(image.width()), uint32_t(image.height()), GS_RGBA, 1, nullptr,
					     GS_DYNAMIC);
		uploadedRevision_ = 0;
	}
	if (!texture_)
		return;
	if (uploadedRevision_ != revision) {
		gs_texture_set_image(texture_, image.constBits(), uint32_t(image.bytesPerLine()), false);
		uploadedRevision_ = revision;
	}
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
	gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), texture_);
	while (gs_effect_loop(effect, "Draw"))
		gs_draw_sprite(texture_, 0, uint32_t(image.width()), uint32_t(image.height()));
	gs_blend_state_pop();
}

static void draw_defaults(obs_data_t *settings)
{
	obs_video_info video{};
	obs_get_video_info(&video);
	obs_data_set_default_int(settings, "width", video.base_width ? video.base_width : 1280);
	obs_data_set_default_int(settings, "height", video.base_height ? video.base_height : 720);
	obs_data_set_default_string(settings, "mirror_source", "");
	obs_data_set_default_bool(settings, "mirror_autosize", true);
	obs_data_set_default_int(settings, "tool", int(Tool::Pen));
	obs_data_set_default_int(settings, "thickness", 3);
	obs_data_set_default_int(settings, "eraser_size", 32);
	obs_data_set_default_int(settings, "color", 0xffff4040);
	obs_data_set_default_int(settings, "opacity", 100);
	obs_data_set_default_int(settings, "release_mode", 0);
	obs_data_set_default_int(settings, "fade_ms", 450);
}

static obs_properties_t *draw_properties(void *)
{
	auto *props = obs_properties_create();
	auto *mirror = obs_properties_add_list(props, "mirror_source", "Mirror source (background)",
					       OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mirror, "(None)", "");
	obs_enum_sources(
		[](void *data, obs_source_t *source) {
			if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) &&
			    std::strcmp(obs_source_get_id(source), kSourceId) != 0) {
				const char *name = obs_source_get_name(source);
				obs_property_list_add_string(static_cast<obs_property_t *>(data), name, name);
			}
			return true;
		},
		mirror);
	obs_properties_add_bool(props, "mirror_autosize", "Auto-size canvas from mirror source");
	obs_properties_add_int(props, "width", "Canvas width", 64, 8192, 1);
	obs_properties_add_int(props, "height", "Canvas height", 64, 8192, 1);
	return props;
}

const obs_source_info *draw_source_get_info()
{
	static const obs_source_info info = [] {
		obs_source_info result{};
		result.id = kSourceId;
		result.type = OBS_SOURCE_TYPE_INPUT;
		result.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION;
		result.get_name = [](void *) {
			return "Instant Highlight Source Draw";
		};
		result.create = [](obs_data_t *settings, obs_source_t *source) -> void * {
			return new DrawSource(settings, source);
		};
		result.destroy = [](void *data) {
			delete static_cast<DrawSource *>(data);
		};
		result.update = [](void *data, obs_data_t *settings) {
			static_cast<DrawSource *>(data)->update(settings);
		};
		result.get_width = [](void *data) {
			return static_cast<DrawSource *>(data)->width();
		};
		result.get_height = [](void *data) {
			return static_cast<DrawSource *>(data)->height();
		};
		result.video_render = [](void *data, gs_effect_t *) {
			static_cast<DrawSource *>(data)->render();
		};
		result.get_defaults = draw_defaults;
		result.get_properties = draw_properties;
		result.mouse_click = [](void *data, const obs_mouse_event *event, int32_t type, bool up,
					uint32_t count) {
			static_cast<DrawSource *>(data)->mouse_click(event, type, up, count);
		};
		result.mouse_move = [](void *data, const obs_mouse_event *event, bool leave) {
			static_cast<DrawSource *>(data)->mouse_move(event, leave);
		};
		result.focus = [](void *data, bool focus) {
			static_cast<DrawSource *>(data)->focus(focus);
		};
		return result;
	}();
	return &info;
}

} // namespace drawsrc
