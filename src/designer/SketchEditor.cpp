#include "SketchEditor.h"
#include "DimensionBox.h"
#include "ExprEdit.h"
#include "TrimLogic.h"
#include "pc/Expression.h"
#include "pc/ShapeViewer.h"
#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <set>
#include <cstdio>
#include <cstdlib>

using namespace pc;

namespace pcd {

namespace {
double distSeg(const QPointF& p, const QPointF& a, const QPointF& b) {
  const QPointF ab = b - a, ap = p - a;
  const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
  double t = l2 > 1e-12 ? (ap.x() * ab.x() + ap.y() * ab.y()) / l2 : 0;
  t = std::clamp(t, 0.0, 1.0);
  const QPointF c = a + ab * t;
  return std::hypot(c.x() - p.x(), c.y() - p.y());
}
double normAngle(double a) {  // to [0, 2pi)
  while (a < 0) a += 2 * M_PI;
  while (a >= 2 * M_PI) a -= 2 * M_PI;
  return a;
}
bool referencesAny(const std::vector<int>& refs, const std::set<int>& ids) {
  for (int r : refs) if (ids.count(r)) return true;
  return false;
}
const QColor kBlue(30, 95, 205), kGreen(20, 130, 50), kOrange(235, 130, 20), kRed(205, 40, 40);
} // namespace

SketchEditor::SketchEditor(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setMinimumSize(300, 240);
  sk_ = Sketch::create();
  updateTransform();
  box_ = new DimensionBox(this);
  box_->setValidator([this](const QString& t, double* v) {
    try {
      reloadParams();
      const double d = evalWith(t.trimmed().toStdString(), params_);
      if (!std::isfinite(d)) return false;
      if (v) *v = d;
      return true;
    } catch (...) { return false; }
  });
  connect(box_, &DimensionBox::committed, this, [this] { commitDrawing(true); });
  connect(box_, &DimensionBox::cancelled, this, [this] { box_->stopEdit(); updateLiveFields(); update(); });
  connect(box_, &DimensionBox::textEdited, this, [this](int) { update(); });
  connect(box_, &DimensionBox::lockedChanged, this, [this](int) { updateLiveFields(); update(); });
  connect(box_, &DimensionBox::createGroupRequested, this, [this] { createGroupFromBox(); });
}

// ------------------------------------------------------------------------- SC-UX: transform / overlay
double SketchEditor::pxPerMm() const { return std::max(1e-9, std::sqrt(std::fabs(T_.determinant()))); }

void SketchEditor::updateTransform() {
  edgeOn_ = false;
  if (viewer_ && haveFrame_) {
    T_ = viewer_->planeToScreen(fo_, fu_, fv_);
    const double ppm = viewer_->pixelsPerMm();
    edgeOn_ = std::fabs(T_.determinant()) < 1e-3 * ppm * ppm;
  } else {
    T_ = QTransform(scale_, 0, 0, -scale_, org_.x(), org_.y());
  }
  bool inv = false;
  Ti_ = T_.inverted(&inv);
  if (!inv) { Ti_ = QTransform(); edgeOn_ = true; }
}

void SketchEditor::attachToViewer(pc::viewer::ShapeViewer* v) {
  if (viewer_ == v) return;
  if (viewer_) { viewer_->removeEventFilter(this); disconnect(camConn_); }
  viewer_ = v;
  if (v) {
    setParent(v);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
    setMinimumSize(0, 0);
    setGeometry(v->rect());
    v->installEventFilter(this);
    camConn_ = connect(v, &pc::viewer::ShapeViewer::cameraChanged, this, [this] {
      updateTransform();
      updateLiveFields();
      update();
    });
    viewInit_ = true;
  }
  updateTransform();
  update();
}

void SketchEditor::setPlaneFrame(const double o[3], const double u[3], const double v[3]) {
  for (int i = 0; i < 3; ++i) { fo_[i] = o[i]; fu_[i] = u[i]; fv_[i] = v[i]; }
  haveFrame_ = true;
  updateTransform();
  syncViewerGrid();
  update();
}

bool SketchEditor::planeFrame(double o[3], double u[3], double v[3]) const {
  if (!haveFrame_) return false;
  for (int i = 0; i < 3; ++i) { o[i] = fo_[i]; u[i] = fu_[i]; v[i] = fv_[i]; }
  return true;
}

void SketchEditor::syncViewerGrid() {
  if (!viewer_ || !haveFrame_) return;
  viewer_->setSketchPlane(isVisible(), fo_, fu_, fv_);
}

void SketchEditor::planView() {
  if (!viewer_ || !haveFrame_) { fitView(); return; }
  viewer_->setPlanView(fo_, fu_, fv_, false);
  updateTransform();
  // a sketch with geometry is fitted; an empty one keeps the zoom (SpaceClaim keeps the scale of the model)
  bool any = false;
  for (const auto& e : sk_.entities) { (void)e; any = true; break; }
  if (any) fitView();
  update();
}

void SketchEditor::showEvent(QShowEvent*) {
  if (viewer_) { setGeometry(viewer_->rect()); raise(); }
  updateTransform();
  syncViewerGrid();
}
void SketchEditor::hideEvent(QHideEvent*) {
  if (viewer_) viewer_->setSketchPlane(false);
  box_->setVisible(false);
}

bool SketchEditor::eventFilter(QObject* o, QEvent* e) {
  if (o == viewer_ && e->type() == QEvent::Resize) {
    setGeometry(viewer_->rect());
    updateTransform();
    updateLiveFields();
  }
  return QWidget::eventFilter(o, e);
}

QString SketchEditor::sketchId() const {
  const QString t = title_.trimmed();
  const int sp = t.indexOf(' ');
  return sp > 0 ? t.left(sp) : (t.isEmpty() ? QString("Sketch") : t);
}

void SketchEditor::clearSelection() { sel_.clear(); update(); emit selectionChanged(); }
void SketchEditor::selectIds(const std::vector<int>& ids) { sel_ = ids; update(); emit selectionChanged(); }

bool SketchEditor::dimensionEditing() const { return box_ && box_->isEditing(); }

void SketchEditor::moveGrid(int steps) {
  double cur = 0;
  try { cur = evalWith(sk_.plane.offsetExpr, params_); } catch (...) {}
  const double g = gridStep();
  setPlaneOffset(QString::fromStdString(formatNumber(cur + steps * g)));
  emit planeOffsetChanged(QString::fromStdString(sk_.plane.offsetExpr));
  emit statusMessage(QString("Izgara taşındı: düzlem ofseti = %1 mm").arg(QString::fromStdString(sk_.plane.offsetExpr)));
}

void SketchEditor::reloadParams() {
  if (!work_) return;
  try { params_ = work_->evaluateParams(); } catch (...) {}
}

void SketchEditor::setSketch(const Sketch& s, const QString& title) {
  sk_ = s;
  title_ = title;
  modified_ = false;
  sel_.clear();
  undo_.clear();
  redo_.clear();
  drawStage_ = 0;
  lastPt_ = firstPt_ = centerPt_ = startPt_ = rectFirstPt_ = -1;
  tool_ = Tool::Select;
  box_->setVisible(false);
  box_->setFields({});
  emit toolChanged(int(tool_));
  reloadParams();
  resolve();
  viewInit_ = viewer_ != nullptr;
  update();
  emit selectionChanged();
}

void SketchEditor::setTool(Tool t) {
  tool_ = t;
  drawStage_ = 0;
  lastPt_ = firstPt_ = centerPt_ = startPt_ = rectFirstPt_ = -1;
  if (box_->isEditing()) box_->stopEdit();
  setupFieldsForStage();
  emit toolChanged(int(t));
  emit drawStageChanged(0);
  static const char* hints[] = {"Seç: tıkla / sürükle (Shift = çoklu seçim)",
                                "Nokta: konum için tıklayın (ya da U, V değerlerini yazın)",
                                "Çizgi: başlangıç noktasını tıklayın; uzunluk/açı yazılabilir (Tab), Enter = oluştur, Esc = zinciri bitir",
                                "Dikdörtgen: ilk köşeyi tıklayın; genişlik Tab yükseklik yazıp Enter",
                                "Çember: merkezi tıklayın; çapı yazıp Enter ya da ikinci noktayı tıklayın",
                                "Yay: merkez, başlangıç (yarıçap yazılabilir), bitiş (açı yazılabilir)",
                                "Kırp (Trim Away): silinecek parçaya tıklayın"};
  emit statusMessage(hints[int(t)]);
  update();
}

void SketchEditor::resolve() {
  solve_ = sketch::solve(sk_, params_);
  QString msg = QString::fromStdString(solve_.message);
  emit solved(solve_.dof, msg, solve_.ok());
  update();
}

void SketchEditor::ensureVisible() {
  // A dimension / parameter change can scale the geometry far beyond the view (a 300 px rectangle drawn at
  // 44 px/mm becomes 40 mm wide): re-fit so the user never "loses" the sketch.
  if (!viewInit_ || width() <= 0) return;
  const QRectF r = QRectF(rect()).adjusted(10, 10, -10, -10);
  for (const auto& p : sk_.points)
    if (!r.contains(toScreen(p.x, p.y))) { fitView(); return; }
}

void SketchEditor::setPlaneOffset(const QString& expr) {
  const std::string e = expr.trimmed().toStdString();
  if (e.empty() || e == sk_.plane.offsetExpr) return;
  pushUndo();
  sk_.plane.offsetExpr = e;
  modified_ = true;
  emit changed();
}

void SketchEditor::renameParameter(const std::string& from, const std::string& to) {
  auto rw = [&](std::string& s) {
    try { s = renameIdentifier(s, from, to); } catch (...) {}
  };
  rw(sk_.plane.offsetExpr);
  for (auto& c : sk_.constraints) if (!c.valueExpr.empty()) rw(c.valueExpr);
  for (auto* stack : {&undo_, &redo_})
    for (auto& st : *stack) {
      rw(st.sk.plane.offsetExpr);
      for (auto& c : st.sk.constraints) if (!c.valueExpr.empty()) rw(c.valueExpr);
      for (auto& p : st.params) {
        if (p.name == from) p.name = to;
        rw(p.expression);
      }
    }
  update();
}

bool SketchEditor::referencesParameter(const std::string& name) const {
  auto uses = [&](const std::string& e) {
    try {
      for (const auto& n : referencedNames(e)) if (n == name) return true;
    } catch (...) {}
    return false;
  };
  if (uses(sk_.plane.offsetExpr)) return true;
  for (const auto& c : sk_.constraints) if (!c.valueExpr.empty() && uses(c.valueExpr)) return true;
  return false;
}

void SketchEditor::pushUndo() {
  undo_.push_back({sk_, {}});
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
}
void SketchEditor::undo() {
  if (undo_.empty()) return;
  UndoStep st = undo_.back();
  undo_.pop_back();
  redo_.push_back({sk_, st.params});
  sk_ = st.sk;
  sel_.clear();
  drawStage_ = 0;
  afterEdit();
  emit selectionChanged();
  if (!st.params.empty()) {
    QStringList names;
    for (const auto& p : st.params) names << QString::fromStdString(p.name);
    emit parametersUndone(names);
  }
}
void SketchEditor::redo() {
  if (redo_.empty()) return;
  UndoStep st = redo_.back();
  redo_.pop_back();
  if (!st.params.empty()) emit parametersRedone(st.params);   // the parameters first: the sketch references them
  undo_.push_back({sk_, st.params});
  sk_ = st.sk;
  sel_.clear();
  reloadParams();
  afterEdit();
  emit selectionChanged();
}

void SketchEditor::applyExternalEdit(const Sketch& s, const std::vector<Parameter>& createdParams, const QString& message) {
  undo_.push_back({sk_, createdParams});
  if (undo_.size() > 100) undo_.erase(undo_.begin());
  redo_.clear();
  sk_ = s;
  reloadParams();
  afterEdit();
  ensureVisible();
  emit paramsChanged();
  emit selectionChanged();
  if (!message.isEmpty()) emit statusMessage(message);
}

bool SketchEditor::selectionAnchor(QPointF* px) const {
  if (sel_.empty()) return false;
  const int id = sel_.back();
  QPointF p;
  if (const SkPoint* q = sk_.point(id)) {
    p = toScreen(q->x, q->y);
  } else if (const SkEntity* e = sk_.entity(id)) {
    if (e->type == SkEntityType::Line && e->points.size() >= 2) {
      p = (ptScreen(e->points[0]) + ptScreen(e->points[1])) / 2;
    } else if (e->type == SkEntityType::Circle && !e->points.empty()) {
      const SkPoint* c = sk_.point(e->points[0]);
      if (!c) return false;
      p = toScreen(c->x + e->radius * std::cos(M_PI / 4), c->y + e->radius * std::sin(M_PI / 4));
    } else if (e->type == SkEntityType::Arc && e->points.size() >= 3) {
      const SkPoint *c = sk_.point(e->points[0]), *s = sk_.point(e->points[1]), *en = sk_.point(e->points[2]);
      if (!c || !s || !en) return false;
      const double r = std::hypot(s->x - c->x, s->y - c->y);
      const double a0 = std::atan2(s->y - c->y, s->x - c->x);
      const double sweep = normAngle(std::atan2(en->y - c->y, en->x - c->x) - a0);
      p = toScreen(c->x + r * std::cos(a0 + sweep / 2), c->y + r * std::sin(a0 + sweep / 2));
    } else {
      return false;
    }
  } else {
    const QRectF r = dimensionLabelRect(id);
    if (r.isEmpty()) return false;
    p = r.center();
  }
  if (px) *px = p;
  return true;
}

QRectF SketchEditor::dimensionLabelRect(int constraintId) const {
  for (const auto& [r, cid] : labelHits_) if (cid == constraintId) return r;
  return QRectF();
}

void SketchEditor::afterEdit(bool doSolve) {
  modified_ = true;
  if (doSolve) resolve();
  update();
  emit changed();
}

void SketchEditor::fitView() {
  if (viewer_ && haveFrame_) {
    double x0 = -20, x1 = 20, y0 = -20, y1 = 20;
    for (const auto& p : sk_.points) { x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y); }
    for (const auto& e : sk_.entities) {
      if (e.type != SkEntityType::Circle) continue;
      if (const SkPoint* c = sk_.point(e.points[0])) {
        x0 = std::min(x0, c->x - e.radius); x1 = std::max(x1, c->x + e.radius);
        y0 = std::min(y0, c->y - e.radius); y1 = std::max(y1, c->y + e.radius);
      }
    }
    double mn[3], mx[3];
    for (int i = 0; i < 3; ++i) { mn[i] = 1e300; mx[i] = -1e300; }
    for (int k = 0; k < 4; ++k) {
      const double a = (k & 1) ? x1 : x0, b = (k & 2) ? y1 : y0;
      for (int i = 0; i < 3; ++i) {
        const double w = fo_[i] + a * fu_[i] + b * fv_[i];
        mn[i] = std::min(mn[i], w); mx[i] = std::max(mx[i], w);
      }
    }
    viewer_->fitToBox(mn, mx);
    updateTransform();
    update();
    return;
  }
  double x0 = -10, x1 = 10, y0 = -10, y1 = 10;
  bool any = false;
  for (const auto& p : sk_.points) {
    if (!any) { x0 = x1 = p.x; y0 = y1 = p.y; any = true; }
    x0 = std::min(x0, p.x); x1 = std::max(x1, p.x); y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
  }
  for (const auto& e : sk_.entities) {
    if (e.type != SkEntityType::Circle) continue;
    if (const SkPoint* c = sk_.point(e.points[0])) {
      x0 = std::min(x0, c->x - e.radius); x1 = std::max(x1, c->x + e.radius);
      y0 = std::min(y0, c->y - e.radius); y1 = std::max(y1, c->y + e.radius);
    }
  }
  x0 = std::min(x0, -5.0); y0 = std::min(y0, -5.0);
  const double w = std::max(x1 - x0, 10.0), h = std::max(y1 - y0, 10.0);
  scale_ = std::min((width() - 160) / w, (height() - 160) / h);
  scale_ = std::clamp(scale_, 0.05, 2000.0);
  const double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  org_ = QPointF(width() / 2.0 - cx * scale_, height() / 2.0 + cy * scale_);
  viewInit_ = true;
  updateTransform();
  update();
}

