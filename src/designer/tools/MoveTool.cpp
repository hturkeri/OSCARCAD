// SC-UX (WP-B) — Move tool. See MoveTool.h.
#include "MoveTool.h"
#include "DimensionBox.h"
#include "PullMapping.h"
#include "SketchRegions.h"
#include "ViewportOverlay.h"
#include "pc/Expression.h"
#include "pc/ShapeViewer.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <cmath>

namespace pcd {

namespace {
QString fmt(double v) { return QString::fromStdString(pc::formatNumber(v)); }
const QColor kAxisCol[3] = {QColor(225, 60, 50), QColor(60, 175, 70), QColor(50, 110, 230)};
} // namespace

QList<ToolGuide> MoveTool::guides() const {
  return {{"select", "Seç", "select", "Seç: taşınacak düzlem, sketch ya da (Desen oluştur ile) özellik yüzü"},
          {"anchor", "Çapa", "anchor", "Çapa (Anchor) — 2. faz", false},
          {"upto", "Kadar", "upto", "Kadar (Up To) — 2. faz", false},
          {"ruler", "Cetvel", "ruler", "Cetvel (Ruler) — 2. faz", false}};
}

void MoveTool::setActiveGuide(const QString& g) {
  for (const auto& gd : guides())
    if (gd.key == g && gd.enabled) { guide_ = g; emit guidesChanged(); }
}

QList<ToolOption> MoveTool::options() const {
  ToolOption p;
  p.key = "pattern";
  p.label = "Desen oluştur (Create Patterns)";
  p.type = ToolOption::Type::Bool;
  p.value = pattern_;
  p.group = "Taşı";
  p.onMiniToolbar = true;
  ToolOption a;
  a.key = "axis";
  a.label = "Eksen";
  a.type = ToolOption::Type::Choice;
  a.choices = {"x|X", "y|Y", "z|Z"};
  a.value = QString(QChar("xyz"[std::clamp(axis_, 0, 2)]));
  a.group = "Taşı";
  a.enabled = kind_ == T::PatternSource;
  ToolOption g;
  g.key = "moveGrid";
  g.label = "Izgarayı taşı (Move grid)";
  g.type = ToolOption::Type::Info;
  g.value = QString("Sketch modunda Ctrl+→ / Ctrl+←");
  g.group = "Taşı";
  return {p, a, g};
}

void MoveTool::setOption(const QString& key, const QVariant& v) {
  if (key == "pattern") pattern_ = v.toBool();
  else if (key == "axis") axis_ = std::max(0, int(QString("xyz").indexOf(v.toString())));
  else return;
  retarget();
  emit optionsChanged();
}

void MoveTool::onActivate() {
  if (host_ && host_->selection())
    selConn_ = connect(host_->selection(), &SelectionModel::changed, this, [this] { cancelValueBox(); retarget(); });
  retarget();
}

void MoveTool::onDeactivate() {
  disconnect(selConn_);
  pressed_ = dragged_ = false;
  setOperation(OpState::Idle);   // SC23
}

void MoveTool::reportState() {
  if (!host_) { setOperation(OpState::Idle); return; }
  const bool empty = !host_->selection() || host_->selection()->isEmpty();
  if (kind_ == T::None) setOperation(empty ? OpState::AwaitingSelection : OpState::Invalid, empty ? QString() : prompt());
  else if (kind_ == T::Body) setOperation(OpState::Invalid, prompt());
  else if (pressed_ && dragged_) setOperation(OpState::Preview, QString(), formatDim(dragValue_), "mm");
  else setOperation(OpState::AwaitingInput, QString(), QString(), "mm");
}

void MoveTool::retarget() {
  struct Report { MoveTool* t; ~Report() { t->reportState(); } } report{this};   // SC23 (O-P0-1)
  kind_ = T::None;
  if (!host_) return;
  const auto& doc = host_->document();
  const auto& model = host_->modelResult();
  const QList<SelItem> sel = host_->selection() ? host_->selection()->items() : QList<SelItem>();
  if (sel.isEmpty()) {
    setPrompt("Taşı (Move): bir datum düzlemi, bir sketch ya da (Desen oluştur ile) bir özellik yüzü seçin.");
    repaintOverlay();
    return;
  }
  const SelItem it = normalize(sel.first());
  std::string sid;
  if (it.kind == SelItem::Kind::Datum) {
    auto dp = model.datumPlanes.find(it.name.toStdString());
    if (dp != model.datumPlanes.end()) {
      kind_ = T::Datum;
      id_ = it.name;
      anchor_ = dp->second.frame.Location();
      normal_ = dp->second.frame.Direction();
      axis_ = 0;
      setPrompt(QString("Taşı: %1 — normal okunu sürükleyin ya da ofset değişimini yazın (Enter).").arg(id_));
    }
  } else if (it.kind == SelItem::Kind::Sketch || ((it.kind == SelItem::Kind::SketchRegion || it.kind == SelItem::Kind::SketchCurve) &&
                                                 parseSketchItemName(it.name, &sid, nullptr, nullptr))) {
    if (it.kind == SelItem::Kind::Sketch) sid = it.name.toStdString();
    gp_Ax3 fr;
    if (sketchFrameOf(model, sid, fr)) {
      kind_ = T::Sketch;
      id_ = QString::fromStdString(sid);
      anchor_ = fr.Location();
      std::vector<TopoDS_Face> faces;
      gp_Ax3 f2;
      if (sketchRegionFaces(doc, model, sid, faces, f2) && !faces.empty()) {
        gp_Pnt c;
        gp_Dir n;
        if (faceCentroidNormal(faces.front(), c, n)) anchor_ = c;
      }
      normal_ = fr.Direction();
      setPrompt(QString("Taşı: %1 sketch düzlemi — normal okunu sürükleyin ya da ofset değişimini yazın.").arg(id_));
    }
  } else if (it.kind == SelItem::Kind::Face) {
    const QString fid = it.name.section('.', 0, 0);
    const pc::Feature* f = doc.feature(fid.toStdString());
    TopoDS_Face face;
    gp_Pnt c;
    gp_Dir n;
    if (pattern_ && f && (f->type == pc::FeatureType::Hole || f->type == pc::FeatureType::Extrude) && faceByName(doc, model, it.name, face) &&
        faceCentroidNormal(face, c, n)) {
      kind_ = T::PatternSource;
      id_ = fid;
      anchor_ = c;
      setPrompt(QString("Taşı + Desen: %1 kaynağı — bir eksen okunu sürükleyin ya da aralığı yazın (X/Y/Z: Seçenekler).").arg(fid));
    } else if (!pattern_) {
      kind_ = T::Body;
      id_ = QString::fromStdString(model.bodyOfFace(it.name.toStdString()));
      if (faceByName(doc, model, it.name, face) && faceCentroidNormal(face, c, n)) anchor_ = c;
      setPrompt("Gövde taşıma henüz desteklenmiyor (parametrik Transform özelliği planlandı). Desen için 'Desen oluştur'u açın.");
    } else {
      setPrompt("Taşı + Desen: kaynak bir Extrude ya da Delik yüzü olmalı.");
    }
  } else if (it.kind == SelItem::Kind::Body) {
    kind_ = T::Body;
    setPrompt("Gövde taşıma henüz desteklenmiyor (parametrik Transform özelliği planlandı).");
  } else {
    setPrompt("Taşı: bu seçim taşınamaz.");
  }
  repaintOverlay();
}

gp_Dir MoveTool::axisDir(int axis) const {
  if (kind_ == T::Datum || kind_ == T::Sketch) return normal_;
  return axis == 0 ? gp_Dir(1, 0, 0) : axis == 1 ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
}

QPointF MoveTool::arrowTip(int axis) const {
  QPointF b, u;
  double ppm = 0;
  if (!screenDir(anchor_, axisDir(axis), &b, &u, &ppm)) u = QPointF(0.7071, -0.7071);
  return b + u * 58.0;
}

int MoveTool::handleCount() const {
  if (kind_ == T::Datum || kind_ == T::Sketch) return 1;
  if (kind_ == T::PatternSource || kind_ == T::Body) return 3;
  return 0;
}

void MoveTool::paintOverlay(QPainter& p, const ViewportOverlay&) {
  if (!handleCount()) return;
  const QPointF b = toScreen(anchor_);
  if (kind_ == T::Datum || kind_ == T::Sketch) {
    ViewportOverlay::drawAxisArrow(p, b, arrowTip(0), QColor(60, 140, 240), pressed_);
  } else {
    for (int a = 0; a < 3; ++a)
      ViewportOverlay::drawAxisArrow(p, b, arrowTip(a), kind_ == T::Body ? kAxisCol[a].lighter(150) : kAxisCol[a], dragAxis_ == a || axis_ == a);
    // rotation arcs of the Move handle (display only in phase 1)
    p.save();
    p.setPen(QPen(QColor(120, 120, 140, 150), 2));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(b.x() - 34, b.y() - 34, 68, 68), 20 * 16, 50 * 16);
    p.drawArc(QRectF(b.x() - 34, b.y() - 34, 68, 68), 140 * 16, 50 * 16);
    p.restore();
  }
  ViewportOverlay::drawAnchor(p, b, false);
  if (pressed_ && dragged_) {
    const QPointF t = arrowTip(std::max(0, dragAxis_));
    ViewportOverlay::drawTag(p, t + QPointF(18, -12), QString("%1 mm").arg(formatDim(dragValue_)), QColor(40, 90, 170));
  }
}

