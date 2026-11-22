// WP-2: OverlayView — sheet background + DrawingIR layers + confidence glow + editing tools (emits UserEdits only).
// Scene coordinates: x = sheet mm, y = -(sheet mm)  (sheet is y-up, Qt scene is y-down). Pens are cosmetic (px).
#include "pc/drawing/ui/OverlayView.h"
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QWheelEvent>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace pc::drawing::ui {

namespace {

using json = nlohmann::json;
constexpr double kPi = 3.14159265358979323846;

QPointF S(Vec2 p) { return {p.x, -p.y}; }
Vec2 V(QPointF q) { return {q.x(), -q.y()}; }
json J(Vec2 p) { return json::array({p.x, p.y}); }
json J(const Box2& b) { return json{{"lo", J(b.lo)}, {"hi", J(b.hi)}}; }
QRectF R(const Box2& b) { return QRectF(QPointF(b.lo.x, -b.hi.y), QPointF(b.hi.x, -b.lo.y)).normalized(); }
Box2 B(const QRectF& r) { const QRectF n = r.normalized(); return {{n.left(), -n.bottom()}, {n.right(), -n.top()}}; }

enum ItemKind { KPrim = 1, KText, KDim, KView, KLayoutBorder, KLayoutTitle, KDeco };
constexpr int kIdKey = 0, kKindKey = 1;

QPen cpen(const QColor& c, double w, Qt::PenStyle st = Qt::SolidLine) {
  QPen p(c, w, st, Qt::RoundCap, Qt::RoundJoin);
  p.setCosmetic(true);
  return p;
}

QPen rolePen(LineRole r, bool thick) {
  const double w = thick ? 2.0 : 1.2;
  switch (r) {
    case LineRole::Visible: return cpen(Qt::black, thick ? 2.0 : 1.6);
    case LineRole::Hidden: { QPen p = cpen(QColor(0, 70, 220), w); p.setDashPattern({6, 4}); return p; }
    case LineRole::Center: { QPen p = cpen(QColor(210, 0, 0), w); p.setDashPattern({12, 3, 2, 3}); return p; }
    case LineRole::Cutting: { QPen p = cpen(QColor(210, 0, 0), 2.2); p.setDashPattern({12, 3, 2, 3}); return p; }
    case LineRole::Dimension: case LineRole::Extension: case LineRole::Leader: return cpen(QColor(0, 150, 0), w);
    case LineRole::Hatch: return cpen(QColor(140, 140, 140), 1.0);
    case LineRole::Thread: return cpen(QColor(0, 70, 220), 1.0);
    case LineRole::Border: case LineRole::TitleBlock: return cpen(QColor(90, 90, 90), w);
    case LineRole::Symbol: return cpen(QColor(130, 0, 160), w);
    case LineRole::Phantom: { QPen p = cpen(QColor(200, 0, 200), w); p.setDashPattern({12, 3, 2, 3, 2, 3}); return p; }
    default: return cpen(QColor(40, 40, 40), w);
  }
}

QPen stylePen(const Primitive& p) {
  const bool thick = p.style.width > 0.4;
  if (!p.style.dash.empty()) {
    QPen q = cpen(QColor(60, 60, 60), thick ? 2.0 : 1.2);
    q.setDashPattern({6, 4});
    return q;
  }
  QColor c(40, 40, 40);
  if (p.style.rgb) c = QColor((p.style.rgb >> 16) & 255, (p.style.rgb >> 8) & 255, p.style.rgb & 255);
  return cpen(c, thick ? 2.0 : 1.2);
}

QColor confColor(float c) {
  if (c < 0.6f) return QColor(230, 0, 0, 90);
  if (c < 0.9f) return QColor(240, 200, 0, 110);
  return QColor();
}

QPointF arcPt(Vec2 c, double r, double deg) {
  return S({c.x + r * std::cos(deg * kPi / 180), c.y + r * std::sin(deg * kPi / 180)});
}

QPainterPath primPath(const Primitive& p) {
  QPainterPath path;
  switch (p.kind) {
    case PrimKind::Line:
      path.moveTo(S(p.p0));
      path.lineTo(S(p.p1));
      break;
    case PrimKind::Circle:
    case PrimKind::Arc: {
      const bool full = p.kind == PrimKind::Circle || p.a1 - p.a0 >= 359.999;
      const QRectF r(p.center.x - p.radius, -p.center.y - p.radius, 2 * p.radius, 2 * p.radius);
      // Qt arcTo: angles CCW in a y-down scene appear CW, so mirror: start = a0, sweep = span (Qt flips y itself)
      const double a0 = full ? 0 : p.a0, span = full ? 360 : p.a1 - p.a0;
      path.arcMoveTo(r, a0);
      path.arcTo(r, a0, span);
      break;
    }
    case PrimKind::Ellipse: {
      const int n = 72;
      const double a0 = p.a0, a1 = p.a1 > p.a0 ? p.a1 : p.a0 + 360;
      const double rr = p.rotDeg * kPi / 180;
      for (int i = 0; i <= n; ++i) {
        const double t = (a0 + (a1 - a0) * i / n) * kPi / 180;
        const double x = p.radius * std::cos(t), y = p.radius * p.minorRatio * std::sin(t);
        const QPointF q = S({p.center.x + x * std::cos(rr) - y * std::sin(rr), p.center.y + x * std::sin(rr) + y * std::cos(rr)});
        if (i == 0) path.moveTo(q); else path.lineTo(q);
      }
      break;
    }
    case PrimKind::Bezier:
      if (!p.pts.empty()) {
        path.moveTo(S(p.pts[0]));
        for (std::size_t i = 1; i + 2 < p.pts.size(); i += 3) path.cubicTo(S(p.pts[i]), S(p.pts[i + 1]), S(p.pts[i + 2]));
      }
      break;
    case PrimKind::Arrowhead: {
      const Vec2 d = p.p1 - p.p0;
      const double L = length(d);
      if (L <= 0) break;
      const Vec2 n{-d.y / L, d.x / L};
      const double hw = p.style.width > 0 ? p.style.width / 2 : L / 6;
      path.moveTo(S(p.p0));
      path.lineTo(S(p.p1 + n * hw));
      if (p.style.filled) path.lineTo(S(p.p1 - n * hw)), path.closeSubpath();
      else path.moveTo(S(p.p0)), path.lineTo(S(p.p1 - n * hw));
      break;
    }
    default:   // Polyline / HatchRegion / Image
      if (!p.pts.empty()) {
        path.moveTo(S(p.pts[0]));
        for (std::size_t i = 1; i < p.pts.size(); ++i) path.lineTo(S(p.pts[i]));
        if (p.closed || p.kind != PrimKind::Polyline) path.closeSubpath();
      }
      break;
  }
  return path;
}

// end point `end` (0/1) of a line or arc, sheet mm
bool primEnd(const Primitive& p, int end, Vec2& out) {
  if (p.kind == PrimKind::Line) { out = end ? p.p1 : p.p0; return true; }
  if (p.kind == PrimKind::Arc) {
    const double a = (end ? p.a1 : p.a0) * kPi / 180;
    out = {p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)};
    return true;
  }
  return false;
}

double distToPrim(const Primitive& p, Vec2 q) {
  switch (p.kind) {
    case PrimKind::Line: {
      const Vec2 d = p.p1 - p.p0;
      const double L2 = dot(d, d);
      const double t = L2 > 0 ? std::clamp(dot(q - p.p0, d) / L2, 0.0, 1.0) : 0;
      return distance(q, p.p0 + d * t);
    }
    case PrimKind::Circle: return std::abs(distance(q, p.center) - p.radius);
    case PrimKind::Arc: {
      double a = std::atan2(q.y - p.center.y, q.x - p.center.x) * 180 / kPi;
      while (a < p.a0) a += 360;
      if (a <= p.a1) return std::abs(distance(q, p.center) - p.radius);
      Vec2 e0, e1;
      primEnd(p, 0, e0);
      primEnd(p, 1, e1);
      return std::min(distance(q, e0), distance(q, e1));
    }
    default: {
      const QPainterPath path = primPath(p);
      const QPointF s = S(q);
      double best = 1e300;
      for (int i = 0; i + 1 < path.elementCount(); ++i) {
        const QPointF a = path.elementAt(i), b = path.elementAt(i + 1);
        const Vec2 A = V(a), Bv = V(b), d = Bv - A;
        const double L2 = dot(d, d);
        const double t = L2 > 0 ? std::clamp(dot(q - A, d) / L2, 0.0, 1.0) : 0;
        best = std::min(best, distance(q, A + d * t));
      }
      if (path.contains(s)) best = 0;
      return best;
    }
  }
}

}  // namespace

