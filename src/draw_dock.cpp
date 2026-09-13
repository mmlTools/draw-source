#ifdef ENABLE_QT

#include "plugin-support.h"

#include "draw_dock.hpp"
#include "draw_source.hpp"
#include "preview_overlay.hpp"

#include <obs.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QFrame>
#include <QDockWidget>
#include <QSignalBlocker>
#include <QStyle>
#include <QPointer>

#include <cstring>
#include <algorithm>
#include <vector>

namespace drawsrc {

static constexpr const char *kSourceId = "instant_highlight_source_draw";

static constexpr const char *kTool = "tool";
static constexpr const char *kThick = "thickness";
static constexpr const char *kColor = "color";
static constexpr const char *kOpacity = "opacity";
static constexpr const char *kReleaseMode = "release_mode"; // 0=keep, 1=fade
static constexpr const char *kFadeMs = "fade_ms";

static bool is_draw_source(obs_source_t *src)
{
	const char *id = obs_source_get_id(src);
	return id && strcmp(id, kSourceId) == 0;
}

DrawDock::DrawDock(QWidget *parent) : QWidget(parent)
{
	auto *root = new QVBoxLayout();
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(8);
	overlay_ = new PreviewOverlay(this);
	drawBtn_ = new QPushButton(tr("Draw on preview"));
	drawBtn_->setCheckable(true);
	connect(drawBtn_, &QPushButton::toggled, this, [this](bool enabled) {
		if (!overlay_->setEnabled(enabled)) {
			QSignalBlocker blocker(drawBtn_);
			drawBtn_->setChecked(false);
		}
	});
	connect(overlay_, &PreviewOverlay::stopped, this, [this]() {
		QSignalBlocker blocker(drawBtn_);
		drawBtn_->setChecked(false);
		status_->setText(tr("Drawing off"));
	});

	// Header row
	{
		auto *row = new QHBoxLayout();
		auto *title = new QLabel(tr("Draw Tools"));
		QFont f = title->font();
		f.setBold(true);
		title->setFont(f);
		row->addWidget(title);
		row->addStretch(1);

		interactBtn_ = new QPushButton(tr("Interact"));
		interactBtn_->setToolTip(tr("Open the Interact window for the selected Draw Source"));
		connect(interactBtn_, &QPushButton::clicked, this, &DrawDock::onOpenInteract);
		row->addWidget(interactBtn_);
		root->addLayout(row);
	}
	{
		auto *row = new QHBoxLayout();
		auto *create = new QPushButton(tr("Add canvas"));
		create->setIcon(style()->standardIcon(QStyle::SP_FileIcon));
		connect(create, &QPushButton::clicked, this, &DrawDock::onCreateCanvas);
		row->addWidget(create);
		row->addWidget(drawBtn_);
		root->addLayout(row);
	}

	// Source selector
	{
		auto *form = new QFormLayout();
		form->setLabelAlignment(Qt::AlignLeft);
		form->setFormAlignment(Qt::AlignTop);

		sourceBox_ = new QComboBox();
		sourceBox_->setToolTip(tr("Choose which Draw Source to control"));
		connect(sourceBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
			&DrawDock::onSourceChanged);
		form->addRow(tr("Target"), sourceBox_);

		toolBox_ = new QComboBox();
		toolBox_->addItem(tr("Pen"), (int)Tool::Pen);
		toolBox_->addItem(tr("Square"), (int)Tool::Square);
		toolBox_->addItem(tr("Circle"), (int)Tool::Circle);
		toolBox_->addItem(tr("Arrow"), (int)Tool::Arrow);
		toolBox_->addItem(tr("Heart"), (int)Tool::Heart);
		toolBox_->addItem(tr("Eraser"), (int)Tool::Eraser);
		connect(toolBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DrawDock::onToolChanged);
		form->addRow(tr("Tool"), toolBox_);

		// Release behavior: keep or fade.
		{
			auto *wrap = new QWidget();
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(8);

			releaseBox_ = new QComboBox();
			releaseBox_->addItem(tr("Keep until cleared"), 0);
			releaseBox_->addItem(tr("Fade after release"), 1);
			connect(releaseBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
				&DrawDock::onReleaseModeChanged);

			fadeMs_ = new QSpinBox();
			fadeMs_->setRange(50, 10000);
			fadeMs_->setSingleStep(50);
			fadeMs_->setSuffix(tr(" ms"));
			fadeMs_->setToolTip(tr("How long a stroke stays visible after mouse release"));
			connect(fadeMs_, QOverload<int>::of(&QSpinBox::valueChanged), this, &DrawDock::onFadeMsChanged);

			h->addWidget(releaseBox_, 1);
			h->addWidget(fadeMs_, 0);
			wrap->setLayout(h);
			form->addRow(tr("On release"), wrap);
		}

		// Color row with swatch + pick
		{
			auto *wrap = new QWidget();
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(8);

			colorSwatch_ = new QLabel();
			colorSwatch_->setFixedSize(28, 18);
			colorSwatch_->setFrameShape(QFrame::StyledPanel);
			colorSwatch_->setAutoFillBackground(true);

			colorBtn_ = new QPushButton(tr("Pick"));
			connect(colorBtn_, &QPushButton::clicked, this, &DrawDock::onPickColor);

			h->addWidget(colorSwatch_);
			h->addWidget(colorBtn_);
			h->addStretch(1);
			wrap->setLayout(h);
			form->addRow(tr("Color"), wrap);
		}

		opacity_ = new QSlider(Qt::Horizontal);
		opacity_->setRange(0, 100);
		opacity_->setValue(100);
		opacity_->setToolTip(tr("0 = fully transparent, 100 = fully opaque"));
		connect(opacity_, &QSlider::valueChanged, this, &DrawDock::onOpacityChanged);

		// Opacity with value label
		{
			auto *wrap = new QWidget();
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(8);

			opacityVal_ = new QLabel(QString::number(opacity_->value()) + "%");
			opacityVal_->setMinimumWidth(44);
			opacityVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

			h->addWidget(opacity_, 1);
			h->addWidget(opacityVal_, 0);
			wrap->setLayout(h);
			form->addRow(tr("Opacity"), wrap);
		}

		// Thickness in source pixels.
		{
			auto *wrap = new QWidget();
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(8);

			thickness_ = new QSlider(Qt::Horizontal);
			thickness_->setRange(1, 64);
			thickness_->setValue(3);
			connect(thickness_, &QSlider::valueChanged, this, &DrawDock::onThicknessChanged);

			thicknessVal_ = new QLabel("3");
			thicknessVal_->setMinimumWidth(28);
			thicknessVal_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

			h->addWidget(thickness_, 1);
			h->addWidget(thicknessVal_, 0);
			wrap->setLayout(h);

			form->addRow(tr("Thickness"), wrap);
		}
		eraserSize_ = new QSpinBox();
		eraserSize_->setRange(4, 256);
		eraserSize_->setSuffix(tr(" px"));
		connect(eraserSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
			[this](int) { applyToSource(); });
		form->addRow(tr("Eraser size"), eraserSize_);

		root->addLayout(form);
	}

	// Actions
	{
		auto *row = new QHBoxLayout();
		undoBtn_ = new QPushButton(tr("Undo"));
		undoBtn_->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
		redoBtn_ = new QPushButton(tr("Redo"));
		redoBtn_->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
		clearBtn_ = new QPushButton(tr("Clear"));
		clearBtn_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
		connect(undoBtn_, &QPushButton::clicked, this, &DrawDock::onUndo);
		connect(redoBtn_, &QPushButton::clicked, this, &DrawDock::onRedo);
		connect(clearBtn_, &QPushButton::clicked, this, &DrawDock::onClear);
		row->addWidget(undoBtn_);
		row->addWidget(redoBtn_);
		row->addWidget(clearBtn_);
		row->addStretch(1);
		root->addLayout(row);
	}

	status_ = new QLabel(tr("Drawing off"));
	status_->setWordWrap(true);
	connect(overlay_, &PreviewOverlay::statusChanged, status_, &QLabel::setText);
	root->addWidget(status_);
	root->addStretch(1);
	setLayout(root);

	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(1500);
	connect(refreshTimer_, &QTimer::timeout, this, &DrawDock::refreshSources);
	refreshTimer_->start();

	refreshSources();
}

DrawDock::~DrawDock()
{
	delete overlay_;
	for (int i = 0; i < sourceBox_->count(); ++i)
		obs_source_release(reinterpret_cast<obs_source_t *>(sourceBox_->itemData(i).value<quintptr>()));
}

void DrawDock::setUiEnabled(bool en)
{
	toolBox_->setEnabled(en);
	if (releaseBox_)
		releaseBox_->setEnabled(en);
	if (fadeMs_)
		fadeMs_->setEnabled(en && releaseBox_ && releaseBox_->currentData().toInt() == 1);
	thickness_->setEnabled(en);
	opacity_->setEnabled(en);
	colorBtn_->setEnabled(en);
	interactBtn_->setEnabled(en);
	undoBtn_->setEnabled(en);
	redoBtn_->setEnabled(en);
	drawBtn_->setEnabled(en);
	eraserSize_->setEnabled(en);
	clearBtn_->setEnabled(en);
}

obs_source_t *DrawDock::currentSource() const
{
	const QVariant v = sourceBox_->currentData();
	const quintptr ptr = v.value<quintptr>();
	return reinterpret_cast<obs_source_t *>(ptr);
}

void DrawDock::refreshSources()
{
	lock_ = true;
	obs_source_t *prev = obs_source_get_ref(currentSource());

	for (int i = 0; i < sourceBox_->count(); ++i) {
		const quintptr ptr = sourceBox_->itemData(i).value<quintptr>();
		if (ptr)
			obs_source_release(reinterpret_cast<obs_source_t *>(ptr));
	}

	sourceBox_->clear();

	std::vector<obs_source_t *> refs;
	obs_enum_sources(
		[](void *param, obs_source_t *s) {
			auto *out = static_cast<std::vector<obs_source_t *> *>(param);
			if (!s)
				return true;
			if (!is_draw_source(s))
				return true;
			out->push_back(obs_source_get_ref(s));
			return true;
		},
		&refs);

	int idxToSelect = -1;
	for (size_t i = 0; i < refs.size(); ++i) {
		obs_source_t *s = refs[i];
		const char *name = obs_source_get_name(s);
		sourceBox_->addItem(QString::fromUtf8(name ? name : "(unnamed)"), QVariant::fromValue((quintptr)s));
		if (prev && s == prev)
			idxToSelect = (int)i;
	}

	if (idxToSelect >= 0)
		sourceBox_->setCurrentIndex(idxToSelect);
	obs_source_release(prev);
	overlay_->setSource(currentSource());

	if (sourceBox_->count() == 0) {
		setUiEnabled(false);
	} else {
		setUiEnabled(true);
		loadFromSource(currentSource());
	}

	lock_ = false;
}

void DrawDock::loadFromSource(obs_source_t *src)
{
	if (!src)
		return;

	lock_ = true;

	obs_data_t *settings = obs_source_get_settings(src);
	const int toolVal = (int)obs_data_get_int(settings, kTool);
	const int thick = (int)obs_data_get_double(settings, kThick);
	const int opacity = (int)obs_data_get_int(settings, kOpacity);
	const uint32_t col = (uint32_t)obs_data_get_int(settings, kColor);
	const int relMode = (int)obs_data_get_int(settings, kReleaseMode);
	const int fadeMs = (int)obs_data_get_int(settings, kFadeMs);
	const int eraserSize = (int)obs_data_get_int(settings, "eraser_size");
	obs_data_release(settings);

	int toolIdx = toolBox_->findData(toolVal);
	if (toolIdx < 0)
		toolIdx = 0;
	toolBox_->setCurrentIndex(toolIdx);

	thickness_->setValue(std::clamp(thick, 1, 64));
	eraserSize_->setValue(std::clamp(eraserSize, 4, 256));
	opacity_->setValue(std::max(0, std::min(100, opacity)));

	colorArgb_ = col;

	releaseMode_ = (relMode == 1) ? 1 : 0;
	fadeMsVal_ = std::max(50, std::min(10000, fadeMs > 0 ? fadeMs : 450));

	if (releaseBox_) {
		int rIdx = releaseBox_->findData(releaseMode_);
		if (rIdx < 0)
			rIdx = 0;
		releaseBox_->setCurrentIndex(rIdx);
	}
	if (fadeMs_) {
		fadeMs_->setValue(fadeMsVal_);
		fadeMs_->setEnabled(releaseMode_ == 1);
	}

	QColor qc;
	qc.setAlpha((colorArgb_ >> 24) & 0xFF);
	qc.setRed((colorArgb_ >> 16) & 0xFF);
	qc.setGreen((colorArgb_ >> 8) & 0xFF);
	qc.setBlue((colorArgb_ >> 0) & 0xFF);

	colorSwatch_->setStyleSheet(
		QString("background-color:%1; border:1px solid rgba(0,0,0,0.35); border-radius:4px;").arg(qc.name()));

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
	obs_data_set_int(s, kTool, toolBox_->currentData().toInt());
	obs_data_set_int(s, kThick, thickness_->value());
	obs_data_set_int(s, "eraser_size", eraserSize_->value());
	obs_data_set_int(s, kColor, (int64_t)colorArgb_);
	obs_data_set_int(s, kOpacity, opacity_->value());
	if (releaseBox_)
		obs_data_set_int(s, kReleaseMode, releaseBox_->currentData().toInt());
	if (fadeMs_)
		obs_data_set_int(s, kFadeMs, fadeMs_->value());
	obs_source_update(src, s);
	obs_data_release(s);
}

void DrawDock::onSourceChanged(int)
{
	if (lock_)
		return;
	overlay_->setSource(currentSource());
	loadFromSource(currentSource());
}

void DrawDock::onToolChanged(int)
{
	applyToSource();
}

void DrawDock::onThicknessChanged(int v)
{
	if (thicknessVal_)
		thicknessVal_->setText(QString::number(v));
	applyToSource();
}

void DrawDock::onOpacityChanged(int v)
{
	if (opacityVal_)
		opacityVal_->setText(QString::number(v) + "%");
	applyToSource();
}

void DrawDock::onReleaseModeChanged(int)
{
	if (fadeMs_ && releaseBox_) {
		fadeMs_->setEnabled(releaseBox_->currentData().toInt() == 1);
	}
	applyToSource();
}

void DrawDock::onFadeMsChanged(int)
{
	applyToSource();
}

void DrawDock::onPickColor()
{
	QColor init;
	init.setAlpha((colorArgb_ >> 24) & 0xFF);
	init.setRed((colorArgb_ >> 16) & 0xFF);
	init.setGreen((colorArgb_ >> 8) & 0xFF);
	init.setBlue((colorArgb_ >> 0) & 0xFF);

	QColor chosen = QColorDialog::getColor(init, this, tr("Pick Draw Color"));
	if (!chosen.isValid())
		return;

	colorArgb_ = ((uint32_t)chosen.alpha() << 24) | ((uint32_t)chosen.red() << 16) |
		     ((uint32_t)chosen.green() << 8) | ((uint32_t)chosen.blue() << 0);

	colorSwatch_->setStyleSheet(
		QString("background-color:%1; border:1px solid rgba(0,0,0,0.35); border-radius:4px;")
			.arg(chosen.name()));

	applyToSource();
}

void DrawDock::onUndo()
{
	obs_source_t *src = currentSource();
	if (!src)
		return;

	obs_data_t *s = obs_source_get_settings(src);
	obs_data_set_bool(s, "_do_undo", true);
	obs_source_update(src, s);
	obs_data_release(s);
}

void DrawDock::onClear()
{
	obs_source_t *src = currentSource();
	if (!src)
		return;

	obs_data_t *s = obs_source_get_settings(src);
	obs_data_set_bool(s, "_do_clear", true);
	obs_source_update(src, s);
	obs_data_release(s);
}

void DrawDock::onRedo()
{
	obs_source_t *src = currentSource();
	if (!src)
		return;
	obs_data_t *settings = obs_source_get_settings(src);
	obs_data_set_bool(settings, "_do_redo", true);
	obs_source_update(src, settings);
	obs_data_release(settings);
}

void DrawDock::onCreateCanvas()
{
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
	obs_source_t *source = obs_source_create(kSourceId, name.toUtf8().constData(), nullptr, nullptr);
	if (source) {
		if (auto *item = obs_scene_add(scene, source)) {
			obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
			obs_sceneitem_set_locked(item, true);
		}
		refreshSources();
		for (int i = 0; i < sourceBox_->count(); ++i)
			if (sourceBox_->itemData(i).value<quintptr>() == reinterpret_cast<quintptr>(source))
				sourceBox_->setCurrentIndex(i);
		obs_source_release(source);
	}
	obs_source_release(sceneSource);
}

void DrawDock::onOpenInteract()
{
#ifdef ENABLE_FRONTEND_API
	obs_source_t *src = currentSource();
	if (!src)
		return;
	obs_frontend_open_source_interaction(src);
#endif
}

} // namespace drawsrc

// -----------------------------------------------------------------------------
// Dock lifecycle (Smart Lower Thirds style)
// -----------------------------------------------------------------------------

static QPointer<QWidget> g_dockWidget;

// Use a stable ID/title (these must never change between versions)
static constexpr const char *kDockId = "draw_tools_dock";
static constexpr const char *kDockTitle = "Draw Tools";

void Draw_create_dock()
{
#if defined(ENABLE_FRONTEND_API) && defined(ENABLE_QT)
	if (g_dockWidget)
		return;

	auto *panel = new drawsrc::DrawDock(nullptr);

	// Helps Qt/OBS identify the widget consistently
	panel->setObjectName(QStringLiteral("DrawToolsDockPanel"));

	// Prefer by-id API (non-deprecated, persistent)
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

	// In your OBS headers, remove_dock expects const char* id.
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