void SketchEditor::resizeEvent(QResizeEvent*) { updateTransform(); }

QPointF SketchEditor::ptScreen(int id) const {
  const SkPoint* p = sk_.point(id);
  return p ? toScreen(p->x, p->y) : QPointF();
}

double SketchEditor::gridStep() const {
  if (gridOverride_ > 0) return gridOverride_;
  const double raw = 25.0 / pxPerMm();
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  const double n = raw / mag;
  return (n < 1.5 ? 1 : n < 3.5 ? 2 : n < 7.5 ? 5 : 10) * mag;
}

SketchEditor::Snap SketchEditor::snap(const QPointF& scr, int refPoint) const {
  Snap s;
  double best = 11.0;
  for (const auto& p : sk_.points) {
    const QPointF q = toScreen(p.x, p.y);
    const double d = std::hypot(q.x() - scr.x(), q.y() - scr.y());
    if (d < best) { best = d; s.pointId = p.id; s.p = QPointF(p.x, p.y); s.kind = 1; }
  }
  if (s.kind == 1) return s;
  QPointF p = toSketch(scr);
  const double g = gridStep();
  bool hAl = false, vAl = false;
  const bool noSnap = QGuiApplication::keyboardModifiers() & Qt::ShiftModifier;   // SC-UX: Shift = no snapping
  if (refPoint >= 0 && autoHV_ && !noSnap) {
    if (const SkPoint* r = sk_.point(refPoint)) {
      if (std::fabs(p.y() - r->y) * pxPerMm() < 8) { p.setY(r->y); hAl = true; }
      if (std::fabs(p.x() - r->x) * pxPerMm() < 8) { p.setX(r->x); vAl = true; }
    }
  }
  const double sub = g / 2;
  if (snapGrid_ && !noSnap) {
    if (!vAl) p.setX(std::round(p.x() / sub) * sub);
    if (!hAl) p.setY(std::round(p.y() / sub) * sub);
  }
  s.p = p;
  s.kind = (hAl && !vAl) ? 3 : (vAl && !hAl) ? 4 : 2;
  return s;
}

int SketchEditor::ensurePoint(const Snap& s) {
  if (s.pointId >= 0) return s.pointId;
  return sk_.addPoint(s.p.x(), s.p.y());
}

bool SketchEditor::isSelected(int id) const { return std::find(sel_.begin(), sel_.end(), id) != sel_.end(); }

SketchEditor::Hit SketchEditor::hitTest(const QPointF& scr) const {
  Hit h;
  for (const auto& [r, cid] : labelHits_)
    if (r.contains(scr)) { h.id = cid; h.kind = 3; return h; }
  double best = 9.0;
  for (const auto& p : sk_.points) {
    const double d = std::hypot(toScreen(p.x, p.y).x() - scr.x(), toScreen(p.x, p.y).y() - scr.y());
    if (d < best) { best = d; h.id = p.id; h.kind = 1; }
  }
  if (h.kind) return h;
  best = 6.5;
  for (const auto& e : sk_.entities) {
    double d = 1e9;
    if (e.type == SkEntityType::Line && e.points.size() >= 2) {
      d = distSeg(scr, ptScreen(e.points[0]), ptScreen(e.points[1]));
    } else if (e.type == SkEntityType::Circle && !e.points.empty()) {
      if (const SkPoint* c = sk_.point(e.points[0])) {
        const QPointF q = toSketch(scr);
        d = std::fabs(std::hypot(q.x() - c->x, q.y() - c->y) - e.radius) * pxPerMm();
      }
    } else if (e.type == SkEntityType::Arc && e.points.size() >= 3) {
      const SkPoint *c = sk_.point(e.points[0]), *s = sk_.point(e.points[1]), *en = sk_.point(e.points[2]);
      if (c && s && en) {
        const double r = std::hypot(s->x - c->x, s->y - c->y);
        const QPointF q = toSketch(scr);
        const double a = normAngle(std::atan2(q.y() - c->y, q.x() - c->x));
        const double a0 = normAngle(std::atan2(s->y - c->y, s->x - c->x));
        const double a1 = normAngle(std::atan2(en->y - c->y, en->x - c->x));
        const double sweep = normAngle(a1 - a0), rel = normAngle(a - a0);
        if (rel <= sweep) d = std::fabs(std::hypot(q.x() - c->x, q.y() - c->y) - r) * pxPerMm();
        else d = std::min(std::hypot(scr.x() - ptScreen(e.points[1]).x(), scr.y() - ptScreen(e.points[1]).y()),
                          std::hypot(scr.x() - ptScreen(e.points[2]).x(), scr.y() - ptScreen(e.points[2]).y()));
      }
    }
    if (d < best) { best = d; h.id = e.id; h.kind = 2; }
  }
  return h;
}

// ------------------------------------------------------------------------- painting
QString SketchEditor::dimLabel(const SkConstraint& c) const {
  const QString ex = QString::fromStdString(c.valueExpr);
  bool num = false;
  ex.toDouble(&num);
  if (num) return ex + (c.type == SkConstraintType::Angle ? "°" : "");
  try {
    const double v = evalWith(c.valueExpr, params_);
    return QString("%1 = %2").arg(ex).arg(v, 0, 'g', 5);
  } catch (...) { return ex + " (?)"; }
}

