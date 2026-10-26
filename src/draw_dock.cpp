#ifdef ENABLE_QT

#include "plugin-support.h"

#include "draw_dock.hpp"
#include "draw_icons.hpp"
#include "draw_source.hpp"
#include "draw_window.hpp"
#include "preview_overlay.hpp"

#include <obs.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#include <QButtonGroup>
#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace drawsrc {

namespace {

constexpr const char *kTool = "tool";
constexpr const char *kThick = "thickness";
constexpr const char *kEraserSize = "eraser_size";
constexpr const char *kColor = "color";
constexpr const char *kOpacity = "opacity";
constexpr const char *kReleaseMode = "release_mode"; // 0=keep, 1=fade
constexpr const char *kFadeMs = "fade_ms";

struct ToolSpec {
	Tool tool;
	const char *name;
};

// Display order of the palette; freehand tools first, then shapes.
constexpr ToolSpec kTools[] = {
	{Tool::Pen, QT_TR_NOOP("Pen")},           {Tool::Highlighter, QT_TR_NOOP("Highlighter")},
	{Tool::Spray, QT_TR_NOOP("Spray paint")}, {Tool::Line, QT_TR_NOOP("Line")},
	{Tool::Arrow, QT_TR_NOOP("Arrow")},       {Tool::Eraser, QT_TR_NOOP("Eraser")},
	{Tool::Square, QT_TR_NOOP("Square")},     {Tool::Circle, QT_TR_NOOP("Circle")},
	{Tool::Triangle, QT_TR_NOOP("Triangle")}, {Tool::Star, QT_TR_NOOP("Star")},
	{Tool::Heart, QT_TR_NOOP("Heart")},
};
constexpr int kToolColumns = 6;

bool is_draw_source(obs_source_t *src)
{
	const char *id = obs_source_get_id(src);
	return id && std::strcmp(id, kDrawSourceId) == 0;
}

QColor argb_to_color(uint32_t argb)
{
	return QColor((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, (argb >> 24) & 0xFF);
}

uint32_t color_to_argb(const QColor &color)
{
	return (uint32_t(color.alpha()) << 24) | (uint32_t(color.red()) << 16) | (uint32_t(color.green()) << 8) |
	       uint32_t(color.blue());
}

QLabel *value_label(QWidget *parent)
{
	auto *label = new QLabel(parent);
	label->setMinimumWidth(34);
	label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	return label;
}

} // namespace

DrawDock::DrawDock(QWidget *parent, bool embedded) : QWidget(parent), embedded_(embedded)
{
	overlay_ = new PreviewOverlay(this);

	// Mirror OBS's own dock contents: the theme paints the background and frame
	// on this widget (a QWidget subclass only honours stylesheet backgrounds
	// with this attribute), so the body and footer sit inside a 1px inset
	// exactly like the Sources dock.
	setAttribute(Qt::WA_StyledBackground, true);
	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(1, 0, 1, 1);
	root->setSpacing(0);
	root->addWidget(buildBody(), 1);
	root->addWidget(buildFooter(), 0);
	if (embedded_) {
		// The sidebar lives inside the drawing window already.
		interactAction_->setVisible(false);
		drawAction_->setVisible(false);
	}

	connect(overlay_, &PreviewOverlay::statusChanged, status_, &QLabel::setText);
	connect(overlay_, &PreviewOverlay::stopped, this, [this]() {
		QSignalBlocker blocker(drawBtn_);
		drawBtn_->setChecked(false);
		status_->setText(tr("Drawing off"));
	});

	refreshTimer_ = new QTimer(this);
	refreshTimer_->setSingleShot(true);
	refreshTimer_->setInterval(150);
	connect(refreshTimer_, &QTimer::timeout, this, &DrawDock::refreshSources);
	reloadTimer_ = new QTimer(this);
	reloadTimer_->setSingleShot(true);
	reloadTimer_->setInterval(60);
	connect(reloadTimer_, &QTimer::timeout, this, &DrawDock::reloadCurrent);

	// Refresh on demand instead of polling: libobs tells us whenever a source
	// appears, disappears or is renamed.
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : {"source_create", "source_destroy", "source_remove", "source_rename"})
		signal_handler_connect(handler, signal, &DrawDock::onSourceSignal, this);
	// Settings may change from the other panel (dock vs. drawing window sidebar).
	signal_handler_connect(handler, "source_update", &DrawDock::onSourceUpdate, this);
#ifdef ENABLE_FRONTEND_API
	obs_frontend_add_event_callback(&DrawDock::onFrontendEvent, this);
#endif