struct OverlayView::Impl {
  OverlayView* q = nullptr;
  const DrawingIR* ir = nullptr;
  uint32_t layers = LayerDefault;
  OverlayTool tool = OverlayTool::Select;
  Stage stage = Stage::Normalize;
  int sketch = -1;
  std::vector<int> selection, highlighted;

  QImage bgImage;
  Affine2 bgXform = kIdentityAffine;
  bool hasBg = false;

  // id -> items (shape items whose pen changes on selection), base pens
  std::map<int, std::vector<QAbstractGraphicsShapeItem*>> items;
  std::map<QAbstractGraphicsShapeItem*, QPen> basePen;

  // interaction state
  bool panning = false;
  QPoint panLast;
  bool pressing = false;
  QPointF pressScene;
  QGraphicsRectItem* rubber = nullptr;
  std::vector<Vec2> clicks;                 // DrawLine / DrawArc / DrawCircle / PickGeometry
  std::vector<int> pickedIds;
  QGraphicsPathItem* preview = nullptr;
  // EditBoxes drag
  int boxKind = 0, boxViewId = -1, boxMode = 0;   // mode: 0 none, 1 move, 2 resize (corner), 3 new
  QRectF boxStart;
  int boxCorner = 0;
  // Snap drag
  int snapId = -1, snapEnd = 0;

  const PrimitiveSheet& sheet() const {
    if (layers & LayerRaw) return ir->raw;
    if (ir->norm.prims.empty() && ir->norm.texts.empty()) return ir->raw;
    return ir->norm;
  }
  double pxToScene(double px) const {
    const double s = q->transform().m11();
    return px / (s > 0 ? s : 1);
  }
  QRectF sheetRect() const {
    if (ir) {
      const Box2& b = sheet().sheet;
      if (!isEmpty(b) && pc::drawing::width(b) > 0 && pc::drawing::height(b) > 0) return R(b);
    }
    QRectF r = q->scene()->itemsBoundingRect();
    return r.isEmpty() ? QRectF(0, -297, 420, 297) : r;
  }