void SketchEditor::paintEvent(QPaintEvent*) {
  if (!viewInit_ && !viewer_) fitView();
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  labelHits_.clear();
  const bool overlay = viewer_ != nullptr;
  // sampled curves (exact under any affine plane->screen transform, i.e. also in oblique 3D views)
  auto arcPoly = [&](double cx, double cy, double r, double a0, double sweep) {
    QPolygonF poly;
    const int n = std::max(8, int(std::ceil(std::fabs(sweep) / (2 * M_PI) * 96)));
    for (int i = 0; i <= n; ++i) {
      const double a = a0 + sweep * i / n;
      poly << toScreen(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    return poly;
  };
  if (!overlay) p.fillRect(rect(), QColor(250, 251, 253));
  if (!overlay) {
  // grid
  const double g = gridStep();
  const QPointF tl = toSketch(QPointF(0, 0)), br = toSketch(QPointF(width(), height()));
  const double u0 = std::floor(tl.x() / g) * g, u1 = br.x();
  const double v0 = std::floor(br.y() / g) * g, v1 = tl.y();
  for (double u = u0; u <= u1; u += g) {
    const bool major = std::fabs(std::fmod(std::fabs(std::round(u / g)), 5.0)) < 1e-9;
    p.setPen(QPen(major ? QColor(205, 212, 224) : QColor(228, 232, 240), 1));
    p.drawLine(toScreen(u, v0), toScreen(u, v1));
  }
  for (double v = v0; v <= v1; v += g) {
    const bool major = std::fabs(std::fmod(std::fabs(std::round(v / g)), 5.0)) < 1e-9;
    p.setPen(QPen(major ? QColor(205, 212, 224) : QColor(228, 232, 240), 1));
    p.drawLine(toScreen(u0, v), toScreen(u1, v));
  }
  p.setPen(QPen(QColor(215, 90, 90), 1.2));
  p.drawLine(QPointF(0, org_.y()), QPointF(width(), org_.y()));
  p.setPen(QPen(QColor(80, 170, 90), 1.2));
  p.drawLine(QPointF(org_.x(), 0), QPointF(org_.x(), height()));
  }

  const bool full = solve_.ok() && solve_.dof == 0;
  const QColor geo = !solve_.ok() && solve_.dof < 0 && solve_.status != sketch::SolveStatus::Ok ? kBlue : (full ? kGreen : kBlue);
  const QColor geoCol = (solve_.status == sketch::SolveStatus::Failed || solve_.status == sketch::SolveStatus::Inconsistent ||
                         solve_.status == sketch::SolveStatus::OverConstrained) && solve_.dof >= 0 ? kRed : geo;

  // entities
  for (const auto& e : sk_.entities) {
    const bool sel = isSelected(e.id), hov = hover_ == e.id;
    QPen pen(sel ? kOrange : hov ? QColor(255, 170, 60) : e.construction ? QColor(120, 130, 150) : (full ? QColor(20, 30, 20) : geoCol),
             sel || hov ? 3.0 : 2.0);
    if (e.construction) pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    if (e.type == SkEntityType::Line && e.points.size() >= 2) {
      p.drawLine(ptScreen(e.points[0]), ptScreen(e.points[1]));
    } else if (e.type == SkEntityType::Circle && !e.points.empty()) {
      if (const SkPoint* c = sk_.point(e.points[0])) p.drawPolyline(arcPoly(c->x, c->y, e.radius, 0, 2 * M_PI));
    } else if (e.type == SkEntityType::Arc && e.points.size() >= 3) {
      const SkPoint *c = sk_.point(e.points[0]), *s = sk_.point(e.points[1]), *en = sk_.point(e.points[2]);
      if (c && s && en) {
        const double r = std::hypot(s->x - c->x, s->y - c->y);
        const double a0 = std::atan2(s->y - c->y, s->x - c->x);
        double sweep = normAngle(std::atan2(en->y - c->y, en->x - c->x) - a0);
        p.drawPolyline(arcPoly(c->x, c->y, r, a0, sweep));
      }
    }
  }

  // constraint glyphs (non-dimensional) and dimensions
  std::map<std::pair<int, int>, int> stack;
  auto glyphAt = [&](QPointF a, const QString& txt, int cid) {
    const auto key = std::make_pair(int(a.x() / 12), int(a.y() / 12));
    const int n = stack[key]++;
    a += QPointF(10 + n * 16, -10);
    const QRectF r(a.x() - 7, a.y() - 7, 15, 15);
    const bool conflict = std::find(solve_.conflicting.begin(), solve_.conflicting.end(), cid) != solve_.conflicting.end();
    const bool selc = isSelected(cid);
    p.setPen(QPen(selc ? kOrange : conflict ? kRed : QColor(70, 110, 190), 1));
    p.setBrush(QColor(235, 242, 255));
    p.drawRoundedRect(r, 3, 3);
    p.setFont(QFont(p.font().family(), 8, QFont::Bold));
    p.setPen(conflict ? kRed : QColor(40, 70, 150));
    p.drawText(r, Qt::AlignCenter, txt);
    labelHits_.push_back({r, cid});
  };
  auto entMid = [&](int id) -> QPointF {
    const SkEntity* e = sk_.entity(id);
    if (!e) return QPointF();
    if (e->type == SkEntityType::Line) return (ptScreen(e->points[0]) + ptScreen(e->points[1])) / 2;
    if (e->type == SkEntityType::Circle) {
      const SkPoint* c = sk_.point(e->points[0]);
      return c ? toScreen(c->x - 0.7 * e->radius, c->y + 0.7 * e->radius) : QPointF();
    }
    const SkPoint *c = sk_.point(e->points[0]), *s = sk_.point(e->points[1]), *en = sk_.point(e->points[2]);
    if (!c || !s || !en) return QPointF();
    const double a0 = std::atan2(s->y - c->y, s->x - c->x);
    const double sw = normAngle(std::atan2(en->y - c->y, en->x - c->x) - a0), r = std::hypot(s->x - c->x, s->y - c->y);
    return toScreen(c->x + r * std::cos(a0 + sw / 2), c->y + r * std::sin(a0 + sw / 2));
  };
  for (const auto& c : sk_.constraints) {
    if (isDimensional(c.type)) continue;
    QString t;
    QPointF a;
    switch (c.type) {
      case SkConstraintType::Coincident: {
        a = ptScreen(c.refs[0]);
        p.setPen(QPen(kOrange, 1.5)); p.setBrush(Qt::NoBrush); p.drawEllipse(a, 6.5, 6.5);
        continue;
      }
      case SkConstraintType::Horizontal: t = "H"; a = entMid(c.refs[0]); break;
      case SkConstraintType::Vertical: t = "V"; a = entMid(c.refs[0]); break;
      case SkConstraintType::HorizontalPts: t = "H"; a = (ptScreen(c.refs[0]) + ptScreen(c.refs[1])) / 2; break;
      case SkConstraintType::VerticalPts: t = "V"; a = (ptScreen(c.refs[0]) + ptScreen(c.refs[1])) / 2; break;
      case SkConstraintType::Parallel: t = "∥"; a = entMid(c.refs[0]); glyphAt(entMid(c.refs[1]), t, c.id); break;
      case SkConstraintType::Perpendicular: t = "⊥"; a = entMid(c.refs[0]); glyphAt(entMid(c.refs[1]), t, c.id); break;
      case SkConstraintType::Tangent: t = "T"; a = entMid(c.refs[0]); break;
      case SkConstraintType::Equal: t = "="; a = entMid(c.refs[0]); glyphAt(entMid(c.refs[1]), t, c.id); break;
      case SkConstraintType::Fixed: t = "F"; a = ptScreen(c.refs[0]); break;
      case SkConstraintType::PointOnEntity: t = "∈"; a = ptScreen(c.refs[0]); break;
      case SkConstraintType::Midpoint: t = "M"; a = ptScreen(c.refs[0]); break;
      case SkConstraintType::Symmetric: t = "S"; a = (ptScreen(c.refs[0]) + ptScreen(c.refs[1])) / 2; break;
      case SkConstraintType::Concentric: t = "◎"; a = ptScreen(sk_.entity(c.refs[0]) ? sk_.entity(c.refs[0])->points[0] : 0); break;
      default: continue;
    }
    glyphAt(a, t, c.id);
  }

  // dimensions
  QFont dimFont(p.font().family(), 9);
  for (const auto& c : sk_.constraints) {
    if (!isDimensional(c.type)) continue;
    const bool conflict = std::find(solve_.conflicting.begin(), solve_.conflicting.end(), c.id) != solve_.conflicting.end();
    const QColor col = isSelected(c.id) ? kOrange : conflict ? kRed : QColor(0, 110, 170);
    p.setPen(QPen(col, 1));
    p.setBrush(Qt::NoBrush);
    QPointF labelPos;
    auto arrow = [&](QPointF tip, QPointF dir) {
      const QPointF n(-dir.y(), dir.x());
      QPolygonF poly({tip, tip + dir * 9 + n * 3, tip + dir * 9 - n * 3});
      p.setBrush(col); p.drawPolygon(poly); p.setBrush(Qt::NoBrush);
    };
    auto linear = [&](QPointF A, QPointF B, QPointF n, double off) {
      const QPointF A2 = A + n * off, B2 = B + n * off;
      p.drawLine(A, A + n * (off + 4));
      p.drawLine(B, B + n * (off + 4));
      p.drawLine(A2, B2);
      const QPointF d = B2 - A2;
      const double l = std::hypot(d.x(), d.y());
      if (l > 22) { const QPointF u = d / l; arrow(A2, u); arrow(B2, -u); }
      labelPos = (A2 + B2) / 2;
    };
    switch (c.type) {
      case SkConstraintType::Distance: case SkConstraintType::Length: {
        QPointF A, B;
        if (c.type == SkConstraintType::Length) {
          const SkEntity* e = sk_.entity(c.refs[0]);
          if (!e) continue;
          A = ptScreen(e->points[0]); B = ptScreen(e->points[1]);
        } else { A = ptScreen(c.refs[0]); B = ptScreen(c.refs[1]); }
        QPointF d = B - A;
        double l = std::hypot(d.x(), d.y());
        if (l < 1e-6) { d = QPointF(1, 0); l = 1; }
        const QPointF n(-d.y() / l, d.x() / l);
        linear(A, B, n * (n.y() > 0 || (n.y() == 0 && n.x() < 0) ? -1 : 1), 24);
        break;
      }
      case SkConstraintType::DistanceX: {
        const QPointF A = ptScreen(c.refs[0]), B = ptScreen(c.refs[1]);
        const double y = std::max(A.y(), B.y()) + 28;
        p.drawLine(A, QPointF(A.x(), y + 4)); p.drawLine(B, QPointF(B.x(), y + 4));
        p.drawLine(QPointF(A.x(), y), QPointF(B.x(), y));
        if (std::fabs(B.x() - A.x()) > 22) { const double s = B.x() > A.x() ? 1 : -1; arrow(QPointF(A.x(), y), QPointF(s, 0)); arrow(QPointF(B.x(), y), QPointF(-s, 0)); }
        labelPos = QPointF((A.x() + B.x()) / 2, y);
        break;
      }
      case SkConstraintType::DistanceY: {
        const QPointF A = ptScreen(c.refs[0]), B = ptScreen(c.refs[1]);
        const double x = std::max(A.x(), B.x()) + 28;
        p.drawLine(A, QPointF(x + 4, A.y())); p.drawLine(B, QPointF(x + 4, B.y()));
        p.drawLine(QPointF(x, A.y()), QPointF(x, B.y()));
        if (std::fabs(B.y() - A.y()) > 22) { const double s = B.y() > A.y() ? 1 : -1; arrow(QPointF(x, A.y()), QPointF(0, s)); arrow(QPointF(x, B.y()), QPointF(0, -s)); }
        labelPos = QPointF(x, (A.y() + B.y()) / 2);
        break;
      }
      case SkConstraintType::PointLineDistance: {
        const SkEntity* e = sk_.entity(c.refs[1]);
        if (!e) continue;
        const QPointF P = ptScreen(c.refs[0]), A = ptScreen(e->points[0]), B = ptScreen(e->points[1]);
        const QPointF ab = B - A;
        const double l2 = ab.x() * ab.x() + ab.y() * ab.y();
        const double t = l2 > 1e-9 ? ((P - A).x() * ab.x() + (P - A).y() * ab.y()) / l2 : 0;
        const QPointF F = A + ab * t;
        p.drawLine(P, F);
        labelPos = (P + F) / 2;
        break;
      }
      case SkConstraintType::Radius: case SkConstraintType::Diameter: {
        const SkEntity* e = sk_.entity(c.refs[0]);
        if (!e) continue;
        const SkPoint* cc = sk_.point(e->points[0]);
        if (!cc) continue;
        const QPointF C = toScreen(cc->x, cc->y);
        double r = e->radius;
        if (e->type == SkEntityType::Arc) {
          const SkPoint* s = sk_.point(e->points[1]);
          if (s) r = std::hypot(s->x - cc->x, s->y - cc->y);
        }
        const QPointF E = toScreen(cc->x + r * std::cos(0.7), cc->y + r * std::sin(0.7));
        const QPointF S = c.type == SkConstraintType::Diameter ? toScreen(cc->x - r * std::cos(0.7), cc->y - r * std::sin(0.7)) : C;
        QPointF u = E - C;
        const double ul = std::hypot(u.x(), u.y());
        u = ul > 1e-9 ? u / ul : QPointF(1, 0);
        p.drawLine(S, E + u * 18);
        arrow(E, -u);
        if (c.type == SkConstraintType::Diameter) arrow(S, u);
        labelPos = E + u * 30;
        break;
      }
      case SkConstraintType::Angle: {
        const SkEntity *l1 = sk_.entity(c.refs[0]), *l2 = sk_.entity(c.refs[1]);
        if (!l1 || !l2) continue;
        const SkPoint *a0 = sk_.point(l1->points[0]), *a1 = sk_.point(l1->points[1]), *b0 = sk_.point(l2->points[0]), *b1 = sk_.point(l2->points[1]);
        if (!a0 || !a1 || !b0 || !b1) continue;
        const double d1x = a1->x - a0->x, d1y = a1->y - a0->y, d2x = b1->x - b0->x, d2y = b1->y - b0->y;
        const double den = d1x * d2y - d1y * d2x;
        double vx = a0->x, vy = a0->y;
        if (std::fabs(den) > 1e-9) {
          const double t = ((b0->x - a0->x) * d2y - (b0->y - a0->y) * d2x) / den;
          vx = a0->x + t * d1x; vy = a0->y + t * d1y;
        }
        const double an1 = std::atan2(d1y, d1x);
        double sw = std::atan2(d2y, d2x) - an1;
        while (sw > M_PI) sw -= 2 * M_PI;
        while (sw <= -M_PI) sw += 2 * M_PI;
        const QPointF V = toScreen(vx, vy);
        const double R = 38;
        p.drawArc(QRectF(V.x() - R, V.y() - R, 2 * R, 2 * R), int(an1 * 180 / M_PI * 16), int(sw * 180 / M_PI * 16));
        labelPos = V + QPointF(std::cos(an1 + sw / 2), -std::sin(an1 + sw / 2)) * (R + 18);
        break;
      }
      default: continue;
    }
    p.setFont(dimFont);
    const QString txt = dimLabel(c);
    QRectF tr = QFontMetricsF(dimFont).boundingRect(txt);
    tr.moveCenter(labelPos);
    tr.adjust(-4, -2, 4, 2);
    p.setBrush(QColor(255, 255, 235));
    p.setPen(QPen(col, 1));
    p.drawRoundedRect(tr, 3, 3);
    p.drawText(tr, Qt::AlignCenter, txt);
    labelHits_.push_back({tr, c.id});
  }

  // points
  for (const auto& pt : sk_.points) {
    const QPointF q = toScreen(pt.x, pt.y);
    const bool sel = isSelected(pt.id), hov = hover_ == pt.id;
    if (pt.id == 0) {
      p.setPen(QPen(kOrange, 1.8)); p.drawLine(q - QPointF(6, 0), q + QPointF(6, 0)); p.drawLine(q - QPointF(0, 6), q + QPointF(0, 6));
      p.setBrush(Qt::NoBrush); p.drawEllipse(q, 3.5, 3.5);
      continue;
    }
    p.setPen(QPen(sel ? kOrange : hov ? QColor(255, 170, 60) : QColor(20, 30, 20), 1));
    p.setBrush(sel ? kOrange : hov ? QColor(255, 200, 110) : (full ? kGreen : kBlue));
    p.drawEllipse(q, sel || hov ? 4.5 : 3.2, sel || hov ? 4.5 : 3.2);
  }

  // rubber band preview (SC-UX: locked dimension fields applied)
  if (tool_ != Tool::Select && tool_ != Tool::Trim && !edgeOn_) {
    const Snap s = curSnap_;
    const QPointF ck = constrainedCursor(s.p);
    const QPointF sp = toScreen(ck.x(), ck.y());
    p.setPen(QPen(QColor(40, 110, 220), 1.6, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    if (tool_ == Tool::Line && drawStage_ == 1 && lastPt_ >= 0) p.drawLine(ptScreen(lastPt_), sp);
    if (tool_ == Tool::Rect && drawStage_ == 1) {
      const double ax = rectStart_.x(), ay = rectStart_.y();
      double x0 = ax, y0 = ay, x1 = ck.x(), y1 = ck.y();
      if (fromCenter_) { x0 = 2 * ax - ck.x(); y0 = 2 * ay - ck.y(); }
      QPolygonF poly;
      poly << toScreen(x0, y0) << toScreen(x1, y0) << toScreen(x1, y1) << toScreen(x0, y1) << toScreen(x0, y0);
      p.setBrush(QColor(80, 150, 240, 28));
      p.drawPolygon(poly);
      p.setBrush(Qt::NoBrush);
    }
    if (tool_ == Tool::Circle && drawStage_ == 1 && centerPt_ >= 0) {
      if (const SkPoint* c = sk_.point(centerPt_)) {
        const double r = std::hypot(ck.x() - c->x, ck.y() - c->y);
        p.drawPolyline(arcPoly(c->x, c->y, r, 0, 2 * M_PI));
        p.setPen(QPen(QColor(40, 110, 220), 1, Qt::DotLine));
        p.drawLine(ptScreen(centerPt_), sp);
      }
    }
    if (tool_ == Tool::Arc && drawStage_ >= 1 && centerPt_ >= 0) {
      const QPointF c = ptScreen(centerPt_);
      if (drawStage_ == 1) {
        p.drawLine(c, sp);
      } else if (startPt_ >= 0) {
        const SkPoint *cc = sk_.point(centerPt_), *s0 = sk_.point(startPt_);
        if (cc && s0) {
          const double r = std::hypot(s0->x - cc->x, s0->y - cc->y);
          const double a0 = std::atan2(s0->y - cc->y, s0->x - cc->x);
          const double sw = normAngle(std::atan2(ck.y() - cc->y, ck.x() - cc->x) - a0);
          p.drawPolyline(arcPoly(cc->x, cc->y, r, a0, sw));
        }
      }
    }
    // snap marker
    p.setPen(QPen(s.kind == 1 ? kOrange : QColor(60, 140, 60), 1.5));
    p.drawRect(QRectF(sp.x() - 5, sp.y() - 5, 10, 10));
    if (s.kind == 3 || s.kind == 4) {
      p.setFont(QFont(p.font().family(), 8));
      p.drawText(sp + QPointF(8, -8), s.kind == 3 ? "yatay" : "dikey");
    }
  }

  // header / status text
  if (overlay) {
    // compact translucent badge (the 3D view stays visible around it)
    QString st = solve_.dof >= 0 ? QString("DOF: %1").arg(solve_.dof) : QString("DOF: ?");
    if (solve_.dof == 0 && solve_.ok()) st += " - tam tanımlı";
    const QString head = "Sketch: " + title_ + "   " + st;
    QFont f(p.font().family(), 9, QFont::Bold);
    p.setFont(f);
    QRectF r = QFontMetricsF(f).boundingRect(head).adjusted(-8, -4, 8, 4);
    // SC23 merge: bottom-right corner; the top-left belongs to the viewport HUD (tool · selection, messages, guides)
    r.moveBottomRight(QPointF(width() - 10, height() - 10));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 200));
    p.drawRoundedRect(r, 4, 4);
    p.setPen(solve_.ok() ? (full ? kGreen : kBlue) : kRed);
    p.drawText(r, Qt::AlignCenter, head);
    if (edgeOn_) {
      p.setPen(kRed);
      p.drawText(QRectF(10, r.top() - 22, width() - 20, 18), Qt::AlignRight | Qt::AlignVCenter,
                 "Düzlem kenardan görünüyor: Plan görünümü için V.");
    }
    return;
  }
  p.setFont(QFont(p.font().family(), 10, QFont::Bold));
  p.setPen(QColor(50, 70, 110));
  p.drawText(QPointF(12, 22), "Sketch: " + title_);
  p.setFont(QFont(p.font().family(), 10));
  QString st = solve_.dof >= 0 ? QString("Serbestlik derecesi (DOF): %1").arg(solve_.dof) : QString("DOF: ?");
  if (solve_.dof == 0 && solve_.ok()) st += "  - tam tanımlı";
  p.setPen(solve_.ok() ? (full ? kGreen : kBlue) : kRed);
  p.drawText(QPointF(12, 42), st);
  if (!solve_.message.empty() && (!solve_.ok() || solve_.status != sketch::SolveStatus::Ok)) {
    p.setPen(solve_.ok() ? QColor(90, 90, 90) : kRed);
    p.drawText(QPointF(12, 62), QString::fromStdString(solve_.message));
  }
}

// ------------------------------------------------------------------------- SC-UX: on-the-fly dimensions
namespace {
bool isDrawTool(SketchEditor::Tool t) {
  return t == SketchEditor::Tool::Line || t == SketchEditor::Tool::Rect || t == SketchEditor::Tool::Circle ||
         t == SketchEditor::Tool::Arc || t == SketchEditor::Tool::Point;
}
double sgn(double v) { return v < 0 ? -1.0 : 1.0; }
} // namespace

void SketchEditor::setupFieldsForStage() {
  QList<DimensionBox::FieldSpec> f;
  switch (tool_) {
    case Tool::Line: if (drawStage_ == 1) f = {{"L", "Uzunluk", "mm"}, {"A", "Açı", "°"}}; break;
    case Tool::Rect: if (drawStage_ == 1) f = {{"W", "Genişlik", "mm"}, {"H", "Yükseklik", "mm"}}; break;
    case Tool::Circle: if (drawStage_ == 1) f = {{"D", "Çap", "mm"}}; break;
    case Tool::Arc:
      if (drawStage_ == 1) f = {{"R", "Yarıçap", "mm"}};
      else if (drawStage_ == 2) f = {{"A", "Açı", "°"}};
      break;
    case Tool::Point: f = {{"U", "U", "mm"}, {"V", "V", "mm"}}; break;
    default: break;
  }
  if (box_->isEditing()) box_->stopEdit();
  box_->setFields(f);
  QStringList words;
  if (work_) for (const auto& prm : work_->params.all()) words << QString::fromStdString(prm.name);
  box_->setCompletions(words);
  box_->setStep(gridStep());
  box_->setVisible(!f.isEmpty() && tool_ != Tool::Point && drawStage_ >= 1);
  updateLiveFields();
}

// value of a field that constrains the drawing (locked, or being typed)
static bool fieldConstrains(const DimensionBox* b, int i, double* v) {
  if (i >= b->fieldCount()) return false;
  if (!b->isLocked(i) && !(b->editingField() == i && b->isTyped(i))) return false;
  return b->value(i, v);
}

QPointF SketchEditor::constrainedCursor(QPointF c) const {
  double a = 0, b = 0;
  switch (tool_) {
    case Tool::Line:
      if (drawStage_ == 1 && lastPt_ >= 0) {
        if (const SkPoint* s0 = sk_.point(lastPt_)) {
          const double dx = c.x() - s0->x, dy = c.y() - s0->y;
          double L = std::hypot(dx, dy), A = std::atan2(dy, dx);
          if (fieldConstrains(box_, 0, &a)) L = std::fabs(a);
          if (fieldConstrains(box_, 1, &b)) A = b * M_PI / 180.0;
          return QPointF(s0->x + L * std::cos(A), s0->y + L * std::sin(A));
        }
      }
      break;
    case Tool::Rect:
      if (drawStage_ == 1) {
        const double k = fromCenter_ ? 0.5 : 1.0;
        if (fieldConstrains(box_, 0, &a)) c.setX(rectStart_.x() + sgn(c.x() - rectStart_.x()) * std::fabs(a) * k);
        if (fieldConstrains(box_, 1, &b)) c.setY(rectStart_.y() + sgn(c.y() - rectStart_.y()) * std::fabs(b) * k);
      }
      break;
    case Tool::Circle:
      if (drawStage_ == 1 && centerPt_ >= 0 && fieldConstrains(box_, 0, &a)) {
        if (const SkPoint* ce = sk_.point(centerPt_)) {
          double dx = c.x() - ce->x, dy = c.y() - ce->y;
          const double l = std::hypot(dx, dy);
          if (l < 1e-12) { dx = 1; dy = 0; } else { dx /= l; dy /= l; }
          return QPointF(ce->x + 0.5 * std::fabs(a) * dx, ce->y + 0.5 * std::fabs(a) * dy);
        }
      }
      break;
    case Tool::Arc:
      if (centerPt_ >= 0) {
        const SkPoint* ce = sk_.point(centerPt_);
        if (!ce) break;
        if (drawStage_ == 1 && fieldConstrains(box_, 0, &a)) {
          double dx = c.x() - ce->x, dy = c.y() - ce->y;
          const double l = std::hypot(dx, dy);
          if (l < 1e-12) { dx = 1; dy = 0; } else { dx /= l; dy /= l; }
          return QPointF(ce->x + std::fabs(a) * dx, ce->y + std::fabs(a) * dy);
        }
        if (drawStage_ == 2 && startPt_ >= 0 && fieldConstrains(box_, 0, &a)) {
          if (const SkPoint* s0 = sk_.point(startPt_)) {
            const double r = std::hypot(s0->x - ce->x, s0->y - ce->y);
            const double a0 = std::atan2(s0->y - ce->y, s0->x - ce->x) + a * M_PI / 180.0;
            return QPointF(ce->x + r * std::cos(a0), ce->y + r * std::sin(a0));
          }
        }
      }
      break;
    case Tool::Point:
      if (fieldConstrains(box_, 0, &a)) c.setX(a);
      if (fieldConstrains(box_, 1, &b)) c.setY(b);
      break;
    default: break;
  }
  return c;
}

QSizeF SketchEditor::previewSize() const {
  const QPointF ck = constrainedCursor(curSnap_.p);
  switch (tool_) {
    case Tool::Rect:
      if (drawStage_ == 1) {
        const double k = fromCenter_ ? 2.0 : 1.0;
        return QSizeF(std::fabs(ck.x() - rectStart_.x()) * k, std::fabs(ck.y() - rectStart_.y()) * k);
      }
      break;
    case Tool::Line:
      if (drawStage_ == 1 && lastPt_ >= 0)
        if (const SkPoint* s0 = sk_.point(lastPt_)) {
          const double dx = ck.x() - s0->x, dy = ck.y() - s0->y;
          return QSizeF(std::hypot(dx, dy), std::atan2(dy, dx) * 180.0 / M_PI);
        }
      break;
    case Tool::Circle:
      if (drawStage_ == 1 && centerPt_ >= 0)
        if (const SkPoint* ce = sk_.point(centerPt_)) {
          const double d = 2 * std::hypot(ck.x() - ce->x, ck.y() - ce->y);
          return QSizeF(d, d);
        }
      break;
    default: break;
  }
  return QSizeF();
}

void SketchEditor::updateLiveFields() {
  if (!box_ || box_->fieldCount() == 0) return;
  const QPointF ck = constrainedCursor(curSnap_.p);
  const QPointF cur = toScreen(ck.x(), ck.y());
  auto away = [](QPointF at, QPointF from, double dist) {
    QPointF d = at - from;
    const double l = std::hypot(d.x(), d.y());
    d = l > 1e-6 ? d / l : QPointF(0, 1);
    return at + d * dist;
  };
  switch (tool_) {
    case Tool::Line:
      if (lastPt_ >= 0)
        if (const SkPoint* s0 = sk_.point(lastPt_)) {
          const double dx = ck.x() - s0->x, dy = ck.y() - s0->y;
          box_->setLiveValue(0, std::hypot(dx, dy));
          box_->setLiveValue(1, std::atan2(dy, dx) * 180.0 / M_PI);
          const QPointF A = toScreen(s0->x, s0->y), mid = (A + cur) / 2;
          QPointF n(-(cur - A).y(), (cur - A).x());
          const double l = std::hypot(n.x(), n.y());
          n = l > 1e-6 ? n / l : QPointF(0, -1);
          if (n.y() > 0) n = -n;
          box_->setAnchor(0, mid + n * 26);
          box_->setAnchor(1, A + QPointF(-46, 22));
        }
      break;
    case Tool::Rect: {
      const double k = fromCenter_ ? 2.0 : 1.0;
      double x0 = rectStart_.x(), y0 = rectStart_.y();
      if (fromCenter_) { x0 = 2 * rectStart_.x() - ck.x(); y0 = 2 * rectStart_.y() - ck.y(); }
      box_->setLiveValue(0, std::fabs(ck.x() - rectStart_.x()) * k);
      box_->setLiveValue(1, std::fabs(ck.y() - rectStart_.y()) * k);
      const double xl = std::min(x0, ck.x()), xr = std::max(x0, ck.x()), yb = std::min(y0, ck.y()), yt = std::max(y0, ck.y());
      const QPointF ctr = toScreen((xl + xr) / 2, (yb + yt) / 2);
      const QWidget* fw = box_->fieldWidget(0);
      const QWidget* fh = box_->fieldWidget(1);
      box_->setAnchor(0, away(toScreen((xl + xr) / 2, yb), ctr, (fw ? fw->height() / 2.0 : 10) + 12));
      box_->setAnchor(1, away(toScreen(xl, (yb + yt) / 2), ctr, (fh ? fh->width() / 2.0 : 60) + 12));
      break;
    }
    case Tool::Circle:
      if (centerPt_ >= 0)
        if (const SkPoint* ce = sk_.point(centerPt_)) {
          const double r = std::hypot(ck.x() - ce->x, ck.y() - ce->y);
          box_->setLiveValue(0, 2 * r);
          box_->setAnchor(0, away(cur, toScreen(ce->x, ce->y), 40));
        }
      break;
    case Tool::Arc:
      if (centerPt_ >= 0)
        if (const SkPoint* ce = sk_.point(centerPt_)) {
          if (drawStage_ == 1) {
            box_->setLiveValue(0, std::hypot(ck.x() - ce->x, ck.y() - ce->y));
            box_->setAnchor(0, (toScreen(ce->x, ce->y) + cur) / 2 + QPointF(0, -22));
          } else if (startPt_ >= 0) {
            if (const SkPoint* s0 = sk_.point(startPt_)) {
              const double a0 = std::atan2(s0->y - ce->y, s0->x - ce->x);
              box_->setLiveValue(0, normAngle(std::atan2(ck.y() - ce->y, ck.x() - ce->x) - a0) * 180.0 / M_PI);
              box_->setAnchor(0, away(cur, toScreen(ce->x, ce->y), 36));
            }
          }
        }
      break;
    case Tool::Point:
      box_->setLiveValue(0, ck.x());
      box_->setLiveValue(1, ck.y());
      box_->setCompactAnchor(cur.toPoint());
      break;
    default: break;
  }
}

void SketchEditor::applyTypedDims(const std::vector<std::pair<SkConstraintType, std::pair<std::vector<int>, std::string>>>& dims) {
  if (dims.empty() || !keepDims_) return;
  reloadParams();
  const Sketch before = sk_;
  for (const auto& d : dims) sk_.addConstraint(d.first, d.second.first, d.second.second);
  const auto r = sketch::solve(sk_, params_);
  if (!r.ok() || r.status == sketch::SolveStatus::OverConstrained) {
    // keep the typed geometry, drop the driving dimensions that do not fit
    sk_ = before;
    emit statusMessage("Yazılan ölçüler kısıt olarak eklenemedi: " + QString::fromStdString(r.message));
    return;
  }
  solve_ = r;
  emit solved(r.dof, QString::fromStdString(r.message), true);
}

bool SketchEditor::commitDrawing(bool fromKeyboard) {
  if (edgeOn_) return false;
  using Dims = std::vector<std::pair<SkConstraintType, std::pair<std::vector<int>, std::string>>>;
  Dims dims;
  const Snap s = curSnap_;
  const QPointF ck = constrainedCursor(s.p);
  auto lockedExpr = [&](int i) -> std::string {
    double v;
    return fieldConstrains(box_, i, &v) ? box_->text(i).toStdString() : std::string();
  };
  bool anyLock = false;
  for (int i = 0; i < box_->fieldCount(); ++i) { double v; if (fieldConstrains(box_, i, &v)) anyLock = true; }
  const int snappedId = anyLock ? -1 : s.pointId;
  bool done = false;
  switch (tool_) {
    case Tool::Point: {
      if (!anyLock && fromKeyboard) return false;
      pushUndo();
      const int pid = sk_.addPoint(ck.x(), ck.y());
      const std::string U = lockedExpr(0), V = lockedExpr(1);
      if (!U.empty()) dims.push_back({SkConstraintType::DistanceX, {{0, pid}, U}});
      if (!V.empty()) dims.push_back({SkConstraintType::DistanceY, {{0, pid}, V}});
      resolve();
      applyTypedDims(dims);
      afterEdit(false);
      done = true;
      break;
    }
    case Tool::Line: {
      if (drawStage_ != 1 || lastPt_ < 0) return false;
      pushUndo();
      const int endPt = snappedId >= 0 ? snappedId : sk_.addPoint(ck.x(), ck.y());
      if (endPt == lastPt_) { undo_.pop_back(); return false; }
      const int ln = sk_.addLine(lastPt_, endPt, construction_);
      const Sketch before = sk_;
      const std::string L = lockedExpr(0), A = lockedExpr(1);
      int hv = 0;
      if (!A.empty()) {
        double ang = 0;
        box_->value(1, &ang);
        const double m = std::fmod(std::fmod(ang, 180.0) + 180.0, 180.0);
        if (m < 1e-9 || std::fabs(m - 180.0) < 1e-9) hv = 3;
        else if (std::fabs(m - 90.0) < 1e-9) hv = 4;
      } else if (L.empty()) {
        hv = (s.kind == 3 || s.kind == 4) ? s.kind : 0;
      } else if (const SkPoint *a0 = sk_.point(lastPt_), *a1 = sk_.point(endPt); a0 && a1) {
        // length typed, direction from the cursor: keep the automatic H/V of the snap
        hv = std::fabs(a1->y - a0->y) < 1e-9 ? 3 : std::fabs(a1->x - a0->x) < 1e-9 ? 4 : 0;
      }
      if (hv == 3) sk_.addConstraint(SkConstraintType::Horizontal, {ln});
      if (hv == 4) sk_.addConstraint(SkConstraintType::Vertical, {ln});
      resolve();
      if (!solve_.ok() && hv) { sk_ = before; resolve(); }
      if (!L.empty()) dims.push_back({SkConstraintType::Length, {{ln}, L}});
      applyTypedDims(dims);
      lastPt_ = endPt;
      modified_ = true;
      emit changed();
      if ((snappedId >= 0 && endPt == firstPt_) || !chain_) finishDrawing();
      else setupFieldsForStage();
      update();
      return true;
    }
    case Tool::Rect: {
      if (drawStage_ != 1) return false;
      double x0 = rectStart_.x(), y0 = rectStart_.y(), x1 = ck.x(), y1 = ck.y();
      if (fromCenter_) { x0 = 2 * rectStart_.x() - ck.x(); y0 = 2 * rectStart_.y() - ck.y(); }
      if (std::fabs(x1 - x0) < 1e-6 || std::fabs(y1 - y0) < 1e-6) return false;
      pushUndo();
      const double xl = std::min(x0, x1), xr = std::max(x0, x1), yb = std::min(y0, y1), yt = std::max(y0, y1);
      auto corner = [&](double x, double y) {
        if (!fromCenter_ && rectFirstPt_ >= 0 && std::fabs(x - x0) < 1e-9 && std::fabs(y - y0) < 1e-9) return rectFirstPt_;
        if (snappedId >= 0 && std::fabs(x - x1) < 1e-9 && std::fabs(y - y1) < 1e-9) return snappedId;
        return sk_.addPoint(x, y);
      };
      const int a = corner(xl, yb), b = corner(xr, yb), c = corner(xr, yt), d = corner(xl, yt);
      const int bottom = sk_.addLine(a, b, construction_), right = sk_.addLine(b, c, construction_);
      const int top = sk_.addLine(c, d, construction_), left = sk_.addLine(d, a, construction_);
      sk_.addConstraint(SkConstraintType::Horizontal, {bottom});
      sk_.addConstraint(SkConstraintType::Horizontal, {top});
      sk_.addConstraint(SkConstraintType::Vertical, {right});
      sk_.addConstraint(SkConstraintType::Vertical, {left});
      resolve();
      const std::string W = lockedExpr(0), H = lockedExpr(1);
      if (!W.empty()) dims.push_back({SkConstraintType::Length, {{bottom}, W}});
      if (!H.empty()) dims.push_back({SkConstraintType::Length, {{left}, H}});
      applyTypedDims(dims);
      afterEdit(false);
      done = true;
      break;
    }
    case Tool::Circle: {
      if (drawStage_ != 1 || centerPt_ < 0) return false;
      const SkPoint* ce = sk_.point(centerPt_);
      const double r = ce ? std::hypot(ck.x() - ce->x, ck.y() - ce->y) : 0;
      if (r <= 1e-6) return false;
      const int cid = sk_.addCircle(centerPt_, r, construction_);
      resolve();
      const std::string D = lockedExpr(0);
      if (!D.empty()) dims.push_back({SkConstraintType::Diameter, {{cid}, D}});
      applyTypedDims(dims);
      afterEdit(false);
      centerPt_ = -1;
      done = true;
      break;
    }
    case Tool::Arc: {
      const SkPoint* ce = centerPt_ >= 0 ? sk_.point(centerPt_) : nullptr;
      if (!ce) return false;
      if (drawStage_ == 1) {
        if (std::hypot(ck.x() - ce->x, ck.y() - ce->y) <= 1e-6) return false;
        arcRadiusExpr_ = lockedExpr(0);
        startPt_ = snappedId >= 0 ? snappedId : sk_.addPoint(ck.x(), ck.y());
        drawStage_ = 2;
        setupFieldsForStage();
        emit drawStageChanged(2);
        update();
        return true;
      }
      if (drawStage_ == 2 && startPt_ >= 0) {
        const SkPoint* s0 = sk_.point(startPt_);
        if (!s0) return false;
        const double r = std::hypot(s0->x - ce->x, s0->y - ce->y);
        double dx = ck.x() - ce->x, dy = ck.y() - ce->y;
        const double l = std::hypot(dx, dy);
        if (l < 1e-9) return false;
        dx /= l; dy /= l;
        const int ep = sk_.addPoint(ce->x + r * dx, ce->y + r * dy);
        const int aid = sk_.addArc(centerPt_, startPt_, ep, construction_);
        resolve();
        if (!arcRadiusExpr_.empty()) dims.push_back({SkConstraintType::Radius, {{aid}, arcRadiusExpr_}});
        applyTypedDims(dims);
        afterEdit(false);
        arcRadiusExpr_.clear();
        centerPt_ = startPt_ = -1;
        done = true;
      }
      break;
    }
    default: break;
  }
  if (done) {
    if (tool_ != Tool::Point) finishDrawing();
    else setupFieldsForStage();
    if (!dims.empty()) emit paramsChanged();
    ensureVisible();
  }
  return done;
}

bool SketchEditor::wantsKey(const QKeyEvent* e) const {
  if (box_->isEditing()) return true;
  const int k = e->key();
  const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
  if ((m & Qt::ControlModifier) && (k == Qt::Key_Right || k == Qt::Key_Left)) return true;
  if (!isDrawTool(tool_)) return false;
  const bool live = drawStage_ >= 1 || tool_ == Tool::Point;
  if (!live) return false;
  if (m & (Qt::ControlModifier | Qt::AltModifier)) return k == Qt::Key_G && box_->fieldCount() > 0;
  const QString t = e->text();
  if (!t.isEmpty() && (t[0].isDigit() || t[0] == '.' || t[0] == '-' || t[0] == '(' || t[0] == ',')) return true;
  if (k == Qt::Key_Space) return true;
  if ((k == Qt::Key_Tab || k == Qt::Key_Backtab) && box_->fieldCount() > 0) return true;
  if ((k == Qt::Key_Return || k == Qt::Key_Enter) && drawStage_ >= 1) return true;
  if ((m & Qt::ShiftModifier) && (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Left || k == Qt::Key_Right)) return box_->fieldCount() > 0;
  return false;
}

bool SketchEditor::handleKey(QKeyEvent* e) {
  if (e->type() != QEvent::KeyPress) return false;
  if (box_->isEditing()) {
    // keys typed while the focus is still on the overlay go to the field being edited
    static bool forwarding = false;   // a key the line edit ignores propagates back to us: do not loop
    if (forwarding) return true;
    if (QLineEdit* le = box_->lineEdit(box_->editingField())) {
      QKeyEvent copy(QEvent::KeyPress, e->key(), e->modifiers(), e->text(), e->isAutoRepeat(), ushort(e->count()));
      forwarding = true;
      QCoreApplication::sendEvent(le, &copy);
      forwarding = false;
      return true;
    }
  }
  if (!wantsKey(e)) return false;
  const int k = e->key();
  const Qt::KeyboardModifiers m = e->modifiers() & ~Qt::KeypadModifier;
  if ((m & Qt::ControlModifier) && (k == Qt::Key_Right || k == Qt::Key_Left)) { moveGrid(k == Qt::Key_Right ? 1 : -1); return true; }
  if ((m & Qt::ControlModifier) && k == Qt::Key_G) return createGroupFromBox();
  if (box_->fieldCount() == 0 && !(k == Qt::Key_Return || k == Qt::Key_Enter)) return false;
  if (k == Qt::Key_Return || k == Qt::Key_Enter) { commitDrawing(true); return true; }
  if (k == Qt::Key_Tab || k == Qt::Key_Backtab) {
    box_->beginEdit(k == Qt::Key_Backtab ? box_->fieldCount() - 1 : 0, QString());
    return true;
  }
  if (k == Qt::Key_Space) { box_->beginEdit(0, QString()); return true; }
  if ((m & Qt::ShiftModifier) && (k == Qt::Key_Up || k == Qt::Key_Down || k == Qt::Key_Left || k == Qt::Key_Right)) {
    const int fld = (k == Qt::Key_Up || k == Qt::Key_Down) ? 0 : std::min(1, box_->fieldCount() - 1);
    box_->beginEdit(fld, QString());
    if (QLineEdit* le = box_->lineEdit(fld)) {
      QKeyEvent copy(QEvent::KeyPress, k, m, QString());
      QCoreApplication::sendEvent(le, &copy);
    }
    return true;
  }
  QString t = e->text();
  if (t == ",") t = ".";
  box_->beginEdit(0, t);
  return true;
}

bool SketchEditor::cancelStep() {
  if (box_->isEditing()) { box_->stopEdit(); updateLiveFields(); update(); return true; }
  if (drawStage_ > 0) { finishDrawing(); return true; }
  return false;
}

bool SketchEditor::createGroupFromBox() {
  if (box_->fieldCount() == 0 || (!box_->isVisible() && !box_->isEditing())) return false;
  const int i = box_->isEditing() ? box_->editingField() : 0;
  QString val = box_->text(i);
  double v = 0;
  if (!box_->value(i, &v)) val = formatDim(box_->liveValue(i));
  const QString key = box_->fieldKey(i);
  const QString suggested = sketchId() + "_" + key;
  const pc::ParamKind kind = key == "A" ? pc::ParamKind::Angle : pc::ParamKind::Length;
  QString name;
  if (paramRequest_) {
    name = paramRequest_(suggested, kind, val);
  } else if (work_) {
    NewParameterDialog dlg(work_, suggested, kind, val, this);
    if (dlg.exec() == QDialog::Accepted) {
      const pc::Parameter prm = dlg.parameter();
      if (!work_->params.find(prm.name)) work_->params.add(prm);
      commitNewParameter(this, prm);
      name = QString::fromStdString(prm.name);
    }
  }
  if (name.isEmpty()) return true;
  reloadParams();
  QStringList words;
  if (work_) for (const auto& prm : work_->params.all()) words << QString::fromStdString(prm.name);
  box_->setCompletions(words);
  box_->beginEdit(i, QString());
  box_->setText(i, name, true);
  emit paramsChanged();
  emit statusMessage(QString("Ölçü '%1' parametresine bağlandı (Enter ile oluşturun).").arg(name));
  update();
  return true;
}

bool SketchEditor::event(QEvent* e) {
  if (e->type() == QEvent::ShortcutOverride) {
    auto* k = static_cast<QKeyEvent*>(e);
    if (wantsKey(k)) { e->accept(); return true; }
  }
  if (e->type() == QEvent::KeyPress) {
    auto* k = static_cast<QKeyEvent*>(e);
    if ((k->key() == Qt::Key_Tab || k->key() == Qt::Key_Backtab) && handleKey(k)) return true;
  }
  return QWidget::event(e);
}

// ------------------------------------------------------------------------- mouse / keys
void SketchEditor::mousePressEvent(QMouseEvent* e) {
  if (viewer_ && e->button() == Qt::MiddleButton) { QCoreApplication::sendEvent(viewer_, e); return; }
  setFocus();
  lastMouse_ = e->pos();
  pressPos_ = e->pos();
  if (!viewer_ && (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier)))) {
    panning_ = true;
    return;
  }
  if (e->button() == Qt::RightButton) {
    if (tool_ != Tool::Select) {
      if (drawStage_ > 0) finishDrawing();
      else setTool(Tool::Select);
      update();
      return;
    }
    // Seç: the item under the cursor joins / becomes the selection, then the context menu (Parametre olarak ekle)
    const Hit h = hitTest(e->position());
    if (h.kind && !isSelected(h.id)) {
      sel_.clear();
      sel_.push_back(h.id);
      update();
      emit selectionChanged();
    }
    emit contextMenuRequested(e->globalPosition().toPoint());
    return;
  }
  if (e->button() != Qt::LeftButton) return;
  const QPointF scr = e->position();

  if (tool_ == Tool::Select) {
    const Hit h = hitTest(scr);
    pressHit_ = h;
    if (h.kind == 0) {
      if (!(e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) sel_.clear();
    } else {
      const bool multi = e->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
      if (multi) {
        auto it = std::find(sel_.begin(), sel_.end(), h.id);
        if (it != sel_.end()) sel_.erase(it); else sel_.push_back(h.id);
      } else if (!isSelected(h.id)) {
        sel_.clear();
        sel_.push_back(h.id);
      }
      // prepare drag
      dragPts_.clear(); dragOffs_.clear(); dragCircle_ = -1;
      const QPointF m = toSketch(scr);
      auto addDrag = [&](int pid) {
        if (pid == 0) return;
        if (std::find(dragPts_.begin(), dragPts_.end(), pid) != dragPts_.end()) return;
        const SkPoint* q = sk_.point(pid);
        if (!q) return;
        dragPts_.push_back(pid);
        dragOffs_.push_back(QPointF(q->x - m.x(), q->y - m.y()));
      };
      if (h.kind == 1) addDrag(h.id);
      else if (h.kind == 2) {
        const SkEntity* en = sk_.entity(h.id);
        if (en && en->type == SkEntityType::Line) { addDrag(en->points[0]); addDrag(en->points[1]); }
        else if (en && en->type == SkEntityType::Circle) dragCircle_ = en->id;
        else if (en && en->type == SkEntityType::Arc) { for (int pid : en->points) addDrag(pid); }
      }
      dragBackup_ = sk_;
    }
    emit statusMessage(sel_.empty() ? QString() : QString("%1 öğe seçili").arg(sel_.size()));
    update();
    emit selectionChanged();
    return;
  }
  if (edgeOn_) {
    emit statusMessage("Düzlem kenardan görünüyor: Plan görünümü için V.");
    return;
  }
  if (tool_ == Tool::Trim) {
    const Hit h = hitTest(scr);
    if (h.kind != 2) { emit statusMessage("Kırp: silinecek parçanın üzerine tıklayın."); return; }
    Sketch out;
    QString msg;
    const QPointF q = toSketch(scr);
    if (trimSketchEntity(sk_, h.id, q.x(), q.y(), out, &msg)) {
      pushUndo();
      sk_ = out;
      sel_.clear();
      afterEdit();
      emit statusMessage(msg.isEmpty() ? QString("Parça kırpıldı.") : msg);
    } else {
      emit statusMessage("Kırpılamadı: " + msg);
    }
    return;
  }

  // drawing tools
  curSnap_ = snap(scr, tool_ == Tool::Line && drawStage_ == 1 ? lastPt_ : -1);
  const Snap s = curSnap_;
  switch (tool_) {
    case Tool::Point:
      if (box_->isEditing()) { commitDrawing(false); break; }
      pushUndo();
      sk_.addPoint(s.p.x(), s.p.y());
      afterEdit();
      break;
    case Tool::Line:
      if (drawStage_ == 0) {
        pushUndo();
        lastPt_ = firstPt_ = ensurePoint(s);
        drawStage_ = 1;
        setupFieldsForStage();
        emit drawStageChanged(1);
        update();
      } else {
        commitDrawing(false);
      }
      break;
    case Tool::Rect:
      if (drawStage_ == 0) {
        rectStart_ = s.p;
        rectFirstPt_ = s.pointId;
        drawStage_ = 1;
        setupFieldsForStage();
        emit drawStageChanged(1);
        update();
      } else {
        commitDrawing(false);
      }
      break;
    case Tool::Circle:
      if (drawStage_ == 0) {
        pushUndo();
        centerPt_ = ensurePoint(s);
        drawStage_ = 1;
        setupFieldsForStage();
        emit drawStageChanged(1);
        update();
      } else {
        commitDrawing(false);
      }
      break;
    case Tool::Arc:
      if (drawStage_ == 0) {
        pushUndo();
        centerPt_ = ensurePoint(s);
        drawStage_ = 1;
        setupFieldsForStage();
        emit drawStageChanged(1);
        update();
      } else {
        commitDrawing(false);
      }
      break;
    default: break;
  }
}

void SketchEditor::finishDrawing() {
  drawStage_ = 0;
  lastPt_ = firstPt_ = centerPt_ = startPt_ = rectFirstPt_ = -1;
  arcRadiusExpr_.clear();
  setupFieldsForStage();
  emit drawStageChanged(0);
  update();
}

void SketchEditor::mouseMoveEvent(QMouseEvent* e) {
  if (viewer_ && (e->buttons() & Qt::MiddleButton)) { QCoreApplication::sendEvent(viewer_, e); return; }
  const QPointF scr = e->position();
  mouse_ = scr;
  if (panning_) {
    org_ += QPointF(e->pos() - lastMouse_);
    lastMouse_ = e->pos();
    updateTransform();
    update();
    return;
  }
  const QPointF sp = toSketch(scr);
  if (!box_->isEditing()) emit statusMessage(QString("u = %1   v = %2").arg(sp.x(), 0, 'f', 2).arg(sp.y(), 0, 'f', 2));

  if (tool_ == Tool::Select) {
    if ((e->buttons() & Qt::LeftButton) && pressHit_.kind && (e->pos() - pressPos_).manhattanLength() > 4 &&
        (!dragPts_.empty() || dragCircle_ >= 0)) {
      if (!dragging_) { dragging_ = true; pushUndo(); }
      Sketch before = sk_;
      const QPointF m = toSketch(scr);
      int savedNext = sk_.nextId;
      std::vector<int> tmp;
      if (dragCircle_ >= 0) {
        SkEntity* en = sk_.entity(dragCircle_);
        const SkPoint* c = en ? sk_.point(en->points[0]) : nullptr;
        if (en && c) en->radius = std::max(1e-3, std::hypot(m.x() - c->x, m.y() - c->y));
      }
      for (size_t i = 0; i < dragPts_.size(); ++i) {
        if (SkPoint* q = sk_.point(dragPts_[i])) { q->x = m.x() + dragOffs_[i].x(); q->y = m.y() + dragOffs_[i].y(); }
        sk_.addConstraint(SkConstraintType::Fixed, {dragPts_[i]});
      }
      const size_t nAdded = dragPts_.size();
      const auto r = sketch::solve(sk_, params_);
      for (size_t i = 0; i < nAdded; ++i) sk_.constraints.pop_back();
      sk_.nextId = savedNext;
      if (std::getenv("PCD_DEBUG")) fprintf(stderr, "drag solve ok=%d status=%d dof=%d msg=%s\n", int(r.ok()), int(r.status), r.dof, r.message.c_str());
      if (!r.ok()) { sk_ = before; }
      else { solve_ = r; emit solved(r.dof, QString::fromStdString(r.message), true); }
      modified_ = true;
      update();
      return;
    }
    const Hit h = hitTest(scr);
    const int nh = h.kind == 3 ? -1 : h.id;
    if (nh != hover_) { hover_ = nh; update(); }
    return;
  }
  if (tool_ == Tool::Trim) {
    const Hit h = hitTest(scr);
    const int nh = h.kind == 2 ? h.id : -1;
    if (nh != hover_) { hover_ = nh; update(); }
    return;
  }
  curSnap_ = snap(scr, tool_ == Tool::Line && drawStage_ == 1 ? lastPt_ : -1);
  cursorSk_ = curSnap_.p;
  updateLiveFields();
  update();
}

void SketchEditor::mouseReleaseEvent(QMouseEvent* e) {
  if (viewer_ && e->button() == Qt::MiddleButton) { QCoreApplication::sendEvent(viewer_, e); return; }
  if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
    if (panning_) { panning_ = false; return; }
  }
  if (dragging_) {
    dragging_ = false;
    dragPts_.clear();
    dragCircle_ = -1;
    resolve();
    emit changed();
  }
  pressHit_ = Hit();
}

