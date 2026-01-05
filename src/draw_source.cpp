#include "draw_source.hpp"

#include "plugin-support.h"

#include <util/platform.h>

#include <cmath>
#include <cstring>
#include <algorithm>

namespace drawsrc {

static constexpr const char *kSourceId = "instant_highlight_source_draw";

// Settings keys
static constexpr const char *kMirror = "mirror_source";
static constexpr const char *kAutoSize = "mirror_autosize";
static constexpr const char *kW = "width";
static constexpr const char *kH = "height";

static constexpr const char *kTool = "tool";
static constexpr const char *kThick = "thickness";
static constexpr const char *kColor = "color";
static constexpr const char *kOpacity = "opacity";

static constexpr const char *kReleaseMode = "release_mode"; // 0=keep, 1=fade
static constexpr const char *kFadeMs      = "fade_ms";      // fade duration (ms)

// NOTE: PLUGIN_NAME is an exported variable (not a string macro), so build tags at runtime.
#define LOGI(fmt, ...) blog(LOG_INFO, "[%s][draw] " fmt, PLUGIN_NAME, ##__VA_ARGS__)
#define LOGW(fmt, ...) blog(LOG_WARNING, "[%s][draw] " fmt, PLUGIN_NAME, ##__VA_ARGS__)

static inline float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

Pt DrawSource::clamp(Pt p, float w, float h)
{
	p.x = clampf(p.x, 0.f, w);
	p.y = clampf(p.y, 0.f, h);
	return p;
}

Color DrawSource::color_from_settings(uint32_t rgba, int opacity_pct)
{
	// OBS 'color' properties are effectively stored as 0x00RRGGBB (no alpha)
	// across modern builds/toolchains. Some environments may carry an alpha byte
	// in the top 8 bits; if present, we honor it.
	uint32_t a = (rgba >> 24) & 0xFFu;
	if (a == 0u)
		a = 0xFFu;

	const uint32_t r = (rgba >> 16) & 0xFFu;
	const uint32_t g = (rgba >> 8) & 0xFFu;
	const uint32_t b = (rgba >> 0) & 0xFFu;

	Color c;
	c.r = r / 255.f;
	c.g = g / 255.f;
	c.b = b / 255.f;

	const float baseA = a / 255.f;
	const float extraA = clampf(opacity_pct / 100.f, 0.f, 1.f);
	c.a = baseA * extraA;
	return c;
}

DrawSource::DrawSource(obs_data_t *settings, obs_source_t *source) : source_(source)
{
	update(settings);
}

DrawSource::~DrawSource()
{
	if (canvas_) {
		obs_enter_graphics();
		gs_texrender_destroy(canvas_);
		obs_leave_graphics();
		canvas_ = nullptr;
	}
	if (mirror_weak_) {
		obs_weak_source_release(mirror_weak_);
		mirror_weak_ = nullptr;
	}
	clear();
}

void DrawSource::update(obs_data_t *settings)
{
	if (!settings)
		return;

	// One-shot commands from the dock (transient flags)
	if (obs_data_get_bool(settings, "_do_clear")) {
		clear();
		obs_data_set_bool(settings, "_do_clear", false);
	}
	if (obs_data_get_bool(settings, "_do_undo")) {
		undo();
		obs_data_set_bool(settings, "_do_undo", false);
	}

	// Mirror source selection (background)
	const char *mirrorNameC = obs_data_get_string(settings, kMirror);
	const std::string mirrorName = mirrorNameC ? std::string(mirrorNameC) : std::string();
	auto_size_from_mirror_ = obs_data_get_bool(settings, kAutoSize);

	if (mirrorName != mirror_source_name_) {
		// Release previous weak source
		if (mirror_weak_) {
			obs_weak_source_release(mirror_weak_);
			mirror_weak_ = nullptr;
		}
		mirror_source_name_ = mirrorName;

		if (!mirror_source_name_.empty()) {
			obs_source_t *src = obs_get_source_by_name(mirror_source_name_.c_str());
			if (src) {
				mirror_weak_ = obs_source_get_weak_source(src);
				obs_source_release(src);
			}
		}
	}

	// If requested, auto-size canvas from mirror source dimensions.
	uint32_t autoW = 0, autoH = 0;
	if (auto_size_from_mirror_ && mirror_weak_) {
		obs_source_t *ms = obs_weak_source_get_source(mirror_weak_);
		if (ms) {
			autoW = obs_source_get_width(ms);
			autoH = obs_source_get_height(ms);
			obs_source_release(ms);
		}
	}

	const int w = (int)obs_data_get_int(settings, kW);
	const int h = (int)obs_data_get_int(settings, kH);

	// If auto-size is enabled and we have a mirror source size, prefer it.
	const uint32_t prefW = (autoW > 0 ? autoW : (uint32_t)(w > 0 ? w : 1920));
	const uint32_t prefH = (autoH > 0 ? autoH : (uint32_t)(h > 0 ? h : 1080));
	width_ = prefW;
	height_ = prefH;

	// If canvas size changed, force a full redraw.
	if (canvas_w_ != width_ || canvas_h_ != height_) {
		canvas_w_ = width_;
		canvas_h_ = height_;
		shapes_rev_++;
	}

	int t = (int)obs_data_get_int(settings, kTool);
	if (t < (int)Tool::Square) t = (int)Tool::Square;
	if (t > (int)Tool::Eraser) t = (int)Tool::Eraser;
	tool_ = (Tool)t;
	thickness_ = (float)obs_data_get_double(settings, kThick);
	// Clamp thickness to a compact range (dock uses 1..8).
	if (thickness_ < 1.f)
		thickness_ = 1.f;
	if (thickness_ > 8.f)
		thickness_ = 8.f;

	rgba_ = (uint32_t)obs_data_get_int(settings, kColor);
	opacity_pct_ = (int)obs_data_get_int(settings, kOpacity);
	if (opacity_pct_ < 0)
		opacity_pct_ = 0;
	if (opacity_pct_ > 100)
		opacity_pct_ = 100;

	color_ = color_from_settings(rgba_, opacity_pct_);

	// Release behavior is controlled from the dock.
	// IMPORTANT: obs_data_get_* returns 0 when a key is missing; avoid treating
	// missing keys as a mode change (older settings / transient updates).
	const bool hasMode = obs_data_has_user_value(settings, kReleaseMode);
	const bool hasFade = obs_data_has_user_value(settings, kFadeMs);

	int newMode = release_mode_;
	uint32_t newFade = fade_ms_;
	if (hasMode)
		newMode = (int)obs_data_get_int(settings, kReleaseMode);
	if (hasFade)
		newFade = (uint32_t)obs_data_get_int(settings, kFadeMs);

	if (newMode != 0 && newMode != 1)
		newMode = 0;
	newFade = (uint32_t)std::max(50, std::min(10000, (int)newFade));

	const bool modeChanged = (newMode != release_mode_);
	release_mode_ = newMode;
	fade_ms_ = newFade;

	// When switching modes, clear to avoid confusing mixed-state behavior.
	if (modeChanged) {
		clear();
		// Also invalidate cached canvas.
		canvas_rev_ = 0;
		shapes_rev_++;
	}
}

void DrawSource::clear()
{
	std::lock_guard<std::mutex> lk(mtx_);
	shapes_.clear();
	has_preview_ = false;
	is_drawing_ = false;
	shapes_rev_++;
}

void DrawSource::undo()
{
	std::lock_guard<std::mutex> lk(mtx_);
	if (!shapes_.empty())
		shapes_.pop_back();
	has_preview_ = false;
	is_drawing_ = false;
	shapes_rev_++;
}

void DrawSource::begin_shape(Pt p)
{
	std::lock_guard<std::mutex> lk(mtx_);
	p = clamp(p, (float)width_, (float)height_);

	Shape s;
	s.tool = tool_;
	s.thickness = thickness_;
	s.color = color_;
	s.start = p;
	s.end = p;


	preview_ = s;
	has_preview_ = true;
	is_drawing_ = true;

	post_commit_preview_ = false;
}

void DrawSource::update_shape(Pt p)
{
	std::lock_guard<std::mutex> lk(mtx_);
	if (!is_drawing_ || !has_preview_)
		return;

	p = clamp(p, (float)width_, (float)height_);

	
	preview_.end = p;
}

void DrawSource::end_shape(Pt p)
{
	std::lock_guard<std::mutex> lk(mtx_);
	if (!is_drawing_ || !has_preview_)
		return;

	p = clamp(p, (float)width_, (float)height_);
	preview_.end = p;

	// Commit and optionally enable fade.
	if (release_mode_ == 1) {
		preview_.born_ns = os_gettime_ns();
		preview_.fade_ms = fade_ms_;
	} else {
		preview_.born_ns = 0;
		preview_.fade_ms = 0;
	}

	shapes_.push_back(preview_);
	// Interact timing: mouse-up can arrive mid-render; keep the last stroke as a one-frame overlay
	// so it never "blinks" even if the cached canvas refresh happens on the next frame.
	post_commit_preview_ = (release_mode_ == 0);
	has_preview_ = post_commit_preview_;
	is_drawing_ = false;
	if (release_mode_ == 0) {
		// Force cached canvas rebuild next frame.
		canvas_rev_ = 0;
	}
	shapes_rev_++;
}

void DrawSource::erase_at(Pt p)
{
	std::lock_guard<std::mutex> lk(mtx_);
	p = clamp(p, (float)width_, (float)height_);

	const float radius = std::max(4.0f, thickness_ * 2.0f);
	const size_t before = shapes_.size();

	shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(),
				     [&](const Shape &s) { return shape_hits_eraser(s, p, radius); }),
		      shapes_.end());

	if (shapes_.size() != before) {
		shapes_rev_++;
	}
}

