#pragma once
// SC-UX (WP-B) — Taşı (Move), SPEC §2.7 (as far as the model allows):
//   * datum plane: normal arrow -> Plane `offset` (+ value); plane parameters follow the Pull binding rule
//   * sketch (3D): normal arrow -> sketch `offsetExpr` (= "Izgarayı taşı")
//   * "Desen oluştur" + a face of an Extrude / Hole: X/Y/Z arrows -> new linear Pattern (count 2, spacing = value)
//   * bodies: refused until CR-3 (Transform feature)
#include "ToolBase.h"
#include <QPoint>

namespace pcd {

class MoveTool : public ToolBase {
  Q_OBJECT
public:
  using ToolBase::ToolBase;
  QString id() const override { return "move"; }
  QString title() const override { return "Taşı (Move)"; }
  QList<ToolGuide> guides() const override;
  QString activeGuide() const override { return guide_; }
  void setActiveGuide(const QString& g) override;
  QList<ToolOption> options() const override;
  void setOption(const QString& key, const QVariant& value) override;
  bool busy() const override { return valueBoxOpen() || pressed_; }
  bool cancel() override;
  bool pick(const PickEvent& e) override;
  bool wantsKey(const QKeyEvent* e) const override;
  bool keyPress(QKeyEvent* e) override;
  void documentChanged() override { retarget(); }
  void paintOverlay(QPainter& p, const ViewportOverlay& o) override;
  int handleCount() const override;
  bool viewerMousePress(QMouseEvent* e) override;
  bool viewerMouseMove(QMouseEvent* e) override;
  bool viewerMouseRelease(QMouseEvent* e) override;
  bool commitValue(const QString& text);   // value = displacement along the active arrow

protected:
  void onActivate() override;
  void onDeactivate() override;

private:
  enum class T { None, Datum, Sketch, PatternSource, Body };
  void retarget();
  void reportState();   // SC23 (O-P0-1)
  QPointF arrowTip(int axis) const;
  gp_Dir axisDir(int axis) const;
  T kind_ = T::None;
  QString id_;            // datum / sketch / source feature id
  gp_Pnt anchor_;
  gp_Dir normal_;
  int axis_ = 0;          // pattern / body: 0 X, 1 Y, 2 Z; datum / sketch: 0 = normal
  bool pattern_ = false;
  QString guide_ = "select";
  bool pressed_ = false, dragged_ = false;
  int dragAxis_ = -1;
  QPoint pressPos_;
  double dragValue_ = 0;
  QMetaObject::Connection selConn_;
};

} // namespace pcd