  void addShape(int id, int kind, QAbstractGraphicsShapeItem* it, const QPen& pen, double z) {
    it->setPen(pen);
    it->setZValue(z);
    it->setData(kIdKey, id);
    it->setData(kKindKey, kind);
    q->scene()->addItem(it);
    if (id >= 0) { items[id].push_back(it); basePen[it] = pen; }
  }
  void addGlow(const QPainterPath& path, float conf, double z) {
    if (!(layers & LayerConfidence)) return;
    const QColor c = confColor(conf);
    if (!c.isValid()) return;
    auto* g = new QGraphicsPathItem(path);
    g->setPen(cpen(c, 7));
    g->setZValue(z);
    g->setData(kKindKey, KDeco);
    g->setAcceptedMouseButtons(Qt::NoButton);
    q->scene()->addItem(g);
  }

  void rebuild() {
    QGraphicsScene* sc = q->scene();
    rubber = nullptr;
    preview = nullptr;
    items.clear();
    basePen.clear();
    sc->clear();
    if (hasBg && (layers & LayerBackground) && !bgImage.isNull()) {
      auto* pm = sc->addPixmap(QPixmap::fromImage(bgImage));
      const Affine2& m = bgXform;
      pm->setTransform(QTransform(m[0], -m[1], m[2], -m[3], m[4], -m[5]));
      pm->setZValue(-100);
      pm->setOpacity(0.55);
      pm->setTransformationMode(Qt::SmoothTransformation);
      pm->setData(kKindKey, KDeco);
    }
    if (!ir) { sc->setSceneRect(sheetRect()); return; }
    const PrimitiveSheet& sh = sheet();
    // sheet outline
    if (!isEmpty(sh.sheet) && pc::drawing::width(sh.sheet) > 0) {
      auto* r = sc->addRect(R(sh.sheet), cpen(QColor(200, 200, 200), 1, Qt::DotLine));
      r->setZValue(-90);
      r->setData(kKindKey, KDeco);
    }
    const bool useRoles = (layers & LayerRoles) && !(layers & LayerRaw) && !ir->annot.roles.empty();
    if (layers & LayerPrimitives) {
      for (const auto& p : sh.prims) {
        const QPainterPath path = primPath(p);
        if (path.isEmpty()) continue;
        QPen pen = useRoles ? rolePen(roleOf(*ir, p.id), p.style.width > 0.4) : stylePen(p);
        if (useRoles && roleOf(*ir, p.id) == LineRole::Unknown) pen = stylePen(p);
        auto* it = new QGraphicsPathItem(path);
        if (p.kind == PrimKind::Arrowhead && p.style.filled) it->setBrush(pen.color());
        addShape(p.id, KPrim, it, pen, 10);
        addGlow(path, p.conf, 5);
      }
    }
    if (layers & LayerTexts) {
      for (const auto& t : sh.texts) {
        QPolygonF poly;
        for (const auto& c : t.quad) poly << S(c);
        auto* box = new QGraphicsPolygonItem(poly);
        addShape(t.id, KText, box, cpen(QColor(0, 120, 140), 1), 20);
        QPainterPath gp;
        gp.addPolygon(poly);
        gp.closeSubpath();
        addGlow(gp, t.conf, 5);
        auto* label = new QGraphicsSimpleTextItem(QString::fromStdString(t.utf8));
        QFont f = label->font();
        f.setPixelSize(20);
        label->setFont(f);
        label->setBrush(QColor(0, 110, 130));
        const double h = std::max(0.5, t.height > 0 ? t.height : distance(t.quad[0], t.quad[3]));
        const double s = h * 1.25 / 20.0;
        label->setTransform(QTransform().rotate(-t.rotDeg).scale(s, s));
        label->setPos(S(t.quad[3]));
        label->setZValue(21);
        label->setData(kIdKey, t.id);
        label->setData(kKindKey, KText);
        sc->addItem(label);
      }
    }
    if (layers & LayerLayout) {
      const SheetLayout& L = ir->layout;
      auto rect = [&](const Box2& b, const QPen& pen, int id, int kind, const QString& text) {
        if (isEmpty(b) || pc::drawing::width(b) <= 0) return;
        auto* r = new QGraphicsRectItem(R(b));
        addShape(id, kind, r, pen, 30);
        if (id < 0) { r->setData(kIdKey, id); }
        if (!text.isEmpty()) {
          auto* lab = sc->addSimpleText(text);
          lab->setBrush(pen.color());
          lab->setFlag(QGraphicsItem::ItemIgnoresTransformations);
          lab->setPos(R(b).topLeft());
          lab->setZValue(31);
          lab->setData(kKindKey, KDeco);
        }
      };
      rect(L.border, cpen(QColor(120, 80, 0), 1.5, Qt::DashLine), -1, KLayoutBorder, QString());
      rect(L.title.box, cpen(QColor(160, 90, 0), 1.5, Qt::DashLine), -1, KLayoutTitle, QStringLiteral("Antet"));
      for (const auto& nb : L.noteBlocks) rect(nb, cpen(QColor(120, 120, 120), 1, Qt::DotLine), -1, KDeco, QStringLiteral("Not"));
      for (const auto& v : L.views) {
        QString t = QString::fromUtf8(labelTr(v.role));
        if (!v.label.empty()) t += QStringLiteral(" ") + QString::fromStdString(v.label);
        QPen pen = cpen(QColor(30, 90, 200), 1.5, Qt::DashLine);
        rect(v.box, pen, v.id, KView, t);
      }
    }
    if ((layers & LayerViews) && ir) {
      for (const auto& [vid, pid] : ir->views.axisOfView) {
        const Primitive* p = findPrim(sh, pid);
        if (!p) continue;
        auto* it = sc->addPath(primPath(*p), cpen(QColor(230, 0, 0, 160), 4));
        it->setZValue(8);
        it->setData(kKindKey, KDeco);
      }
      for (const auto& r : ir->views.relations) {
        const ViewRegion* a = findView(ir->layout, r.a);
        const ViewRegion* b = findView(ir->layout, r.b);
        if (!a || !b) continue;
        auto* l = sc->addLine(QLineF(R(a->box).center(), R(b->box).center()), cpen(QColor(30, 90, 200, 150), 1, Qt::DashDotLine));
        l->setZValue(32);
        l->setData(kKindKey, KDeco);
      }
    }
    if (layers & LayerDimensions) {
      for (const auto& d : ir->annot.dims) {
        const QColor c = d.outOfBand ? QColor(220, 0, 0) : d.conf < 0.6f ? QColor(230, 150, 0) : QColor(0, 160, 0);
        for (int gid : d.geomPrimIds)
          if (const Primitive* p = findPrim(sh, gid)) {
            auto* it = sc->addPath(primPath(*p), cpen(QColor(c.red(), c.green(), c.blue(), 110), 5));
            it->setZValue(7);
            it->setData(kKindKey, KDeco);
          }
        QPainterPath ap;
        ap.moveTo(S(d.anchors[0]));
        ap.lineTo(S(d.anchors[1]));
        for (const auto& a : d.anchors) ap.addEllipse(S(a), 0.6, 0.6);
        auto* it = new QGraphicsPathItem(ap);
        addShape(d.id, KDim, it, cpen(c, 1.5, Qt::DashLine), 40);
      }
    }
    if ((layers & LayerSketch) && sketch >= 0) {
      for (const auto& s : ir->sketches) {
        if (s.id != sketch) continue;
        for (const auto& [ent, pid] : s.entityToPrimitive) {
          (void)ent;
          if (const Primitive* p = findPrim(sh, pid)) {
            auto* it = sc->addPath(primPath(*p), cpen(QColor(255, 120, 0, 170), 4));
            it->setZValue(9);
            it->setData(kKindKey, KDeco);
          }
        }
      }
    }
    sc->setSceneRect(sheetRect().united(sc->itemsBoundingRect()));
    applySelectionPens();
  }