static float dist2_point_segment(Pt p, Pt a, Pt b)
{
	const float vx = b.x - a.x;
	const float vy = b.y - a.y;
	const float wx = p.x - a.x;
	const float wy = p.y - a.y;

	const float vv = vx * vx + vy * vy;
	float t = 0.0f;
	if (vv > 1e-6f)
		t = (wx * vx + wy * vy) / vv;
	t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);

	const float px = a.x + t * vx;
	const float py = a.y + t * vy;
	const float dx = p.x - px;
	const float dy = p.y - py;
	return dx * dx + dy * dy;
}

bool DrawSource::shape_hits_eraser(const Shape &s, Pt p, float radius)
{
	const float r2 = radius * radius;

	
	// For non-pen shapes, test endpoints and the segment between start/end.
	const float dxs = p.x - s.start.x;
	const float dys = p.y - s.start.y;
	if ((dxs * dxs + dys * dys) <= r2)
		return true;

	const float dxe = p.x - s.end.x;
	const float dye = p.y - s.end.y;
	if ((dxe * dxe + dye * dye) <= r2)
		return true;

	return dist2_point_segment(p, s.start, s.end) <= r2;
}

void DrawSource::mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t /*click_count*/)
{
	// Only left button draws/erases
	if (type != MOUSE_LEFT)
		return;

	const Pt p{(float)event->x, (float)event->y};

	// Eraser uses click+drag to delete existing shapes.
	if (tool_ == Tool::Eraser) {
		if (!mouse_up) {
			is_erasing_ = true;
			erase_at(p);
		} else {
			is_erasing_ = false;
		}
		return;
	}

	// Normal drawing tools.
	if (!mouse_up) {
		begin_shape(p);
	} else {
		end_shape(p);
	}
}