	rebuildIcons();
	refreshSources();
}

DrawDock::~DrawDock()
{
#ifdef ENABLE_FRONTEND_API
	obs_frontend_remove_event_callback(&DrawDock::onFrontendEvent, this);
#endif
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : {"source_create", "source_destroy", "source_remove", "source_rename"})
		signal_handler_disconnect(handler, signal, &DrawDock::onSourceSignal, this);
	signal_handler_disconnect(handler, "source_update", &DrawDock::onSourceUpdate, this);
	delete window_;
	delete overlay_;
	overlay_ = nullptr;
	lock_ = true;
	for (int i = 0; i < sourceBox_->count(); ++i)
		obs_weak_source_release(
			reinterpret_cast<obs_weak_source_t *>(sourceBox_->itemData(i).value<quintptr>()));
	sourceBox_->clear();
}

QWidget *DrawDock::buildBody()
{
	auto *body = new QWidget(this);
	body->setObjectName(QStringLiteral("drawToolsBody"));
	auto *layout = new QVBoxLayout(body);
	layout->setContentsMargins(6, 6, 6, 4);
	layout->setSpacing(4);

	sourceBox_ = new QComboBox(body);
	sourceBox_->setToolTip(tr("Draw Source to control"));
	sourceBox_->setPlaceholderText(tr("No Draw Source yet"));
	sourceBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	sourceBox_->setMinimumContentsLength(10);
	connect(sourceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DrawDock::onSourceChanged);
	layout->addWidget(sourceBox_);

	// Tool palette: one icon button per tool, exclusive like a toolbox.
	auto *grid = new QGridLayout();
	grid->setContentsMargins(0, 0, 0, 0);
	grid->setSpacing(2);
	tools_ = new QButtonGroup(this);
	tools_->setExclusive(true);
	int index = 0;
	for (const ToolSpec &spec : kTools) {
		auto *button = new QToolButton(body);
		button->setCheckable(true);
		button->setToolTip(tr(spec.name));
		button->setAccessibleName(tr(spec.name));
		button->setIconSize(QSize(16, 16));
		button->setFocusPolicy(Qt::NoFocus);
		button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
		tools_->addButton(button, int(spec.tool));
		toolButtons_[size_t(spec.tool)] = button;
		grid->addWidget(button, index / kToolColumns, index % kToolColumns);
		++index;
	}
	for (int column = 0; column < kToolColumns; ++column)
		grid->setColumnStretch(column, 1);
	connect(tools_, &QButtonGroup::idClicked, this, &DrawDock::onToolSelected);
	layout->addLayout(grid);

	auto *form = new QGridLayout();
	form->setContentsMargins(0, 2, 0, 0);
	form->setHorizontalSpacing(6);
	form->setVerticalSpacing(2);
	form->setColumnStretch(1, 1);

	auto addSlider = [&](int row, const QString &label, QSlider *&slider, QLabel *&value, int min, int max) {
		slider = new QSlider(Qt::Horizontal, body);
		slider->setRange(min, max);
		value = value_label(body);
		form->addWidget(new QLabel(label, body), row, 0);
		form->addWidget(slider, row, 1);
		form->addWidget(value, row, 2);
	};
	addSlider(0, tr("Size"), thickness_, thicknessVal_, 1, 64);
	thickness_->setToolTip(tr("Stroke thickness in canvas pixels"));
	connect(thickness_, &QSlider::valueChanged, this, &DrawDock::onThicknessChanged);
	addSlider(1, tr("Eraser"), eraserSize_, eraserVal_, 4, 256);
	eraserSize_->setToolTip(tr("Eraser diameter in canvas pixels"));
	connect(eraserSize_, &QSlider::valueChanged, this, &DrawDock::onEraserSizeChanged);
	addSlider(2, tr("Opacity"), opacity_, opacityVal_, 0, 100);
	opacity_->setToolTip(tr("0 = fully transparent, 100 = fully opaque"));
	connect(opacity_, &QSlider::valueChanged, this, &DrawDock::onOpacityChanged);

	releaseBox_ = new QComboBox(body);
	releaseBox_->addItem(tr("Keep until cleared"), 0);
	releaseBox_->addItem(tr("Fade after release"), 1);
	releaseBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	releaseBox_->setMinimumContentsLength(8);
	connect(releaseBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
		&DrawDock::onReleaseModeChanged);
	fadeMs_ = new QSpinBox(body);
	fadeMs_->setRange(50, 10000);
	fadeMs_->setSingleStep(50);
	fadeMs_->setSuffix(tr(" ms"));
	fadeMs_->setToolTip(tr("How long a stroke stays visible after release"));
	connect(fadeMs_, QOverload<int>::of(&QSpinBox::valueChanged), this, &DrawDock::onFadeMsChanged);
	form->addWidget(new QLabel(tr("Release"), body), 3, 0);
	form->addWidget(releaseBox_, 3, 1);
	form->addWidget(fadeMs_, 3, 2);
	layout->addLayout(form);

	status_ = new QLabel(tr("Drawing off"), body);
	status_->setProperty("class", QStringLiteral("text-small text-muted"));
	status_->setWordWrap(true);
	layout->addWidget(status_);
	layout->addStretch(1);
	return body;
}