  void applySelectionPens() {
    const std::set<int> sel(selection.begin(), selection.end()), hl(highlighted.begin(), highlighted.end());
    for (auto& [it, pen] : basePen) {
      const int id = it->data(kIdKey).toInt();
      QPen p = pen;
      if (sel.count(id)) { p.setColor(QColor(255, 110, 0)); p.setWidthF(std::max(2.5, pen.widthF() + 1.5)); }
      else if (hl.count(id)) { p.setColor(QColor(220, 0, 220)); p.setWidthF(std::max(3.0, pen.widthF() + 2)); }
      it->setPen(p);
    }
  }

  void setSel(std::vector<int> ids, bool emitSignal) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    const bool changed = ids != selection;
    selection = std::move(ids);
    applySelectionPens();
    if (changed && emitSignal) emit q->selectionChanged(selection);
  }

  // nearest element id at scene point (primitives, texts, dims, views); -1 none
  int pick(QPointF s, int* kindOut = nullptr) const {
    if (!ir) return -1;
    const double tol = pxToScene(6);
    const Vec2 p = V(s);
    int best = -1, bestKind = 0;
    double bd = tol;
    const PrimitiveSheet& sh = sheet();
    if (layers & LayerPrimitives)
      for (const auto& pr : sh.prims) {
        const double d = distToPrim(pr, p);
        if (d < bd) { bd = d; best = pr.id; bestKind = KPrim; }
      }
    if (layers & LayerTexts)
      for (const auto& t : sh.texts) {
        QPolygonF poly;
        for (const auto& c : t.quad) poly << S(c);
        if (poly.containsPoint(s, Qt::OddEvenFill) && best < 0) { best = t.id; bestKind = KText; bd = 0; }
      }
    if (best < 0 && (layers & LayerDimensions))
      for (const auto& d : ir->annot.dims) {
        Primitive l;
        l.p0 = d.anchors[0];
        l.p1 = d.anchors[1];
        if (distToPrim(l, p) < bd) { bd = distToPrim(l, p); best = d.id; bestKind = KDim; }
      }
    if (best < 0 && (layers & LayerLayout)) {
      double area = 1e300;
      for (const auto& v : ir->layout.views)
        if (contains(v.box, p) && pc::drawing::width(v.box) * pc::drawing::height(v.box) < area) { area = pc::drawing::width(v.box) * pc::drawing::height(v.box); best = v.id; bestKind = KView; }
    }
    if (kindOut) *kindOut = bestKind;
    return best;
  }

  std::vector<int> idsInRect(const QRectF& r) const {
    std::vector<int> ids;
    for (QGraphicsItem* it : q->scene()->items(r, Qt::ContainsItemShape)) {
      const int k = it->data(kKindKey).toInt();
      if (k == KPrim || k == KText || k == KDim) ids.push_back(it->data(kIdKey).toInt());
    }
    return ids;
  }

  void emitEdit(const char* op, const json& payload) {
    UserEdit e;
    e.stage = stage;
    e.op = op;
    e.payload = payload.dump();
    emit q->editRequested(e);
  }

  void clearPreview() {
    if (preview) { q->scene()->removeItem(preview); delete preview; preview = nullptr; }
  }
  void setPreview(const QPainterPath& path) {
    if (!preview) {
      preview = new QGraphicsPathItem;
      preview->setPen(cpen(QColor(255, 0, 160), 1.5, Qt::DashLine));
      preview->setZValue(100);
      preview->setData(kKindKey, KDeco);
      q->scene()->addItem(preview);
    }
    preview->setPath(path);
  }
  void resetInteraction() {
    clicks.clear();
    pickedIds.clear();
    clearPreview();
    if (rubber) { q->scene()->removeItem(rubber); delete rubber; rubber = nullptr; }
    pressing = false;
    boxMode = 0;
    snapId = -1;
  }

  // ---- layout boxes for EditBoxes: find the box under / near p (smallest first)
  struct BoxHit { int kind = 0, viewId = -1; QRectF rect; int corner = -1; };
  BoxHit hitBox(QPointF s) const {
    BoxHit h;
    if (!ir) return h;
    const double tol = pxToScene(7);
    std::vector<BoxHit> cands;
    for (const auto& v : ir->layout.views) cands.push_back({KView, v.id, R(v.box), -1});
    if (!isEmpty(ir->layout.title.box) && pc::drawing::width(ir->layout.title.box) > 0) cands.push_back({KLayoutTitle, -1, R(ir->layout.title.box), -1});
    if (!isEmpty(ir->layout.border) && pc::drawing::width(ir->layout.border) > 0) cands.push_back({KLayoutBorder, -1, R(ir->layout.border), -1});
    double area = 1e300;
    for (auto& c : cands) {
      const QPointF corners[4] = {c.rect.topLeft(), c.rect.topRight(), c.rect.bottomRight(), c.rect.bottomLeft()};
      for (int k = 0; k < 4; ++k)
        if (QLineF(corners[k], s).length() <= tol) { c.corner = k; return c; }
      const QRectF grown = c.rect.adjusted(-tol, -tol, tol, tol);
      const bool onEdge = grown.contains(s) && !c.rect.adjusted(tol, tol, -tol, -tol).contains(s);
      const bool inside = c.kind == KView ? c.rect.contains(s) : onEdge;   // border/title: grab at the edge only
      if (inside && c.rect.width() * c.rect.height() < area) { area = c.rect.width() * c.rect.height(); h = c; }
    }
    return h;
  }
  QRectF draggedRect(QPointF cur) const {
    const QPointF d = cur - pressScene;
    if (boxMode == 1) return boxStart.translated(d);
    if (boxMode == 2) {
      QRectF r = boxStart;
      switch (boxCorner) {
        case 0: r.setTopLeft(r.topLeft() + d); break;
        case 1: r.setTopRight(r.topRight() + d); break;
        case 2: r.setBottomRight(r.bottomRight() + d); break;
        default: r.setBottomLeft(r.bottomLeft() + d); break;
      }
      return r.normalized();
    }
    return QRectF(pressScene, cur).normalized();
  }

  bool onWheel(QWheelEvent* e);
  bool onPress(QMouseEvent* e);
  bool onMove(QMouseEvent* e);
  bool onRelease(QMouseEvent* e);
  bool onDouble(QMouseEvent* e);
  bool onKey(QKeyEvent* e);
  bool dispatch(QEvent* e);

  // ---- snap: nearest free end (line / arc) within tolerance
  bool hitEnd(QPointF s, int& id, int& end) const {
    if (!ir) return false;
    const double tol = pxToScene(8);
    double bd = tol;
    bool found = false;
    for (const auto& p : sheet().prims)
      for (int e = 0; e < 2; ++e) {
        Vec2 pt;
        if (!primEnd(p, e, pt)) continue;
        const double d = distance(pt, V(s));
        if (d < bd) { bd = d; id = p.id; end = e; found = true; }
      }
    return found;
  }
};

