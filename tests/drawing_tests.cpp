#include "drawing_document.hpp"
#include <cstdlib>
#include <iostream>

using namespace drawsrc;

static void require(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << message << '\n';
		std::exit(1);
	}
}

static void stroke(DrawingDocument &doc, Tool tool, QPointF start, QPointF end, qreal width = 8, QColor color = Qt::red,
		   uint32_t fade = 0)
{
	doc.begin(start, tool, width, color, fade);
	doc.move(end);
	doc.end(1000000000);
}

static int visible_pixels(const QImage &image, QRect area = QRect())
{
	if (area.isNull())
		area = image.rect();
	int visible = 0;
	for (int y = area.top(); y <= area.bottom(); ++y)
		for (int x = area.left(); x <= area.right(); ++x)
			visible += image.pixelColor(x, y).alpha() > 0;
	return visible;
}

int main()
{
	DrawingDocument doc;
	doc.resize({200, 120});
	stroke(doc, Tool::Pen, {10, 60}, {190, 60});
	require(doc.image(1000000000).pixelColor(100, 60).red() == 255, "Pen must cover unsampled segments");
	stroke(doc, Tool::Eraser, {100, 10}, {100, 110}, 20);
	const auto erased = doc.image(1000000000);
	require(erased.pixelColor(100, 60).alpha() == 0, "Eraser must remove intersecting pixels");
	require(erased.pixelColor(40, 60).alpha() == 255, "Partial eraser must preserve the rest of a stroke");
	doc.undo();
	require(doc.image(1000000000).pixelColor(100, 60).alpha() == 255, "Undo eraser must restore pixels");
	doc.redo();
	require(doc.image(1000000000).pixelColor(100, 60).alpha() == 0, "Redo eraser must remove pixels");
	doc.clear();
	require(doc.image(1000000000).pixelColor(40, 60).alpha() == 0, "Clear must empty the canvas");
	doc.undo();
	require(doc.image(1000000000).pixelColor(40, 60).alpha() == 255, "Clear must be undoable");
	stroke(doc, Tool::Pen, {100, 60}, {100, 60});
	require(!doc.canRedo(), "New ink must discard redo history");
	require(doc.image(1000000000).pixelColor(100, 60).alpha() == 255,
		"Ink drawn after erasing must remain visible");
	const auto revision = doc.revision();
	doc.image(2000000000);
	require(doc.revision() == revision, "Unchanged persistent ink must use the cache");

	DrawingDocument idle;
	idle.resize({100, 100});
	idle.begin({10, 50}, Tool::Pen, 6, Qt::red, 0);
	idle.move({50, 50});
	idle.image(1000000000);
	const auto held = idle.revision();
	idle.image(1100000000);
	require(idle.revision() == held, "A stroke held still must not recomposite every frame");
	idle.move({90, 50});
	idle.image(1200000000);
	require(idle.revision() > held, "Pointer movement must refresh the live stroke");

	DrawingDocument alpha;
	alpha.resize({100, 100});
	alpha.begin({10, 50}, Tool::Pen, 12, QColor(255, 0, 0, 128), 0);
	alpha.move({50, 50});
	alpha.move({90, 50});
	const int before = alpha.image(1000000000).pixelColor(50, 50).alpha();
	alpha.end(1000000000);
	require(alpha.image(1000000000).pixelColor(50, 50).alpha() == before, "Commit must not double alpha");
	require(before == 128, "Pen joins must not darken translucent strokes");

	DrawingDocument fade;
	fade.resize({100, 100});
	stroke(fade, Tool::Pen, {10, 50}, {90, 50}, 8, Qt::red, 1000);
	require(fade.image(1500000000).pixelColor(50, 50).alpha() >= 127, "Fade midpoint must retain half opacity");
	require(fade.image(2000000000).pixelColor(50, 50).alpha() == 0, "Expired ink must disappear");

	for (Tool tool :
	     {Tool::Square, Tool::Circle, Tool::Arrow, Tool::Heart, Tool::Line, Tool::Triangle, Tool::Star}) {
		DrawingDocument shape;
		shape.resize({100, 100});
		stroke(shape, tool, {10, 10}, {90, 90});
		require(visible_pixels(shape.image(1000000000)) > 100, "Every shape tool must produce visible ink");
	}

	DrawingDocument line;
	line.resize({100, 100});
	stroke(line, Tool::Line, {10, 50}, {90, 50}, 4);
	const auto lineImage = line.image(1000000000);
	require(lineImage.pixelColor(50, 50).alpha() == 255, "Line must connect its end points");
	require(lineImage.pixelColor(90, 40).alpha() == 0, "Line must not grow an arrow head");

	DrawingDocument marker;
	marker.resize({100, 100});
	stroke(marker, Tool::Highlighter, {10, 50}, {90, 50}, 4, Qt::yellow);
	const int markerAlpha = marker.image(1000000000).pixelColor(50, 50).alpha();
	require(markerAlpha > 90 && markerAlpha < 140, "Highlighter must be translucent");
	require(marker.image(1000000000).pixelColor(50, 45).alpha() == markerAlpha,
		"Highlighter must be wider than the requested pen size");

	DrawingDocument spray;
	spray.resize({120, 120});
	stroke(spray, Tool::Spray, {60, 60}, {70, 60}, 8, Qt::blue);
	const auto sprayed = spray.image(1000000000);
	require(visible_pixels(sprayed, QRect(40, 40, 50, 40)) > 10, "Spray must deposit dots around the pointer");
	require(visible_pixels(sprayed, QRect(0, 0, 25, 25)) == 0, "Spray must stay within its radius");
	spray.undo();
	require(visible_pixels(spray.image(1000000000)) == 0, "Spray must be undoable");
	spray.redo();
	require(visible_pixels(spray.image(1000000000), QRect(40, 40, 50, 40)) > 10,
		"Redo must replay the identical spray pattern");

	DrawingDocument history;
	history.resize({100, 100});
	for (int i = 0; i < 140; ++i)
		stroke(history, Tool::Pen, {10, 50}, {90, 50});
	for (int i = 0; i < 128; ++i)
		history.undo();
	require(!history.canUndo(), "Undo history must be bounded");
	require(history.image(1000000000).pixelColor(50, 50).alpha() == 255,
		"Baked old ink must survive history eviction");
	std::cout << "Drawing regression checks passed\n";
}