void DrawSource::mouse_move(const obs_mouse_event *event, bool mouse_leave)
{
	// OBS interaction can sometimes emit a "leave" instead of a final mouse_up
	// (e.g. focus loss / capture drop). If we are mid-stroke, commit what we have
	// so drawings don't "disappear" when the user releases the mouse.
	if (mouse_leave) {
		if (tool_ == Tool::Eraser) {
			is_erasing_ = false;
			return;
		}
		if (is_drawing_) {
			// Commit using the last known point if available.
			Pt p = preview_.end;
			end_shape(p);
		}
		return;
	}

	const Pt p{(float)event->x, (float)event->y};

	if (tool_ == Tool::Eraser) {
		if (is_erasing_)
			erase_at(p);
		return;
	}

	update_shape(p);
}

void DrawSource::focus(bool focus)
{
	has_focus_ = focus;
}

static void apply_color(gs_effect_t *effect, const Color &c)
{
	if (!effect)
		return;
	gs_eparam_t *param = gs_effect_get_param_by_name(effect, "color");
	if (!param)
		return;
	vec4 v{c.r, c.g, c.b, c.a};
	gs_effect_set_vec4(param, &v);
}

void DrawSource::draw_triangle(Pt a, Pt b, Pt c, Color col)
{
	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!effect)
		return;

	apply_color(effect, col);
	while (gs_effect_loop(effect, "Solid")) {
		gs_render_start(true);
		gs_vertex2f(a.x, a.y);
		gs_vertex2f(b.x, b.y);
		gs_vertex2f(c.x, c.y);
		gs_render_stop(GS_TRIS);
	}
}

