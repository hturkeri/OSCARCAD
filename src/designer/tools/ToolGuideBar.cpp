// SC-UX (WP-B) — tool guide strip. See ToolGuideBar.h.
#include "ToolGuideBar.h"
#include <QLabel>
#include "ToolApi.h"
#include "ViewportHud.h"
#include <QGridLayout>
#include <QButtonGroup>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <cmath>

namespace pcd {

QIcon ToolGuideBar::guideIcon(const QString& key, int S) {
  QPixmap pm(S, S);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(S / 24.0, S / 24.0);
  const QColor blue(40, 100, 200), yel(245, 190, 30), dark(60, 60, 70);
  if (key == "select") {
    QPainterPath a;
    a.moveTo(6, 3); a.lineTo(6, 19); a.lineTo(10, 15); a.lineTo(13, 21); a.lineTo(15.5, 20); a.lineTo(12.5, 14); a.lineTo(18, 14);
    a.closeSubpath();
    p.setPen(QPen(dark, 1.2));
    p.setBrush(Qt::white);
    p.drawPath(a);
  } else if (key == "direction" || key == "pulldir") {
    p.setPen(QPen(QColor(120, 85, 0), 1));
    p.setBrush(yel);
    p.drawPolygon(QPolygonF({QPointF(12, 2), QPointF(19, 11), QPointF(14.5, 11), QPointF(14.5, 21), QPointF(9.5, 21), QPointF(9.5, 11), QPointF(5, 11)}));
  } else if (key == "revolve") {
    p.setPen(QPen(QColor(230, 120, 20), 1.4, Qt::DashLine));
    p.drawLine(QPointF(12, 1), QPointF(12, 23));
    p.setPen(QPen(yel.darker(110), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(QRectF(4, 6, 16, 12), 30 * 16, 260 * 16);
    p.setBrush(yel.darker(110));
    p.setPen(Qt::NoPen);
    p.drawPolygon(QPolygonF({QPointF(19, 6), QPointF(21, 12), QPointF(15, 10)}));
  } else if (key == "upto") {
    p.setPen(QPen(dark, 2));
    p.drawLine(QPointF(4, 4), QPointF(20, 4));
    p.setPen(QPen(QColor(120, 85, 0), 1));
    p.setBrush(yel);
    p.drawPolygon(QPolygonF({QPointF(12, 6), QPointF(17, 13), QPointF(14, 13), QPointF(14, 21), QPointF(10, 21), QPointF(10, 13), QPointF(7, 13)}));
  } else if (key == "ruler") {
    p.setPen(QPen(dark, 1));
    p.setBrush(QColor(250, 225, 140));
    p.translate(12, 12);
    p.rotate(-35);
    p.drawRect(QRectF(-10, -3.5, 20, 7));
    for (int i = -8; i <= 8; i += 3) p.drawLine(QPointF(i, -3.5), QPointF(i, i % 2 ? -0.5 : 0.5));
  } else if (key == "target" || key == "cutter") {
    p.setPen(QPen(blue, 1.4));
    p.setBrush(key == "target" ? QColor(140, 180, 240) : QColor(240, 170, 140));
    p.drawRect(QRectF(4, 7, 12, 12));
    p.setBrush(key == "target" ? QColor(240, 170, 140, 140) : QColor(140, 180, 240, 140));
    p.drawEllipse(QRectF(10, 3, 11, 11));
  } else if (key == "anchor") {
    p.setPen(QPen(dark, 1.2));
    p.setBrush(QColor(40, 120, 230));
    p.drawEllipse(QPointF(12, 12), 5, 5);
  } else {
    p.setPen(QPen(dark, 1.4));
    p.drawEllipse(QPointF(12, 12), 7, 7);
  }
  return QIcon(pm);
}

ToolGuideBar::ToolGuideBar(ToolManager* tm, QWidget* viewer) : QFrame(viewer), tm_(tm), viewer_(viewer) {
  setObjectName("toolGuideBar");
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet("QFrame#toolGuideBar{background:rgba(250,251,253,215); border:1px solid #b9c3d3; border-radius:4px;}"
                "QToolButton{border:1px solid transparent; border-radius:3px; padding:2px; background:transparent;}"
                "QToolButton:hover{border:1px solid #9ab0d0; background:#eef3fb;}"
                "QToolButton:checked{background:#f7c97a; border:1px solid #e09a2b;}"
                "QToolButton:disabled{background:transparent;}");
  lay_ = new QGridLayout(this);
  lay_->setContentsMargins(3, 3, 3, 3);
  lay_->setSpacing(2);
  group_ = new QButtonGroup(this);
  group_->setExclusive(true);
  if (viewer) viewer->installEventFilter(this);
  connect(tm, &ToolManager::activeToolChanged, this, [this] { rebuild(); });
  connect(tm, &ToolManager::guidesChanged, this, [this] { rebuild(); });
  rebuild();
}

QString ToolGuideBar::checkedKey() const {
  auto* b = group_->checkedButton();
  return b ? b->property("guideKey").toString() : QString();
}

void ToolGuideBar::rebuild() {
  for (auto* b : group_->buttons()) { group_->removeButton(b); lay_->removeWidget(b); b->hide(); b->deleteLater(); }
  IActiveTool* t = tm_ ? tm_->active() : nullptr;
  const QList<ToolGuide> gs = t ? t->guides() : QList<ToolGuide>();
  const QString act = t ? t->activeGuide() : QString();
  for (const ToolGuide& g : gs) {
    auto* b = new QToolButton(this);
    b->setObjectName("guide." + g.key);
    b->setProperty("guideKey", g.key);
    b->setCheckable(true);
    b->setIcon(guideIcon(g.icon.isEmpty() ? g.key : g.icon));
    b->setIconSize(QSize(24, 24));
    b->setToolTip(g.tip.isEmpty() ? g.label : g.tip);
    b->setEnabled(g.enabled);
    b->setChecked(g.key == act);
    b->setFocusPolicy(Qt::NoFocus);
    group_->addButton(b);
    // "Seç" on its own row, the others two per row (S-P1-5)
    const int n = group_->buttons().size() - 1;   // index of b
    const bool firstIsSelect = !gs.isEmpty() && gs.first().key == "select";
    if (firstIsSelect && n == 0) lay_->addWidget(b, 0, 0);
    else {
      const int k = firstIsSelect ? n - 1 : n;
      lay_->addWidget(b, (firstIsSelect ? 1 : 0) + k / 2, k % 2);
    }
    b->show();
    const QString key = g.key;
    connect(b, &QToolButton::clicked, this, [this, key] {
      if (tm_ && tm_->active()) tm_->active()->setActiveGuide(key);
    });
  }
  setVisible(!gs.isEmpty());
  adjustSize();
  place();
  raise();
}

void ToolGuideBar::place() {
  if (!viewer_) return;
  adjustSize();
  if (!hud_) {
    if (auto* h = viewer_->findChild<ViewportHud*>("viewportHud", Qt::FindDirectChildrenOnly)) {
      hud_ = h;
      connect(h, &ViewportHud::layoutChanged, this, [this] { place(); });
    }
  }
  QPoint at(8, 72);   // no HUD: below one tool line + one message line
  if (auto* h = qobject_cast<ViewportHud*>(hud_.data())) {
    at = h->guideArea().topLeft();
    // the slot holds ONE message line; a longer (wrapped) message, e.g. a failed feature's reason + hint, would
    // cover the guides: step below it while it is shown, back into the slot when it goes (SC23 merge)
    if (QLabel* m = h->messageLabel(); m && m->isVisible() && m->geometry().bottom() + 4 > at.y())
      at.setY(m->geometry().bottom() + 4);
  }
  move(at);
}

bool ToolGuideBar::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize) place();
  if (o == viewer_ && e->type() == QEvent::ChildAdded) QMetaObject::invokeMethod(this, [this] { raise(); }, Qt::QueuedConnection);
  return QFrame::eventFilter(o, e);
}

} // namespace pcd
