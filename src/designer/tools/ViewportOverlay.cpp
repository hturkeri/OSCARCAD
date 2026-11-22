// SC-UX (WP-B) — tool handle overlay. See ViewportOverlay.h.
#include "ViewportOverlay.h"
#include "ToolBase.h"
#include "pc/ShapeViewer.h"
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace pcd {

ViewportOverlay::ViewportOverlay(pc::viewer::ShapeViewer* viewer) : QWidget(viewer), viewer_(viewer) {
  setObjectName("viewportOverlay");
  setAttribute(Qt::WA_TransparentForMouseEvents, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::NoFocus);
  if (viewer) {
    setGeometry(viewer->rect());
    viewer->installEventFilter(this);
    connect(viewer, &pc::viewer::ShapeViewer::cameraChanged, this, qOverload<>(&QWidget::update));
  }
  show();
}

void ViewportOverlay::setTool(ToolBase* t) {
  tool_ = t;
  update();
}

int ViewportOverlay::handleCount() const { return tool_ ? tool_->handleCount() : 0; }

bool ViewportOverlay::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize) setGeometry(viewer_->rect());
  return QWidget::eventFilter(o, e);
}

void ViewportOverlay::paintEvent(QPaintEvent*) {
  if (!tool_ && gaps_.empty()) return;
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  if (tool_) tool_->paintOverlay(p, *this);
  if (!gaps_.empty() && viewer_) {   // SC23 (O-P0-5)
    for (size_t i = 0; i < gaps_.size(); ++i) {
      const double a[3] = {gaps_[i].X(), gaps_[i].Y(), gaps_[i].Z()};
      drawGapMarker(p, viewer_->worldToScreen(a), i == 0);
    }
  }
}

void ViewportOverlay::drawGapMarker(QPainter& p, const QPointF& c, bool tag) {
  p.save();
  p.setRenderHint(QPainter::Antialiasing);
  p.setPen(QPen(QColor(220, 40, 40), 2.0));
  p.setBrush(QColor(220, 40, 40, 60));
  p.drawEllipse(c, 9.0, 9.0);
  p.restore();
  if (tag) drawTag(p, c + QPointF(12, -14), "açık uç", QColor(220, 40, 40));
}

void ViewportOverlay::setGapMarkers(const std::vector<gp_Pnt>& pts) {
  gaps_ = pts;
  raise();
  update();
}

void ViewportOverlay::clearMarkers() {
  if (gaps_.empty()) return;
  gaps_.clear();
  update();
}

// SpaceClaim-like pull arrow: a thick yellow shaft with a cone-like head, dark outline, small base disc.
void ViewportOverlay::drawPullArrow(QPainter& p, const QPointF& base, const QPointF& tip, bool hot, const QColor& c0) {
  QPointF d = tip - base;
  const double L = std::hypot(d.x(), d.y());
  if (L < 1e-6) return;
  d /= L;
  const QPointF n(-d.y(), d.x());
  const QColor c = hot ? c0.lighter(118) : c0;
  const QColor edge(120, 85, 0);
  const double headL = std::min(20.0, L * 0.42), headW = 9.5, shaftW = 3.2;
  const QPointF hb = tip - d * headL;
  QPainterPath shaft;
  shaft.moveTo(base + n * shaftW);
  shaft.lineTo(hb + n * shaftW);
  shaft.lineTo(hb - n * shaftW);
  shaft.lineTo(base - n * shaftW);
  shaft.closeSubpath();
  QPainterPath head;
  head.moveTo(tip);
  head.lineTo(hb + n * headW);
  head.quadTo(hb + d * 3.0, hb - n * headW);
  head.closeSubpath();
  p.save();
  // soft shadow
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 45));
  p.drawPath(shaft.translated(1.5, 2));
  p.drawPath(head.translated(1.5, 2));
  QLinearGradient g(base + n * shaftW, base - n * shaftW);
  g.setColorAt(0, c.lighter(135));
  g.setColorAt(0.5, c);
  g.setColorAt(1, c.darker(125));
  p.setPen(QPen(edge, 1.0));
  p.setBrush(g);
  p.drawPath(shaft);
  QLinearGradient gh(hb + n * headW, hb - n * headW);
  gh.setColorAt(0, c.lighter(140));
  gh.setColorAt(0.45, c);
  gh.setColorAt(1, c.darker(135));
  p.setBrush(gh);
  p.drawPath(head);
  // base disc (the dragged face's anchor)
  p.setBrush(QColor(255, 255, 255, 230));
  p.setPen(QPen(edge, 1.2));
  p.drawEllipse(base, 4.0, 4.0);
  if (hot) {
    p.setPen(QPen(QColor(255, 255, 255, 180), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(tip, headW + 3, headW + 3);
  }
  p.restore();
}

void ViewportOverlay::drawRevolveArc(QPainter& p, const QPointF& c, double r, double a0, double sw, bool hot) {
  p.save();
  const QColor col = hot ? QColor(255, 220, 80) : QColor(250, 200, 30);
  QPainterPath path;
  const int n = 48;
  for (int i = 0; i <= n; ++i) {
    const double a = (a0 + sw * i / n) * M_PI / 180.0;
    const QPointF q = c + QPointF(std::cos(a), -std::sin(a)) * r;
    if (i == 0) path.moveTo(q); else path.lineTo(q);
  }
  p.setPen(QPen(QColor(120, 85, 0), 7.0, Qt::SolidLine, Qt::RoundCap));
  p.drawPath(path);
  p.setPen(QPen(col, 5.0, Qt::SolidLine, Qt::RoundCap));
  p.drawPath(path);
  // arrow head at the end of the sweep
  const double ae = (a0 + sw) * M_PI / 180.0, s = sw >= 0 ? 1 : -1;
  const QPointF e = c + QPointF(std::cos(ae), -std::sin(ae)) * r;
  const QPointF t(-std::sin(ae) * s, -std::cos(ae) * s);   // tangent (screen)
  const QPointF nn(-t.y(), t.x());
  QPolygonF head({e + t * 14, e + nn * 8, e - nn * 8});
  p.setPen(QPen(QColor(120, 85, 0), 1));
  p.setBrush(col);
  p.drawPolygon(head);
  p.restore();
}

void ViewportOverlay::drawAxisArrow(QPainter& p, const QPointF& base, const QPointF& tip, const QColor& c, bool hot) {
  drawPullArrow(p, base, tip, hot, c);
}

void ViewportOverlay::drawAnchor(QPainter& p, const QPointF& at, bool hot) {
  p.save();
  QRadialGradient g(at - QPointF(2, 2), 8);
  g.setColorAt(0, QColor(255, 255, 255));
  g.setColorAt(1, hot ? QColor(255, 170, 40) : QColor(40, 120, 230));
  p.setBrush(g);
  p.setPen(QPen(QColor(20, 50, 110), 1));
  p.drawEllipse(at, 6.5, 6.5);
  p.restore();
}

void ViewportOverlay::drawTag(QPainter& p, const QPointF& at, const QString& text, const QColor& c) {
  p.save();
  QFont f = p.font();
  f.setPointSizeF(8.5);
  f.setBold(true);
  p.setFont(f);
  QRectF r = QFontMetricsF(f).boundingRect(text).adjusted(-5, -2, 5, 2);
  r.moveCenter(at);
  p.setPen(QPen(c.darker(140), 1));
  p.setBrush(QColor(255, 255, 255, 225));
  p.drawRoundedRect(r, 3, 3);
  p.setPen(c.darker(160));
  p.drawText(r, Qt::AlignCenter, text);
  p.restore();
}

} // namespace pcd
