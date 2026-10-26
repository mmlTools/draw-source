#pragma once

#ifdef ENABLE_QT

#include "drawing_document.hpp"

#include <QPointer>
#include <QWidget>
#include <array>
#include <cstdint>

#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

// obs_source is an opaque C struct; Qt's connect() must not try to inspect it
// when a signal carries a pointer to it (GCC/Clang reject sizeof otherwise).
Q_DECLARE_OPAQUE_POINTER(obs_source_t *)

class QAction;
class QButtonGroup;
class QComboBox;
class QLabel;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

struct obs_source;
typedef struct obs_source obs_source_t;
struct calldata;
typedef struct calldata calldata_t;

namespace drawsrc {

class DrawWindow;
class PreviewOverlay;

class DrawDock : public QWidget {
	Q_OBJECT
public:
	// `embedded` builds the panel as the drawing window's sidebar: same tools
	// and settings, minus the buttons that only make sense in the main dock.
	explicit DrawDock(QWidget *parent = nullptr, bool embedded = false);
	~DrawDock() override;

	// Rebuild the list of Draw Sources from the current scene collection.
	void refreshSources();
	// Select the given Draw Source in the picker if it is listed.
	void selectSource(obs_source_t *source);

signals:
	void sourceSelected(obs_source_t *source);
	void toolHintChanged(int tool, int eraserSize);

public slots:
	// Coalesces bursts of source signals into one refresh on the UI thread.
	void scheduleRefresh();
	// Re-reads the current source's settings (another panel may have changed them).
	void scheduleReload();
	void reloadCurrent();
	void releaseSources();
	void rebuildIcons();
	void setStatus(const QString &status);

protected:
	bool event(QEvent *event) override;

private slots:
	void onSourceChanged(int idx);
	void onToolSelected(int tool);
	void onThicknessChanged(int v);
	void onEraserSizeChanged(int v);
	void onOpacityChanged(int v);
	void onReleaseModeChanged(int idx);
	void onFadeMsChanged(int v);
	void onPickColor();
	void onUndo();
	void onRedo();
	void onClear();
	void onOpenInteract();
	void onCreateCanvas();

private:
	QWidget *buildBody();
	QWidget *buildFooter();
	QToolButton *makeFooterButton(const QString &tip, const char *themeClass, const char *themeIcon);
	void updateSwatch();
	void setUiEnabled(bool en);
	void loadFromSource(obs_source_t *src);
	void applyToSource();
	void sendCommand(const char *key);
	void hintOverlays();
	void syncWindow();
	obs_source_t *currentSource() const;
	Tool currentTool() const;
	bool darkTheme() const;
	static void onSourceSignal(void *data, calldata_t *cd);
	static void onSourceUpdate(void *data, calldata_t *cd);
#ifdef ENABLE_FRONTEND_API
	static void onFrontendEvent(enum obs_frontend_event event, void *data);
#endif

private:
	QComboBox *sourceBox_ = nullptr;
	QButtonGroup *tools_ = nullptr;
	std::array<QToolButton *, kToolMax + 1> toolButtons_{};
	QSlider *thickness_ = nullptr;
	QLabel *thicknessVal_ = nullptr;
	QSlider *eraserSize_ = nullptr;
	QLabel *eraserVal_ = nullptr;
	QSlider *opacity_ = nullptr;
	QLabel *opacityVal_ = nullptr;
	QComboBox *releaseBox_ = nullptr;
	QSpinBox *fadeMs_ = nullptr;
	QLabel *status_ = nullptr;
	QToolButton *interactBtn_ = nullptr;
	QToolButton *addBtn_ = nullptr;
	QToolButton *drawBtn_ = nullptr;
	QToolButton *undoBtn_ = nullptr;
	QToolButton *redoBtn_ = nullptr;
	QToolButton *clearBtn_ = nullptr;
	QToolButton *colorBtn_ = nullptr;
	QAction *interactAction_ = nullptr;
	QAction *drawAction_ = nullptr;
	PreviewOverlay *overlay_ = nullptr;
	QPointer<DrawWindow> window_;
	QTimer *refreshTimer_ = nullptr;
	QTimer *reloadTimer_ = nullptr;

	bool embedded_ = false;
	bool lock_ = false;
	bool shuttingDown_ = false;
	uint32_t colorArgb_ = 0xFFFFFFFFu;
};

} // namespace drawsrc

// Dock lifecycle helpers implemented in draw_dock.cpp; used by plugin-main.cpp.
void Draw_create_dock();
void Draw_destroy_dock();
drawsrc::DrawDock *Draw_get_dock();

#endif // ENABLE_QT