void SketchEditor::mouseDoubleClickEvent(QMouseEvent* e) {
  if (viewer_ && e->button() == Qt::MiddleButton) { QCoreApplication::sendEvent(viewer_, e); return; }
  if (tool_ == Tool::Line && drawStage_ == 1) { finishDrawing(); return; }
  if (tool_ != Tool::Select || !work_) return;
  for (const auto& [r, cid] : labelHits_) {
    if (!r.contains(e->position())) continue;
    for (auto& c : sk_.constraints) {
      if (c.id != cid || !isDimensional(c.type)) continue;
      DimensionDialog dlg(work_, "Ölçüyü düzenle", QString::fromStdString(c.valueExpr), c.type == SkConstraintType::Angle, this);
      if (dlg.exec() != QDialog::Accepted) return;
      pushUndo();
      reloadParams();
      const std::string old = c.valueExpr;
      c.valueExpr = dlg.expression().toStdString();
      const auto r2 = sketch::solve(sk_, params_);
      if (!r2.ok()) {
        c.valueExpr = old;
        emit statusMessage("Ölçü çözülemedi: " + QString::fromStdString(r2.message));
        undo_.pop_back();
        resolve();
      } else {
        afterEdit();
        ensureVisible();
        emit paramsChanged();
      }
      return;
    }
  }
}