bool MoveTool::viewerMousePress(QMouseEvent* e) {
  if (e->button() != Qt::LeftButton || !handleCount() || kind_ == T::Body) return false;
  const QPointF b = toScreen(anchor_);
  const int n = (kind_ == T::PatternSource) ? 3 : 1;
  for (int a = 0; a < n; ++a) {
    const QPointF t = arrowTip(a), ab = t - b, ap = e->position() - b;
    const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double s = l2 > 0 ? std::clamp((ap.x() * ab.x() + ap.y() * ab.y()) / l2, 0.0, 1.0) : 0;
    const QPointF c = b + ab * s;
    if (std::hypot(e->position().x() - c.x(), e->position().y() - c.y()) < 10) {
      pressed_ = true;
      dragged_ = false;
      dragAxis_ = a;
      pressPos_ = e->pos();
      return true;
    }
  }
  return false;
}

bool MoveTool::viewerMouseMove(QMouseEvent* e) {
  if (!pressed_) return false;
  if (!dragged_ && (e->pos() - pressPos_).manhattanLength() <= 3) return true;
  dragged_ = true;
  QPointF b, u;
  double ppm = 0;
  screenDir(anchor_, axisDir(dragAxis_), &b, &u, &ppm);
  const QPointF d = e->position() - QPointF(pressPos_);
  double v = ppm > 1e-3 ? (d.x() * u.x() + d.y() * u.y()) / ppm : 0;
  v = std::round(v * 2) / 2;
  dragValue_ = v;
  setPrompt(QString("Taşı: %1 mm").arg(formatDim(v)));
  repaintOverlay();
  return true;
}