QToolButton *DrawDock::makeFooterButton(const QString &tip, const char *themeClass, const char *themeIcon)
{
	auto *button = new QToolButton(this);
	button->setToolTip(tip);
	button->setAccessibleName(tip);
	button->setAutoRaise(true);
	button->setFocusPolicy(Qt::NoFocus);
	button->setIconSize(QSize(16, 16));
	// Fallback for themes without icon classes; themed icons replace it on polish.
	if (themeIcon)
		button->setIcon(theme_icon(themeIcon, darkTheme()));
	if (themeClass)
		button->setProperty("class", QString::fromLatin1(themeClass));
	return button;
}

QWidget *DrawDock::buildFooter()
{
	auto *bar = new QToolBar(this);
	bar->setObjectName(QStringLiteral("drawToolsToolbar"));
	bar->setIconSize(QSize(16, 16));
	bar->setFloatable(false);
	bar->setMovable(false);

	interactBtn_ = makeFooterButton(
		tr("Open the drawing window: the live preview you can draw on, movable to a tablet or second screen"),
		"icon-touch", "interact.svg");
	connect(interactBtn_, &QToolButton::clicked, this, &DrawDock::onOpenInteract);
	addBtn_ = makeFooterButton(tr("Add a full-scene drawing canvas to the current scene"), "icon-plus", "plus.svg");
	connect(addBtn_, &QToolButton::clicked, this, &DrawDock::onCreateCanvas);
	drawBtn_ = makeFooterButton(tr("Draw on the OBS preview (Esc stops)"), nullptr, nullptr);
	drawBtn_->setCheckable(true);
	connect(drawBtn_, &QToolButton::toggled, this, [this](bool enabled) {
		if (!overlay_->setEnabled(enabled)) {
			QSignalBlocker blocker(drawBtn_);
			drawBtn_->setChecked(false);
		}
	});
	interactAction_ = bar->addWidget(interactBtn_);
	bar->addWidget(addBtn_);
	drawAction_ = bar->addWidget(drawBtn_);
	bar->addSeparator();

	undoBtn_ = makeFooterButton(tr("Undo"), "icon-revert", "revert.svg");
	connect(undoBtn_, &QToolButton::clicked, this, &DrawDock::onUndo);
	redoBtn_ = makeFooterButton(tr("Redo"), nullptr, nullptr);
	connect(redoBtn_, &QToolButton::clicked, this, &DrawDock::onRedo);
	clearBtn_ = makeFooterButton(tr("Clear the canvas"), "icon-trash", "trash.svg");
	connect(clearBtn_, &QToolButton::clicked, this, &DrawDock::onClear);
	bar->addWidget(undoBtn_);
	bar->addWidget(redoBtn_);
	bar->addWidget(clearBtn_);
	bar->addSeparator();

	auto *spacer = new QWidget(bar);
	spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	bar->addWidget(spacer);

	colorBtn_ = makeFooterButton(tr("Pick the ink color"), nullptr, nullptr);
	connect(colorBtn_, &QToolButton::clicked, this, &DrawDock::onPickColor);
	bar->addWidget(colorBtn_);
	return bar;
}