void SketchEditor::wheelEvent(QWheelEvent* e) {
  if (viewer_) { QCoreApplication::sendEvent(viewer_, e); return; }
  const double f = std::pow(1.15, e->angleDelta().y() / 120.0);
  const QPointF m = e->position();
  const QPointF before = toSketch(m);
  scale_ = std::clamp(scale_ * f, 0.02, 5000.0);
  org_ = QPointF(m.x() - before.x() * scale_, m.y() + before.y() * scale_);
  updateTransform();
  updateLiveFields();
  update();
}

void SketchEditor::keyPressEvent(QKeyEvent* e) {
  if (handleKey(e)) return;
  if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) deleteSelection();
  else if (e->key() == Qt::Key_Escape) {
    if (box_->isEditing()) { box_->stopEdit(); updateLiveFields(); update(); }
    else if (drawStage_ > 0) finishDrawing();
    else if (tool_ != Tool::Select) setTool(Tool::Select);
    else { sel_.clear(); update(); emit selectionChanged(); }
  } else if (e->matches(QKeySequence::Undo)) undo();
  else if (e->matches(QKeySequence::Redo)) redo();
  else if (e->key() == Qt::Key_F) fitView();
  else QWidget::keyPressEvent(e);
}

// ------------------------------------------------------------------------- editing commands
void SketchEditor::removeIds(const std::vector<int>& ids) {
  std::set<int> dead(ids.begin(), ids.end());
  // entities that reference a deleted point die too
  std::set<int> orphanCandidates;
  for (const auto& en : sk_.entities)
    if (dead.count(en.id) || referencesAny(en.points, dead)) {
      dead.insert(en.id);
      for (int pid : en.points) orphanCandidates.insert(pid);
    }
  // points used only by deleted entities are deleted as well (they are not user "free points")
  for (int pid : orphanCandidates) {
    if (pid == 0 || dead.count(pid)) continue;
    bool used = false;
    for (const auto& en : sk_.entities)
      if (!dead.count(en.id) && std::find(en.points.begin(), en.points.end(), pid) != en.points.end()) used = true;
    if (!used) dead.insert(pid);
  }
  dead.erase(0);
  sk_.constraints.erase(std::remove_if(sk_.constraints.begin(), sk_.constraints.end(),
                                       [&](const SkConstraint& c) { return dead.count(c.id) || referencesAny(c.refs, dead); }),
                        sk_.constraints.end());
  sk_.entities.erase(std::remove_if(sk_.entities.begin(), sk_.entities.end(), [&](const SkEntity& e) { return dead.count(e.id) > 0; }),
                     sk_.entities.end());
  sk_.points.erase(std::remove_if(sk_.points.begin(), sk_.points.end(), [&](const SkPoint& p) { return dead.count(p.id) > 0; }),
                   sk_.points.end());
}

