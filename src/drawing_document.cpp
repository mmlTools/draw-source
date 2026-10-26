#include "drawing_document.hpp"

#include <QLineF>
#include <QPainter>
#include <QTransform>
#include <algorithm>
#include <cmath>

namespace drawsrc {

namespace {

constexpr qreal kPi = 3.14159265358979323846;

// Spray radius grows with the requested thickness but never collapses to a pinpoint.
qreal spray_radius(qreal width)
{
	return std::max(6.0, width * 2.0);
}

qreal spray_dot_size(qreal width)
{
	return std::clamp(spray_radius(width) / 8.0, 1.5, 6.0);
}

} // namespace

void DrawingDocument::resize(QSize size)
{
	if (size == size_ || size.isEmpty())
		return;
	// Preserve ink in source coordinates when canvas dimensions change.
	if (!base_.isNull()) {
		QImage next(size, QImage::Format_RGBA8888_Premultiplied);
		next.fill(Qt::transparent);
		QPainter painter(&next);
		painter.drawImage(0, 0, base_);
		painter.end();
		base_ = next;
	}
	size_ = size;
	dirty_ = true;
	committedDirty_ = true;
}

QPointF DrawingDocument::bounded(QPointF point) const
{
	return {std::clamp(point.x(), 0.0, double(size_.width())), std::clamp(point.y(), 0.0, double(size_.height()))};
}

void DrawingDocument::begin(QPointF point, Tool tool, qreal width, QColor color, uint32_t fadeMs)
{
	preview_ = {};
	preview_.tool = tool;
	preview_.width = std::clamp(width, 1.0, 256.0);
	preview_.color = color;
	preview_.fadeMs = tool == Tool::Eraser ? 0 : fadeMs;
	if (tool == Tool::Highlighter) {
		// A marker is wide and translucent regardless of the requested ink.
		preview_.width = std::clamp(width * 3.0, 6.0, 256.0);
		preview_.color.setAlphaF(preview_.color.alphaF() * 0.45f);
	}
	preview_.start = preview_.end = bounded(point);
	preview_.path.moveTo(preview_.start);
	if (tool == Tool::Spray)
		spray(preview_.start, preview_.start, int(spray_radius(preview_.width)));
	drawing_ = true;
	dirty_ = true;
}

void DrawingDocument::move(QPointF point)
{
	if (!drawing_)
		return;
	point = bounded(point);
	if (point == preview_.end)
		return;
	const QPointF previous = preview_.end;
	preview_.end = point;
	if (preview_.tool == Tool::Spray) {
		// Deposit proportionally to travel so slow and fast strokes look alike.
		const qreal distance = QLineF(previous, point).length();
		spray(previous, point, std::max(1, int(std::ceil(distance * spray_radius(preview_.width) / 6.0))));
	} else if (tool_is_freehand(preview_.tool)) {
		preview_.path.lineTo(point);
	}
	dirty_ = true;
}

void DrawingDocument::spray(QPointF from, QPointF to, int count)
{
	const qreal radius = spray_radius(preview_.width);
	std::uniform_real_distribution<qreal> unit(0.0, 1.0);
	for (int i = 0; i < count; ++i) {
		const QPointF center = from + (to - from) * unit(rng_);
		// sqrt keeps the density uniform across the disc instead of clumping in the middle.
		const qreal r = radius * std::sqrt(unit(rng_));
		const qreal a = unit(rng_) * 2 * kPi;
		preview_.dots.append(bounded(center + QPointF(std::cos(a) * r, std::sin(a) * r)));
	}
}

void DrawingDocument::append(Action action)
{
	redo_.clear();
	actions_.push_back(std::move(action));
	// Bake old persistent operations to bound undo history without losing ink.
	while (actions_.size() > 128) {
		if (base_.isNull()) {
			base_ = QImage(size_, QImage::Format_RGBA8888_Premultiplied);
			base_.fill(Qt::transparent);
		}
		if (!actions_.front().fadeMs)
			paint(base_, actions_.front(), 0);
		actions_.pop_front();
	}
	dirty_ = true;
	committedDirty_ = true;
}

void DrawingDocument::end(uint64_t now)
{
	if (!drawing_)
		return;
	preview_.born = now;
	append(preview_);
	drawing_ = false;
}

void DrawingDocument::cancel()
{
	drawing_ = false;
	dirty_ = true;
}

void DrawingDocument::clear()
{
	cancel();
	Action action;
	action.clear = true;
	append(action);
}

void DrawingDocument::undo()
{
	cancel();
	if (!actions_.empty()) {
		redo_.push_back(std::move(actions_.back()));
		actions_.pop_back();
		committedDirty_ = true;
	}
}

void DrawingDocument::redo()
{
	cancel();
	if (!redo_.empty()) {
		actions_.push_back(std::move(redo_.back()));
		redo_.pop_back();
		committedDirty_ = true;
	}
}

QPainterPath DrawingDocument::pathFor(const Action &a)
{
	QPainterPath path;
	const QRectF bounds = QRectF(a.start, a.end).normalized();
	switch (a.tool) {
	case Tool::Pen:
	case Tool::Eraser:
	case Tool::Highlighter:
	case Tool::Spray:
		return a.path;
	case Tool::Line:
		path.moveTo(a.start);
		path.lineTo(a.end);
		break;
	case Tool::Square:
		path.addRect(bounds);
		break;
	case Tool::Circle:
		path.addEllipse(bounds);
		break;
	case Tool::Triangle:
		path.moveTo(bounds.center().x(), bounds.top());
		path.lineTo(bounds.bottomRight());
		path.lineTo(bounds.bottomLeft());
		path.closeSubpath();
		break;
	case Tool::Star: {
		const QPointF center = bounds.center();
		const qreal rx = bounds.width() / 2;
		const qreal ry = bounds.height() / 2;
		for (int i = 0; i < 10; ++i) {
			const qreal r = i % 2 ? 0.4 : 1.0;
			const qreal angle = -kPi / 2 + i * kPi / 5;
			const QPointF point(center.x() + std::cos(angle) * rx * r,
					    center.y() + std::sin(angle) * ry * r);
			if (i)
				path.lineTo(point);
			else
				path.moveTo(point);
		}
		path.closeSubpath();
		break;
	}
	case Tool::Arrow: {
		QLineF line(a.start, a.end);
		if (line.length() < 1)
			break;
		const QPointF direction = (a.end - a.start) / line.length();
		const QPointF normal(-direction.y(), direction.x());
		const qreal head = std::min(line.length(), std::max(10.0, a.width * 4));
		path.moveTo(a.start);
		path.lineTo(a.end);
		path.moveTo(a.end - direction * head + normal * head * 0.5);
		path.lineTo(a.end);
		path.lineTo(a.end - direction * head - normal * head * 0.5);
		break;
	}
	case Tool::Heart: {
		path.moveTo(0.5, 1);
		path.cubicTo(-0.6, 0.25, 0.15, -0.35, 0.5, 0.2);
		path.cubicTo(0.85, -0.35, 1.6, 0.25, 0.5, 1);
		QTransform transform;
		transform.translate(bounds.x(), bounds.y());
		transform.scale(bounds.width(), bounds.height());
		path = transform.map(path);
		break;
	}
	}
	return path;
}

void DrawingDocument::paint(QImage &image, const Action &a, uint64_t now)
{
	if (a.clear) {
		image.fill(Qt::transparent);
		return;
	}
	QColor color = a.color;
	if (a.fadeMs && a.born) {
		const double elapsed = double(now > a.born ? now - a.born : 0) / 1000000.0;
		color.setAlphaF(color.alphaF() * std::clamp(1.0 - elapsed / a.fadeMs, 0.0, 1.0));
		if (color.alpha() == 0)
			return;
	}
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	if (a.tool == Tool::Eraser)
		painter.setCompositionMode(QPainter::CompositionMode_Clear);
	if (a.tool == Tool::Spray) {
		if (a.dots.isEmpty())
			return;
		painter.setPen(QPen(color, spray_dot_size(a.width), Qt::SolidLine, Qt::RoundCap));
		painter.drawPoints(a.dots);
		return;
	}
	QPen pen(color, a.width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
	if (a.tool == Tool::Highlighter) {
		pen.setCapStyle(Qt::SquareCap);
		pen.setJoinStyle(Qt::BevelJoin);
	}
	painter.setPen(pen);
	if (a.start == a.end && a.path.elementCount() <= 1)
		painter.drawPoint(a.start);
	else
		painter.drawPath(pathFor(a));
}

const QImage &DrawingDocument::image(uint64_t now)
{
	// Rebuilding committed_ replays every non-fading action, so it only
	// happens when the action list itself changed (append/undo/redo/clear/
	// resize) - never merely because a stroke is being dragged or a frame
	// is being rendered, or the cost would grow with total ink drawn.
	if (committedDirty_) {
		committed_ = QImage(size_, QImage::Format_RGBA8888_Premultiplied);
		committed_.fill(Qt::transparent);
		if (!base_.isNull()) {
			QPainter painter(&committed_);
			painter.drawImage(0, 0, base_);
		}
		for (const auto &action : actions_)
			if (!action.fadeMs)
				paint(committed_, action, now);
		committedDirty_ = false;
		dirty_ = true;
	}

	bool activeFade = false;
	for (const auto &action : actions_)
		if (action.fadeMs && now - std::min(now, action.born) < uint64_t(action.fadeMs) * 1000000)
			activeFade = true;

	// fading_ (previous frame's state) forces one last recompute the instant
	// a fade finishes, so the now-transparent ink is actually cleared instead
	// of leaving the last pre-expiry frame cached indefinitely. A stroke held
	// still mid-drag raises no dirty flag, so idle frames reuse the cache.
	if (!dirty_ && !activeFade && !fading_)
		return cache_;

	// Cheap per-frame composite: start from the baked layer and only paint
	// the handful of still-fading actions plus the live preview stroke.
	cache_ = committed_;
	for (const auto &action : actions_)
		if (action.fadeMs)
			paint(cache_, action, now);
	if (drawing_)
		paint(cache_, preview_, now);
	fading_ = activeFade;
	dirty_ = false;
	++revision_;
	return cache_;
}

} // namespace drawsrc