void DrawSource::draw_filled_circle(Pt center, float radius, Color col)
{
	if (radius <= 0.1f)
		return;

	gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_SOLID);
	if (!effect)
		return;

	// Segment count based on radius for smooth caps/joins.
	const int segments = (int)clampf(radius * 0.9f, 10.f, 48.f);

	apply_color(effect, col);
	while (gs_effect_loop(effect, "Solid")) {
		gs_render_start(true);
		// Build a triangle fan as a triangle list (GS_TRIS).
		Pt prev{center.x + radius, center.y};
		for (int i = 1; i <= segments; ++i) {
			const float t = (float)i / (float)segments;
			const float ang = t * 6.28318530718f;
			Pt cur{center.x + std::cos(ang) * radius, center.y + std::sin(ang) * radius};
			gs_vertex2f(center.x, center.y);
			gs_vertex2f(prev.x, prev.y);
			gs_vertex2f(cur.x, cur.y);
			prev = cur;
		}
		gs_render_stop(GS_TRIS);
	}
}

void DrawSource::draw_thick_line(Pt a, Pt b, float t, Color col)
{
	const float dx = b.x - a.x;
	const float dy = b.y - a.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 0.5f) {
		// Render a tiny square dot
		const float r = t * 0.5f;
		Pt p1{a.x - r, a.y - r};
		Pt p2{a.x + r, a.y - r};
		Pt p3{a.x + r, a.y + r};
		Pt p4{a.x - r, a.y + r};
		draw_triangle(p1, p2, p3, col);
		draw_triangle(p1, p3, p4, col);
		return;
	}

	const float nx = -dy / len;
	const float ny = dx / len;
	const float r = t * 0.5f;

	Pt v1{a.x + nx * r, a.y + ny * r};
	Pt v2{a.x - nx * r, a.y - ny * r};
	Pt v3{b.x - nx * r, b.y - ny * r};
	Pt v4{b.x + nx * r, b.y + ny * r};

	draw_triangle(v1, v2, v3, col);
	draw_triangle(v1, v3, v4, col);
}


void DrawSource::draw_square(const Shape &s)
{
	const Pt a{s.start.x, s.start.y};
	const Pt b{s.end.x, s.start.y};
	const Pt c{s.end.x, s.end.y};
	const Pt d{s.start.x, s.end.y};
	draw_thick_line(a, b, s.thickness, s.color);
	draw_thick_line(b, c, s.thickness, s.color);
	draw_thick_line(c, d, s.thickness, s.color);
	draw_thick_line(d, a, s.thickness, s.color);
}

void DrawSource::draw_circle(const Shape &s)
{
	const float cx = (s.start.x + s.end.x) * 0.5f;
	const float cy = (s.start.y + s.end.y) * 0.5f;
	const float rx = std::fabs(s.end.x - s.start.x) * 0.5f;
	const float ry = std::fabs(s.end.y - s.start.y) * 0.5f;

	const float r = std::max(1.f, std::max(rx, ry));
	const int segments = (int)clampf(r * 0.35f, 24.f, 96.f);

	Pt prev{cx + rx, cy};
	for (int i = 1; i <= segments; ++i) {
		const float t = (float)i / (float)segments;
		const float ang = t * 6.28318530718f;
		Pt cur{cx + std::cos(ang) * rx, cy + std::sin(ang) * ry};
		draw_thick_line(prev, cur, s.thickness, s.color);
		prev = cur;
	}
}