void SketchEditor::deleteSelection() {
  if (sel_.empty()) return;
  pushUndo();
  removeIds(sel_);
  sel_.clear();
  afterEdit();
  emit selectionChanged();
}

void SketchEditor::toggleConstructionOfSelection() {
  bool any = false;
  for (int id : sel_)
    if (SkEntity* e = sk_.entity(id)) any = true;
  if (!any) return;
  pushUndo();
  for (int id : sel_)
    if (SkEntity* e = sk_.entity(id)) e->construction = !e->construction;
  afterEdit();
}

bool SketchEditor::addConstraintChecked(SkConstraintType t, std::vector<int> refs, const std::string& expr) {
  pushUndo();
  const Sketch backup = sk_;
  sk_.addConstraint(t, refs, expr);
  const auto r = sketch::solve(sk_, params_);
  if (!r.ok() || r.status == sketch::SolveStatus::OverConstrained) {
    const QString msg = QString::fromStdString(r.message);
    sk_ = backup;
    undo_.pop_back();
    resolve();
    emit statusMessage("Kısıt eklenemedi: " + (msg.isEmpty() ? QString("çelişkili / aşırı kısıtlı") : msg));
    return false;
  }
  solve_ = r;
  modified_ = true;
  emit solved(r.dof, QString::fromStdString(r.message), true);
  emit changed();
  update();
  return true;
}

