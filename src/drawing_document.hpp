#pragma once

#include <QColor>
#include <QImage>
#include <QPainterPath>
#include <QPointF>
#include <cstdint>
#include <deque>

namespace drawsrc {

// Preserve tool values used in saved OBS settings.
enum class Tool : int { Square = 0, Circle = 1, Arrow = 2, Heart = 3, Eraser = 4, Pen = 5 };

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
	QPointF bounded(QPointF point) const;
	QSize size_{1280, 720};
	QImage base_;
	QImage cache_;
	std::deque<Action> actions_;
	std::deque<Action> redo_;
	Action preview_;
	bool drawing_ = false;
	bool dirty_ = true;
	bool fading_ = false;
	uint64_t revision_ = 0;
};

} // namespace drawsrc