bool MoveTool::viewerMouseRelease(QMouseEvent* e) {
  if (!pressed_ || e->button() != Qt::LeftButton) return false;
  pressed_ = false;
  if (dragged_ && std::fabs(dragValue_) > 1e-9) {
    if (kind_ == T::PatternSource) axis_ = dragAxis_;
    commitValue(fmt(dragValue_));
  } else if (!dragged_ && kind_ == T::PatternSource) {
    axis_ = dragAxis_;
    emit optionsChanged();
  }
  dragged_ = false;
  dragAxis_ = -1;
  repaintOverlay();
  return true;
}

bool MoveTool::wantsKey(const QKeyEvent* e) const {
  if (valueBoxOpen()) return true;
  return (kind_ == T::Datum || kind_ == T::Sketch || kind_ == T::PatternSource) && startsValue(e);
}

bool MoveTool::keyPress(QKeyEvent* e) {
  if (forwardToValueBox(e)) return true;
  if (!startsValue(e)) return false;
  const QString label = kind_ == T::PatternSource ? "Aralık" : "Mesafe";
  openValueBox(label, "mm", e->key() == Qt::Key_Space ? QString() : e->text(), 0, arrowTip(axis_).toPoint(),
               [this](const QString& t) { commitValue(t); });
  return true;
}

bool MoveTool::cancel() {
  if (cancelValueBox()) return true;
  if (pressed_) { pressed_ = dragged_ = false; repaintOverlay(); return true; }
  return false;
}