void SketchEditor::applyConstraint(SkConstraintType t) {
  std::vector<int> pts, lines, curves, ents;
  for (int id : sel_) {
    if (sk_.point(id)) pts.push_back(id);
    else if (const SkEntity* e = sk_.entity(id)) {
      ents.push_back(id);
      (e->type == SkEntityType::Line ? lines : curves).push_back(id);
    }
  }
  auto bad = [&](const QString& why) { emit statusMessage("Seçim uygun değil: " + why); };
  switch (t) {
    case SkConstraintType::Coincident:
      if (pts.size() == 2) addConstraintChecked(t, {pts[0], pts[1]});
      else if (pts.size() == 1 && ents.size() == 1) addConstraintChecked(SkConstraintType::PointOnEntity, {pts[0], ents[0]});
      else bad("iki nokta (veya nokta + eleman) seçin");
      break;
    case SkConstraintType::Horizontal: case SkConstraintType::Vertical: {
      const bool h = t == SkConstraintType::Horizontal;
      if (!lines.empty()) for (int l : lines) addConstraintChecked(t, {l});
      else if (pts.size() == 2) addConstraintChecked(h ? SkConstraintType::HorizontalPts : SkConstraintType::VerticalPts, {pts[0], pts[1]});
      else bad("bir çizgi veya iki nokta seçin");
      break;
    }
    case SkConstraintType::Parallel: case SkConstraintType::Perpendicular:
      if (lines.size() == 2) addConstraintChecked(t, {lines[0], lines[1]}); else bad("iki çizgi seçin");
      break;
    case SkConstraintType::Tangent: case SkConstraintType::Equal:
      if (ents.size() == 2) addConstraintChecked(t, {ents[0], ents[1]}); else bad("iki eleman seçin");
      break;
    case SkConstraintType::Fixed:
      if (!pts.empty()) for (int pid : pts) addConstraintChecked(t, {pid});
      else if (!ents.empty()) {
        for (int eid : ents) for (int pid : sk_.entity(eid)->points) if (pid != 0) addConstraintChecked(t, {pid});
      } else bad("nokta seçin");
      break;
    case SkConstraintType::Midpoint:
      if (pts.size() == 1 && lines.size() == 1) addConstraintChecked(t, {pts[0], lines[0]}); else bad("bir nokta ve bir çizgi seçin");
      break;
    case SkConstraintType::Symmetric:
      if (pts.size() == 2 && lines.size() == 1) addConstraintChecked(t, {pts[0], pts[1], lines[0]}); else bad("iki nokta ve simetri çizgisi seçin");
      break;
    case SkConstraintType::Concentric:
      if (curves.size() == 2) addConstraintChecked(t, {curves[0], curves[1]}); else bad("iki çember/yay seçin");
      break;
    default: break;
  }
}