namespace {
class OverlayEventFilter : public QObject {
 public:
  explicit OverlayEventFilter(std::function<bool(QEvent*)> f, QObject* parent) : QObject(parent), f_(std::move(f)) {}
  bool eventFilter(QObject*, QEvent* e) override { return f_(e); }
 private:
  std::function<bool(QEvent*)> f_;
};
}  // namespace

OverlayView::OverlayView(QWidget* parent) : QGraphicsView(parent), d_(std::make_unique<Impl>()) {
  d_->q = this;
  setScene(new QGraphicsScene(this));
  setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setResizeAnchor(QGraphicsView::AnchorViewCenter);
  setDragMode(QGraphicsView::NoDrag);
  setMouseTracking(true);
  setBackgroundBrush(Qt::white);
  setFocusPolicy(Qt::StrongFocus);
  setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
  // The frozen header declares no event overrides: route viewport + key events through an event filter.
  viewport()->installEventFilter(new OverlayEventFilter(
      [this](QEvent* e) { return e->type() != QEvent::KeyPress && d_->dispatch(e); }, this));
  installEventFilter(new OverlayEventFilter([this](QEvent* e) { return e->type() == QEvent::KeyPress && d_->dispatch(e); }, this));
}
OverlayView::~OverlayView() = default;

void OverlayView::setIR(const DrawingIR* ir) {
  d_->ir = ir;
  d_->resetInteraction();
  d_->selection.clear();
  d_->highlighted.clear();
  refresh();
}
const DrawingIR* OverlayView::ir() const { return d_->ir; }