bool MoveTool::pick(const PickEvent& e) {
  if (e.button != Qt::LeftButton) return false;
  cancelValueBox();
  return selectFromPick(e, false);
}

bool MoveTool::commitValue(const QString& text) {
  if (!host_) return false;
  double d = 0;
  if (!host_->evaluateExpression(text, &d)) {
    setPrompt("Taşı: geçersiz ifade.");
    setOperation(OpState::Invalid, QString("Taşı: ‘%1’ bir sayı, parametre adı ya da ifade değil.").arg(text.trimmed()));   // SC23
    return false;
  }
  pc::Document cand = host_->document();
  const auto& model = host_->modelResult();
  QString undo, status;
  if (kind_ == T::Datum) {
    pc::Feature* f = cand.feature(id_.toStdString());
    if (!f) return false;
    const std::string cur = f->prop("offset", "0");
    double cv = 0;
    try { cv = pc::evalWith(cur, model.params); } catch (...) {}
    QString param, msg;
    const int b = pull::binding(cand, cur, "Ofset", &param, &msg);
    if (b == 2) { setPrompt(msg); host_->showPrompt(msg, 0); return false; }
    if (b == 1) {
      const bool ok = host_->changeParameter(param.toStdString(), pc::formatNumber(cv + d));
      if (ok) setPrompt(QString("%1 = %2 (parametre güncellendi)").arg(param, fmt(cv + d)));
      return ok;
    }
    f->props["offset"] = pc::formatNumber(cv + d);
    undo = QString("Taşı: %1.offset = %2").arg(id_, fmt(cv + d));
    status = QString("Taşı: %1 ofset = %2 mm").arg(id_, fmt(cv + d));
  } else if (kind_ == T::Sketch) {
    pc::Feature* f = cand.feature(id_.toStdString());
    if (!f || !f->sketch) return false;
    std::string& off = f->sketch->plane.offsetExpr;
    double cv = 0;
    try { cv = pc::evalWith(off.empty() ? "0" : off, model.params); } catch (...) {}
    QString param, msg;
    const int b = pull::binding(cand, off, "Düzlem ofseti", &param, &msg);
    if (b == 2) { setPrompt(msg); host_->showPrompt(msg, 0); return false; }
    if (b == 1) return host_->changeParameter(param.toStdString(), pc::formatNumber(cv + d));
    off = pc::formatNumber(cv + d);
    undo = QString("Taşı: %1 ofset = %2").arg(id_, fmt(cv + d));
    status = QString("Taşı: %1 sketch düzlemi ofseti = %2 mm").arg(id_, fmt(cv + d));
  } else if (kind_ == T::PatternSource) {
    if (std::fabs(d) < 1e-9) return false;
    pc::Feature p;
    p.id = cand.uniqueFeatureId(pc::FeatureType::Pattern);
    p.type = pc::FeatureType::Pattern;
    p.props["kind"] = "linear";
    p.props["source"] = id_.toStdString();
    p.props["count"] = "2";
    const bool num = pull::isPlainNumber(text.toStdString());
    p.props["spacing"] = num ? pc::formatNumber(std::fabs(d)) : text.trimmed().toStdString();
    const gp_Dir ax = axisDir(axis_);
    const double s = d < 0 ? -1 : 1;
    p.props["dirX"] = pc::formatNumber(ax.X() * s);
    p.props["dirY"] = pc::formatNumber(ax.Y() * s);
    p.props["dirZ"] = pc::formatNumber(ax.Z() * s);
    if (host_->insertFeature(p, QString("Taşı + Desen: %1").arg(QString::fromStdString(p.id)))) {
      setPrompt(QString("Taşı: %1 oluşturuldu (%2, 2 adet, aralık %3). Adet: Özellikler panelinde.")
                    .arg(QString::fromStdString(p.id), id_, QString::fromStdString(p.props["spacing"])));
      return true;
    }
    return false;
  } else {
    setPrompt("Gövde taşıma henüz desteklenmiyor (parametrik Transform özelliği planlandı).");
    return false;
  }
  const bool ok = host_->commitDocument(cand, undo);
  if (ok) { setPrompt(status); host_->showPrompt(status, 0); emitMomentary(OpState::Completed, status); }   // SC23
  return ok;
}

} // namespace pcd
