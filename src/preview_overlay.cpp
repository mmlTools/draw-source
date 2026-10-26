#include "preview_overlay.hpp"

#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace drawsrc {

PreviewFit fit_canvas(uint32_t canvasW, uint32_t canvasH, int pixelsW, int pixelsH, int border)
{
	PreviewFit fit;
	const int availableW = pixelsW - 2 * border;
	const int availableH = pixelsH - 2 * border;
	if (!canvasW || !canvasH || availableW <= 0 || availableH <= 0)
		return fit;
	// Match OBS's integer centering in Fit to Window mode.
	fit.scale = std::min(float(availableW) / float(canvasW), float(availableH) / float(canvasH));
	const double aspect = double(canvasW) / canvasH;
	const bool heightLimited = double(availableW) / availableH > aspect;
	fit.width = heightLimited ? int(availableH * aspect) : availableW;
	fit.height = heightLimited ? availableH : int(availableW / aspect);
	fit.x = border + availableW / 2 - fit.width / 2;
	fit.y = border + availableH / 2 - fit.height / 2;
	fit.valid = true;
	return fit;
}

obs_sceneitem_t *find_visible_item(obs_source_t *sceneSource, obs_source_t *target)
{
	struct Context {
		obs_source_t *source;
		obs_sceneitem_t *item = nullptr;
	} context{target};
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene || !target)
		return nullptr;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *data) {
			auto &ctx = *static_cast<Context *>(data);
			if (obs_sceneitem_get_source(item) == ctx.source && obs_sceneitem_visible(item)) {
				if (ctx.item)
					obs_sceneitem_release(ctx.item);
				obs_sceneitem_addref(item);
				ctx.item = item;
			}
			return true;
		},
		&context);
	return context.item;
}

namespace {

constexpr int kObsPreviewBorder = 10;

obs_source_t *current_scene_source()
{
	return obs_frontend_preview_program_mode_active() ? obs_frontend_get_current_preview_scene()
							  : obs_frontend_get_current_scene();
}

QSize pixel_size(const QWidget *widget)
{
	return widget->size() * widget->devicePixelRatioF();
}

} // namespace

PreviewOverlay::PreviewOverlay(QObject *parent) : QObject(parent) {}

PreviewOverlay::~PreviewOverlay()
{
	setEnabled(false);
	obs_weak_source_release(weak_);
}

void PreviewOverlay::setSource(obs_source_t *source)
{
	if (source ? (weak_ && obs_weak_source_references_source(weak_, source)) : !weak_)
		return;
	setEnabled(false);
	obs_weak_source_release(weak_);
	weak_ = source ? obs_source_get_weak_source(source) : nullptr;
}

void PreviewOverlay::setToolHint(Tool tool, int eraserSize)
{
	tool_ = tool;
	eraserSize_ = std::clamp(eraserSize, 4, 256);
	updateCursor();
}

void PreviewOverlay::setRingHidden(bool hidden)
{
	if (!source_)
		return;
	calldata_t data;
	calldata_init(&data);
	calldata_set_bool(&data, "hidden", hidden);
	proc_handler_call(obs_source_get_proc_handler(source_), "set_eraser_ring_hidden", &data);
	calldata_free(&data);
}

void PreviewOverlay::begin(QWidget *widget, Mode mode, obs_source_t *source)
{
	source_ = source;
	mode_ = mode;
	preview_ = widget;
	previousCursor_ = widget->cursor();
	previousTouch_ = widget->testAttribute(Qt::WA_AcceptTouchEvents);
	widget->setAttribute(Qt::WA_AcceptTouchEvents);
	widget->installEventFilter(this);
	enabled_ = true;
	setRingHidden(true);
	updateCursor();
}