void OverlayView::refresh() {
  d_->resetInteraction();
  const bool first = scene()->items().isEmpty();
  d_->rebuild();
  // keep the selection where ids survive
  std::vector<int> keep;
  for (int id : d_->selection)
    if (d_->items.count(id)) keep.push_back(id);
  d_->setSel(keep, true);
  if (first && d_->ir) zoomToSheet();
}

void OverlayView::setBackground(const QImage& img, const Affine2& pxToSheet) {
  d_->bgImage = img;
  d_->bgXform = pxToSheet;
  d_->hasBg = !img.isNull();
  refresh();
}
void OverlayView::clearBackground() {
  d_->bgImage = QImage();
  d_->hasBg = false;
  refresh();
}

void OverlayView::setLayers(uint32_t mask) {
  if (mask == d_->layers) return;
  d_->layers = mask;
  refresh();
}
uint32_t OverlayView::layers() const { return d_->layers; }
void OverlayView::setTool(OverlayTool t) {
  d_->resetInteraction();
  d_->tool = t;
  setCursor(t == OverlayTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
}
OverlayTool OverlayView::tool() const { return d_->tool; }
void OverlayView::setStage(Stage s) { d_->stage = s; }
void OverlayView::setCurrentSketch(int id) {
  if (id == d_->sketch) return;
  d_->sketch = id;
  if (d_->layers & LayerSketch) refresh();
}

void OverlayView::setSelection(const std::vector<int>& ids) { d_->setSel(ids, false); }
std::vector<int> OverlayView::selection() const { return d_->selection; }
void OverlayView::highlight(const std::vector<int>& ids) {
  d_->highlighted = ids;
  d_->applySelectionPens();
}

void OverlayView::zoomToSheet() {
  const QRectF r = d_->sheetRect();
  fitInView(r.adjusted(-2, -2, 2, 2), Qt::KeepAspectRatio);
}
void OverlayView::zoomToBox(const Box2& b, double marginMm) {
  if (isEmpty(b)) return;
  fitInView(R(b).adjusted(-marginMm, -marginMm, marginMm, marginMm), Qt::KeepAspectRatio);
}

QImage OverlayView::renderToImage(int widthPx) {
  const QRectF src = d_->sheetRect();
  const int w = std::max(1, widthPx);
  const int h = std::max(1, static_cast<int>(std::lround(w * src.height() / std::max(1e-9, src.width()))));
  QImage img(w, std::min(h, 20000), QImage::Format_RGB32);
  img.fill(Qt::white);
  // cosmetic pens are in device px: render at the target scale through a temporary view-independent painter
  QPainter p(&img);
  p.setRenderHint(QPainter::Antialiasing);
  scene()->render(&p, QRectF(0, 0, img.width(), img.height()), src, Qt::KeepAspectRatio);
  p.end();
  return img;
}

// ------------------------------------------------------------------------------------------------ events
bool OverlayView::Impl::onWheel(QWheelEvent* e) {
  const double steps = e->angleDelta().y() / 120.0;
  if (steps == 0) return false;
  const double f = std::pow(1.25, steps);
  const double s = q->transform().m11() * f;
  if (s < 1e-3 || s > 1e4) return true;
  q->scale(f, f);
  return true;
}

bool OverlayView::Impl::onPress(QMouseEvent* e) {
  auto& d = *this;
  if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && (e->modifiers() & Qt::AltModifier))) {
    d.panning = true;
    d.panLast = e->pos();
    q->setCursor(Qt::ClosedHandCursor);
    return true;
  }
  if (e->button() == Qt::RightButton) { d.resetInteraction(); return true; }
  if (e->button() != Qt::LeftButton || !d.ir) return false;
  const QPointF s = q->mapToScene(e->pos());
  d.pressing = true;
  d.pressScene = s;
  switch (d.tool) {
    case OverlayTool::EditBoxes: {
      const auto h = d.hitBox(s);
      d.boxKind = h.kind;
      d.boxViewId = h.viewId;
      d.boxStart = h.rect;
      d.boxCorner = h.corner;
      d.boxMode = h.kind == 0 ? 3 : (h.corner >= 0 ? 2 : 1);
      break;
    }
    case OverlayTool::Snap:
      if (!d.hitEnd(s, d.snapId, d.snapEnd)) d.snapId = -1;
      break;
    default: break;
  }
  return true;
}