void DrawSource::draw_heart(const Shape &s)
{
	// Draw a simple parametric heart outline, scaled to the shape bounds.
	// We approximate the curve with a polyline and render it as thick segments.
	Pt a = s.start;
	Pt b = s.end;

	const float x0 = std::min(a.x, b.x);
	const float y0 = std::min(a.y, b.y);
	const float x1 = std::max(a.x, b.x);
	const float y1 = std::max(a.y, b.y);

	const float w = std::max(1.f, x1 - x0);
	const float h = std::max(1.f, y1 - y0);

	// Parametric heart in [-1..1] range (classic curve)
	// x = 16 sin^3(t), y = 13 cos(t) - 5 cos(2t) - 2 cos(3t) - cos(4t)
	// We'll normalize and map to bounds. Note: y is inverted (screen coords).
	const int steps = 64;
	std::vector<Pt> pts;
	pts.reserve(steps + 1);

	float minx =  1e9f, maxx = -1e9f;
	float miny =  1e9f, maxy = -1e9f;

	for (int i = 0; i <= steps; ++i) {
		const float t = (float)i / (float)steps * (2.f * 3.14159265358979323846f);
		const float st = std::sin(t);
		const float ct = std::cos(t);

		const float x = 16.f * st * st * st;
		const float y = 13.f * ct - 5.f * std::cos(2.f * t) - 2.f * std::cos(3.f * t) - std::cos(4.f * t);

		minx = std::min(minx, x); maxx = std::max(maxx, x);
		miny = std::min(miny, y); maxy = std::max(maxy, y);

		pts.push_back(Pt{x, y});
	}

	const float sx = w / std::max(1e-6f, (maxx - minx));
	const float sy = h / std::max(1e-6f, (maxy - miny));

	for (auto &p : pts) {
		const float nx = (p.x - minx) * sx + x0;
		// y: invert so the "top lobes" are on top in screen coordinates
		const float ny = y1 - (p.y - miny) * sy;
		p.x = nx;
		p.y = ny;
	}

	// Draw segments
	for (size_t i = 1; i < pts.size(); ++i) {
		draw_thick_line(pts[i - 1], pts[i], s.thickness, s.color);
	}
}


void DrawSource::draw_arrow(const Shape &s)
{
	// Arrow head
	const float dx = s.end.x - s.start.x;
	const float dy = s.end.y - s.start.y;
	const float len = std::sqrt(dx * dx + dy * dy);
	if (len < 1.f)
		return;

	const float ux = dx / len;
	const float uy = dy / len;
	const float nx = -uy;
	const float ny = ux;

	const float headLen = std::max(10.f, s.thickness * 4.f);
	const float headWid = std::max(6.f, s.thickness * 2.5f);

	Pt tip = s.end;
	Pt base{tip.x - ux * headLen, tip.y - uy * headLen};
	Pt left{base.x + nx * headWid, base.y + ny * headWid};
	Pt right{base.x - nx * headWid, base.y - ny * headWid};

	// Draw shaft only up to the base of the head to avoid alpha-overlap darkening.
	draw_thick_line(s.start, base, s.thickness, s.color);

	draw_triangle(tip, left, right, s.color);
}

void DrawSource::draw_shape(const Shape &s)
{
	switch (s.tool) {
	case Tool::Square:
		draw_square(s);
		break;
	case Tool::Circle:
		draw_circle(s);
		break;
	case Tool::Arrow:
		draw_arrow(s);
		break;
	case Tool::Heart:
		draw_heart(s);
		break;
	default:
		break;
	}
}