bool PreviewOverlay::setEnabled(bool enabled)
{
	if (!enabled) {
		release();
		if (preview_) {
			preview_->removeEventFilter(this);
			preview_->setCursor(previousCursor_);
			preview_->setAttribute(Qt::WA_AcceptTouchEvents, previousTouch_);
		}
		if (enabled_)
			setRingHidden(false);
		obs_source_release(source_);
		source_ = nullptr;
		preview_ = nullptr;
		enabled_ = false;
		emit stopped();
		return true;
	}
	if (enabled_)
		return true;
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *preview = main ? main->findChild<QWidget *>(QStringLiteral("preview")) : nullptr;
	auto *fit = main ? main->findChild<QAction *>(QStringLiteral("actionScaleWindow")) : nullptr;
	obs_source_t *source = obs_weak_source_get_source(weak_);
	if (!source || !preview || !fit || !preview->isVisible() || !obs_frontend_preview_enabled()) {
		obs_source_release(source);
		emit statusChanged(tr("Preview or drawing source unavailable"));
		return false;
	}
	// The action is checkable; triggering it while checked would untick it.
	if (!fit->isChecked())
		fit->trigger();
	begin(preview, Mode::ObsPreview, source);
	emit statusChanged(tr("Drawing on preview"));
	return true;
}

bool PreviewOverlay::attach(QWidget *widget)
{
	if (enabled_ && preview_ == widget)
		return true;
	setEnabled(false);
	obs_source_t *source = widget ? obs_weak_source_get_source(weak_) : nullptr;
	if (!source)
		return false;
	begin(widget, Mode::Window, source);
	emit statusChanged(tr("Drawing in the drawing window"));
	return true;
}

double PreviewOverlay::sourceToPreviewScale() const
{
	if (!preview_ || !source_)
		return 0;
	const QSize pixels = pixel_size(preview_);
	const int border = mode_ == Mode::ObsPreview ? kObsPreviewBorder : 0;
	obs_source_t *sceneSource = current_scene_source();
	obs_sceneitem_t *item = find_visible_item(sceneSource, source_);
	obs_source_release(sceneSource);
	if (!item) {
		if (mode_ != Mode::Window)
			return 0;
		const PreviewFit fit = fit_canvas(obs_source_get_width(source_), obs_source_get_height(source_),
						  pixels.width(), pixels.height(), border);
		return fit.valid ? fit.scale / preview_->devicePixelRatioF() : 0;
	}
	matrix4 transform;
	obs_sceneitem_get_draw_transform(item, &transform);
	obs_sceneitem_release(item);
	const double determinant = double(transform.x.x) * transform.y.y - double(transform.x.y) * transform.y.x;
	obs_video_info video{};
	obs_get_video_info(&video);
	const PreviewFit fit = fit_canvas(video.base_width, video.base_height, pixels.width(), pixels.height(), border);
	if (!fit.valid)
		return 0;
	return std::sqrt(std::abs(determinant)) * fit.scale / preview_->devicePixelRatioF();
}

void PreviewOverlay::updateCursor()
{
	if (!enabled_ || !preview_)
		return;
	const bool eraser = drawing_ ? eraserStroke_ : tool_ == Tool::Eraser;
	// Logical widget pixels covered by the eraser at the current zoom.
	const double diameter = eraser ? std::min(eraserSize_ * sourceToPreviewScale(), 250.0) : 0.0;
	if (diameter < 4.0) {
		preview_->setCursor(Qt::CrossCursor);
		return;
	}
	const int size = int(std::ceil(diameter)) + 6;
	QPixmap pixmap(size, size);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	const QPointF center(size / 2.0, size / 2.0);
	const double radius = diameter / 2.0;
	painter.setPen(QPen(QColor(0, 0, 0, 170), 3.0));
	painter.drawEllipse(center, radius, radius);
	painter.setPen(QPen(Qt::white, 1.2));
	painter.drawEllipse(center, radius, radius);
	painter.drawLine(center - QPointF(3, 0), center + QPointF(3, 0));
	painter.drawLine(center - QPointF(0, 3), center + QPointF(0, 3));
	painter.end();
	preview_->setCursor(QCursor(pixmap, size / 2, size / 2));
}