bool OverlayView::Impl::onMove(QMouseEvent* e) {
  auto& d = *this;
  const QPointF s = q->mapToScene(e->pos());
  emit q->cursorMoved(s.x(), -s.y());
  if (d.panning) {
    const QPoint dd = e->pos() - d.panLast;
    d.panLast = e->pos();
    q->horizontalScrollBar()->setValue(q->horizontalScrollBar()->value() - dd.x());
    q->verticalScrollBar()->setValue(q->verticalScrollBar()->value() - dd.y());
    return true;
  }
  if (!d.ir) return false;
  switch (d.tool) {
    case OverlayTool::Select:
      if (d.pressing && QLineF(q->mapFromScene(d.pressScene), e->pos()).length() > 4) {
        if (!d.rubber) {
          d.rubber = q->scene()->addRect(QRectF(), cpen(QColor(0, 120, 255), 1, Qt::DashLine), QColor(0, 120, 255, 30));
          d.rubber->setZValue(200);
          d.rubber->setData(kKindKey, KDeco);
        }
        d.rubber->setRect(QRectF(d.pressScene, s).normalized());
      }
      break;
    case OverlayTool::EditBoxes:
      if (d.pressing && d.boxMode) {
        QPainterPath pp;
        pp.addRect(d.draggedRect(s));
        d.setPreview(pp);
      }
      break;
    case OverlayTool::Snap:
      if (d.pressing && d.snapId >= 0) {
        Vec2 a;
        if (const Primitive* p = findPrim(d.sheet(), d.snapId)) primEnd(*p, 1 - d.snapEnd, a);
        QPainterPath pp;
        pp.moveTo(S(a));
        pp.lineTo(s);
        d.setPreview(pp);
      }
      break;
    case OverlayTool::DrawLine:
    case OverlayTool::DrawArc:
    case OverlayTool::DrawCircle:
      if (!d.clicks.empty()) {
        QPainterPath pp;
        if (d.tool == OverlayTool::DrawCircle) {
          const double r = distance(d.clicks[0], V(s));
          pp.addEllipse(S(d.clicks[0]), r, r);
        } else {
          pp.moveTo(S(d.clicks[0]));
          for (std::size_t i = 1; i < d.clicks.size(); ++i) pp.lineTo(S(d.clicks[i]));
          pp.lineTo(s);
        }
        d.setPreview(pp);
      }
      break;
    default: break;
  }
  return false;
}

