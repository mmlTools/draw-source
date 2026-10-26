#pragma once

#ifdef ENABLE_QT

#include <QDialog>
#include <QWidget>
#include <mutex>

#include <obs.h>
#ifdef ENABLE_FRONTEND_API
#include <obs-frontend-api.h>
#endif

class QToolButton;

namespace drawsrc {

class DrawDock;
class PreviewOverlay;

// Native OBS display that mirrors the live preview: the whole scene (program
// output, or the preview scene in Studio Mode) when the draw source is placed
// in it, otherwise the draw source alone.
class DrawDisplay : public QWidget {
	Q_OBJECT
public:
	explicit DrawDisplay(QWidget *parent = nullptr);
	~DrawDisplay() override;
	void setSource(obs_source_t *source);
	void setScene(obs_source_t *scene, bool studioMode);
	QPaintEngine *paintEngine() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;
	void moveEvent(QMoveEvent *event) override;

private:
	void createDisplay();
	void destroyDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);
	obs_display_t *display_ = nullptr;
	// Read on the graphics thread, written on the UI thread.
	std::mutex mutex_;
	obs_weak_source_t *source_ = nullptr;
	obs_weak_source_t *scene_ = nullptr;
	bool studioMode_ = false;
};

// Plugin-owned replacement for OBS's Interact window: shows the preview with
// the ink composited in, accepts mouse, pen and touch input, and can be moved
// to a tablet or second screen.
class DrawWindow : public QDialog {
	Q_OBJECT
public:
	explicit DrawWindow(QWidget *parent);
	~DrawWindow() override;
	void setSource(obs_source_t *source);
	void present();
	PreviewOverlay *overlay() const { return overlay_; }

signals:
	void statusChanged(const QString &status);

public slots:
	void setSidebarVisible(bool visible);
	void toggleSidebar();

protected:
	void showEvent(QShowEvent *event) override;
	void hideEvent(QHideEvent *event) override;
	bool event(QEvent *event) override;

private:
	void refreshScene();
	void updateTitle();
	void updateToggleIcon();
	void saveState();
#ifdef ENABLE_FRONTEND_API
	static void onFrontendEvent(enum obs_frontend_event event, void *data);
#endif
	DrawDisplay *display_ = nullptr;
	PreviewOverlay *overlay_ = nullptr;
	// Collapsible copy of the dock's tools so a tablet screen is self-sufficient.
	DrawDock *sidebar_ = nullptr;
	QToolButton *toggle_ = nullptr;
	// Weak so a hidden window never keeps a deleted Draw Source alive.
	obs_weak_source_t *source_ = nullptr;
};

} // namespace drawsrc

#endif // ENABLE_QT
