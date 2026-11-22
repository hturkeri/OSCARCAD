// SC-UX (WP-B) — shared tool helpers. See ToolBase.h.
#include "ToolBase.h"
#include "DimensionBox.h"
#include "SketchRegions.h"
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include "ViewportOverlay.h"
#include "pc/ShapeViewer.h"
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <cmath>

namespace pcd {

void ToolBase::activate(IDesignHost* host) {
  host_ = host;
  onActivate();
  repaintOverlay();
}

void ToolBase::deactivate() {
  cancelValueBox();
  onDeactivate();
  repaintOverlay();
  host_ = nullptr;
}

void ToolBase::setPrompt(const QString& s) {
  prompt_ = s;
  emit promptChanged(s);
}

OpStatus ToolBase::operation() const {
  OpStatus s = op_;
  s.toolId = id();
  return s;
}

void ToolBase::setOperation(OpState st, const QString& reason, const QString& value, const QString& unit) {
  OpStatus n;
  n.state = st;
  n.toolId = id();
  n.reason = reason;
  n.valueText = value;
  n.unit = unit;
  if (n == op_) return;
  op_ = n;
  emit operationChanged(op_);
}

void ToolBase::emitMomentary(OpState st, const QString& reason) {
  OpStatus m;
  m.state = st;
  m.toolId = id();
  m.reason = reason;
  emit operationChanged(m);
  emit operationChanged(op_);   // the lasting state again (ANALYSIS §4.2: momentary events)
}

void ToolBase::repaintOverlay() {
  if (overlay_) overlay_->update();
}

QPointF ToolBase::toScreen(const gp_Pnt& p) const {
  if (!host_ || !host_->viewer()) return QPointF();
  const double a[3] = {p.X(), p.Y(), p.Z()};
  return host_->viewer()->worldToScreen(a);
}

bool ToolBase::screenDir(const gp_Pnt& p, const gp_Dir& d, QPointF* base, QPointF* unit, double* ppm) const {
  if (!host_ || !host_->viewer()) return false;
  const QPointF b = toScreen(p);
  const QPointF t = toScreen(gp_Pnt(p.XYZ() + d.XYZ()));
  const QPointF v = t - b;
  const double l = std::hypot(v.x(), v.y());
  if (base) *base = b;
  if (ppm) *ppm = l;
  if (unit) *unit = l > 1e-9 ? v / l : QPointF(0, -1);
  return l > 1e-3;
}

SelItem ToolBase::normalize(const SelItem& it) {
  SelItem o = it;
  std::string sid;
  char type = 0;
  int k = 0;
  if (parseSketchItemName(it.name, &sid, &type, &k)) {
    o.kind = type == 'r' ? SelItem::Kind::SketchRegion : type == 'e' ? SelItem::Kind::SketchCurve : SelItem::Kind::SketchPoint;
  }
  return o;
}

bool ToolBase::selectFromPick(const PickEvent& e, bool loops) {
  if (!host_ || !host_->selection()) return false;
  SelectionModel* sel = host_->selection();
  const bool ctrl = e.modifiers & Qt::ControlModifier;
  if (e.isBackground()) {
    if (!ctrl) sel->clear();
    return true;
  }
  SelItem it = normalize(e.item);
  const auto& model = host_->modelResult();
  if (e.clickCount >= 3 && (it.kind == SelItem::Kind::Face || it.kind == SelItem::Kind::Edge)) {
    // triple click: the solid body
    QString face = it.name;
    if (it.kind == SelItem::Kind::Edge && face.startsWith("edge(")) face = face.mid(5, face.lastIndexOf(')') - 5).split('|').value(0);
    const std::string b = model.bodyOfFace(face.toStdString());
    if (!b.empty()) it = SelItem{SelItem::Kind::Body, QString::fromStdString(b)};
  } else if (e.clickCount == 2 && loops && it.kind == SelItem::Kind::Edge && it.name.startsWith("edge(")) {
    // double click: edge loop = every edge bounding the first adjacent face that contains this edge
    const QStringList faces = it.name.mid(5, it.name.lastIndexOf(')') - 5).split('|');
    const QString f = faces.value(0);
    auto fit = model.faces.find(f.toStdString());
    if (fit != model.faces.end()) {
      TopTools_IndexedMapOfShape em;
      TopExp::MapShapes(fit->second, TopAbs_EDGE, em);
      QList<SelItem> loop;
      for (int i = 1; i <= em.Extent(); ++i) {
        const std::string n = model.nameOf(TopoDS::Edge(em(i)));
        if (!n.empty()) loop << SelItem{SelItem::Kind::Edge, QString::fromStdString(n)};
      }
      if (!loop.isEmpty()) {
        if (ctrl) for (const auto& x : loop) sel->add(x);
        else sel->set(loop);
        return true;
      }
    }
  }
  if (!sel->accepts(it.kind)) return true;   // filtered out: consumed, nothing selected
  if (ctrl) sel->toggle(it);
  else sel->set({it});
  return true;
}

void ToolBase::openValueBox(const QString& label, const QString& unit, const QString& initial, double live, const QPoint& at,
                            std::function<void(const QString&)> onCommit) {
  if (!host_ || !host_->viewer()) return;
  if (!vbox_) {
    vbox_ = new DimensionBox(host_->viewer());
    vbox_->setShowGroupButton(false);
    vbox_->setValidator([this](const QString& t, double* v) { return host_ && host_->evaluateExpression(t, v); });
    connect(vbox_, &DimensionBox::committed, this, [this] {
      const QString t = vbox_->text(0);
      auto fn = vboxCommit_;
      cancelValueBox();
      if (fn) fn(t);
    });
    connect(vbox_, &DimensionBox::cancelled, this, [this] { cancelValueBox(); });
  }
  vboxCommit_ = std::move(onCommit);
  vbox_->setFields({{"v", label, unit}});
  QStringList words;
  for (const auto& p : host_->document().params.all()) words << QString::fromStdString(p.name);
  vbox_->setCompletions(words);
  vbox_->setLiveValue(0, live);
  vbox_->setCompactAnchor(at);
  vbox_->setVisible(true);
  vbox_->beginEdit(0, initial);
}

bool ToolBase::valueBoxOpen() const { return vbox_ && vbox_->isVisible(); }

bool ToolBase::cancelValueBox() {
  if (!vbox_ || !vbox_->isVisible()) return false;
  if (vbox_->isEditing()) vbox_->stopEdit();
  vbox_->setVisible(false);
  if (host_ && host_->viewer()) host_->viewer()->setFocus();
  return true;
}

bool ToolBase::forwardToValueBox(QKeyEvent* e) {
  if (!vbox_ || !vbox_->isEditing()) return false;
  static bool forwarding = false;
  if (forwarding) return true;
  if (QLineEdit* le = vbox_->lineEdit(vbox_->editingField())) {
    QKeyEvent copy(QEvent::KeyPress, e->key(), e->modifiers(), e->text());
    forwarding = true;
    QCoreApplication::sendEvent(le, &copy);
    forwarding = false;
    return true;
  }
  return false;
}

bool ToolBase::startsValue(const QKeyEvent* e) {
  if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) return false;
  if (e->key() == Qt::Key_Space) return true;
  const QString t = e->text();
  return !t.isEmpty() && (t[0].isDigit() || t[0] == '.' || t[0] == '-' || t[0] == '(');
}

} // namespace pcd
