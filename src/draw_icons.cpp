#include "draw_icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QTransform>
#include <cmath>

namespace drawsrc {

namespace {

constexpr qreal kPi = 3.14159265358979323846;

// Paints a 16x16 logical glyph at 1x, 2x and 3x so it stays crisp on HiDPI docks.
template<typename Painter> QIcon render(const Painter &draw)
{
	QIcon icon;
	for (int scale : {1, 2, 3}) {
		QPixmap pixmap(16 * scale, 16 * scale);
		pixmap.setDevicePixelRatio(scale);
		pixmap.fill(Qt::transparent);
		QPainter painter(&pixmap);
		painter.setRenderHint(QPainter::Antialiasing);
		draw(painter);
		painter.end();
		icon.addPixmap(pixmap);
	}
	return icon;
}

QPolygonF star_polygon(QPointF center, qreal outer, qreal inner)
{
	QPolygonF star;
	for (int i = 0; i < 10; ++i) {
		const qreal r = i % 2 ? inner : outer;
		const qreal angle = -kPi / 2 + i * kPi / 5;
		star << QPointF(center.x() + std::cos(angle) * r, center.y() + std::sin(angle) * r);
	}
	return star;
}

} // namespace

QIcon tool_icon(Tool tool, const QColor &color)
{
	return render([tool, color](QPainter &p) {
		const QPen pen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
		p.setPen(pen);
		p.setBrush(Qt::NoBrush);
		switch (tool) {
		case Tool::Pen:
			p.drawPolygon(QPolygonF{{3.0, 13.0}, {3.0, 10.5}, {10.5, 3.0}, {13.0, 5.5}, {5.5, 13.0}});
			p.drawLine(QPointF(8.5, 5.0), QPointF(11.0, 7.5));
			break;
		case Tool::Highlighter: {
			QColor wash = color;
			wash.setAlphaF(0.4f);
			p.setPen(QPen(wash, 6.0, Qt::SolidLine, Qt::FlatCap));
			p.drawLine(QPointF(4.0, 12.0), QPointF(12.0, 4.0));
			p.setPen(pen);
			p.drawLine(QPointF(4.0, 12.0), QPointF(12.0, 4.0));
			break;
		}
		case Tool::Spray: {
			p.setPen(Qt::NoPen);
			p.setBrush(color);
			const struct {
				qreal x, y, r;
			} dots[] = {{8.0, 8.0, 1.7},  {11.5, 5.5, 1.2},  {4.5, 6.5, 1.1},  {12.5, 10.0, 1.0},
				    {6.5, 12.0, 1.3}, {10.0, 13.0, 0.9}, {3.5, 11.0, 0.9}, {13.0, 3.0, 0.8},
				    {6.0, 3.5, 0.8},  {9.5, 10.5, 0.8}};
			for (const auto &dot : dots)
				p.drawEllipse(QPointF(dot.x, dot.y), dot.r, dot.r);
			break;
		}
		case Tool::Line:
			p.drawLine(QPointF(3.0, 13.0), QPointF(13.0, 3.0));
			break;
		case Tool::Arrow:
			p.drawLine(QPointF(3.0, 13.0), QPointF(13.0, 3.0));
			p.drawLine(QPointF(13.0, 3.0), QPointF(7.5, 3.0));
			p.drawLine(QPointF(13.0, 3.0), QPointF(13.0, 8.5));
			break;
		case Tool::Square:
			p.drawRect(QRectF(3.0, 3.0, 10.0, 10.0));
			break;
		case Tool::Circle:
			p.drawEllipse(QRectF(3.0, 3.0, 10.0, 10.0));
			break;
		case Tool::Triangle:
			p.drawPolygon(QPolygonF{{8.0, 2.5}, {13.5, 13.0}, {2.5, 13.0}});
			break;
		case Tool::Star:
			p.drawPolygon(star_polygon(QPointF(8.0, 8.6), 6.3, 2.6));
			break;
		case Tool::Heart: {
			QPainterPath path;
			path.moveTo(0.5, 1);
			path.cubicTo(-0.6, 0.25, 0.15, -0.35, 0.5, 0.2);
			path.cubicTo(0.85, -0.35, 1.6, 0.25, 0.5, 1);
			QTransform transform;
			transform.translate(2.5, 3.5);
			transform.scale(11.0, 10.0);
			p.drawPath(transform.map(path));
			break;
		}
		case Tool::Eraser:
			p.drawPolygon(QPolygonF{{2.5, 10.0}, {9.0, 3.5}, {14.0, 8.5}, {7.5, 15.0}});
			p.drawLine(QPointF(5.5, 7.0), QPointF(10.5, 12.0));
			break;
		}
	});
}

QIcon theme_icon(const char *relative, bool dark)
{
	return QIcon(
		QStringLiteral("theme:%1/%2")
			.arg(dark ? QStringLiteral("Dark") : QStringLiteral("Light"), QString::fromLatin1(relative)));
}

QIcon mirrored_icon(const QIcon &icon)
{
	QIcon out;
	for (qreal ratio : {1.0, 2.0, 3.0}) {
		QPixmap pixmap = icon.pixmap(QSize(16, 16), ratio);
		if (pixmap.isNull())
			continue;
		out.addPixmap(pixmap.transformed(QTransform().scale(-1, 1)));
	}
	return out;
}

QIcon color_swatch_icon(const QColor &fill, const QColor &outline)
{
	return render([fill, outline](QPainter &p) {
		p.setPen(QPen(outline, 1.0));
		p.setBrush(fill);
		p.drawRoundedRect(QRectF(2.5, 2.5, 11.0, 11.0), 2.5, 2.5);
	});
}

} // namespace drawsrc
