#pragma once

#ifdef ENABLE_QT

#include <QWidget>

class QComboBox;
class QSlider;
class QSpinBox;
class QPushButton;
class QLabel;
class QTimer;

struct obs_source;
typedef struct obs_source obs_source_t;

namespace drawsrc {

class DrawDock : public QWidget {
  Q_OBJECT
public:
  explicit DrawDock(QWidget *parent = nullptr);
  ~DrawDock() override;

  // Refresh list of draw sources.
  void refreshSources();

private slots:
  void onSourceChanged(int idx);
  void onToolChanged(int idx);
  void onThicknessChanged(int v);
  void onOpacityChanged(int v);
  void onReleaseModeChanged(int idx);
  void onFadeMsChanged(int v);
  void onPickColor();
  void onUndo();
  void onClear();
  void onOpenInteract();

private:
  void setUiEnabled(bool en);
  void loadFromSource(obs_source_t *src);
  void applyToSource();
  obs_source_t *currentSource() const;

private:
  QComboBox *sourceBox_ = nullptr;
  QComboBox *toolBox_ = nullptr;
  QComboBox *releaseBox_ = nullptr;
  QSpinBox  *fadeMs_ = nullptr;
  QSlider *thickness_ = nullptr;
  QLabel *thicknessVal_ = nullptr;
  QSlider *opacity_ = nullptr;
  QLabel *opacityVal_ = nullptr;
  QPushButton *colorBtn_ = nullptr;
  QLabel *colorSwatch_ = nullptr;
  QPushButton *interactBtn_ = nullptr;
  QPushButton *undoBtn_ = nullptr;
  QPushButton *clearBtn_ = nullptr;
  QTimer *refreshTimer_ = nullptr;

  bool lock_ = false;
  uint32_t colorArgb_ = 0xFFFFFFFFu;
  int opacityPct_ = 100;
  int releaseMode_ = 0;
  int fadeMsVal_ = 450;
};

} // namespace drawsrc

// SLT-style dock lifecycle helpers.
// Implemented in draw_dock.cpp; used by plugin-main.cpp.
void Draw_create_dock();
void Draw_destroy_dock();
drawsrc::DrawDock *Draw_get_dock();

#endif // ENABLE_QT