bool DrawDock::darkTheme() const
{
	return palette().color(QPalette::Window).lightness() < 128;
}

void DrawDock::rebuildIcons()
{
	const bool dark = darkTheme();
	QColor glyph = palette().color(QPalette::ButtonText);
	// Before the theme has polished this widget the palette may still be Qt's
	// default; make sure the glyphs contrast with whatever background is shown.
	if (std::abs(glyph.lightness() - palette().color(QPalette::Window).lightness()) < 80)
		glyph = dark ? QColor(0xe0, 0xe0, 0xe0) : QColor(0x20, 0x20, 0x20);
	for (const ToolSpec &spec : kTools)
		if (auto *button = toolButtons_[size_t(spec.tool)])
			button->setIcon(tool_icon(spec.tool, glyph));
	if (drawBtn_)
		drawBtn_->setIcon(theme_icon("sources/brush.svg", dark));
	if (redoBtn_)
		redoBtn_->setIcon(mirrored_icon(theme_icon("revert.svg", dark)));
	updateSwatch();
}

void DrawDock::updateSwatch()
{
	if (!colorBtn_)
		return;
	QColor outline = palette().color(QPalette::ButtonText);
	outline.setAlphaF(0.6f);
	colorBtn_->setIcon(color_swatch_icon(argb_to_color(colorArgb_ | 0xFF000000u), outline));
}

bool DrawDock::event(QEvent *e)
{
	switch (e->type()) {
	case QEvent::PaletteChange:
	case QEvent::ApplicationPaletteChange:
	case QEvent::StyleChange:
		rebuildIcons();
		break;
	default:
		break;
	}
	return QWidget::event(e);
}

void DrawDock::setUiEnabled(bool en)
{
	for (QToolButton *button : toolButtons_)
		if (button)
			button->setEnabled(en);
	releaseBox_->setEnabled(en);
	fadeMs_->setEnabled(en && releaseBox_->currentData().toInt() == 1);
	thickness_->setEnabled(en);
	eraserSize_->setEnabled(en);
	opacity_->setEnabled(en);
	colorBtn_->setEnabled(en);
	interactBtn_->setEnabled(en);
	undoBtn_->setEnabled(en);
	redoBtn_->setEnabled(en);
	drawBtn_->setEnabled(en);
	clearBtn_->setEnabled(en);
}

// Returns a strong reference (or null); the caller releases it. The combo only
// keeps weak references so deleting a Draw Source in OBS actually destroys it.
obs_source_t *DrawDock::currentSource() const
{
	auto *weak = reinterpret_cast<obs_weak_source_t *>(sourceBox_->currentData().value<quintptr>());
	return obs_weak_source_get_source(weak);
}

Tool DrawDock::currentTool() const
{
	const int id = tools_->checkedId();
	return id < 0 ? Tool::Pen : Tool(id);
}

void DrawDock::onSourceSignal(void *data, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (source && !is_draw_source(source))
		return;
	QMetaObject::invokeMethod(static_cast<DrawDock *>(data), "scheduleRefresh", Qt::QueuedConnection);
}