bool PreviewOverlay::mapPoint(QPointF position, obs_mouse_event &event, bool beginning)
{
	if (!preview_ || !source_ || obs_source_removed(source_))
		return false;
	obs_source_t *sceneSource = current_scene_source();
	if (!beginning && sceneSource != strokeScene_) {
		obs_source_release(sceneSource);
		return false;
	}
	obs_sceneitem_t *item = find_visible_item(sceneSource, source_);
	if (beginning)
		sourceOnly_ = !item && mode_ == Mode::Window;
	if (!item && !sourceOnly_) {
		obs_source_release(sceneSource);
		emit statusChanged(tr("Target is not visible at the scene's top level"));
		return false;
	}
	const QSize pixels = pixel_size(preview_);
	const qreal ratio = preview_->devicePixelRatioF();
	const int border = mode_ == Mode::ObsPreview ? kObsPreviewBorder : 0;
	const int width = int(obs_source_get_width(source_));
	const int height = int(obs_source_get_height(source_));

	if (sourceOnly_) {
		// The window shows the bare source, so widget pixels map straight onto it.
		if (item)
			obs_sceneitem_release(item);
		const PreviewFit fit =
			fit_canvas(uint32_t(width), uint32_t(height), pixels.width(), pixels.height(), border);
		if (!fit.valid || width <= 0 || height <= 0) {
			obs_source_release(sceneSource);
			return false;
		}
		const double sourceX = (position.x() * ratio - fit.x) / fit.scale;
		const double sourceY = (position.y() * ratio - fit.y) / fit.scale;
		if (beginning && (sourceX < 0 || sourceY < 0 || sourceX >= width || sourceY >= height)) {
			obs_source_release(sceneSource);
			return false;
		}
		event = {};
		event.x = int(std::clamp(sourceX, 0.0, double(width - 1)));
		event.y = int(std::clamp(sourceY, 0.0, double(height - 1)));
		if (beginning) {
			obs_source_release(strokeScene_);
			strokeScene_ = obs_source_get_ref(sceneSource);
		}
		obs_source_release(sceneSource);
		return true;
	}

	obs_video_info video{};
	obs_get_video_info(&video);
	const PreviewFit fit = fit_canvas(video.base_width, video.base_height, pixels.width(), pixels.height(), border);
	if (!fit.valid) {
		obs_sceneitem_release(item);
		obs_source_release(sceneSource);
		return false;
	}
	const double sceneX = (position.x() * ratio - fit.x) / fit.scale;
	const double sceneY = (position.y() * ratio - fit.y) / fit.scale;
	matrix4 transform;
	obs_sceneitem_get_draw_transform(item, &transform);
	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(item, &crop);
	// Bounds cropping adds an internal crop not exposed by libobs; reject it explicitly.
	const bool boundsCrop = obs_sceneitem_get_bounds_crop(item);
	obs_sceneitem_release(item);
	const double determinant = double(transform.x.x) * transform.y.y - double(transform.x.y) * transform.y.x;
	if (boundsCrop || std::abs(determinant) < 1e-9) {
		obs_source_release(sceneSource);
		emit statusChanged(tr("Target transform is not drawable"));
		return false;
	}
	const double dx = sceneX - transform.t.x;
	const double dy = sceneY - transform.t.y;
	const double sourceX = (dx * transform.y.y - dy * transform.y.x) / determinant + crop.left;
	const double sourceY = (dy * transform.x.x - dx * transform.x.y) / determinant + crop.top;
	if (width <= crop.left + crop.right || height <= crop.top + crop.bottom ||
	    (beginning && (sceneX < 0 || sceneY < 0 || sceneX >= video.base_width || sceneY >= video.base_height ||
			   sourceX < crop.left || sourceY < crop.top || sourceX >= width - crop.right ||
			   sourceY >= height - crop.bottom))) {
		obs_source_release(sceneSource);
		return false;
	}
	event = {};
	event.x = int(std::clamp(sourceX, double(crop.left), double(width - crop.right - 1)));
	event.y = int(std::clamp(sourceY, double(crop.top), double(height - crop.bottom - 1)));
	if (beginning) {
		obs_source_release(strokeScene_);
		strokeScene_ = obs_source_get_ref(sceneSource);
	}
	obs_source_release(sceneSource);
	return true;
}

