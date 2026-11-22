#pragma once
// SC-UX (WP-B) — QPainter overlay over the 3D viewer for tool handles (SPEC §2.4, §6.2): the yellow Pull arrow,
// the revolve arc, the Move triad, Combine / Fill markers. Transparent for the mouse: the ToolManager's viewer event
// filter gives handle drags to the active tool. Positions come from ShapeViewer::worldToScreen (no GL code).
#include <QPointF>
#include <QPointer>
#include <QWidget>
#include <gp_Pnt.hxx>
#include <vector>
#include "pc/ShapeViewer.h"

namespace pcd {
class ToolBase;

class ViewportOverlay : public QWidget {
  Q_OBJECT
  Q_PROPERTY(int handleCount READ handleCount)
public:
  explicit ViewportOverlay(pc::viewer::ShapeViewer* viewer);
  void setTool(ToolBase* t);
  ToolBase* tool() const { return tool_; }
  int handleCount() const;                     // test hook (ACCEPTANCE #13)
  pc::viewer::ShapeViewer* viewer() const { return viewer_; }

  // drawing helpers (screen space)
  static void drawPullArrow(QPainter& p, const QPointF& base, const QPointF& tip, bool hot, const QColor& c = QColor(250, 200, 30));
  static void drawRevolveArc(QPainter& p, const QPointF& center, double radius, double startDeg, double sweepDeg, bool hot);
  static void drawAxisArrow(QPainter& p, const QPointF& base, const QPointF& tip, const QColor& c, bool hot);
  static void drawAnchor(QPainter& p, const QPointF& at, bool hot);
  static void drawTag(QPainter& p, const QPointF& at, const QString& text, const QColor& c);
  // Red open-end marker of Doldur (Fill): circle + optional "boşluk" tag (FillTool and the End Sketch summary).
  static void drawGapMarker(QPainter& p, const QPointF& at, bool tag);

  // SC23 (O-P0-5): open ends of the sketch just finished, drawn on top of every tool until cleared (document
  // change, entering Sketch mode, clearMarkers()). World coordinates.
  void setGapMarkers(const std::vector<gp_Pnt>& pts);
  void clearMarkers();
  int gapMarkerCount() const { return int(gaps_.size()); }

protected:
  void paintEvent(QPaintEvent*) override;
  bool eventFilter(QObject* o, QEvent* e) override;

private:
  QPointer<pc::viewer::ShapeViewer> viewer_;
  ToolBase* tool_ = nullptr;
  std::vector<gp_Pnt> gaps_;   // SC23
};

} // namespace pcd
