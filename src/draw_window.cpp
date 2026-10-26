#ifdef ENABLE_QT

#include "draw_window.hpp"
#include "draw_dock.hpp"
#include "draw_icons.hpp"
#include "preview_overlay.hpp"

#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScreen>
#include <QShowEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <util/config-file.h>

#if !defined(_WIN32) && !defined(__APPLE__)
#include <obs-nix-platform.h>
#endif

namespace drawsrc {

namespace {

// obs_display colors are packed r | g << 8 | b << 16 | a << 24.
constexpr uint32_t kBackground = 0xff232323u;

// User-config section for remembering the window layout between sessions.
constexpr const char *kConfigSection = "InstantHighlightDraw";
constexpr const char *kConfigSidebar = "DrawWindowSidebar";
constexpr const char *kConfigGeometry = "DrawWindowGeometry";
constexpr int kSidebarWidth = 250;

config_t *user_config()
{
#ifdef ENABLE_FRONTEND_API
	return obs_frontend_get_user_config();
#else
	return nullptr;
#endif
}

QSize pixel_size(const QWidget *widget)
{
	return widget->size() * widget->devicePixelRatioF();
}

bool to_gs_window(QWindow *window, gs_window &out)
{
#if defined(_WIN32)
	out.hwnd = reinterpret_cast<void *>(window->winId());
	return out.hwnd != nullptr;
#elif defined(__APPLE__)
	out.view = (id)window->winId();
	return out.view != nullptr;
#else
	switch (obs_get_nix_platform()) {
	case OBS_NIX_PLATFORM_X11_EGL:
		out.id = uint32_t(window->winId());
		out.display = obs_get_nix_platform_display();
		return true;
	case OBS_NIX_PLATFORM_WAYLAND:
		out.display = reinterpret_cast<void *>(window->winId());
		return out.display != nullptr;
	default:
		return false;
	}
#endif
}

} // namespace

// -----------------------------------------------------------------------------
// DrawDisplay
// -----------------------------------------------------------------------------

DrawDisplay::DrawDisplay(QWidget *parent) : QWidget(parent)
{
	// Same attributes OBS's own OBSQTDisplay uses: the surface belongs to the
	// graphics thread, Qt must never paint or clear it.
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setFocusPolicy(Qt::StrongFocus);
	setMinimumSize(160, 90);

	if (QWindow *handle = windowHandle()) {
		connect(handle, &QWindow::visibleChanged, this, [this](bool visible) {
			if (visible)
				createDisplay();
			else
				destroyDisplay();
		});
		connect(handle, &QWindow::screenChanged, this, [this](QScreen *) {
			createDisplay();
			if (display_) {
				const QSize size = pixel_size(this);
				obs_display_resize(display_, uint32_t(size.width()), uint32_t(size.height()));
			}
		});
	}
}

DrawDisplay::~DrawDisplay()
{
	destroyDisplay();
	std::lock_guard<std::mutex> lock(mutex_);
	obs_weak_source_release(source_);
	obs_weak_source_release(scene_);
	source_ = nullptr;
	scene_ = nullptr;
}

void DrawDisplay::setSource(obs_source_t *source)
{
	std::lock_guard<std::mutex> lock(mutex_);
	obs_weak_source_release(source_);
	source_ = source ? obs_source_get_weak_source(source) : nullptr;
}

void DrawDisplay::setScene(obs_source_t *scene, bool studioMode)
{
	std::lock_guard<std::mutex> lock(mutex_);
	obs_weak_source_release(scene_);
	scene_ = scene ? obs_source_get_weak_source(scene) : nullptr;
	studioMode_ = studioMode;
}

QPaintEngine *DrawDisplay::paintEngine() const
{
	return nullptr;
}

void DrawDisplay::createDisplay()
{
	if (display_)
		return;
	QWindow *handle = windowHandle();
	if (!handle || !handle->isExposed())
		return;
	const QSize size = pixel_size(this);
	gs_init_data info{};
	info.cx = uint32_t(size.width());
	info.cy = uint32_t(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	if (!to_gs_window(handle, info.window))
		return;
	display_ = obs_display_create(&info, kBackground);
	if (display_)
		obs_display_add_draw_callback(display_, &DrawDisplay::draw, this);
}

void DrawDisplay::destroyDisplay()
{
	if (!display_)
		return;
	obs_display_remove_draw_callback(display_, &DrawDisplay::draw, this);
	obs_display_destroy(display_);
	display_ = nullptr;
}

void DrawDisplay::paintEvent(QPaintEvent *event)
{
	createDisplay();
	QWidget::paintEvent(event);
}

void DrawDisplay::resizeEvent(QResizeEvent *event)
{
	QWidget::resizeEvent(event);
	createDisplay();
	if (isVisible() && display_) {
		const QSize size = pixel_size(this);
		obs_display_resize(display_, uint32_t(size.width()), uint32_t(size.height()));
	}
}

void DrawDisplay::moveEvent(QMoveEvent *event)
{
	QWidget::moveEvent(event);
	if (display_)
		obs_display_update_color_space(display_);
}

void DrawDisplay::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<DrawDisplay *>(data);
	obs_source_t *source;
	obs_source_t *scene;
	bool studio;
	{
		std::lock_guard<std::mutex> lock(self->mutex_);
		source = obs_weak_source_get_source(self->source_);
		scene = obs_weak_source_get_source(self->scene_);
		studio = self->studioMode_;
	}
	if (source) {
		obs_video_info video{};
		obs_get_video_info(&video);
		obs_sceneitem_t *item = find_visible_item(scene, source);
		const bool wholeScene = item != nullptr;
		if (item)
			obs_sceneitem_release(item);
		const uint32_t w = wholeScene ? video.base_width : obs_source_get_width(source);
		const uint32_t h = wholeScene ? video.base_height : obs_source_get_height(source);
		const PreviewFit fit = fit_canvas(w, h, int(cx), int(cy), 0);
		if (fit.valid) {
			gs_viewport_push();
			gs_projection_push();
			gs_ortho(0.0f, float(w), 0.0f, float(h), -100.0f, 100.0f);
			gs_set_viewport(fit.x, fit.y, fit.width, fit.height);
			if (!wholeScene)
				obs_source_video_render(source);
			else if (studio && scene)
				obs_source_video_render(scene);
			else
				obs_render_main_texture();
			gs_projection_pop();
			gs_viewport_pop();
		}
	}
	obs_source_release(scene);
	obs_source_release(source);
}

// -----------------------------------------------------------------------------
// DrawWindow
// -----------------------------------------------------------------------------

DrawWindow::DrawWindow(QWidget *parent) : QDialog(parent)
{
	setWindowFlags(Qt::Window | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
	setSizeGripEnabled(false);
	auto *layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	display_ = new DrawDisplay(this);
	layout->addWidget(display_, 1);

	// A tall, thin handle between the canvas and the sidebar toggles the tools.
	toggle_ = new QToolButton(this);
	toggle_->setAutoRaise(true);
	toggle_->setFocusPolicy(Qt::NoFocus);
	toggle_->setIconSize(QSize(10, 10));
	toggle_->setFixedWidth(16);
	toggle_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
	toggle_->setToolTip(tr("Show or hide the tools (Tab)"));
	connect(toggle_, &QToolButton::clicked, this, &DrawWindow::toggleSidebar);
	layout->addWidget(toggle_, 0);

	sidebar_ = new DrawDock(this, true);
	sidebar_->setFixedWidth(kSidebarWidth);
	layout->addWidget(sidebar_, 0);

	overlay_ = new PreviewOverlay(this);
	connect(overlay_, &PreviewOverlay::statusChanged, this, &DrawWindow::statusChanged);
	connect(overlay_, &PreviewOverlay::statusChanged, sidebar_, &DrawDock::setStatus);
	connect(overlay_, &PreviewOverlay::escapePressed, this, &QDialog::close);
	connect(overlay_, &PreviewOverlay::sidebarToggleRequested, this, &DrawWindow::toggleSidebar);
	connect(sidebar_, &DrawDock::sourceSelected, this, &DrawWindow::setSource);
	connect(sidebar_, &DrawDock::toolHintChanged, this,
		[this](int tool, int eraser) { overlay_->setToolHint(Tool(tool), eraser); });

	// Default to a comfortable 16:9 canvas on the current screen, then restore
	// whatever the user had last time.
	QSize size(960, 540);
	if (QScreen *screen = QGuiApplication::primaryScreen())
		size = (screen->availableSize() * 0.6).boundedTo(QSize(1600, 900)).expandedTo(QSize(480, 270));
	resize(size + QSize(toggle_->width() + kSidebarWidth, 0));
	bool sidebarVisible = true;
	if (config_t *config = user_config()) {
		if (config_has_user_value(config, kConfigSection, kConfigSidebar))
			sidebarVisible = config_get_bool(config, kConfigSection, kConfigSidebar);
		if (const char *geometry = config_get_string(config, kConfigSection, kConfigGeometry))
			restoreGeometry(QByteArray::fromBase64(QByteArray(geometry)));
	}
	sidebar_->setVisible(sidebarVisible);
	updateToggleIcon();

#ifdef ENABLE_FRONTEND_API
	obs_frontend_add_event_callback(&DrawWindow::onFrontendEvent, this);
#endif
	refreshScene();
	updateTitle();
}

DrawWindow::~DrawWindow()
{
#ifdef ENABLE_FRONTEND_API
	obs_frontend_remove_event_callback(&DrawWindow::onFrontendEvent, this);
#endif
	// Detach the input filter before the display widget goes away.
	delete overlay_;
	overlay_ = nullptr;
	obs_weak_source_release(source_);
	source_ = nullptr;
}

void DrawWindow::setSource(obs_source_t *source)
{
	if (source ? (source_ && obs_weak_source_references_source(source_, source)) : !source_)
		return;
	obs_weak_source_release(source_);
	source_ = source ? obs_source_get_weak_source(source) : nullptr;
	display_->setSource(source);
	overlay_->setSource(source);
	sidebar_->selectSource(source);
	updateTitle();
	if (!source)
		hide();
	else if (isVisible())
		overlay_->attach(display_);
}

void DrawWindow::present()
{
	obs_source_t *source = obs_weak_source_get_source(source_);
	if (!source)
		return;
	obs_source_release(source);
	refreshScene();
	show();
	raise();
	activateWindow();
	overlay_->attach(display_);
	display_->setFocus(Qt::OtherFocusReason);
}

void DrawWindow::showEvent(QShowEvent *event)
{
	QDialog::showEvent(event);
	refreshScene();
	overlay_->attach(display_);
}

void DrawWindow::hideEvent(QHideEvent *event)
{
	QDialog::hideEvent(event);
	overlay_->setEnabled(false);
	saveState();
}

bool DrawWindow::event(QEvent *event)
{
	if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
		updateToggleIcon();
	return QDialog::event(event);
}

void DrawWindow::setSidebarVisible(bool visible)
{
	if (sidebar_->isVisible() == visible)
		return;
	sidebar_->setVisible(visible);
	updateToggleIcon();
	saveState();
}

void DrawWindow::toggleSidebar()
{
	setSidebarVisible(!sidebar_->isVisible());
}

void DrawWindow::updateToggleIcon()
{
	// Style/palette events already arrive while the constructor is still building the UI.
	if (!toggle_ || !sidebar_)
		return;
	const bool dark = palette().color(QPalette::Window).lightness() < 128;
	// Arrow points the way the panel will move when clicked.
	toggle_->setIcon(theme_icon(sidebar_->isVisible() ? "right.svg" : "left.svg", dark));
}

void DrawWindow::saveState()
{
	config_t *config = user_config();
	if (!config)
		return;
	config_set_bool(config, kConfigSection, kConfigSidebar, sidebar_->isVisible());
	config_set_string(config, kConfigSection, kConfigGeometry, saveGeometry().toBase64().constData());
}

void DrawWindow::refreshScene()
{
#ifdef ENABLE_FRONTEND_API
	const bool studio = obs_frontend_preview_program_mode_active();
	obs_source_t *scene = studio ? obs_frontend_get_current_preview_scene() : obs_frontend_get_current_scene();
	display_->setScene(scene, studio);
	obs_source_release(scene);
#endif
}

void DrawWindow::updateTitle()
{
	obs_source_t *source = obs_weak_source_get_source(source_);
	const char *name = source ? obs_source_get_name(source) : nullptr;
	setWindowTitle(name ? tr("Draw: %1").arg(QString::fromUtf8(name)) : tr("Draw"));
	obs_source_release(source);
}

#ifdef ENABLE_FRONTEND_API
void DrawWindow::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto *window = static_cast<DrawWindow *>(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		window->refreshScene();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		window->setSource(nullptr);
		window->display_->setScene(nullptr, false);
		break;
	default:
		break;
	}
}
#endif

} // namespace drawsrc

#endif // ENABLE_QT