void DrawDock::onSourceUpdate(void *data, calldata_t *cd)
{
	auto *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source || !is_draw_source(source))
		return;
	QMetaObject::invokeMethod(static_cast<DrawDock *>(data), "scheduleReload", Qt::QueuedConnection);
}

void DrawDock::scheduleReload()
{
	if (!shuttingDown_)
		reloadTimer_->start();
}

void DrawDock::reloadCurrent()
{
	if (shuttingDown_ || lock_)
		return;
	obs_source_t *src = currentSource();
	if (src)
		loadFromSource(src);
	obs_source_release(src);
}

void DrawDock::setStatus(const QString &status)
{
	status_->setText(status);
}

void DrawDock::selectSource(obs_source_t *source)
{
	if (!source)
		return;
	for (int i = 0; i < sourceBox_->count(); ++i) {
		auto *weak = reinterpret_cast<obs_weak_source_t *>(sourceBox_->itemData(i).value<quintptr>());
		if (obs_weak_source_references_source(weak, source)) {
			if (i != sourceBox_->currentIndex())
				sourceBox_->setCurrentIndex(i);
			return;
		}
	}
}

#ifdef ENABLE_FRONTEND_API
void DrawDock::onFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto *dock = static_cast<DrawDock *>(data);
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		QMetaObject::invokeMethod(dock, "scheduleRefresh", Qt::QueuedConnection);
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		// Drop our references so the old collection can actually be torn down.
		QMetaObject::invokeMethod(dock, "releaseSources", Qt::AutoConnection);
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
	case OBS_FRONTEND_EVENT_EXIT:
		dock->shuttingDown_ = true;
		QMetaObject::invokeMethod(dock, "releaseSources", Qt::AutoConnection);
		break;
	case OBS_FRONTEND_EVENT_THEME_CHANGED:
		QMetaObject::invokeMethod(dock, "rebuildIcons", Qt::QueuedConnection);
		break;
	default:
		break;
	}
}
#endif

void DrawDock::scheduleRefresh()
{
	if (!shuttingDown_)
		refreshTimer_->start();
}

void DrawDock::releaseSources()
{
	lock_ = true;
	overlay_->setEnabled(false);
	overlay_->setSource(nullptr);
	if (window_)
		window_->setSource(nullptr);
	for (int i = 0; i < sourceBox_->count(); ++i)
		obs_weak_source_release(
			reinterpret_cast<obs_weak_source_t *>(sourceBox_->itemData(i).value<quintptr>()));
	sourceBox_->clear();
	setUiEnabled(false);
	lock_ = false;
}

void DrawDock::refreshSources()
{
	if (shuttingDown_)
		return;
	lock_ = true;
	obs_source_t *prev = currentSource();

	for (int i = 0; i < sourceBox_->count(); ++i)
		obs_weak_source_release(
			reinterpret_cast<obs_weak_source_t *>(sourceBox_->itemData(i).value<quintptr>()));
	sourceBox_->clear();

	std::vector<obs_source_t *> refs;
	obs_enum_sources(
		[](void *param, obs_source_t *s) {
			if (s && is_draw_source(s))
				static_cast<std::vector<obs_source_t *> *>(param)->push_back(obs_source_get_ref(s));
			return true;
		},
		&refs);

	int idxToSelect = -1;
	for (size_t i = 0; i < refs.size(); ++i) {
		const char *name = obs_source_get_name(refs[i]);
		sourceBox_->addItem(QString::fromUtf8(name ? name : "(unnamed)"),
				    QVariant::fromValue(quintptr(obs_source_get_weak_source(refs[i]))));
		if (prev && refs[i] == prev)
			idxToSelect = int(i);
		obs_source_release(refs[i]);
	}
	// A placeholder text stops Qt from auto-selecting the first row, so pick it ourselves.
	if (idxToSelect < 0 && !refs.empty())
		idxToSelect = 0;
	if (idxToSelect >= 0)
		sourceBox_->setCurrentIndex(idxToSelect);
	obs_source_release(prev);

	obs_source_t *current = currentSource();
	overlay_->setSource(current);
	if (!current) {
		setUiEnabled(false);
	} else {
		setUiEnabled(true);
		loadFromSource(current);
	}
	syncWindow();
	lock_ = false;
	emit sourceSelected(current);
	obs_source_release(current);
}