void PreviewOverlay::press(QPointF position, bool eraser)
{
	release();
	if (mode_ == Mode::ObsPreview) {
		// Reassert fit after any external preview scaling changes.
		auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		auto *fit = main ? main->findChild<QAction *>(QStringLiteral("actionScaleWindow")) : nullptr;
		if (fit && !fit->isChecked())
			fit->trigger();
	}
	if (!mapPoint(position, last_, true))
		return;
	drawing_ = true;
	eraserStroke_ = eraser;
	preview_->setFocus(Qt::MouseFocusReason);
	obs_source_send_mouse_click(source_, &last_, eraser ? MOUSE_RIGHT : MOUSE_LEFT, false, 1);
	updateCursor();
	emit statusChanged(mode_ == Mode::ObsPreview ? tr("Drawing on preview") : tr("Drawing in the drawing window"));
}

void PreviewOverlay::move(QPointF position)
{
	if (!drawing_)
		return;
	if (mapPoint(position, last_, false))
		obs_source_send_mouse_move(source_, &last_, false);
	else
		release();
}

void PreviewOverlay::release()
{
	const bool wasDrawing = drawing_;
	if (drawing_ && source_)
		obs_source_send_focus(source_, false);
	drawing_ = false;
	eraserStroke_ = false;
	touchId_ = -1;
	obs_source_release(strokeScene_);
	strokeScene_ = nullptr;
	if (wasDrawing)
		updateCursor();
}

void PreviewOverlay::command(const char *key)
{
	release();
	obs_data_t *settings = obs_source_get_settings(source_);
	obs_data_set_bool(settings, key, true);
	obs_source_update(source_, settings);
	obs_data_release(settings);
}

bool PreviewOverlay::eventFilter(QObject *object, QEvent *event)
{
	if (!enabled_ || object != preview_)
		return false;
	switch (event->type()) {
	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonDblClick: {
		auto *mouse = static_cast<QMouseEvent *>(event);
		if (mouse->button() == Qt::LeftButton || mouse->button() == Qt::RightButton)
			press(mouse->position(), mouse->button() == Qt::RightButton);
		return true;
	}
	case QEvent::MouseMove:
		move(static_cast<QMouseEvent *>(event)->position());
		return true;
	case QEvent::MouseButtonRelease:
		move(static_cast<QMouseEvent *>(event)->position());
		release();
		return true;
	case QEvent::TabletPress:
	case QEvent::TabletMove:
	case QEvent::TabletRelease: {
		auto *tablet = static_cast<QTabletEvent *>(event);
		if (event->type() == QEvent::TabletPress)
			press(tablet->position(), tablet->pointerType() == QPointingDevice::PointerType::Eraser);
		else
			move(tablet->position());
		if (event->type() == QEvent::TabletRelease)
			release();
		event->accept();
		return true;
	}
	case QEvent::TouchBegin:
	case QEvent::TouchUpdate:
	case QEvent::TouchEnd: {
		auto *touch = static_cast<QTouchEvent *>(event);
		if (event->type() == QEvent::TouchBegin && !touch->points().isEmpty()) {
			const auto &point = touch->points().first();
			press(point.position(), false);
			touchId_ = point.id();
		} else {
			for (const auto &point : touch->points())
				if (point.id() == touchId_) {
					move(point.position());
					if (point.state() == QEventPoint::State::Released)
						release();
				}
		}
		if (event->type() == QEvent::TouchEnd)
			release();
		event->accept();
		return true;
	}
	case QEvent::KeyPress: {
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() == Qt::Key_Escape) {
			if (mode_ == Mode::ObsPreview)
				setEnabled(false);
			else
				emit escapePressed();
		} else if (key->key() == Qt::Key_Tab && mode_ == Mode::Window) {
			emit sidebarToggleRequested();
		} else if (key->matches(QKeySequence::Undo)) {
			command("_do_undo");
		} else if (key->matches(QKeySequence::Redo)) {
			command("_do_redo");
		}
		return true;
	}
	case QEvent::ShortcutOverride:
		event->accept();
		return true;
	case QEvent::ContextMenu:
	case QEvent::Wheel:
		return true;
	case QEvent::Resize:
		// Zoom changed, so the eraser footprint on screen changed too.
		updateCursor();
		break;
	case QEvent::FocusOut:
	case QEvent::Leave:
	case QEvent::TouchCancel:
		release();
		break;
	case QEvent::Hide:
		setEnabled(false);
		break;
	default:
		break;
	}
	return false;
}

} // namespace drawsrc
