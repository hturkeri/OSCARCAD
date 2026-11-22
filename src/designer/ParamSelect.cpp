// "Seçimden parametre" — pure logic. See ParamSelect.h.
#include "ParamSelect.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <QPointF>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

using namespace pc;

namespace pcd::psel {

namespace {
QString fmt(double v) { return QString::fromStdString(formatNumber(std::round(v * 1e4) / 1e4)); }

struct Geo {
  const Sketch& sk;
  QPointF P(int id) const {
    const SkPoint* p = sk.point(id);
    return p ? QPointF(p->x, p->y) : QPointF();
  }
  double len(const SkEntity& e) const {
    const QPointF a = P(e.points[0]), b = P(e.points[1]);
    return std::hypot(b.x() - a.x(), b.y() - a.y());
  }
  double rad(const SkEntity& e) const {
    if (e.type == SkEntityType::Circle) return e.radius;
    const QPointF c = P(e.points[0]), s = P(e.points[1]);
    return std::hypot(s.x() - c.x(), s.y() - c.y());
  }
};

double normDeg(double d) {
  while (d > 180) d -= 360;
  while (d <= -180) d += 360;
  return d;
}

// Existing dimension equivalent to (type, refs) -> id and how the parameter enters it.
int findExisting(const Sketch& sk, SkConstraintType type, const std::vector<int>& refs, std::string* scale) {
  using T = SkConstraintType;
  auto lineOf = [&](int a, int b) -> int {
    for (const auto& e : sk.entities)
      if (e.type == SkEntityType::Line && e.points.size() >= 2 &&
          ((e.points[0] == a && e.points[1] == b) || (e.points[0] == b && e.points[1] == a)))
        return e.id;
    return -1;
  };
  for (const auto& c : sk.constraints) {
    if (!isDimensional(c.type)) continue;
    *scale = "";
    switch (type) {
      case T::Length:
        if (c.type == T::Length && c.refs == refs) return c.id;
        if (c.type == T::Distance && c.refs.size() == 2 && lineOf(c.refs[0], c.refs[1]) == refs[0]) return c.id;
        break;
      case T::Distance:
        if (c.type == T::Distance && c.refs.size() == 2 &&
            ((c.refs[0] == refs[0] && c.refs[1] == refs[1]) || (c.refs[0] == refs[1] && c.refs[1] == refs[0])))
          return c.id;
        if (c.type == T::Length && !c.refs.empty() && lineOf(refs[0], refs[1]) == c.refs[0]) return c.id;
        break;
      case T::DistanceX: case T::DistanceY:
        if (c.type == type && c.refs == refs) return c.id;
        if (c.type == type && c.refs.size() == 2 && c.refs[0] == refs[1] && c.refs[1] == refs[0]) { *scale = "neg"; return c.id; }
        break;
      case T::Diameter: case T::Radius:
        if ((c.type == T::Diameter || c.type == T::Radius) && !c.refs.empty() && c.refs[0] == refs[0]) {
          if (c.type != type) *scale = type == T::Diameter ? "half" : "double";
          return c.id;
        }
        break;
      default:
        if (c.type == type && c.refs == refs) return c.id;
        break;
    }
  }
  scale->clear();
  return -1;
}

std::string exprFor(const std::string& param, const std::string& scale) {
  if (scale == "half") return param + "/2";
  if (scale == "double") return "2*" + param;
  if (scale == "neg") return "-" + param;
  return param;
}

void finish(Measure& m, const ParamValues& vals) {
  // common current expression / bound parameter of the targets
  QString common;
  bool same = true;
  for (int i = 0; i < m.targets.size(); ++i) {
    const QString e = QString::fromStdString(m.targets[i].currentExpr);
    if (i == 0) common = e;
    else if (e != common) same = false;
  }
  m.currentExpr = same ? common : QString();
  m.boundParam.clear();
  if (same && !common.isEmpty()) {
    bool allPlain = true;
    for (const auto& t : m.targets) allPlain = allPlain && t.scale.empty();
    try {
      if (auto id = singleIdentifier(common.toStdString()); id && vals.count(*id) && allPlain) m.boundParam = QString::fromStdString(*id);
    } catch (...) {}
  }
}

Target dimTarget(const Sketch& sk, SkConstraintType type, std::vector<int> refs) {
  Target t;
  t.kind = Target::Kind::SketchDim;
  t.type = type;
  t.refs = std::move(refs);
  t.constraintId = findExisting(sk, type, t.refs, &t.scale);
  if (t.constraintId >= 0)
    for (const auto& c : sk.constraints)
      if (c.id == t.constraintId) t.currentExpr = c.valueExpr;
  return t;
}

// Rectangle side (4 axis-aligned lines in a closed loop): 'W' for horizontal, 'H' for vertical, 0 otherwise.
char rectangleSide(const Sketch& sk, int lineId) {
  std::map<int, int> parent;
  std::function<int(int)> find = [&](int x) -> int {
    auto it = parent.find(x);
    if (it == parent.end() || it->second == x) return x;
    return it->second = find(it->second);
  };
  for (const auto& c : sk.constraints)
    if (c.type == SkConstraintType::Coincident && c.refs.size() == 2) parent[find(c.refs[0])] = find(c.refs[1]);
  std::vector<const SkEntity*> lines;
  for (const auto& e : sk.entities)
    if (e.type == SkEntityType::Line && !e.construction && e.points.size() >= 2) lines.push_back(&e);
  const SkEntity* l0 = nullptr;
  for (auto* l : lines) if (l->id == lineId) l0 = l;
  if (!l0) return 0;
  const Geo g{sk};
  auto axis = [&](const SkEntity* l) {
    const QPointF a = g.P(l->points[0]), b = g.P(l->points[1]);
    const double L = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (L < 1e-9) return 0;
    if (std::fabs(b.y() - a.y()) < 1e-6 * L) return 1;
    if (std::fabs(b.x() - a.x()) < 1e-6 * L) return 2;
    return 0;
  };
  const int start = find(l0->points[0]);
  // depth-first walk of length 4 back to `start`
  std::function<bool(const SkEntity*, int, int, std::set<int>&)> walk = [&](const SkEntity* cur, int node, int depth, std::set<int>& used) -> bool {
    if (depth == 4) return node == start;
    for (auto* l : lines) {
      if (used.count(l->id) || !axis(l)) continue;
      const int a = find(l->points[0]), b = find(l->points[1]);
      if (a != node && b != node) continue;
      if (axis(l) == axis(cur)) continue;   // corners alternate horizontal / vertical
      used.insert(l->id);
      if (walk(l, a == node ? b : a, depth + 1, used)) return true;
      used.erase(l->id);
    }
    return false;
  };
  if (!axis(l0)) return 0;
  std::set<int> used{l0->id};
  if (!walk(l0, find(l0->points[1]), 1, used)) return 0;
  return axis(l0) == 1 ? 'W' : 'H';
}

QString typeLabel(SkConstraintType t) {
  switch (t) {
    case SkConstraintType::Length: return "Uzunluk";
    case SkConstraintType::Distance: return "Mesafe";
    case SkConstraintType::DistanceX: return "Yatay mesafe";
    case SkConstraintType::DistanceY: return "Dikey mesafe";
    case SkConstraintType::PointLineDistance: return "Nokta-çizgi mesafesi";
    case SkConstraintType::Radius: return "Yarıçap";
    case SkConstraintType::Diameter: return "Çap";
    case SkConstraintType::Angle: return "Açı";
    case SkConstraintType::Coincident: return "Çakışık";
    case SkConstraintType::Horizontal: return "Yatay";
    case SkConstraintType::Vertical: return "Dikey";
    case SkConstraintType::HorizontalPts: return "Yatay (noktalar)";
    case SkConstraintType::VerticalPts: return "Dikey (noktalar)";
    case SkConstraintType::Parallel: return "Paralel";
    case SkConstraintType::Perpendicular: return "Dik";
    case SkConstraintType::Tangent: return "Teğet";
    case SkConstraintType::Equal: return "Eşit";
    case SkConstraintType::Fixed: return "Sabit";
    case SkConstraintType::PointOnEntity: return "Üzerinde";
    case SkConstraintType::Midpoint: return "Orta nokta";
    case SkConstraintType::Symmetric: return "Simetrik";
    case SkConstraintType::Concentric: return "Eş merkezli";
  }
  return "?";
}
QString typePrefix(SkConstraintType t) {
  switch (t) {
    case SkConstraintType::Radius: return "R";
    case SkConstraintType::Diameter: return "D";
    case SkConstraintType::Angle: return "A";
    case SkConstraintType::DistanceX: return "DX";
    case SkConstraintType::DistanceY: return "DY";
    default: return "L";
  }
}
} // namespace

QString dimensionTypeLabel(SkConstraintType t) { return typeLabel(t); }

QString Measure::valueText() const {
  return kind == ParamKind::Angle ? fmt(value) + QString::fromUtf8("°") : fmt(value) + " mm";
}
QString Measure::text() const {
  QString sym = key == "D" ? QString::fromUtf8("Ø") : key;
  if (key == "D") return boundParam.isEmpty() ? sym + " " + valueText() : sym + " " + boundParam + " = " + valueText();
  if (!boundParam.isEmpty()) return label + ": " + boundParam + " = " + valueText();
  return label + " = " + valueText();
}

QString constraintText(const Sketch& sk, const ParamValues& vals, int cid) {
  for (const auto& c : sk.constraints) {
    if (c.id != cid) continue;
    QString s = typeLabel(c.type);
    if (isDimensional(c.type)) {
      s += " " + QString::fromStdString(c.valueExpr);
      try {
        const double v = evalWith(c.valueExpr, vals);
        if (QString::fromStdString(c.valueExpr) != fmt(v)) s += " = " + fmt(v);
      } catch (...) {}
    }
    return s + QString(" (#%1)").arg(cid);
  }
  return QString("#%1").arg(cid);
}

// =========================================================================================== sketch measures
QList<Measure> sketchMeasures(const Sketch& sk, const ParamValues& vals, const std::vector<int>& sel) {
  QList<Measure> out;
  const Geo g{sk};
  std::vector<int> pts, lines, circles, arcs, dims;
  for (int id : sel) {
    if (const SkEntity* e = sk.entity(id)) {
      (e->type == SkEntityType::Line ? lines : e->type == SkEntityType::Circle ? circles : arcs).push_back(id);
    } else if (sk.point(id)) {
      pts.push_back(id);
    } else {
      for (const auto& c : sk.constraints) if (c.id == id && isDimensional(c.type)) dims.push_back(id);
    }
  }
  const size_t total = pts.size() + lines.size() + circles.size() + arcs.size() + dims.size();
  if (total == 0) return out;

  // ---- one existing dimension label: re-bind it
  if (dims.size() == 1 && total == 1) {
    const SkConstraint* c = nullptr;
    for (const auto& k : sk.constraints) if (k.id == dims[0]) c = &k;
    Measure m;
    m.key = "dim";
    m.label = typeLabel(c->type);
    m.prefix = typePrefix(c->type);
    m.kind = c->type == SkConstraintType::Angle ? ParamKind::Angle : ParamKind::Length;
    try { m.value = evalWith(c->valueExpr, vals); } catch (...) { m.value = 0; }
    Target t;
    t.type = c->type;
    t.refs = c->refs;
    t.constraintId = c->id;
    t.currentExpr = c->valueExpr;
    if (c->type == SkConstraintType::Length && !c->refs.empty()) {
      if (const char side = rectangleSide(sk, c->refs[0])) { m.prefix = QString(QChar(side)); m.barePrefix = true; }
    }
    m.targets << t;
    m.where = "ölçü";
    finish(m, vals);
    out << m;
    return out;
  }

  // ---- circles / arcs only: diameter + radius (same parameter for all of them)
  if (!circles.empty() || !arcs.empty()) {
    if (pts.empty() && lines.empty() && dims.empty()) {
      std::vector<int> curves = circles;
      curves.insert(curves.end(), arcs.begin(), arcs.end());
      auto make = [&](SkConstraintType type) {
        Measure m;
        const bool dia = type == SkConstraintType::Diameter;
        m.key = dia ? "D" : "R";
        m.label = dia ? "Çap" : "Yarıçap";
        m.prefix = m.key;
        m.alt = dia ? "R" : "D";
        const SkEntity* e0 = sk.entity(curves[0]);
        m.value = (dia ? 2.0 : 1.0) * g.rad(*e0);
        for (int id : curves) m.targets << dimTarget(sk, type, {id});
        m.where = curves.size() == 1 ? QString() : QString("%1 eğri").arg(curves.size());
        finish(m, vals);
        return m;
      };
      if (!circles.empty()) out << make(SkConstraintType::Diameter) << make(SkConstraintType::Radius);
      else out << make(SkConstraintType::Radius) << make(SkConstraintType::Diameter);
      if (curves.size() == 1 && !arcs.empty()) {
        const SkEntity* a = sk.entity(arcs[0]);
        const QPointF c = g.P(a->points[0]), s = g.P(a->points[1]), en = g.P(a->points[2]);
        double sw = (std::atan2(en.y() - c.y(), en.x() - c.x()) - std::atan2(s.y() - c.y(), s.x() - c.x())) * 180 / M_PI;
        while (sw <= 0) sw += 360;
        while (sw > 360) sw -= 360;
        Measure m;
        m.key = "sweep";
        m.label = "Yay açısı";
        m.prefix = "A";
        m.kind = ParamKind::Angle;
        m.value = sw;
        m.canParam = false;
        m.why = "Yay açısı için sürücü ölçü türü yok: yarıçapı ve uç noktalarının konumunu ölçülendirin.";
        out << m;
      }
    }
    return out;
  }

  // ---- lines only
  if (!lines.empty() && pts.empty() && dims.empty()) {
    if (lines.size() == 2) {
      const SkEntity *l1 = sk.entity(lines[0]), *l2 = sk.entity(lines[1]);
      const QPointF a = g.P(l1->points[1]) - g.P(l1->points[0]), b = g.P(l2->points[1]) - g.P(l2->points[0]);
      const double la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
      const double cr = la > 1e-12 && lb > 1e-12 ? (a.x() * b.y() - a.y() * b.x()) / (la * lb) : 0;
      if (std::fabs(cr) < 1e-6) {
        Measure m;
        m.key = "pld";
        m.label = "Paralel mesafe";
        m.prefix = "L";
        const QPointF A = g.P(l2->points[0]), B = g.P(l2->points[1]), Q = g.P(l1->points[0]);
        m.value = lb > 1e-12 ? std::fabs((Q.x() - A.x()) * b.y() - (Q.y() - A.y()) * b.x()) / lb : 0;
        m.targets << dimTarget(sk, SkConstraintType::PointLineDistance, {l1->points[0], l2->id});
        finish(m, vals);
        out << m;
      } else {
        Measure m;
        m.key = "angle";
        m.label = "Açı";
        m.prefix = "A";
        m.kind = ParamKind::Angle;
        m.value = normDeg((std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180 / M_PI);
        m.targets << dimTarget(sk, SkConstraintType::Angle, {l1->id, l2->id});
        finish(m, vals);
        out << m;
      }
    }
    Measure m;
    m.key = "L";
    m.label = "Uzunluk";
    m.prefix = "L";
    char side = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
      const char s = rectangleSide(sk, lines[i]);
      if (i == 0) side = s;
      else if (s != side) side = 0;
    }
    if (side) { m.prefix = QString(QChar(side)); m.barePrefix = true; }
    m.value = g.len(*sk.entity(lines[0]));
    for (int id : lines) m.targets << dimTarget(sk, SkConstraintType::Length, {id});
    if (lines.size() > 1) m.where = QString("%1 çizgi").arg(lines.size());
    finish(m, vals);
    out << m;
    return out;
  }

  // ---- points (+ one line)
  if (dims.empty() && circles.empty() && arcs.empty()) {
    if (pts.size() == 1 && lines.size() == 1) {
      const SkEntity* l = sk.entity(lines[0]);
      const QPointF A = g.P(l->points[0]), B = g.P(l->points[1]), Q = g.P(pts[0]);
      const double dx = B.x() - A.x(), dy = B.y() - A.y(), L = std::hypot(dx, dy);
      Measure m;
      m.key = "pld";
      m.label = "Nokta-çizgi mesafesi";
      m.prefix = "L";
      m.value = L > 1e-12 ? std::fabs((Q.x() - A.x()) * dy - (Q.y() - A.y()) * dx) / L : 0;
      m.targets << dimTarget(sk, SkConstraintType::PointLineDistance, {pts[0], l->id});
      finish(m, vals);
      out << m;
      return out;
    }
    if (lines.empty() && pts.size() == 1 && pts[0] != 0) {
      const QPointF p = g.P(pts[0]);
      for (int axis = 0; axis < 2; ++axis) {
        Measure m;
        m.key = axis == 0 ? "x" : "y";
        m.label = axis == 0 ? "X (orijinden)" : "Y (orijinden)";
        m.prefix = axis == 0 ? "X" : "Y";
        m.value = axis == 0 ? p.x() : p.y();
        m.targets << dimTarget(sk, axis == 0 ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {0, pts[0]});
        finish(m, vals);
        out << m;
      }
      return out;
    }
    if (lines.empty() && pts.size() == 2) {
      const QPointF a = g.P(pts[0]), b = g.P(pts[1]);
      Measure d;
      d.key = "dist";
      d.label = "Mesafe";
      d.prefix = "L";
      d.value = std::hypot(b.x() - a.x(), b.y() - a.y());
      d.targets << dimTarget(sk, SkConstraintType::Distance, {pts[0], pts[1]});
      finish(d, vals);
      out << d;
      for (int axis = 0; axis < 2; ++axis) {
        Measure m;
        m.key = axis == 0 ? "dx" : "dy";
        m.label = axis == 0 ? "Yatay mesafe" : "Dikey mesafe";
        m.prefix = axis == 0 ? "DX" : "DY";
        m.value = axis == 0 ? b.x() - a.x() : b.y() - a.y();
        m.targets << dimTarget(sk, axis == 0 ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {pts[0], pts[1]});
        finish(m, vals);
        out << m;
      }
      return out;
    }
  }
  return out;
}

// =========================================================================================== 3D measures
namespace {
QString stripSplit(QString n) {
  static const QRegularExpression re(":(\\d+)$");
  return n.remove(re);
}
Measure propMeasure(const Document& doc, const model::ModelResult& model, const Feature& f, const std::string& prop,
                    const QString& key, const QString& label, const QString& prefix, ParamKind kind, const std::string& def) {
  Measure m;
  m.key = key;
  m.label = label;
  m.prefix = prefix;
  m.kind = kind;
  Target t;
  t.kind = Target::Kind::FeatureProp;
  t.featureId = f.id;
  t.prop = prop;
  t.currentExpr = f.prop(prop, def);
  try { m.value = evalWith(t.currentExpr, model.params); } catch (...) { m.value = 0; }
  m.targets << t;
  m.where = QString::fromStdString(f.id);
  (void)doc;
  finish(m, model.params);
  return m;
}
const Sketch* sketchOf(const Document& doc, const model::ModelResult& model, const std::string& sid) {
  auto it = model.solvedSketches.find(sid);
  if (it != model.solvedSketches.end()) return &it->second;
  const Feature* f = doc.feature(sid);
  return f && f->sketch ? &*f->sketch : nullptr;
}
QList<Measure> entityMeasures(const Document& doc, const model::ModelResult& model, const std::string& sid, int eid) {
  QList<Measure> out;
  const Sketch* sk = sketchOf(doc, model, sid);
  const Feature* sf = doc.feature(sid);
  if (!sk || !sf || !sf->sketch || !sk->entity(eid)) return out;
  // targets are looked up in the DOCUMENT sketch (the one that is edited), values from the solved copy
  const QList<Measure> docM = sketchMeasures(*sf->sketch, model.params, {eid});
  const QList<Measure> solM = sketchMeasures(*sk, model.params, {eid});
  for (int i = 0; i < docM.size(); ++i) {
    Measure m = docM[i];
    if (m.key == "sweep") continue;
    if (i < solM.size() && solM[i].key == m.key) m.value = solM[i].value;
    for (auto& t : m.targets) t.sketchFeature = sid;
    m.where = QString::fromStdString(sid);
    out << m;
  }
  return out;
}
QList<Measure> faceMeasures(const Document& doc, const model::ModelResult& model, const QString& faceName, int depth = 0) {
  QList<Measure> out;
  const QString base = stripSplit(faceName);
  const int dot = base.indexOf('.');
  if (dot <= 0 || depth > 4) return out;
  const std::string fid = base.left(dot).toStdString();
  const QString rest = base.mid(dot + 1);
  const Feature* f = doc.feature(fid);
  if (!f) return out;
  static const QRegularExpression reSide("^side\\.e(\\d+)$"), reF("^f\\d+$"), reC("^c\\d+$"),
      reHole("^h\\d+\\.(wall|cb_wall|bottom|cb_floor|cs)$"), rePat("^i\\d+\\.(.+)$"), reInner("^inner\\.(.+)$");
  switch (f->type) {
    case FeatureType::Extrude: {
      if (rest == "end" || rest == "start") {
        out << propMeasure(doc, model, *f, "distance", "T", "Mesafe", "T", ParamKind::Length, "10");
        return out;
      }
      const auto m = reSide.match(rest);
      if (m.hasMatch()) {
        out << entityMeasures(doc, model, f->prop("sketch"), m.captured(1).toInt());
        Measure t = propMeasure(doc, model, *f, "distance", "T", "Yükseklik (mesafe)", "T", ParamKind::Length, "10");
        out << t;
      }
      return out;
    }
    case FeatureType::Revolve:
      out << propMeasure(doc, model, *f, "angle", "A", "Açı", "A", ParamKind::Angle, "360");
      return out;
    case FeatureType::Fillet:
      if (reF.match(rest).hasMatch()) out << propMeasure(doc, model, *f, "radius", "R", "Yarıçap", "R", ParamKind::Length, "1");
      return out;
    case FeatureType::Chamfer:
      if (reC.match(rest).hasMatch()) out << propMeasure(doc, model, *f, "distance", "C", "Pah mesafesi", "C", ParamKind::Length, "1");
      return out;
    case FeatureType::Hole: {
      const auto m = reHole.match(rest);
      if (!m.hasMatch()) return out;
      const QString part = m.captured(1);
      if (part == "wall") out << propMeasure(doc, model, *f, "diameter", "D", "Çap", "D", ParamKind::Length, "5");
      else if (part == "cb_wall") out << propMeasure(doc, model, *f, "cbDiameter", "D", "Havşa çapı", "D", ParamKind::Length, "10");
      else if (part == "bottom" && f->prop("through") != "1") out << propMeasure(doc, model, *f, "depth", "HD", "Derinlik", "HD", ParamKind::Length, "10");
      else if (part == "cb_floor") out << propMeasure(doc, model, *f, "cbDepth", "HD", "Havşa derinliği", "HD", ParamKind::Length, "3");
      return out;
    }
    case FeatureType::Shell:
      if (reInner.match(rest).hasMatch()) out << propMeasure(doc, model, *f, "thickness", "T", "Et kalınlığı", "T", ParamKind::Length, "1");
      return out;
    case FeatureType::Pattern: {
      const auto m = rePat.match(rest);
      if (m.hasMatch()) return faceMeasures(doc, model, m.captured(1), depth + 1);
      return out;
    }
    default: return out;
  }
}
// read-only geometric measure (no parameter mapping)
QList<Measure> geomEdge(const model::ModelResult& model, const QString& name) {
  QList<Measure> out;
  auto it = model.edges.find(name.toStdString());
  if (it == model.edges.end()) return out;
  try {
    BRepAdaptor_Curve c(it->second);
    Measure m;
    m.canParam = false;
    m.why = "Bu kenar bir sketch ölçüsüne ya da özellik alanına eşlenemiyor (yalnız ölçüm).";
    if (c.GetType() == GeomAbs_Circle) {
      m.key = "D";
      m.label = "Çap";
      m.value = 2 * c.Circle().Radius();
    } else {
      m.key = "L";
      m.label = "Uzunluk";
      m.value = GCPnts_AbscissaPoint::Length(c);
    }
    m.prefix = m.key;
    out << m;
  } catch (...) {}
  return out;
}
QList<Measure> geomFace(const model::ModelResult& model, const QString& name) {
  QList<Measure> out;
  auto it = model.faces.find(name.toStdString());
  if (it == model.faces.end()) return out;
  try {
    BRepAdaptor_Surface s(it->second);
    if (s.GetType() == GeomAbs_Cylinder) {
      Measure m;
      m.key = "D";
      m.label = "Çap";
      m.prefix = "D";
      m.value = 2 * s.Cylinder().Radius();
      m.canParam = false;
      m.why = "Bu yüz bir sketch ölçüsüne ya da özellik alanına eşlenemiyor (yalnız ölçüm).";
      out << m;
    }
  } catch (...) {}
  return out;
}
QList<Measure> edgeMeasures(const Document& doc, const model::ModelResult& model, const QString& edgeName) {
  static const QRegularExpression reSk("^sketch:([^:]+):e(\\d+)$"), reEdge("^edge\\((.*)\\)(:\\d+)?$"), reSide("^(.+)\\.side\\.e(\\d+)$");
  if (const auto m = reSk.match(edgeName); m.hasMatch()) return entityMeasures(doc, model, m.captured(1).toStdString(), m.captured(2).toInt());
  const auto m = reEdge.match(edgeName);
  if (!m.hasMatch()) return geomEdge(model, edgeName);
  const QStringList faces = m.captured(1).split('|');
  QList<Measure> out;
  if (faces.size() == 2) {
    const QString a = stripSplit(faces[0]), b = stripSplit(faces[1]);
    const auto sa = reSide.match(a), sb = reSide.match(b);
    // two side faces of one extrude: the edge runs along the extrusion
    if (sa.hasMatch() && sb.hasMatch() && sa.captured(1) == sb.captured(1)) {
      if (const Feature* f = doc.feature(sa.captured(1).toStdString()); f && f->type == FeatureType::Extrude)
        out << propMeasure(doc, model, *f, "distance", "T", "Uzunluk (Extrude mesafesi)", "T", ParamKind::Length, "10");
    }
    // side face + start/end face of the same extrude: the edge is the sketch curve itself
    for (int k = 0; k < 2 && out.isEmpty(); ++k) {
      const auto& s = k == 0 ? sa : sb;
      const QString other = k == 0 ? b : a;
      if (!s.hasMatch()) continue;
      const QString fid = s.captured(1);
      if (other == fid + ".start" || other == fid + ".end")
        if (const Feature* f = doc.feature(fid.toStdString()); f && f->type == FeatureType::Extrude)
          out << entityMeasures(doc, model, f->prop("sketch"), s.captured(2).toInt());
    }
    // a hole rim / fillet boundary: the feature of the curved face
    for (const QString& fc : {a, b}) {
      if (!out.isEmpty()) break;
      if (fc.contains(".h") && fc.endsWith(".wall")) out << faceMeasures(doc, model, fc);
    }
  }
  if (out.isEmpty()) out = geomEdge(model, edgeName);
  return out;
}
} // namespace

QList<Measure> modelMeasures(const Document& doc, const model::ModelResult& model, const QList<SelItem>& sel) {
  QList<QList<Measure>> per;
  for (const SelItem& it : sel) {
    QList<Measure> l;
    switch (it.kind) {
      case SelItem::Kind::Face:
        l = faceMeasures(doc, model, it.name);
        if (l.isEmpty()) l = geomFace(model, it.name);
        break;
      case SelItem::Kind::Edge:
      case SelItem::Kind::SketchCurve:
        l = edgeMeasures(doc, model, it.name);
        break;
      default: return {};
    }
    if (l.isEmpty()) return {};
    per << l;
  }
  if (per.isEmpty()) return {};
  if (per.size() == 1) return per.first();
  // several items: the measures every item offers, merged (one parameter for all of them)
  QList<Measure> out;
  for (const Measure& m0 : per.first()) {
    Measure m = m0;
    bool all = true;
    for (int i = 1; i < per.size() && all; ++i) {
      const Measure* o = nullptr;
      for (const Measure& x : per[i]) if (x.key == m0.key && x.kind == m0.kind) { o = &x; break; }
      if (!o) { all = false; break; }
      for (const Target& t : o->targets) if (!m.targets.contains(t)) m.targets << t;
      m.canParam = m.canParam && o->canParam;
      if (m.why.isEmpty()) m.why = o->why;
    }
    if (!all) continue;
    m.where = QString("%1 öğe").arg(sel.size());
    finish(m, model.params);
    out << m;
  }
  return out;
}

// =========================================================================================== names
QString checkName(const Document& doc, const QString& name) {
  if (!ParamTable::isValidName(name.toStdString())) return "Geçersiz ad: harf ya da _ ile başlamalı, yalnız harf/rakam/_ içermeli.";
  if (doc.params.find(name.toStdString())) return QString("'%1' adlı bir parametre zaten var.").arg(name);
  return QString();
}

QString suggestName(const Document& doc, const Measure& m, const QStringList& alsoTaken) {
  const QString p = m.prefix.isEmpty() ? QString("P") : m.prefix;
  auto free = [&](const QString& n) { return !doc.params.find(n.toStdString()) && !alsoTaken.contains(n); };
  if (m.barePrefix && free(p)) return p;
  for (int i = m.barePrefix ? 2 : 1;; ++i) {
    const QString n = p + QString::number(i);
    if (free(n)) return n;
  }
}

// =========================================================================================== binding
namespace {
std::set<int> involved(const Sketch& sk, const QList<Target>& ts) {
  std::set<int> ids;
  for (const auto& t : ts)
    for (int r : t.refs) {
      ids.insert(r);
      if (const SkEntity* e = sk.entity(r)) for (int p : e->points) ids.insert(p);
    }
  ids.erase(0);
  return ids;
}
bool touches(const Sketch& sk, const SkConstraint& c, const std::set<int>& ids) {
  for (int r : c.refs) {
    if (ids.count(r)) return true;
    if (const SkEntity* e = sk.entity(r)) for (int p : e->points) if (ids.count(p)) return true;
  }
  return false;
}
bool solvesCleanly(Sketch s, const ParamValues& vals) {
  const auto r = sketch::solve(s, vals);
  return r.ok() && r.status != sketch::SolveStatus::OverConstrained;
}

BindResult bindTargets(Sketch& sk, const ParamValues& vals, const QList<Target>& ts, const std::string& param,
                       const std::vector<int>& removeIds) {
  BindResult res;
  Sketch work = sk;
  if (!removeIds.empty()) {
    const std::set<int> dead(removeIds.begin(), removeIds.end());
    work.constraints.erase(std::remove_if(work.constraints.begin(), work.constraints.end(),
                                          [&](const SkConstraint& c) { return dead.count(c.id) > 0; }),
                           work.constraints.end());
  }
  std::set<int> fresh;
  for (const Target& t0 : ts) {
    Target t = t0;
    // the target may be stale (removed as a conflict / re-measured): look the dimension up again
    std::string scale;
    const int ex = findExisting(work, t.type, t.refs, &scale);
    if (ex >= 0) {
      for (auto& c : work.constraints) if (c.id == ex) c.valueExpr = exprFor(param, scale);
      ++res.rebound;
    } else {
      fresh.insert(work.addConstraint(t.type, t.refs, param));
      ++res.added;
    }
  }
  Sketch solved = work;
  const auto r = sketch::solve(solved, vals);
  if (r.ok() && r.status != sketch::SolveStatus::OverConstrained) {
    sk = solved;
    res.status = BindResult::Status::Ok;
    return res;
  }
  // conflict: which existing constraint(s) make the binding impossible? (solver hints first, then a trial search
  // over the constraints that touch the measured geometry)
  // candidate order (what a user expects to be told): other DIMENSIONS first (touching the measured geometry, then
  // the rest), then the solver's hints, then the geometric constraints that touch the measured geometry
  std::vector<int> cands;
  const std::set<int> ids = involved(work, ts);
  auto push = [&](int id) { if (!fresh.count(id) && std::find(cands.begin(), cands.end(), id) == cands.end()) cands.push_back(id); };
  for (const auto& c : work.constraints) if (isDimensional(c.type) && touches(work, c, ids)) push(c.id);
  for (const auto& c : work.constraints) if (isDimensional(c.type)) push(c.id);
  for (int id : r.conflicting) push(id);
  for (const auto& c : work.constraints) if (touches(work, c, ids)) push(c.id);
  if (cands.size() > 48) cands.resize(48);
  for (int id : cands) {
    Sketch t = work;
    t.constraints.erase(std::remove_if(t.constraints.begin(), t.constraints.end(), [&](const SkConstraint& c) { return c.id == id; }),
                        t.constraints.end());
    if (solvesCleanly(t, vals)) {
      res.conflicting.push_back(id);
      if (res.conflicting.size() >= 3) break;
    }
  }
  QStringList alt;
  for (size_t i = 1; i < res.conflicting.size(); ++i) alt << constraintText(work, vals, res.conflicting[i]);
  if (!res.conflicting.empty())
    res.conflictText = constraintText(work, vals, res.conflicting.front()) +
                       (alt.isEmpty() ? QString() : QString(" (başka seçenek: %1)").arg(alt.join(", ")));
  res.message = QString::fromStdString(r.message);
  res.status = res.conflicting.empty() ? BindResult::Status::Failed : BindResult::Status::Conflict;
  if (res.message.isEmpty())
    res.message = r.status == sketch::SolveStatus::OverConstrained ? "sketch aşırı kısıtlı olur" : "sketch çözülemedi";
  return res;
}
} // namespace

BindResult bindInSketch(Sketch& sk, const ParamValues& vals, const Measure& m, const std::string& param, const std::vector<int>& removeIds) {
  return bindTargets(sk, vals, m.targets, param, removeIds);
}

BindResult bindInDocument(Document& cand, const Measure& m, const std::string& param,
                          const std::map<std::string, std::vector<int>>& removeIds) {
  BindResult total;
  total.status = BindResult::Status::Ok;
  ParamValues vals;
  try { vals = cand.evaluateParams(); } catch (const std::exception& e) {
    total.status = BindResult::Status::Failed;
    total.message = QString::fromUtf8(e.what());
    return total;
  }
  std::map<std::string, QList<Target>> bySketch;
  for (const Target& t : m.targets) {
    if (t.kind == Target::Kind::FeatureProp) {
      Feature* f = cand.feature(t.featureId);
      if (!f) { total.status = BindResult::Status::Failed; total.message = "Özellik bulunamadı: " + QString::fromStdString(t.featureId); return total; }
      f->props[t.prop] = param;
      ++total.rebound;
    } else {
      bySketch[t.sketchFeature].push_back(t);
    }
  }
  for (auto& [sid, ts] : bySketch) {
    Feature* f = cand.feature(sid);
    if (!f || !f->sketch) { total.status = BindResult::Status::Failed; total.message = "Sketch bulunamadı: " + QString::fromStdString(sid); return total; }
    auto it = removeIds.find(sid);
    BindResult r = bindTargets(*f->sketch, vals, ts, param, it == removeIds.end() ? std::vector<int>() : it->second);
    total.added += r.added;
    total.rebound += r.rebound;
    if (r.status != BindResult::Status::Ok) {
      r.conflictSketch = sid;
      r.added = total.added;
      r.rebound = total.rebound;
      return r;
    }
  }
  return total;
}

bool documentUsesParameter(const Document& doc, const std::string& name) {
  auto uses = [&](const std::string& e) {
    if (e.empty()) return false;
    try {
      for (const auto& n : referencedNames(e)) if (n == name) return true;
    } catch (...) {}
    return false;
  };
  for (const auto& p : doc.params.all()) if (p.name != name && uses(p.expression)) return true;
  for (const auto& f : doc.features) {
    for (const auto& [k, v] : f.props) if (uses(v)) return true;
    if (f.sketch) {
      if (uses(f.sketch->plane.offsetExpr)) return true;
      for (const auto& c : f.sketch->constraints) if (uses(c.valueExpr)) return true;
    }
  }
  return false;
}

} // namespace pcd::psel
