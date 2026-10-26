#pragma once

#include "drawing_document.hpp"

#include <QCursor>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <obs.h>

class QWidget;

namespace drawsrc {

// Letterboxed placement of a canvas inside a display, in device pixels.
struct PreviewFit {
	float scale = 0; // device pixels per canvas pixel
	int x = 0;
	int y = 0;
	int width = 0;
	int height = 0;
	bool valid = false;
};

// Shared by the input mapping and the drawing window's renderer so both agree
// on where the canvas sits. `border` reproduces OBS's preview padding.
PreviewFit fit_canvas(uint32_t canvasW, uint32_t canvasH, int pixelsW, int pixelsH, int border);

// Returns an owned reference to the visible top-level item showing `target`, or null.
obs_sceneitem_t *find_visible_item(obs_source_t *sceneSource, obs_source_t *target);

// Intercepts pointer input on a display widget without painting over OBS's
// native video surface and forwards it to the draw source in canvas pixels.
class PreviewOverlay : public QObject {
	Q_OBJECT
public:
	explicit PreviewOverlay(QObject *parent);
	~PreviewOverlay() override;
	void setSource(obs_source_t *source);
	// Draw on OBS's own preview (the dock's "Draw on preview" toggle).
	bool setEnabled(bool enabled);
	// Draw on a plugin-owned display (the drawing window). When the source is
	// shown in the current scene the widget is assumed to show the whole scene,
	// otherwise the source alone, matching what DrawDisplay renders.
	bool attach(QWidget *widget);
	bool enabled() const { return enabled_; }
	// Keeps the cursor in sync with the dock's tool and eraser size so the
	// eraser footprint is visible before anything is wiped.
	void setToolHint(Tool tool, int eraserSize);

signals:
	void statusChanged(const QString &status);
	void stopped();
	// Esc / Tab in window mode; the window decides what to do.
	void escapePressed();
	void sidebarToggleRequested();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	enum class Mode { ObsPreview, Window };
	bool mapPoint(QPointF position, obs_mouse_event &event, bool beginning);
	double sourceToPreviewScale() const;
	void updateCursor();
	void setRingHidden(bool hidden);
	void press(QPointF position, bool eraser);
	void move(QPointF position);
	void release();
	void command(const char *key);
	void begin(QWidget *widget, Mode mode, obs_source_t *source);
	QPointer<QWidget> preview_;
	QCursor previousCursor_;
	bool previousTouch_ = false;
	// Weak while idle so the dock never keeps a deleted Draw Source alive; a
	// strong reference is only held while input is being captured.
	obs_weak_source_t *weak_ = nullptr;
	obs_source_t *source_ = nullptr;
	obs_source_t *strokeScene_ = nullptr;
	Mode mode_ = Mode::ObsPreview;
	// Window mode only: the current stroke maps 1:1 onto the source because the
	// source is not placed in the current scene.
	bool sourceOnly_ = false;
	Tool tool_ = Tool::Pen;
	int eraserSize_ = 32;
	bool eraserStroke_ = false;
	bool enabled_ = false;
	bool drawing_ = false;
	int touchId_ = -1;
	obs_mouse_event last_{};
};

} // namespace drawsrc