double SketchEditor::currentValue(SkConstraintType t, const std::vector<int>& r) const {
  auto P = [&](int id) { const SkPoint* p = sk_.point(id); return p ? QPointF(p->x, p->y) : QPointF(); };
  auto rad = [&](const SkEntity* e) {
    if (e->type == SkEntityType::Circle) return e->radius;
    const QPointF c = P(e->points[0]), s = P(e->points[1]);
    return std::hypot(s.x() - c.x(), s.y() - c.y());
  };
  switch (t) {
    case SkConstraintType::Distance: return std::hypot(P(r[1]).x() - P(r[0]).x(), P(r[1]).y() - P(r[0]).y());
    case SkConstraintType::DistanceX: return P(r[1]).x() - P(r[0]).x();
    case SkConstraintType::DistanceY: return P(r[1]).y() - P(r[0]).y();
    case SkConstraintType::Length: { const SkEntity* e = sk_.entity(r[0]); return std::hypot(P(e->points[1]).x() - P(e->points[0]).x(), P(e->points[1]).y() - P(e->points[0]).y()); }
    case SkConstraintType::Radius: return rad(sk_.entity(r[0]));
    case SkConstraintType::Diameter: return 2 * rad(sk_.entity(r[0]));
    case SkConstraintType::PointLineDistance: {
      const SkEntity* e = sk_.entity(r[1]);
      const QPointF A = P(e->points[0]), B = P(e->points[1]), Q = P(r[0]);
      const double dx = B.x() - A.x(), dy = B.y() - A.y(), l = std::hypot(dx, dy);
      return l > 1e-12 ? std::fabs((Q.x() - A.x()) * dy - (Q.y() - A.y()) * dx) / l : 0;
    }
    case SkConstraintType::Angle: {
      const SkEntity *l1 = sk_.entity(r[0]), *l2 = sk_.entity(r[1]);
      const QPointF a = P(l1->points[1]) - P(l1->points[0]), b = P(l2->points[1]) - P(l2->points[0]);
      double d = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180 / M_PI;
      while (d > 180) d -= 360;
      while (d <= -180) d += 360;
      return d;
    }
    default: return 0;
  }
}

void SketchEditor::applyDimension(SkConstraintType t) {
  if (!work_) return;
  std::vector<int> pts, lines, curves;
  for (int id : sel_) {
    if (sk_.point(id)) pts.push_back(id);
    else if (const SkEntity* e = sk_.entity(id)) (e->type == SkEntityType::Line ? lines : curves).push_back(id);
  }
  std::vector<int> refs;
  SkConstraintType type = t;
  QString title;
  switch (t) {
    case SkConstraintType::Distance:
      title = "Mesafe";
      if (pts.size() == 2) refs = {pts[0], pts[1]};
      else if (pts.size() == 1 && lines.size() == 1) { type = SkConstraintType::PointLineDistance; refs = {pts[0], lines[0]}; }
      else if (lines.size() == 1) { type = SkConstraintType::Length; refs = {lines[0]}; }
      break;
    case SkConstraintType::DistanceX: case SkConstraintType::DistanceY:
      title = t == SkConstraintType::DistanceX ? "Yatay mesafe" : "Dikey mesafe";
      if (pts.size() == 2) refs = {pts[0], pts[1]};
      else if (lines.size() == 1) { const SkEntity* e = sk_.entity(lines[0]); refs = {e->points[0], e->points[1]}; }
      break;
    case SkConstraintType::Length:
      title = "Uzunluk";
      if (lines.size() == 1) refs = {lines[0]};
      else if (pts.size() == 2) { type = SkConstraintType::Distance; refs = {pts[0], pts[1]}; }
      break;
    case SkConstraintType::Radius: case SkConstraintType::Diameter:
      title = t == SkConstraintType::Radius ? "Yarıçap" : "Çap";
      if (curves.size() == 1) refs = {curves[0]};
      break;
    case SkConstraintType::Angle:
      title = "Açı";
      if (lines.size() == 2) refs = {lines[0], lines[1]};
      break;
    default: return;
  }
  if (refs.empty()) { emit statusMessage("Ölçü için uygun seçim yok (" + title + ")"); return; }
  double v = currentValue(type, refs);
  if (type == SkConstraintType::Distance || type == SkConstraintType::Length || type == SkConstraintType::PointLineDistance)
    v = std::fabs(v);
  v = std::round(v * 1000) / 1000;
  DimensionDialog dlg(work_, title, QString::fromStdString(formatNumber(v)), type == SkConstraintType::Angle, this);
  if (dlg.exec() != QDialog::Accepted) return;
  reloadParams();
  if (addConstraintChecked(type, refs, dlg.expression().toStdString())) {
    ensureVisible();
    emit paramsChanged();
  }
}

} // namespace pcd