void DrawDock::loadFromSource(obs_source_t *src)
{
	if (!src)
		return;
	lock_ = true;

	obs_data_t *settings = obs_source_get_settings(src);
	const int toolVal = std::clamp(int(obs_data_get_int(settings, kTool)), 0, kToolMax);
	const int thick = int(obs_data_get_double(settings, kThick));
	const int eraser = int(obs_data_get_int(settings, kEraserSize));
	const int opacity = int(obs_data_get_int(settings, kOpacity));
	const uint32_t col = uint32_t(obs_data_get_int(settings, kColor));
	const int relMode = int(obs_data_get_int(settings, kReleaseMode));
	const int fadeMs = int(obs_data_get_int(settings, kFadeMs));
	obs_data_release(settings);

	if (auto *button = toolButtons_[size_t(toolVal)])
		button->setChecked(true);
	thickness_->setValue(std::clamp(thick, 1, 64));
	thicknessVal_->setText(QString::number(thickness_->value()));
	eraserSize_->setValue(std::clamp(eraser, 4, 256));
	eraserVal_->setText(QString::number(eraserSize_->value()));
	opacity_->setValue(std::clamp(opacity, 0, 100));
	opacityVal_->setText(QString::number(opacity_->value()) + "%");

	colorArgb_ = col;
	updateSwatch();

	int rIdx = releaseBox_->findData(relMode == 1 ? 1 : 0);
	releaseBox_->setCurrentIndex(rIdx < 0 ? 0 : rIdx);
	fadeMs_->setValue(std::clamp(fadeMs > 0 ? fadeMs : 450, 50, 10000));
	fadeMs_->setEnabled(relMode == 1);

	hintOverlays();
	lock_ = false;
}

void DrawDock::applyToSource()
{
	if (lock_)
		return;
	obs_source_t *src = currentSource();
	if (!src)
		return;
	obs_data_t *s = obs_source_get_settings(src);
	obs_data_set_int(s, kTool, int(currentTool()));
	obs_data_set_int(s, kThick, thickness_->value());
	obs_data_set_int(s, kEraserSize, eraserSize_->value());
	obs_data_set_int(s, kColor, int64_t(colorArgb_));
	obs_data_set_int(s, kOpacity, opacity_->value());
	obs_data_set_int(s, kReleaseMode, releaseBox_->currentData().toInt());
	obs_data_set_int(s, kFadeMs, fadeMs_->value());
	obs_source_update(src, s);
	obs_data_release(s);
	obs_source_release(src);
}

void DrawDock::sendCommand(const char *key)
{
	obs_source_t *src = currentSource();
	if (!src)
		return;
	obs_data_t *s = obs_source_get_settings(src);
	obs_data_set_bool(s, key, true);
	obs_source_update(src, s);
	obs_data_release(s);
	obs_source_release(src);
}

void DrawDock::hintOverlays()
{
	const Tool tool = currentTool();
	const int eraser = eraserSize_->value();
	overlay_->setToolHint(tool, eraser);
	if (window_)
		window_->overlay()->setToolHint(tool, eraser);
	emit toolHintChanged(int(tool), eraser);
}

void DrawDock::syncWindow()
{
	if (!window_)
		return;
	obs_source_t *src = currentSource();
	window_->setSource(src);
	obs_source_release(src);
}

void DrawDock::onSourceChanged(int)
{
	if (lock_)
		return;
	obs_source_t *src = currentSource();
	overlay_->setSource(src);
	loadFromSource(src);
	syncWindow();
	emit sourceSelected(src);
	obs_source_release(src);
}

void DrawDock::onToolSelected(int tool)
{
	(void)tool;
	hintOverlays();
	applyToSource();
}

void DrawDock::onThicknessChanged(int v)
{
	thicknessVal_->setText(QString::number(v));
	applyToSource();
}