bool OverlayView::Impl::onRelease(QMouseEvent* e) {
  auto& d = *this;
  if (d.panning && (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton)) {
    d.panning = false;
    q->setCursor(d.tool == OverlayTool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    return true;
  }
  if (e->button() != Qt::LeftButton || !d.ir || !d.pressing) return false;
  d.pressing = false;
  const QPointF s = q->mapToScene(e->pos());
  const bool click = QLineF(q->mapFromScene(d.pressScene), e->pos()).length() <= 4;
  const Vec2 p = V(s);
  switch (d.tool) {
    case OverlayTool::Select: {
      std::vector<int> ids;
      if (d.rubber) {
        ids = d.idsInRect(d.rubber->rect());
        q->scene()->removeItem(d.rubber);
        delete d.rubber;
        d.rubber = nullptr;
      } else {
        const int id = d.pick(s);
        if (id >= 0) ids.push_back(id);
      }
      if (e->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) {
        std::vector<int> cur = d.selection;
        for (int id : ids) {
          auto it = std::find(cur.begin(), cur.end(), id);
          if (it != cur.end() && ids.size() == 1) cur.erase(it); else cur.push_back(id);
        }
        ids = cur;
      }
      d.setSel(ids, true);
      break;
    }
    case OverlayTool::EditBoxes: {
      const QRectF r = d.draggedRect(s);
      const int mode = d.boxMode;
      d.clearPreview();
      d.boxMode = 0;
      if (mode == 3) {
        if (r.width() > d.pxToScene(5) && r.height() > d.pxToScene(5))
          d.emitEdit(edit_op::addView, {{"box", J(B(r))}, {"role", "Unknown"}, {"label", ""}});
      } else if (mode && !click) {
        const json box = J(B(r));
        if (d.boxKind == KView) d.emitEdit(edit_op::setViewBox, {{"viewId", d.boxViewId}, {"box", box}});
        else if (d.boxKind == KLayoutTitle) d.emitEdit(edit_op::setTitleBlockBox, {{"box", box}});
        else if (d.boxKind == KLayoutBorder) d.emitEdit(edit_op::setBorder, {{"box", box}});
      } else if (mode && click && d.boxKind == KView) {
        d.setSel({d.boxViewId}, true);
      }
      break;
    }
    case OverlayTool::Snap: {
      d.clearPreview();
      if (d.snapId >= 0 && !click) {
        // snap target: nearest other end point within tolerance, else the release point
        Vec2 to = p;
        int oid = -1, oend = 0;
        if (d.hitEnd(s, oid, oend) && oid != d.snapId)
          if (const Primitive* o = findPrim(d.sheet(), oid)) primEnd(*o, oend, to);
        d.emitEdit(edit_op::snapPoint, {{"id", d.snapId}, {"end", d.snapEnd}, {"to", J(to)}});
      }
      d.snapId = -1;
      break;
    }
    case OverlayTool::DrawLine:
    case OverlayTool::DrawCircle:
    case OverlayTool::DrawArc: {
      if (!click) break;
      d.clicks.push_back(p);
      if (d.tool == OverlayTool::DrawLine && d.clicks.size() == 2) {
        if (distance(d.clicks[0], d.clicks[1]) > 1e-6)
          d.emitEdit(edit_op::addPrim, {{"kind", "Line"}, {"p0", J(d.clicks[0])}, {"p1", J(d.clicks[1])}});
        d.resetInteraction();
      } else if (d.tool == OverlayTool::DrawCircle && d.clicks.size() == 2) {
        const double r = distance(d.clicks[0], d.clicks[1]);
        if (r > 1e-6) d.emitEdit(edit_op::addPrim, {{"kind", "Circle"}, {"center", J(d.clicks[0])}, {"radius", r}});
        d.resetInteraction();
      } else if (d.tool == OverlayTool::DrawArc && d.clicks.size() == 3) {
        const Vec2 a = d.clicks[0], b = d.clicks[1], c = d.clicks[2];
        const double den = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
        if (std::abs(den) > 1e-9) {
          const double a2 = dot(a, a), b2 = dot(b, b), c2 = dot(c, c);
          const Vec2 o{(a2 * (b.y - c.y) + b2 * (c.y - a.y) + c2 * (a.y - b.y)) / den,
                       (a2 * (c.x - b.x) + b2 * (a.x - c.x) + c2 * (b.x - a.x)) / den};
          auto ang = [&](Vec2 q) { double t = std::atan2(q.y - o.y, q.x - o.x) * 180 / kPi; return t < 0 ? t + 360 : t; };
          double s0 = ang(a), sm = ang(b), s1 = ang(c);
          // CCW from s0 must pass through sm before s1; otherwise the arc runs CCW from c to a
          auto ccw = [](double from, double to) { double x = to - from; while (x < 0) x += 360; return x; };
          if (ccw(s0, sm) > ccw(s0, s1)) std::swap(s0, s1);
          double e1 = s0 + ccw(s0, s1);
          d.emitEdit(edit_op::addPrim, {{"kind", "Arc"}, {"center", J(o)}, {"radius", distance(o, a)}, {"a0", s0}, {"a1", e1}});
        }
        d.resetInteraction();
      }
      break;
    }
    case OverlayTool::PickGeometry: {
      if (!click) break;
      int kind = 0;
      const int id = d.pick(s, &kind);
      if (id < 0 || kind != KPrim) break;
      d.pickedIds.push_back(id);
      d.clicks.push_back(p);
      d.highlighted = d.pickedIds;
      d.applySelectionPens();
      if (d.pickedIds.size() == 2) {
        const auto ids = d.pickedIds;
        const auto pts = d.clicks;
        d.resetInteraction();
        d.highlighted.clear();
        d.applySelectionPens();
        emit q->geometryPicked(ids, pts);
      }
      break;
    }
  }
  return true;
}

bool OverlayView::Impl::onDouble(QMouseEvent* e) {
  if (e->button() == Qt::LeftButton && ir && tool == OverlayTool::Select) {
    const int id = pick(q->mapToScene(e->pos()));
    if (id >= 0) emit q->elementActivated(id);
    return true;
  }
  if (e->button() == Qt::LeftButton && tool == OverlayTool::PickGeometry && pickedIds.size() == 1) {
    // double click on one element: single-geometry binding (circle / arc / one edge)
    const auto ids = pickedIds;
    const auto pts = clicks;
    resetInteraction();
    highlighted.clear();
    applySelectionPens();
    emit q->geometryPicked(ids, pts);
    return true;
  }
  return false;
}

bool OverlayView::Impl::onKey(QKeyEvent* e) {
  auto& d = *this;
  if (e->key() == Qt::Key_Escape) { d.resetInteraction(); return true; }
  if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) && d.ir && !d.selection.empty() &&
      d.tool == OverlayTool::Select) {
    json prims = json::array(), texts = json::array();
    for (int id : d.selection) {
      if (findPrim(d.sheet(), id)) prims.push_back(id);
      else if (findText(d.sheet(), id)) texts.push_back(id);
      else if (findView(d.ir->layout, id)) d.emitEdit(edit_op::deleteView, {{"viewId", id}});
      else if (findDimension(d.ir->annot, id)) d.emitEdit(edit_op::deleteDimension, {{"dimId", id}});
    }
    if (!prims.empty()) d.emitEdit(edit_op::deletePrims, {{"ids", prims}});
    if (!texts.empty()) d.emitEdit(edit_op::deleteTexts, {{"ids", texts}});
    return true;
  }
  if (e->key() == Qt::Key_Home || (e->key() == Qt::Key_F && !e->modifiers())) { q->zoomToSheet(); return true; }
  return false;
}



bool OverlayView::Impl::dispatch(QEvent* e) {
  switch (e->type()) {
    case QEvent::Wheel: return onWheel(static_cast<QWheelEvent*>(e));
    case QEvent::MouseButtonPress: return onPress(static_cast<QMouseEvent*>(e));
    case QEvent::MouseMove: return onMove(static_cast<QMouseEvent*>(e));
    case QEvent::MouseButtonRelease: return onRelease(static_cast<QMouseEvent*>(e));
    case QEvent::MouseButtonDblClick: return onDouble(static_cast<QMouseEvent*>(e));
    case QEvent::KeyPress: return onKey(static_cast<QKeyEvent*>(e));
    default: return false;
  }
}

}  // namespace pc::drawing::ui
