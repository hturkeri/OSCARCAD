// SC-UX (WP-B) — mini-toolbar. See MiniToolbar.h.
#include "MiniToolbar.h"
#include "SketchEditor.h"
#include "SketchRegions.h"
#include "ToolApi.h"
#include "ToolGuideBar.h"
#include "ViewportHud.h"
#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <cmath>

namespace pcd {

namespace {
QIcon miniIcon(const QString& k) {
  QPixmap pm(20, 20);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  p.setRenderHint(QPainter::Antialiasing);
  const QColor dark(55, 65, 85), blue(40, 110, 220);
  if (k == "to3d") {
    p.setPen(QPen(dark, 1.2));
    p.setBrush(QColor(170, 200, 240));
    p.drawPolygon(QPolygonF({QPointF(3, 8), QPointF(11, 4), QPointF(17, 7), QPointF(9, 11)}));
    p.setBrush(QColor(120, 160, 220));
    p.drawPolygon(QPolygonF({QPointF(3, 8), QPointF(9, 11), QPointF(9, 17), QPointF(3, 14)}));
    p.setBrush(QColor(90, 130, 200));
    p.drawPolygon(QPolygonF({QPointF(9, 11), QPointF(17, 7), QPointF(17, 13), QPointF(9, 17)}));
  } else if (k == "newplane") {
    p.setPen(QPen(blue, 1.3));
    p.setBrush(QColor(110, 170, 245, 90));
    p.drawPolygon(QPolygonF({QPointF(2, 13), QPointF(9, 8), QPointF(18, 9), QPointF(11, 15)}));
    p.setPen(QPen(QColor(230, 120, 20), 1.6));
    p.drawLine(QPointF(10, 11.5), QPointF(10, 2));
    p.drawLine(QPointF(10, 2), QPointF(8, 5));
    p.drawLine(QPointF(10, 2), QPointF(12, 5));
  } else if (k == "movegrid") {
    p.setPen(QPen(QColor(120, 140, 170), 1));
    for (int i = 3; i <= 17; i += 4) { p.drawLine(QPointF(i, 3), QPointF(i, 17)); p.drawLine(QPointF(3, i), QPointF(17, i)); }
    p.setPen(QPen(blue, 2));
    p.drawLine(QPointF(10, 10), QPointF(17, 3));
    p.drawLine(QPointF(17, 3), QPointF(13, 3.5));
    p.drawLine(QPointF(17, 3), QPointF(16.5, 7));
  } else if (k == "planview") {
    p.setPen(QPen(dark, 1.3));
    p.setBrush(QColor(235, 240, 250));
    p.drawRect(QRectF(3, 3, 14, 14));
    p.setPen(QPen(blue, 1.6));
    p.drawEllipse(QPointF(10, 10), 3.5, 3.5);
    p.drawLine(QPointF(10, 3), QPointF(10, 6));
  } else if (k == "group") {
    p.setPen(QPen(QColor(30, 95, 190), 1.3));
    p.setBrush(QColor(238, 244, 255));
    p.drawRoundedRect(QRectF(2.5, 4.5, 15, 11), 2, 2);
    QFont f = p.font();
    f.setBold(true);
    f.setPixelSize(10);
    p.setFont(f);
    p.drawText(QRectF(2.5, 4.5, 15, 11), Qt::AlignCenter, "P");
  } else if (k == "merge.auto" || k == "merge.add" || k == "merge.cut" || k == "merge.nomerge") {
    p.setPen(QPen(dark, 1.1));
    p.setBrush(QColor(170, 195, 230));
    if (k == "merge.nomerge") {
      p.drawRect(QRectF(2, 8, 7, 9));
      p.setBrush(QColor(245, 200, 90));
      p.drawRect(QRectF(11, 3, 7, 9));
    } else {
      p.drawRect(QRectF(2, 7, 13, 10));
      p.setBrush(QColor(245, 200, 90));
      if (k != "merge.cut") p.drawRect(QRectF(7, 2, 9, 6));
      QFont f = p.font();
      f.setBold(true);
      f.setPixelSize(11);
      p.setFont(f);
      p.setPen(k == "merge.cut" ? QColor(200, 40, 40) : k == "merge.add" ? QColor(20, 140, 50) : QColor(40, 90, 170));
      p.drawText(QRectF(9, 8, 11, 11), Qt::AlignCenter, k == "merge.cut" ? "−" : k == "merge.add" ? "+" : "A");
    }
  } else if (k == "edge.round" || k == "edge.chamfer") {
    p.setPen(QPen(dark, 1.2));
    p.setBrush(QColor(170, 195, 230));
    QPainterPath path;
    path.moveTo(3, 17);
    path.lineTo(3, 9);
    if (k == "edge.round") path.quadTo(3, 3, 9, 3); else path.lineTo(9, 3);
    path.lineTo(17, 3);
    path.lineTo(17, 17);
    path.closeSubpath();
    p.drawPath(path);
    p.setPen(QPen(QColor(230, 130, 20), 2));
    p.setBrush(Qt::NoBrush);
    if (k == "edge.round") p.drawArc(QRectF(3, 3, 12, 12), 90 * 16, 90 * 16); else p.drawLine(QPointF(3, 9), QPointF(9, 3));
  } else if (k == "bothSides") {
    p.setPen(QPen(QColor(120, 85, 0), 1));
    p.setBrush(QColor(245, 190, 30));
    p.drawPolygon(QPolygonF({QPointF(10, 1), QPointF(15, 6), QPointF(12, 6), QPointF(12, 14), QPointF(15, 14), QPointF(10, 19), QPointF(5, 14),
                             QPointF(8, 14), QPointF(8, 6), QPointF(5, 6)}));
    p.setPen(QPen(dark, 1.2));
    p.drawLine(QPointF(2, 10), QPointF(18, 10));
  } else {
    return ToolGuideBar::guideIcon(k, 20);
  }
  return QIcon(pm);
}
} // namespace

MiniToolbar::MiniToolbar(ToolManager* tm, IDesignHost* host, QWidget* viewer) : QFrame(viewer), tm_(tm), host_(host), viewer_(viewer) {
  setObjectName("miniToolbar");
  setAttribute(Qt::WA_StyledBackground, true);
  setStyleSheet("QFrame#miniToolbar{background:rgba(248,250,253,232); border:1px solid #aebbd0; border-radius:4px;}"
                "QToolButton{border:1px solid transparent; border-radius:3px; padding:1px 4px; background:transparent; color:#1d2d44;}"
                "QToolButton:hover{border:1px solid #9ab0d0; background:#eef3fb;}"
                "QToolButton:checked{background:#f7c97a; border:1px solid #e09a2b;}");
  lay_ = new QHBoxLayout(this);
  lay_->setContentsMargins(3, 2, 3, 2);
  lay_->setSpacing(2);
  fx_ = new QGraphicsOpacityEffect(this);
  fx_->setOpacity(1.0);
  setGraphicsEffect(fx_);
  if (viewer) viewer->installEventFilter(this);
  connect(tm, &ToolManager::activeToolChanged, this, [this] { rebuild(); });
  connect(tm, &ToolManager::optionsChanged, this, [this] { rebuild(); });
  rebuild();
}

QToolButton* MiniToolbar::addButton(const QString& name, const QString& text, const QString& tip, const QIcon& icon) {
  auto* b = new QToolButton(this);
  b->setObjectName(name);
  b->setText(text);
  b->setToolTip(tip);
  b->setIcon(icon);
  b->setIconSize(QSize(18, 18));
  b->setToolButtonStyle(icon.isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
  b->setFocusPolicy(Qt::NoFocus);
  lay_->addWidget(b);
  b->show();   // children added to a visible parent are otherwise shown only later (queued)
  return b;
}

void MiniToolbar::rebuild() {
  while (QLayoutItem* it = lay_->takeAt(0)) {
    if (it->widget()) { it->widget()->hide(); it->widget()->deleteLater(); }
    delete it;
  }
  sketch_ = host_ && host_->designMode() == DesignMode::Sketch;
  if (sketch_) {
    // SPEC §2.2 sketch mini-toolbar (+ Seç, the most used tool)
    auto* sel = addButton("mini.select", "Seç", "Seç (Select) — S", ToolGuideBar::guideIcon("select", 20));
    connect(sel, &QToolButton::clicked, this, [this] { if (tm_) tm_->activate("sketch.select"); });
    auto* to3d = addButton("mini.to3d", "3B'ye Dön", "3B'ye Dön (Return to 3D Mode): sketch'i bitirir, Çek aracına geçer", miniIcon("to3d"));
    connect(to3d, &QToolButton::clicked, this, [this] {
      if (!host_ || !tm_) return;
      const std::string sid = host_->openSketchId();
      if (!host_->setDesignMode(DesignMode::ThreeD)) return;
      tm_->activate("pull");
      // regions of the sketch just finished: emphasized, ready to be clicked and pulled
      std::vector<TopoDS_Face> faces;
      gp_Ax3 fr;
      QStringList names;
      if (!sid.empty() && sketchRegionFaces(host_->document(), host_->modelResult(), sid, faces, fr))
        for (size_t k = 0; k < faces.size(); ++k) names << regionName(sid, int(k));
      host_->setToolEmphasis(names, {});
      if (!names.isEmpty())
        host_->showPrompt(QString("Çek: %1 bölgesine tıklayın, oku sürükleyin ya da mesafeyi yazın.").arg(QString::fromStdString(sid)), 0);
    });
    auto* np = addButton("mini.newPlane", "Yeni sketch düzlemi", "Yeni sketch düzlemi seç (Select New Sketch Plane)", miniIcon("newplane"));
    connect(np, &QToolButton::clicked, this, [this] { if (host_) host_->beginSketchPlanePick(); });
    auto* mg = addButton("mini.moveGrid", "Izgarayı taşı", "Izgarayı taşı (Move Grid): Ctrl+→ / Ctrl+← bir ızgara adımı", miniIcon("movegrid"));
    connect(mg, &QToolButton::clicked, this, [this, mg] {
      if (!host_ || !host_->sketchEditor()) return;
      QMenu m(this);
      SketchEditor* ed = host_->sketchEditor();
      m.addAction("İleri (Ctrl+→)", ed, [ed] { ed->moveGrid(1); });
      m.addAction("Geri (Ctrl+←)", ed, [ed] { ed->moveGrid(-1); });
      m.exec(mg->mapToGlobal(QPoint(0, mg->height())));
    });
    auto* pv = addButton("mini.planView", "Plan görünümü", "Plan görünümü (Plan View) — V", miniIcon("planview"));
    connect(pv, &QToolButton::clicked, this, [this] { if (host_ && host_->sketchEditor()) host_->sketchEditor()->planView(); });
  } else if (tm_ && tm_->active()) {
    for (const ToolOption& o : tm_->active()->options()) {
      if (!o.onMiniToolbar) continue;
      if (o.type == ToolOption::Type::Choice) {
        for (const QString& c : o.choices) {
          const QString key = c.section('|', 0, 0), label = c.section('|', 1);
          if (o.key == "merge" && key == "intersect") continue;   // advanced: Options panel only
          QString shortText = label.section(" (", 0, 0);
          const QString ik = o.key + "." + key;
          const bool hasIcon = o.key == "merge" || o.key == "edge";
          auto* b = addButton(QString("mini.%1.%2").arg(o.key, key), shortText, label, hasIcon ? miniIcon(ik) : QIcon());
          if (hasIcon) b->setToolButtonStyle(Qt::ToolButtonIconOnly);
          b->setCheckable(true);
          b->setChecked(o.value.toString() == key);
          b->setEnabled(o.enabled);
          const QString ok = o.key;
          connect(b, &QToolButton::clicked, this, [this, ok, key] { if (tm_) tm_->setOption(ok, key); });
        }
      } else if (o.type == ToolOption::Type::Bool) {
        auto* b = addButton("mini." + o.key, o.label.section(" (", 0, 0), o.label, o.key == "bothSides" ? miniIcon("bothSides") : QIcon());
        if (o.key == "bothSides") b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setCheckable(true);
        b->setChecked(o.value.toBool());
        b->setEnabled(o.enabled);
        const QString ok = o.key;
        connect(b, &QToolButton::toggled, this, [this, ok](bool on) { if (tm_) tm_->setOption(ok, on); });
      } else if (o.type == ToolOption::Type::Action) {
        auto* b = addButton("mini." + o.key, o.label.section(" (", 0, 0), o.label, o.icon.isEmpty() ? QIcon() : miniIcon(o.icon));
        if (!o.icon.isEmpty()) b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setEnabled(o.enabled);
        const QString ok = o.key;
        connect(b, &QToolButton::clicked, this, [this, ok] { if (tm_) tm_->setOption(ok, QVariant()); });
      }
      // thin separator between option groups
      auto* sep = new QFrame(this);
      sep->setFrameShape(QFrame::VLine);
      sep->setStyleSheet("color:#c3cddc;");
      lay_->addWidget(sep);
      sep->show();
    }
    if (QLayoutItem* last = lay_->count() ? lay_->itemAt(lay_->count() - 1) : nullptr; last && qobject_cast<QFrame*>(last->widget()) &&
        !qobject_cast<QToolButton*>(last->widget())) {
      QWidget* w = last->widget();
      lay_->removeWidget(w);
      w->deleteLater();
    }
  }
  setVisible(lay_->count() > 0);
  adjustSize();
  place();
  raise();
}

void MiniToolbar::setAnchor(const QPoint& p) {
  anchor_ = p;
  ctxAnchor_ = false;
  if (fx_) fx_->setOpacity(1.0);
  place();
}

void MiniToolbar::showForContextMenu(const QPoint& p) {
  anchor_ = p;
  ctxAnchor_ = true;
  if (fx_) fx_->setOpacity(1.0);
  if (lay_->count() > 0) show();
  place();
  raise();
}

void MiniToolbar::cursorMoved(const QPoint& p) {
  if (sketch_ || !isVisible() || !fx_) return;
  const QRect r = geometry().adjusted(-30, -30, 30, 30);
  const double dx = std::max({0, r.left() - p.x(), p.x() - r.right()}), dy = std::max({0, r.top() - p.y(), p.y() - r.bottom()});
  const double d = std::hypot(dx, dy);
  fx_->setOpacity(std::clamp(1.0 - d / 220.0, 0.25, 1.0));
}

void MiniToolbar::place() {
  if (!viewer_) return;
  if (!hudConnected_) {   // SC23: follow the viewport HUD (its position changes while sketching)
    if (auto* hud = viewer_->findChild<ViewportHud*>("viewportHud", Qt::FindDirectChildrenOnly)) {
      hudConnected_ = true;
      connect(hud, &ViewportHud::layoutChanged, this, [this] { if (sketch_) place(); });
    }
  }
  adjustSize();
  QPoint at;
  if (sketch_) {
    // SC23: below the viewport HUD's tool guide slot is where the guides go; the sketch strip sits at its top
    at = QPoint(10, 72);
    if (auto* hud = viewer_->findChild<QWidget*>("viewportHud", Qt::FindDirectChildrenOnly))
      at = QPoint(hud->x() + 2, std::max(40, hud->y() + hud->height() + 36));
  } else if (anchor_.x() >= 0 && ctxAnchor_) at = anchor_ + QPoint(0, -height() - 6);
  else if (anchor_.x() >= 0) at = anchor_ + QPoint(30, 58);
  else at = QPoint(10, viewer_->height() - height() - 10);
  at.setX(std::clamp(at.x(), 4, std::max(4, viewer_->width() - width() - 60)));
  at.setY(std::clamp(at.y(), 4, std::max(4, viewer_->height() - height() - 4)));
  move(at);
}

bool MiniToolbar::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize) place();
  if (o == viewer_ && e->type() == QEvent::ChildAdded) QMetaObject::invokeMethod(this, [this] { raise(); }, Qt::QueuedConnection);
  return QFrame::eventFilter(o, e);
}

} // namespace pcd
