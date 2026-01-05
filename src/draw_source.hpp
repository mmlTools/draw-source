#pragma once

#include <obs-module.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

// Simple interactive drawing source.
// The source stores vector primitives and renders them each frame.
// Interaction happens through OBS' source interaction (Interact window).

namespace drawsrc {

enum class Tool : int {
	Square = 0,
	Circle = 1,
	Arrow = 2,
	Heart = 3,
	Eraser = 4,
};

struct Pt {
	float x = 0.f;
	float y = 0.f;
};

struct Color {
	float r = 1.f;
	float g = 1.f;
	float b = 1.f;
	float a = 1.f;
};

struct Shape {
	Tool tool = Tool::Square;
	float thickness = 4.f;
	Color color{};

	// Optional fade-out (used when release behavior is set to "Fade after release").
	// If fade_ms == 0, the shape is persistent until cleared/erased.
	uint64_t born_ns = 0;
	uint32_t fade_ms = 0;

	// (Removed Pen tool) Reserved for future polyline tools.
	std::vector<Pt> points;

	// Other tools: start/end points.
	Pt start{};
	Pt end{};
};

class DrawSource {
public:
	explicit DrawSource(obs_data_t *settings, obs_source_t *source);
	~DrawSource();

	void update(obs_data_t *settings);
	void render();
	uint32_t width() const { return width_; }
	uint32_t height() const { return height_; }

	// Interaction callbacks
	void mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count);
	void mouse_move(const obs_mouse_event *event, bool mouse_leave);
	void focus(bool focus);

	// Commands
	void clear();
	void undo();

	// Current tool settings (from dock or properties)
	Tool tool() const { return tool_; }
	float thickness() const { return thickness_; }
	Color color() const { return color_; }

private:
	void begin_shape(Pt p);
	void update_shape(Pt p);
	void end_shape(Pt p);

	// Eraser
	void erase_at(Pt p);
	static bool shape_hits_eraser(const Shape &s, Pt p, float radius);

	// Pen helpers
	static std::vector<Pt> simplify_polyline(const std::vector<Pt> &pts, float minDist);
	static std::vector<Pt> smooth_catmull_rom(const std::vector<Pt> &pts, float stepPx);

	void draw_shape(const Shape &s);
	void draw_square(const Shape &s);
	void draw_circle(const Shape &s);
	void draw_arrow(const Shape &s);
	void draw_heart(const Shape &s);
	void draw_thick_line(Pt a, Pt b, float t, Color c);
	void draw_triangle(Pt a, Pt b, Pt c, Color col);
	void draw_filled_circle(Pt center, float radius, Color col);

	static Pt clamp(Pt p, float w, float h);
	static Color color_from_settings(uint32_t rgba, int opacity_pct);

private:
	obs_source_t *source_ = nullptr;

	// Settings
	uint32_t width_ = 1920;
	uint32_t height_ = 1080;
	Tool tool_ = Tool::Square;
	float thickness_ = 4.f;
	uint32_t rgba_ = 0xFFFFFFFFu; // OBS color format varies; decoded in color_from_settings
	int opacity_pct_ = 100;       // 0..100
	Color color_{};

	// Release behavior
	// 0 = Keep until cleared, 1 = Fade after release
	int release_mode_ = 0;
	uint32_t fade_ms_ = 450;

	// Optional mirrored background source (useful when drawing via Interact)
	std::string mirror_source_name_;
	bool auto_size_from_mirror_ = false;
	obs_weak_source_t *mirror_weak_ = nullptr;

	// Runtime
	mutable std::mutex mtx_;
	std::vector<Shape> shapes_;
	bool is_drawing_ = false;
	bool is_erasing_ = false;
	Shape preview_{};
	bool has_preview_ = false;

	bool has_focus_ = false;
	bool post_commit_preview_ = false; // keeps last stroke visible for 1 frame after commit (Interact timing)

	// Performance: cache committed shapes into a texture and only re-render when changed.
	gs_texrender_t *canvas_ = nullptr;
	uint64_t shapes_rev_ = 1; // increments on any committed change
	uint64_t canvas_rev_ = 0; // last revision rendered into canvas_
	uint32_t canvas_w_ = 0;
	uint32_t canvas_h_ = 0;
};

// OBS source definition accessor
const obs_source_info *draw_source_get_info();

} // namespace drawsrc