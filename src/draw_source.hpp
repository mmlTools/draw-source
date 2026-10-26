#pragma once

#include "drawing_document.hpp"
#include <obs-module.h>
#include <atomic>
#include <mutex>

namespace drawsrc {

// Source type id; persisted in scene collections, never change it.
constexpr const char *kDrawSourceId = "instant_highlight_source_draw";

class DrawSource {
public:
	explicit DrawSource(obs_data_t *settings, obs_source_t *source);
	~DrawSource();
	void update(obs_data_t *settings);
	void render();
	uint32_t width() const { return width_.load(); }
	uint32_t height() const { return height_.load(); }
	void mouse_click(const obs_mouse_event *event, int32_t type, bool mouse_up, uint32_t click_count);
	void mouse_move(const obs_mouse_event *event, bool mouse_leave);
	void focus(bool focus);

private:
	static void draw_eraser_ring(QPointF center, float radius);
	obs_source_t *source_;
	std::mutex mutex_;
	DrawingDocument document_;
	std::atomic<uint32_t> width_{1280};
	std::atomic<uint32_t> height_{720};
	Tool tool_ = Tool::Pen;
	qreal thickness_ = 3;
	qreal eraserSize_ = 32;
	QColor color_{Qt::red};
	uint32_t fadeMs_ = 0;
	// Pointer state from the Interact window (or the preview overlay) used to
	// show the eraser footprint while hovering.
	bool hovering_ = false;
	bool strokeActive_ = false;
	bool strokeEraser_ = false;
	QPointF hover_;
	// Overlays draw their own cursor ring, so each active one asks the source
	// to keep the ring out of the program output (counted, several may run).
	std::atomic<int> ringHidden_{0};
	obs_source_t *mirror_ = nullptr;
	gs_texture_t *texture_ = nullptr;
	uint64_t uploadedRevision_ = 0;
};

const obs_source_info *draw_source_get_info();

} // namespace drawsrc