void DrawSource::render()
{
	const uint64_t now = os_gettime_ns();

	// Fade mode needs per-frame alpha updates and pruning.
	if (release_mode_ == 1) {
		std::vector<Shape> shapesCopy;
		Shape previewCopy;
		bool hasPreview = false;

		// Prune expired shapes and snapshot state.
		{
			std::lock_guard<std::mutex> lk(mtx_);
			if (!shapes_.empty()) {
				size_t before = shapes_.size();
				shapes_.erase(std::remove_if(shapes_.begin(), shapes_.end(), [&](const Shape &s) {
					if (s.fade_ms == 0 || s.born_ns == 0)
						return false;
					const uint64_t elapsed_ns = (now > s.born_ns) ? (now - s.born_ns) : 0;
					const uint64_t fade_ns = (uint64_t)s.fade_ms * 1000000ULL;
					return elapsed_ns >= fade_ns;
				}),
					      shapes_.end());
				if (shapes_.size() != before)
					shapes_rev_++;
			}

			shapesCopy = shapes_;
			if (has_preview_) {
				previewCopy = preview_;
				hasPreview = true;
			}
		}

		// Mirror background source (if selected)
		if (mirror_weak_) {
			obs_source_t *ms = obs_weak_source_get_source(mirror_weak_);
			if (ms) {
				const uint32_t mw = obs_source_get_width(ms);
				const uint32_t mh = obs_source_get_height(ms);

				gs_matrix_push();
				gs_ortho(0.f, (float)width_, 0.f, (float)height_, -100.f, 100.f);
				if (mw > 0 && mh > 0) {
					gs_matrix_scale3f((float)width_ / (float)mw, (float)height_ / (float)mh, 1.f);
				}
				obs_source_video_render(ms);
				gs_matrix_pop();

				obs_source_release(ms);
			}
		}

		// Draw shapes with alpha adjusted per elapsed time.
		gs_matrix_push();
		gs_ortho(0.f, (float)width_, 0.f, (float)height_, -100.f, 100.f);
		for (auto &s : shapesCopy) {
			if (s.fade_ms > 0 && s.born_ns > 0) {
				const uint64_t elapsed_ns = (now > s.born_ns) ? (now - s.born_ns) : 0;
				const float elapsed_ms = (float)elapsed_ns / 1000000.f;
				const float a = 1.f - clampf(elapsed_ms / (float)s.fade_ms, 0.f, 1.f);
				Color col = s.color;
				col.a *= a;
				Shape tmp = s;
				tmp.color = col;
				draw_shape(tmp);
			} else {
				draw_shape(s);
			}
		}
		if (hasPreview) {
			// Preview should be fully visible while dragging.
			draw_shape(previewCopy);
		}
		gs_matrix_pop();
		return;
	}

	// Keep-until-cleared should behave like fade mode in terms of rendering
	// (i.e. re-draw all committed shapes every frame), but without any alpha
	// decay. This avoids edge cases in the Interact rendering path where a
	// cached texrender can appear to "blink" or not refresh immediately.
	std::vector<Shape> shapesCopy;
	Shape previewCopy;
	bool hasPreview = false;

	{
		std::lock_guard<std::mutex> lk(mtx_);
		shapesCopy = shapes_;
		if (has_preview_) {
			previewCopy = preview_;
			hasPreview = true;
		}
	}

	// Mirror background source (if selected)
	if (mirror_weak_) {
		obs_source_t *ms = obs_weak_source_get_source(mirror_weak_);
		if (ms) {
			const uint32_t mw = obs_source_get_width(ms);
			const uint32_t mh = obs_source_get_height(ms);

			gs_matrix_push();
			gs_ortho(0.f, (float)width_, 0.f, (float)height_, -100.f, 100.f);
			if (mw > 0 && mh > 0) {
				gs_matrix_scale3f((float)width_ / (float)mw, (float)height_ / (float)mh, 1.f);
			}
			obs_source_video_render(ms);
			gs_matrix_pop();

			obs_source_release(ms);
		}
	}

	// Draw committed shapes.
	gs_matrix_push();
	gs_ortho(0.f, (float)width_, 0.f, (float)height_, -100.f, 100.f);
	for (const auto &s : shapesCopy)
		draw_shape(s);
	if (hasPreview)
		draw_shape(previewCopy);
	gs_matrix_pop();

	// If this was a post-commit one-frame overlay, clear it now.
	if (hasPreview) {
		std::lock_guard<std::mutex> lk(mtx_);
		if (post_commit_preview_ && !is_drawing_) {
			post_commit_preview_ = false;
			has_preview_ = false;
		}
	}
}

// --- Pen smoothing helpers ---

