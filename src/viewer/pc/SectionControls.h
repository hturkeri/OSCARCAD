#pragma once
// Section-view controls shared by the designer and the generated apps (runner): a checkable "Kesit"
// action plus a compact panel (plane preset, offset slider, flip) bound to a ShapeViewer.
// The state is view-only; nothing is stored in the document.
#include "pc/ShapeViewer.h"
#include <QWidget>

class QAction;
class QCheckBox;
class QComboBox;
class QLabel;
class QSlider;

namespace pc::viewer {

class SectionControls : public QWidget {
  Q_OBJECT
public:
  // overlay=true: the panel is a child of the viewer, floats in its top-right corner and is only
  // visible while the section is on. overlay=false: a plain row of widgets (e.g. for a toolbar),
  // always visible, disabled while the section is off.
  explicit SectionControls(ShapeViewer* viewer, bool overlay, QWidget* parent = nullptr);

  QAction* toggleAction() const { return toggle_; }   // checkable "Kesit" (objectName "sectionToggle")

  bool sectionOn() const;
  ShapeViewer::SectionPreset preset() const;
  int offsetPercent() const;                          // -100..100 (% of the bbox half-extent)
  bool flipped() const;

  void setSectionOn(bool on);
  void setPreset(ShapeViewer::SectionPreset p);       // None switches the section off
  void setOffsetPercent(int percent);
  void setFlipped(bool on);
  // CLI helper: "xz" | "yz" | "xy" | "quarter" | "none". Returns false for an unknown value.
  bool applySpec(const QString& spec);

signals:
  void changed();

protected:
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  void apply();
  void updateLabel();
  void reposition();

  ShapeViewer* viewer_ = nullptr;
  bool overlay_ = false;
  QAction* toggle_ = nullptr;
  QComboBox* combo_ = nullptr;
  QSlider* slider_ = nullptr;
  QLabel* label_ = nullptr;
  QCheckBox* flip_ = nullptr;
};

} // namespace pc::viewer