void DrawDock::onEraserSizeChanged(int v)
{
	eraserVal_->setText(QString::number(v));
	hintOverlays();
	applyToSource();
}

void DrawDock::onOpacityChanged(int v)
{
	opacityVal_->setText(QString::number(v) + "%");
	applyToSource();
}

void DrawDock::onReleaseModeChanged(int)
{
	fadeMs_->setEnabled(releaseBox_->currentData().toInt() == 1);
	applyToSource();
}

void DrawDock::onFadeMsChanged(int)
{
	applyToSource();
}

void DrawDock::onPickColor()
{
	const QColor chosen = QColorDialog::getColor(argb_to_color(colorArgb_ | 0xFF000000u), this, tr("Ink color"));
	if (!chosen.isValid())
		return;
	colorArgb_ = color_to_argb(chosen);
	updateSwatch();
	applyToSource();
}

void DrawDock::onUndo()
{
	sendCommand("_do_undo");
}

void DrawDock::onRedo()
{
	sendCommand("_do_redo");
}

void DrawDock::onClear()
{
	sendCommand("_do_clear");
}

void DrawDock::onCreateCanvas()
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *sceneSource = obs_frontend_preview_program_mode_active()
					    ? obs_frontend_get_current_preview_scene()
					    : obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		obs_source_release(sceneSource);
		return;
	}
	QString name = tr("Smart Screen");
	for (int suffix = 2;; ++suffix) {
		obs_source_t *existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			break;
		obs_source_release(existing);
		name = tr("Smart Screen %1").arg(suffix);
	}
	obs_source_t *source = obs_source_create(kDrawSourceId, name.toUtf8().constData(), nullptr, nullptr);
	if (source) {
		if (auto *item = obs_scene_add(scene, source)) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			obs_sceneitem_set_locked(item, true);
		}
		refreshSources();
		for (int i = 0; i < sourceBox_->count(); ++i)
			if (obs_weak_source_references_source(
				    reinterpret_cast<obs_weak_source_t *>(sourceBox_->itemData(i).value<quintptr>()),
				    source))
				sourceBox_->setCurrentIndex(i);
		obs_source_release(source);
	}
	obs_source_release(sceneSource);
#endif
}

void DrawDock::onOpenInteract()
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *src = currentSource();
	if (!src)
		return;
	if (!window_) {
		window_ = new DrawWindow(static_cast<QWidget *>(obs_frontend_get_main_window()));
		connect(window_, &DrawWindow::statusChanged, status_, &QLabel::setText);
	}
	window_->setSource(src);
	obs_source_release(src);
	hintOverlays();
	window_->present();
#endif
}

} // namespace drawsrc

// -----------------------------------------------------------------------------
// Dock lifecycle
// -----------------------------------------------------------------------------

static QPointer<QWidget> g_dockWidget;

// Stable ID/title (these must never change between versions).
static constexpr const char *kDockId = "draw_tools_dock";
static constexpr const char *kDockTitle = "Draw Tools";

void Draw_create_dock()
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	if (g_dockWidget)
		return;

	auto *panel = new drawsrc::DrawDock(nullptr);
	panel->setObjectName(QStringLiteral("DrawToolsDockPanel"));

	if (!obs_frontend_add_dock_by_id(kDockId, kDockTitle, panel)) {
		delete panel;
		return;
	}

	g_dockWidget = panel;
	blog(LOG_INFO, "[%s][dock] Dock created (id=%s)", PLUGIN_NAME, kDockId);
#endif
}

void Draw_destroy_dock()
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	if (!g_dockWidget)
		return;
	obs_frontend_remove_dock(kDockId);
	g_dockWidget = nullptr;
	blog(LOG_INFO, "[%s][dock] Dock destroyed (id=%s)", PLUGIN_NAME, kDockId);
#endif
}

drawsrc::DrawDock *Draw_get_dock()
{
	return qobject_cast<drawsrc::DrawDock *>(g_dockWidget.data());
}

#endif // ENABLE_QT