std::vector<Pt> DrawSource::simplify_polyline(const std::vector<Pt> &pts, float minDist)
{
	if (pts.size() <= 2)
		return pts;

	const float md2 = minDist * minDist;
	std::vector<Pt> out;
	out.reserve(pts.size());
	out.push_back(pts.front());
	Pt last = pts.front();

	for (size_t i = 1; i + 1 < pts.size(); ++i) {
		const float dx = pts[i].x - last.x;
		const float dy = pts[i].y - last.y;
		if (dx * dx + dy * dy >= md2) {
			out.push_back(pts[i]);
			last = pts[i];
		}
	}
	out.push_back(pts.back());
	return out;
}

static inline Pt catmull(const Pt &p0, const Pt &p1, const Pt &p2, const Pt &p3, float t)
{
	const float t2 = t * t;
	const float t3 = t2 * t;
	Pt r;
	r.x = 0.5f * ((2.f * p1.x) + (-p0.x + p2.x) * t + (2.f * p0.x - 5.f * p1.x + 4.f * p2.x - p3.x) * t2 +
		      (-p0.x + 3.f * p1.x - 3.f * p2.x + p3.x) * t3);
	r.y = 0.5f * ((2.f * p1.y) + (-p0.y + p2.y) * t + (2.f * p0.y - 5.f * p1.y + 4.f * p2.y - p3.y) * t2 +
		      (-p0.y + 3.f * p1.y - 3.f * p2.y + p3.y) * t3);
	return r;
}

std::vector<Pt> DrawSource::smooth_catmull_rom(const std::vector<Pt> &pts, float stepPx)
{
	if (pts.size() <= 2)
		return pts;

	// Build a smoothed path with roughly constant spacing.
	std::vector<Pt> out;
	out.reserve(pts.size() * 2);

	// Extend endpoints.
	std::vector<Pt> p;
	p.reserve(pts.size() + 2);
	p.push_back(pts.front());
	p.insert(p.end(), pts.begin(), pts.end());
	p.push_back(pts.back());

	Pt prev = pts.front();
	out.push_back(prev);

	for (size_t i = 0; i + 3 < p.size(); ++i) {
		const Pt &p0 = p[i];
		const Pt &p1 = p[i + 1];
		const Pt &p2 = p[i + 2];
		const Pt &p3 = p[i + 3];

		// Subdivide this segment.
		const int steps = 12;
		for (int s = 1; s <= steps; ++s) {
			const float t = (float)s / (float)steps;
			Pt cur = catmull(p0, p1, p2, p3, t);
			const float dx = cur.x - prev.x;
			const float dy = cur.y - prev.y;
			const float d = std::sqrt(dx * dx + dy * dy);
			if (d >= stepPx) {
				out.push_back(cur);
				prev = cur;
			}
		}
	}

	if (out.back().x != pts.back().x || out.back().y != pts.back().y)
		out.push_back(pts.back());

	return out;
}

// ---------------- OBS callbacks ----------------

static const char *draw_get_name(void * /*unused*/)
{
	return "Instant Highlight Source Draw";
}

static void *draw_create(obs_data_t *settings, obs_source_t *source)
{
	return new DrawSource(settings, source);
}

static void draw_destroy(void *data)
{
	delete static_cast<DrawSource *>(data);
}

static void draw_update(void *data, obs_data_t *settings)
{
	static_cast<DrawSource *>(data)->update(settings);
}

static uint32_t draw_width(void *data)
{
	return static_cast<DrawSource *>(data)->width();
}

static uint32_t draw_height(void *data)
{
	return static_cast<DrawSource *>(data)->height();
}

static void draw_render(void *data, gs_effect_t * /*effect*/)
{
	static_cast<DrawSource *>(data)->render();
}

static void draw_defaults(obs_data_t *settings)
{
	// Mirror
	obs_data_set_default_string(settings, kMirror, "");
	obs_data_set_default_bool(settings, kAutoSize, true);

	// Canvas size (used when autosize is off or mirror is none)
	obs_data_set_default_int(settings, kW, 1280);
	obs_data_set_default_int(settings, kH, 720);

	// Drawing defaults
	obs_data_set_default_int(settings, kTool, 0); // 0 = Square
	obs_data_set_default_int(settings, kThick, 3);
	obs_data_set_default_int(settings, kColor, 0x000000FF); // OBS color property is typically 0x00BBGGRR
	obs_data_set_default_int(settings, kOpacity, 100);

	// Release behavior (controlled from dock)
	obs_data_set_default_int(settings, kReleaseMode, 0); // keep
	obs_data_set_default_int(settings, kFadeMs, 450);
}

