#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <cstdint>
#include <deque>
#include <random>

namespace drawsrc {

// Tool values are persisted in OBS scene collections; never renumber them.
enum class Tool : int {
	Square = 0,
	Circle = 1,
	Arrow = 2,
	Heart = 3,
	Eraser = 4,
	Pen = 5,
	Line = 6,
	Highlighter = 7,
	Spray = 8,
	Triangle = 9,
	Star = 10,
};

constexpr int kToolMax = int(Tool::Star);

// Freehand tools follow the pointer path; the others stretch between two corners.
constexpr bool tool_is_freehand(Tool tool)
{
	return tool == Tool::Pen || tool == Tool::Eraser || tool == Tool::Highlighter || tool == Tool::Spray;
}

class DrawingDocument {
public:
	void resize(QSize size);
	void begin(QPointF point, Tool tool, qreal width, QColor color, uint32_t fadeMs);
	void move(QPointF point);
	void end(uint64_t now);
	void cancel();
	void clear();
	void undo();
	void redo();
	bool drawing() const { return drawing_; }
	bool canUndo() const { return !actions_.empty(); }
	bool canRedo() const { return !redo_.empty(); }
	const QImage &image(uint64_t now);
	uint64_t revision() const { return revision_; }

private:
	struct Action {
		Tool tool = Tool::Pen;
		QPainterPath path;
		// Spray deposits are stored explicitly so undo/redo replays are deterministic.
		QPolygonF dots;
		QPointF start;
		QPointF end;
		QColor color;
		qreal width = 3;
		uint32_t fadeMs = 0;
		uint64_t born = 0;
		bool clear = false;
	};
	static void paint(QImage &image, const Action &action, uint64_t now);
	static QPainterPath pathFor(const Action &action);
	void append(Action action);
	void spray(QPointF from, QPointF to, int count);
	QPointF bounded(QPointF point) const;
	QSize size_{1280, 720};
	QImage base_;
	// Baked composite of base_ plus every non-fading action; rebuilding this is
	// O(action count), so it only happens when the action list actually
	// changes, not on every mouse-move/render tick while drawing.
	QImage committed_;
	bool committedDirty_ = true;
	QImage cache_;
	std::deque<Action> actions_;
	std::deque<Action> redo_;
	Action preview_;
	std::mt19937 rng_{0x5eed1234u};
	bool drawing_ = false;
	bool dirty_ = true;
	// Whether the previous frame had a fading action still animating; used to
	// force one final recompute the instant a fade finishes, so the expired
	// ink actually disappears instead of lingering in a stale cached frame.
	bool fading_ = false;
	uint64_t revision_ = 0;
};

} // namespace drawsrc
