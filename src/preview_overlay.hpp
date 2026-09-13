#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QCursor>
#include <obs.h>

class QWidget;

namespace drawsrc {

// Intercepts preview input without painting over OBS's native video surface.
class PreviewOverlay : public QObject {
	Q_OBJECT
public:
	explicit PreviewOverlay(QObject *parent);
	~PreviewOverlay() override;
	void setSource(obs_source_t *source);
	bool setEnabled(bool enabled);
	bool enabled() const { return enabled_; }

signals:
	void statusChanged(const QString &status);
	void stopped();

protected:
	bool eventFilter(QObject *object, QEvent *event) override;

private:
	bool mapPoint(QPointF position, obs_mouse_event &event, bool beginning);
	void press(QPointF position, bool eraser);
	void move(QPointF position);
	void release();
	void command(const char *key);
	QPointer<QWidget> preview_;
	QCursor previousCursor_;
	bool previousTouch_ = false;
	obs_source_t *source_ = nullptr;
	obs_source_t *strokeScene_ = nullptr;
	bool enabled_ = false;
	bool drawing_ = false;
	int touchId_ = -1;
	obs_mouse_event last_{};
};

} // namespace drawsrc