static bool props_clear_cb(obs_properties_t * /*props*/, obs_property_t * /*p*/, void *data)
{
	static_cast<DrawSource *>(data)->clear();
	return true;
}

static bool props_undo_cb(obs_properties_t * /*props*/, obs_property_t * /*p*/, void *data)
{
	static_cast<DrawSource *>(data)->undo();
	return true;
}

static obs_properties_t *draw_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	// Mirror an existing OBS source as a background (useful for drawing via Interact).
	// Note: this mirrors into the Draw Source output as well (OBS does not provide a reliable
	// "interact-only" render path for a source).
	obs_property_t *mirror = obs_properties_add_list(props, kMirror, "Mirror source (background)",
							 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
	obs_property_list_add_string(mirror, "(None)", "");

	// Populate list with current sources.
	struct EnumCtx {
		obs_property_t *prop;
	};
	EnumCtx ctx{mirror};

	auto enum_cb = [](void *d, obs_source_t *s) -> bool {
		auto *c = (EnumCtx *)d;
		if (!s)
			return true;

		// Only show sources that can produce video.
		const uint32_t flags = obs_source_get_output_flags(s);
		if ((flags & OBS_SOURCE_VIDEO) == 0)
			return true;

		const char *id = obs_source_get_id(s);
		if (!id || !*id)
			return true;

		// Exclude this plugin's own sources to avoid recursion/self-mirroring.
		if (strcmp(id, kSourceId) == 0)
			return true;

		// Exclude text sources (GDI+/FT2/etc.) and other non-useful overlays.
		// Common IDs: text_gdiplus, text_ft2_source (and many start with "text_").
		if (strncmp(id, "text_", 5) == 0 || strcmp(id, "text_gdiplus") == 0 || strcmp(id, "text_ft2_source") == 0)
			return true;

		const char *n = obs_source_get_name(s);
		if (!n || !*n)
			return true;

		obs_property_list_add_string(c->prop, n, n);
		return true;
	};
	obs_enum_sources(enum_cb, &ctx);

	obs_properties_add_bool(props, kAutoSize, "Auto-size canvas from mirror source");

	obs_properties_add_int(props, kW, "Canvas width", 64, 8192, 1);
	obs_properties_add_int(props, kH, "Canvas height", 64, 8192, 1);

	// The tool UI (tool/thickness/color/opacity/undo/clear) is controlled from the dock.
	// Keeping duplicate controls here causes confusion and drift.
	// We intentionally expose only canvas/background sizing here.

	(void)data;
	return props;
}

static void draw_mouse_click(void *data, const obs_mouse_event *event, int32_t type, bool mouse_up,
			     uint32_t click_count)
{
	static_cast<DrawSource *>(data)->mouse_click(event, type, mouse_up, click_count);
}

static void draw_mouse_move(void *data, const obs_mouse_event *event, bool mouse_leave)
{
	static_cast<DrawSource *>(data)->mouse_move(event, mouse_leave);
}

static void draw_focus(void *data, bool focus)
{
	static_cast<DrawSource *>(data)->focus(focus);
}

const obs_source_info *draw_source_get_info()
{
	static obs_source_info info;
	static bool inited = false;
	if (!inited) {
		memset(&info, 0, sizeof(info));
		info.id = kSourceId;
		info.type = OBS_SOURCE_TYPE_INPUT;
		info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW | OBS_SOURCE_INTERACTION;

		info.get_name = draw_get_name;
		info.create = draw_create;
		info.destroy = draw_destroy;
		info.update = draw_update;
		info.get_width = draw_width;
		info.get_height = draw_height;
		info.video_render = draw_render;
		info.get_defaults = draw_defaults;
		info.get_properties = draw_properties;

		info.mouse_click = draw_mouse_click;
		info.mouse_move = draw_mouse_move;
		info.focus = draw_focus;
		inited = true;
	}
	return &info;
}

} // namespace drawsrc
