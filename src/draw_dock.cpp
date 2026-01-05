#ifdef ENABLE_QT

#include "plugin-support.h"

#include "draw_dock.hpp"
#include "draw_source.hpp"

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

		// Thickness 1-8
		{
			auto *wrap = new QWidget();
			auto *h = new QHBoxLayout();
			h->setContentsMargins(0, 0, 0, 0);
			h->setSpacing(8);

			thickness_ = new QSlider(Qt::Horizontal);
			thickness_->setRange(1, 8);
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

		root->addLayout(form);
	}

	// Actions
	{
		auto *row = new QHBoxLayout();
		undoBtn_ = new QPushButton(tr("Undo"));
		clearBtn_ = new QPushButton(tr("Clear"));
		connect(undoBtn_, &QPushButton::clicked, this, &DrawDock::onUndo);
		connect(clearBtn_, &QPushButton::clicked, this, &DrawDock::onClear);
		row->addWidget(undoBtn_);
		row->addWidget(clearBtn_);
		row->addStretch(1);
		root->addLayout(row);
	}

	setLayout(root);

	refreshTimer_ = new QTimer(this);
	refreshTimer_->setInterval(1500);
	connect(refreshTimer_, &QTimer::timeout, this, &DrawDock::refreshSources);
	refreshTimer_->start();

	refreshSources();
}

DrawDock::~DrawDock() = default;

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

	for (int i = 0; i < sourceBox_->count(); ++i) {
		const quintptr ptr = sourceBox_->itemData(i).value<quintptr>();
		if (ptr)
			obs_source_release(reinterpret_cast<obs_source_t *>(ptr));
	}

	obs_source_t *prev = currentSource();
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
	obs_data_release(settings);

	int toolIdx = toolBox_->findData(toolVal);
	if (toolIdx < 0)
		toolIdx = 0;
	toolBox_->setCurrentIndex(toolIdx);

	thickness_->setValue(std::max(1, std::min(8, thick)));
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

	QColor chosen = QColorDialog::getColor(init, this, tr("Pick Draw Color"), QColorDialog::ShowAlphaChannel);
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

static QWidget *g_dockWidget = nullptr;

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
	obs_frontend_add_dock_by_id(kDockId, kDockTitle, panel);

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
	return qobject_cast<drawsrc::DrawDock *>(g_dockWidget);
}

#endif // ENABLE_QT
