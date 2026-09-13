#include "preview_overlay.hpp"

#include <obs-frontend-api.h>
#include <graphics/matrix4.h>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWidget>
#include <algorithm>
#include <cmath>

namespace drawsrc {

PreviewOverlay::PreviewOverlay(QObject *parent) : QObject(parent) {}

PreviewOverlay::~PreviewOverlay()
{
	setEnabled(false);
	obs_source_release(source_);
}

void PreviewOverlay::setSource(obs_source_t *source)
{
	if (source == source_)
		return;
	setEnabled(false);
	obs_source_release(source_);
	source_ = obs_source_get_ref(source);
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
	if (!source_ || !preview || !fit || !preview->isVisible() || !obs_frontend_preview_enabled()) {
		emit statusChanged(tr("Preview or drawing source unavailable"));
		return false;
	}
	fit->trigger();
	preview_ = preview;
	previousCursor_ = preview->cursor();
	previousTouch_ = preview->testAttribute(Qt::WA_AcceptTouchEvents);
	preview->setCursor(Qt::CrossCursor);
	preview->setAttribute(Qt::WA_AcceptTouchEvents);
	preview->installEventFilter(this);
	enabled_ = true;
	emit statusChanged(tr("Drawing on preview"));
	return true;
}

bool PreviewOverlay::mapPoint(QPointF position, obs_mouse_event &event, bool beginning)
{
	if (!preview_ || !source_ || obs_source_removed(source_))
		return false;
	obs_source_t *sceneSource = obs_frontend_preview_program_mode_active()
					    ? obs_frontend_get_current_preview_scene()
					    : obs_frontend_get_current_scene();
	if (!sceneSource)
		return false;
	if (!beginning && sceneSource != strokeScene_) {
		obs_source_release(sceneSource);
		return false;
	}
	struct Context {
		obs_source_t *source;
		obs_sceneitem_t *item = nullptr;
	} context{source_};
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (scene)
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
	if (!context.item) {
		obs_source_release(sceneSource);
		emit statusChanged(tr("Target is not visible at the scene's top level"));
		return false;
	}
	obs_video_info video{};
	obs_get_video_info(&video);
	const qreal ratio = preview_->devicePixelRatioF();
	const QSize pixels = preview_->size() * ratio;
	const int availableW = pixels.width() - 20;
	const int availableH = pixels.height() - 20;
	if (!video.base_width || !video.base_height || availableW <= 0 || availableH <= 0) {
		obs_sceneitem_release(context.item);
		obs_source_release(sceneSource);
		return false;
	}
	// Match OBS's physical-pixel border and integer centering in Fit to Window mode.
	const float scale =
		std::min(float(availableW) / float(video.base_width), float(availableH) / float(video.base_height));
	const double aspect = double(video.base_width) / video.base_height;
	const bool heightLimited = double(availableW) / availableH > aspect;
	const int renderedW = heightLimited ? int(availableH * aspect) : availableW;
	const int renderedH = heightLimited ? availableH : int(availableW / aspect);
	const int x = 10 + availableW / 2 - renderedW / 2;
	const int y = 10 + availableH / 2 - renderedH / 2;
	const double sceneX = (position.x() * ratio - x) / scale;
	const double sceneY = (position.y() * ratio - y) / scale;
	matrix4 transform;
	obs_sceneitem_get_draw_transform(context.item, &transform);
	obs_sceneitem_crop crop{};
	obs_sceneitem_get_crop(context.item, &crop);
	// Bounds cropping adds an internal crop not exposed by libobs; reject it explicitly.
	const bool boundsCrop = obs_sceneitem_get_bounds_crop(context.item);
	obs_sceneitem_release(context.item);
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
	const int width = int(obs_source_get_width(source_));
	const int height = int(obs_source_get_height(source_));
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
	// Reassert fit after any external preview scaling changes.
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (auto *fit = main->findChild<QAction *>(QStringLiteral("actionScaleWindow")))
		fit->trigger();
	if (!mapPoint(position, last_, true))
		return;
	drawing_ = true;
	preview_->setFocus(Qt::MouseFocusReason);
	obs_source_send_mouse_click(source_, &last_, eraser ? MOUSE_RIGHT : MOUSE_LEFT, false, 1);
	emit statusChanged(tr("Drawing on preview"));
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
	if (drawing_ && source_)
		obs_source_send_focus(source_, false);
	drawing_ = false;
	touchId_ = -1;
	obs_source_release(strokeScene_);
	strokeScene_ = nullptr;
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
		if (key->key() == Qt::Key_Escape)
			setEnabled(false);
		else if (key->matches(QKeySequence::Undo))
			command("_do_undo");
		else if (key->matches(QKeySequence::Redo))
			command("_do_redo");
		return true;
	}
	case QEvent::ShortcutOverride:
		event->accept();
		return true;
	case QEvent::ContextMenu:
	case QEvent::Wheel:
		return true;
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
