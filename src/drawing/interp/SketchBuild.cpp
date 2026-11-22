// S6 — sketch reconstruction: clean closed profiles per view (planar arrangement + material classification),
// pc::Sketch entities, geometric constraints (DOF-checked), driving dimensions as named parameters, DOF closing
// with measured values, solve + deviation report.
#include "interp/Arrangement.h"
#include "interp/InterpUtil.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

namespace pc::drawing {
using namespace interp;

namespace {

// ===================================================================================== parameter naming registry
struct NameReg {
  std::set<std::string> used;
  std::map<std::string, int> next;
  std::string take(const std::string& prefix, bool numbered = true) {
    if (!numbered && !used.count(prefix) && ParamTable::isValidName(prefix)) { used.insert(prefix); return prefix; }
    for (int& k = next[prefix];;) {
      std::string n = prefix + std::to_string(++k);
      if (!used.count(n)) { used.insert(n); return n; }
    }
  }
  std::string takeExact(const std::string& n) {
    if (!used.count(n)) { used.insert(n); return n; }
    for (int k = 2;; ++k) { std::string m = n + "_" + std::to_string(k); if (!used.count(m)) { used.insert(m); return m; } }
  }
};

std::string dimLabel(const Dimension& d, const DrawingIR& ir) {
  const ViewRegion* v = findView(ir.layout, d.viewId);
  std::string vn = v ? (v->label.empty() ? std::string(labelTr(v->role)) : v->label) : "?";
  std::string s = "[D] \"" + d.text.raw;
  for (int t : d.tolTextIds) if (auto* tr = findText(ir.norm, t)) s += " " + tr->utf8;
  s += "\" — görünüş " + vn + ", sayfa " + std::to_string(ir.norm.page + 1);
  if (d.text.tolPlus || d.text.tolMinus) s += " (tol. ortası " + fmt(d.text.tolMid(), 4) + ")";
  if (d.outOfBand) s += " [geometriyle uyumsuz: ölçülen " + fmt(d.measuredPart, 3) + "]";
  return s;
}

// ===================================================================================== sketch builder
struct SkBuild {
  Sketch sk = Sketch::create();
  double tol = 1e-3;
  std::map<int, Vec2> orig;          // point id -> drawing coordinate
  std::map<int, int> ent2prim;
  ParamValues vals;
  std::vector<ParamProposal> params;
  std::map<int, int> c2dim;
  std::vector<int> measured;
  int pointAt(Vec2 p) {
    for (auto& q : sk.points) if (std::hypot(q.x - p.x, q.y - p.y) < tol) return q.id;
    int id = sk.addPoint(p.x, p.y);
    orig[id] = p;
    return id;
  }
  int dof() { return sketch::degreesOfFreedom(sk, vals); }
  // add constraint; keep it only if DOF drops (returns id or -1)
  int tryAdd(SkConstraintType t, std::vector<int> refs, const std::string& expr = "") {
    int before = dof();
    int id = sk.addConstraint(t, std::move(refs), expr);
    int after = dof();
    if (after >= 0 && before >= 0 && after < before) return id;
    sk.constraints.pop_back();
    return -1;
  }
  void addParam(const std::string& name, double value, ParamKind kind, ParamTag tag, const std::string& desc, int dimId,
                const Dimension* d = nullptr, bool tolMid = false) {
    ParamProposal pp;
    pp.param.name = name;
    pp.param.kind = kind;
    pp.tag = tag;
    pp.dimensionId = dimId;
    pp.nominalValue = value;
    pp.tolMidValue = value;
    if (d) {
      pp.tolPlus = d->text.tolPlus;
      pp.tolMinus = d->text.tolMinus;
      pp.tolMidValue = d->text.tolMid();
    }
    double v = tolMid ? pp.tolMidValue : value;
    pp.param.expression = kind == ParamKind::Count ? fmt(std::round(v), 0) : fmt(v, 6);
    pp.param.description = desc;
    if (tag == ParamTag::FromDimension) {   // dimension-driven: listed as an app input (Save as App), own group
      pp.param.exposed = true;
      pp.param.group = "Teknik resim";
    }
    if (kind == ParamKind::Length && v > 0) pp.param.minValue = 0;
    params.push_back(pp);
    vals[name] = v;
  }
  const SkEntity* entityWithPoint(int pid, SkEntityType t) const {
    for (auto& e : sk.entities) if (e.type == t && std::find(e.points.begin(), e.points.end(), pid) != e.points.end()) return &e;
    return nullptr;
  }
};

// map an arrangement / boundary segment set to sketch entities
void addSegs(SkBuild& B, const std::vector<ASeg>& segs) {
  for (auto& s : segs) {
    if (!s.arc) {
      int a = B.pointAt(s.a), b = B.pointAt(s.b);
      if (a == b) continue;
      int e = B.sk.addLine(a, b);
      if (s.src >= 0) B.ent2prim[e] = s.src;
    } else {
      Vec2 st = s.ccw ? s.a : s.b, en = s.ccw ? s.b : s.a;
      int c = B.pointAt(s.c), a = B.pointAt(st), b = B.pointAt(en);
      int e = B.sk.addArc(c, a, b);
      B.sk.entity(e)->radius = s.r;
      if (s.src >= 0) B.ent2prim[e] = s.src;
    }
  }
}

// geometric constraints (each kept only if it removes a DOF)
void autoConstraints(SkBuild& B, double angTolDeg, bool onAxisV) {
  const double sa = std::sin(deg2rad(angTolDeg));
  std::vector<SkEntity> ents = B.sk.entities;
  for (auto& e : ents) {
    if (e.type != SkEntityType::Line) continue;
    auto* p = B.sk.point(e.points[0]);
    auto* q = B.sk.point(e.points[1]);
    double dx = q->x - p->x, dy = q->y - p->y, L = std::hypot(dx, dy);
    if (std::abs(dy) <= sa * L) B.tryAdd(SkConstraintType::Horizontal, {e.id});
    else if (std::abs(dx) <= sa * L) B.tryAdd(SkConstraintType::Vertical, {e.id});
  }
  // tangency at shared points
  auto tangentAt = [&](const SkEntity& e, int pid) -> Vec2 {
    if (e.type == SkEntityType::Line) {
      auto* p = B.sk.point(e.points[0]); auto* q = B.sk.point(e.points[1]);
      return unit(Vec2{q->x - p->x, q->y - p->y});
    }
    auto* c = B.sk.point(e.points[0]); auto* p = B.sk.point(pid);
    return perp(unit(Vec2{p->x - c->x, p->y - c->y}));
  };
  for (size_t i = 0; i < ents.size(); ++i)
    for (size_t j = i + 1; j < ents.size(); ++j) {
      const SkEntity &a = ents[i], &b = ents[j];
      if (a.type == SkEntityType::Line && b.type == SkEntityType::Line) continue;
      if (a.type == SkEntityType::Circle || b.type == SkEntityType::Circle) continue;
      std::vector<int> pa(a.type == SkEntityType::Arc ? a.points.begin() + 1 : a.points.begin(), a.points.end());
      std::vector<int> pb(b.type == SkEntityType::Arc ? b.points.begin() + 1 : b.points.begin(), b.points.end());
      for (int p : pa)
        if (std::find(pb.begin(), pb.end(), p) != pb.end()) {
          if (std::abs(cross(tangentAt(a, p), tangentAt(b, p))) < std::sin(deg2rad(1.0))) B.tryAdd(SkConstraintType::Tangent, {a.id, b.id});
          break;
        }
    }
  // concentric arcs/circles with separate centre points, equal radii of circles
  for (size_t i = 0; i < ents.size(); ++i)
    for (size_t j = i + 1; j < ents.size(); ++j) {
      const SkEntity &a = ents[i], &b = ents[j];
      if (a.type == SkEntityType::Line || b.type == SkEntityType::Line) continue;
      if (a.points[0] != b.points[0]) {
        auto* ca = B.sk.point(a.points[0]); auto* cb = B.sk.point(b.points[0]);
        if (std::hypot(ca->x - cb->x, ca->y - cb->y) < B.tol * 5) B.tryAdd(SkConstraintType::Concentric, {a.id, b.id});
      }
      if (a.type == SkEntityType::Circle && b.type == SkEntityType::Circle && std::abs(a.radius - b.radius) < B.tol)
        B.tryAdd(SkConstraintType::Equal, {a.id, b.id});
    }
  if (onAxisV)
    for (auto& p : std::vector<SkPoint>(B.sk.points))
      if (p.id != 0 && std::abs(p.x) < B.tol) B.tryAdd(SkConstraintType::VerticalPts, {0, p.id});
}

void closeWithMeasured(SkBuild& B, NameReg& reg, double rounding, const std::string& viewName) {
  if (B.dof() <= 0) return;
  std::vector<SkPoint> pts(B.sk.points.begin(), B.sk.points.end());
  std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
  std::set<int> arcPts;
  for (auto& e : B.sk.entities) if (e.type == SkEntityType::Arc) for (int p : e.points) arcPts.insert(p);
  auto rnd = [&](double v, int pid) { return arcPts.count(pid) || rounding <= 0 ? v : std::round(v / rounding) * rounding; };
  for (auto& p : pts) {
    if (p.id == 0) continue;
    for (int axis = 0; axis < 2; ++axis) {
      if (B.dof() <= 0) return;
      double v = rnd(axis == 0 ? p.x : p.y, p.id);
      if (std::abs(v) < 1e-9) {   // on the origin's line: geometric constraint instead of a zero dimension
        int id = B.tryAdd(axis == 0 ? SkConstraintType::VerticalPts : SkConstraintType::HorizontalPts, {0, p.id});
        if (id >= 0) B.measured.push_back(id);
        continue;
      }
      std::string name = reg.take("L_olc");
      B.vals[name] = std::abs(v);
      std::string expr = v >= 0 ? name : "-" + name;
      int id = B.tryAdd(axis == 0 ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {0, p.id}, expr);
      if (id < 0) { B.vals.erase(name); reg.used.erase(name); reg.next["L_olc"]--; continue; }
      B.measured.push_back(id);
      B.addParam(name, std::abs(v), ParamKind::Length, ParamTag::Measured,
                 std::string("[Ö] geometriden ölçüldü (") + (axis == 0 ? "yatay" : "dikey") + " konum), görünüş " + viewName, -1);
    }
  }
  for (auto& e : B.sk.entities) {
    if (B.dof() <= 0) return;
    if (e.type != SkEntityType::Circle) continue;
    std::string name = reg.take("R_olc");
    B.vals[name] = e.radius;
    int id = B.tryAdd(SkConstraintType::Radius, {e.id}, name);
    if (id < 0) { B.vals.erase(name); reg.used.erase(name); reg.next["R_olc"]--; continue; }
    B.measured.push_back(id);
    B.addParam(name, rnd(e.radius, -1), ParamKind::Length, ParamTag::Measured, "[Ö] yarıçap geometriden ölçüldü, görünüş " + viewName, -1);
  }
}

void finishCandidate(SkBuild& B, SketchCandidate& c, double devTol) {
  c.dof = B.dof();
  Sketch solved = B.sk;
  sketch::SolveResult r = sketch::solve(solved, B.vals);
  c.conflicting = r.conflicting;
  if (r.converged) B.sk = solved;
  c.dof = r.dof >= 0 ? r.dof : c.dof;
  double maxDev = 0;
  std::set<int> movedPts;
  for (auto& [pid, o] : B.orig) {
    auto* p = B.sk.point(pid);
    if (!p) continue;
    double d = std::hypot(p->x - o.x, p->y - o.y);
    maxDev = std::max(maxDev, d);
    if (d > devTol) movedPts.insert(pid);
  }
  c.maxGeomDeviation = maxDev;
  for (auto& e : B.sk.entities)
    for (int p : e.points) if (movedPts.count(p)) { c.deviatingEntities.push_back(e.id); break; }
  c.sketch = B.sk;
  c.params = B.params;
  c.constraintToDimension = B.c2dim;
  c.entityToPrimitive = B.ent2prim;
  c.measuredConstraints = B.measured;
  c.attrs["interp.solveStatus"] = r.message;
  c.attrs["interp.converged"] = r.converged ? "1" : "0";
}

// boundary of material faces as segments
std::vector<ASeg> materialBoundary(const Arrangement& A, const std::vector<char>& material) {
  auto faceMat = [&](int cycle) -> bool {
    const auto& c = A.cycles[cycle];
    if (c.area > 0) return material[cycle];
    return c.container >= 0 ? material[c.container] : false;
  };
  std::vector<ASeg> out;
  for (size_t e = 0; e < A.edges.size(); ++e) {
    int h0 = 2 * (int)e, h1 = h0 + 1;
    bool m0 = faceMat(A.halves[h0].cycle), m1 = faceMat(A.halves[h1].cycle);
    if (m0 == m1) continue;
    ASeg s = A.halfSeg(m0 ? h0 : h1);    // material on the left -> CCW outer loops
    s.src = A.edges[e].srcs.empty() ? -1 : *std::max_element(A.edges[e].srcs.begin(), A.edges[e].srcs.end());
    for (size_t k = 0; k < A.edges[e].srcs.size(); ++k)
      if (A.edges[e].tags[k] == 0) { s.src = A.edges[e].srcs[k]; break; }
    out.push_back(s);
  }
  return out;
}

// merge collinear lines / co-circular arcs that meet end to end
std::vector<ASeg> simplify(std::vector<ASeg> segs, double tol) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < segs.size() && !changed; ++i)
      for (size_t j = 0; j < segs.size() && !changed; ++j) {
        if (i == j) continue;
        ASeg &a = segs[i], &b = segs[j];
        if (distance(a.b, b.a) > tol) continue;
        // the junction must not be shared by a third segment
        int deg = 0;
        for (auto& s : segs) deg += (distance(s.a, a.b) < tol) + (distance(s.b, a.b) < tol);
        if (deg != 2) continue;
        bool merge = false;
        if (!a.arc && !b.arc) merge = std::abs(cross(unit(a.b - a.a), unit(b.b - b.a))) < 1e-6 && dot(a.b - a.a, b.b - b.a) > 0;
        else if (a.arc && b.arc) merge = distance(a.c, b.c) < tol && std::abs(a.r - b.r) < tol && a.ccw == b.ccw;
        if (!merge) continue;
        ASeg m = a;
        m.b = b.b;
        if (a.arc && distance(m.a, m.b) < tol) continue;   // would become a full circle: keep two arcs
        if (m.src < 0) m.src = b.src;
        segs[i] = m;
        segs.erase(segs.begin() + j);
        changed = true;
      }
  }
  return segs;
}

// ------------------------------------------------------------------ view -> local mapping
struct ViewMap {
  const ViewRegion* v = nullptr;
  bool swap = false;       // (x,y) -> (y,x)
  bool mirrorU = false;    // revolve: fold negative radial side
  Vec2 toUV(Vec2 sheet) const {
    Vec2 p = transformPoint(v->sheetToView, sheet);
    Vec2 q = swap ? Vec2{p.y, p.x} : p;
    if (mirrorU) q.x = std::abs(q.x);
    return q;
  }
  Vec2 toUVnoMirror(Vec2 sheet) const { Vec2 p = transformPoint(v->sheetToView, sheet); return swap ? Vec2{p.y, p.x} : p; }
  double f() const { return viewScaleFactor(*v); }
};

ASeg primToSeg(const Primitive& p, const ViewMap& M, int tag) {
  ASeg s;
  s.src = p.id;
  s.tag = tag;
  if (p.kind == PrimKind::Line) { s.a = M.toUVnoMirror(p.p0); s.b = M.toUVnoMirror(p.p1); return s; }
  s.arc = true;
  s.c = M.toUVnoMirror(p.center);
  s.r = p.radius * M.f();
  if (p.kind == PrimKind::Circle) {
    s.a = M.toUVnoMirror({p.center.x + p.radius, p.center.y});
    s.b = s.a;
    s.ccw = true;
  } else {
    auto e = primEndpoints(p);
    s.a = M.toUVnoMirror(e[0]); s.b = M.toUVnoMirror(e[1]);
    s.ccw = !M.swap;   // reflection reverses orientation
  }
  return s;
}

// clip a segment to u >= 0
void clipPositive(const ASeg& s, std::vector<ASeg>& out, double tol) {
  if (!s.arc) {
    ASeg t = s;
    if (t.a.x < -tol && t.b.x < -tol) return;
    if (t.a.x >= -tol && t.b.x >= -tol) { t.a.x = std::max(0.0, t.a.x); t.b.x = std::max(0.0, t.b.x); out.push_back(t); return; }
    double k = t.a.x / (t.a.x - t.b.x);
    Vec2 m = t.a + (t.b - t.a) * k;
    m.x = 0;
    if (t.a.x < 0) t.a = m; else t.b = m;
    out.push_back(t);
    return;
  }
  // arc: split at u = 0 crossings
  std::vector<double> ts{0, 1};
  if (std::abs(s.c.x) < s.r) {
    double dy = std::sqrt(s.r * s.r - s.c.x * s.c.x);
    for (Vec2 q : {Vec2{0, s.c.y + dy}, Vec2{0, s.c.y - dy}}) {
      if (distToSeg(s, q) > 1e-7) continue;
      // parameter
      double best = 0, bd = 1e300;
      for (int i = 0; i <= 2000; ++i) { double t = i / 2000.0; double d = distance(segPoint(s, t), q); if (d < bd) { bd = d; best = t; } }
      ts.push_back(best);
    }
  }
  std::sort(ts.begin(), ts.end());
  for (size_t i = 1; i < ts.size(); ++i) {
    if (ts[i] - ts[i - 1] < 1e-9) continue;
    Vec2 mid = segPoint(s, 0.5 * (ts[i] + ts[i - 1]));
    if (mid.x < 0) continue;
    ASeg t = s;
    t.a = segPoint(s, ts[i - 1]);
    t.b = segPoint(s, ts[i]);
    if (std::abs(t.a.x) < tol) t.a.x = 0;
    if (std::abs(t.b.x) < tol) t.b.x = 0;
    out.push_back(t);
  }
}

// --------------------------------------------------------------------------------------------- dimension resolution
// point id carrying the anchor's coordinate along `axis` (0 = u, 1 = v)
int resolvePoint(const SkBuild& B, Vec2 a, int axis, double tol) {
  for (auto& p : B.sk.points) if (std::hypot(p.x - a.x, p.y - a.y) < tol) return p.id;
  for (auto& e : B.sk.entities)
    if (e.type != SkEntityType::Line && std::hypot(B.sk.point(e.points[0])->x - a.x, B.sk.point(e.points[0])->y - a.y) < tol)
      return e.points[0];
  for (auto& e : B.sk.entities) {
    if (e.type != SkEntityType::Line) continue;
    auto* p = B.sk.point(e.points[0]); auto* q = B.sk.point(e.points[1]);
    if (distPointSeg(a, {p->x, p->y}, {q->x, q->y}) > tol) continue;
    double along = axis == 0 ? std::abs(q->x - p->x) : std::abs(q->y - p->y);
    if (along < tol) return p->id;   // the line is perpendicular to the measuring axis
  }
  return -1;
}

std::string viewNameOf(const ViewRegion& v) { return v.label.empty() ? std::string(labelTr(v.role)) : v.label; }

}  // namespace

// ================================================================================================= stage
StageResult reconstructSketches(DrawingIR& ir, const InterpretOptions& opt, ProgressSink* prog) {
  StageResult res = StageResult::success();
  const Stage st = Stage::Sketch;
  ir.sketches.clear();
  if (ir.layout.views.empty()) return StageResult::failure(Status::InvalidInput, "Görünüş yok");
  NameReg reg;
  const bool raster = ir.norm.source == SourceKind::RasterImage || ir.norm.source == SourceKind::ScannedPdf ||
                      ir.norm.source == SourceKind::Photo;
  const double angTol = raster ? 1.0 : 0.3;
  const double rounding = raster ? std::max(0.1, opt.measuredRounding) : opt.measuredRounding;

  auto dimsOfView = [&](int vid) {
    std::vector<const Dimension*> out;
    for (auto& d : ir.annot.dims)
      if (d.viewId == vid && d.text.valid && (!opt.strict || usableInStrict(d.conf) || d.userEdited)) out.push_back(&d);
    std::sort(out.begin(), out.end(), [](auto* a, auto* b) {
      return a->measuredPart != b->measuredPart ? a->measuredPart < b->measuredPart : a->id < b->id;
    });
    return out;
  };
  auto useTolMid = [&](const Dimension& d) { return opt.preferTolMid && (d.text.tolPlus || d.text.tolMinus); };
  int idx = 0;
  for (auto& vref : ir.layout.views) {
    report(prog, 0.1 + 0.8 * idx++ / std::max<size_t>(1, ir.layout.views.size()), "Sketch: " + viewNameOf(vref));
    const ViewRegion& v = vref;
    if (v.role == ViewRole::Ignore) continue;
    const bool rot = ir.views.rotational.count(v.id) && ir.views.rotational.at(v.id);
    const bool primary = v.id == ir.views.primaryView;
    if (!rot && !primary) continue;
    const double f = viewScaleFactor(v);
    const double tol = std::max(1e-4, 0.02 * f);
    const double devTol = std::max(0.05, 0.05 * f);
    std::string vname = viewNameOf(v);

    if (rot) {
      // ================================================================= (a)/(c) revolve half profile
      const bool horizontalAxis = v.attrs.count("interp.axisDir") && v.attrs.at("interp.axisDir") == "h";
      ViewMap M{&v, horizontalAxis, false};
      bool section = false;
      std::vector<std::vector<Vec2>> hatch;
      for (int id : v.primIds)
        if (auto* p = findPrim(ir.norm, id); p && p->kind == PrimKind::HatchRegion) {
          section = true;
          std::vector<Vec2> poly;
          for (auto& q : p->pts) poly.push_back(M.toUVnoMirror(q));
          hatch.push_back(poly);
        }
      // Hidden lines that are only the projection of off-axis holes seen on the end view (bolt circle) are not part
      // of the half profile (else a hole lying in the profile plane reads as a bore of the hole's radius).
      std::vector<double> holeU;
      {
        auto evIt0 = ir.views.attrs.find("interp.endView." + std::to_string(v.id));
        const ViewRegion* ev0 = evIt0 != ir.views.attrs.end() ? findView(ir.layout, std::stoi(evIt0->second)) : nullptr;
        if (ev0)
          for (auto* p : viewPrims(ir, *ev0, {LineRole::Visible})) {
            if (p->kind != PrimKind::Circle) continue;
            Vec2 q = transformPoint(ev0->sheetToView, p->center);
            if (length(q) <= tol * 5) continue;
            double x = horizontalAxis ? q.y : q.x, rh = p->radius * viewScaleFactor(*ev0);
            holeU.push_back(std::abs(x - rh));
            holeU.push_back(std::abs(x + rh));
          }
      }
      std::vector<ASeg> segs;
      for (int id : v.primIds) {
        const Primitive* p = findPrim(ir.norm, id);
        if (!p || !isCurve(*p)) continue;
        LineRole r = roleOf(ir, id);
        int tag = r == LineRole::Visible ? 0 : (r == LineRole::Hidden && !section ? 1 : -1);
        if (tag == 1 && p->kind == PrimKind::Line && !holeU.empty()) {
          Vec2 a = M.toUVnoMirror(p->p0), b = M.toUVnoMirror(p->p1);
          if (std::abs(a.x - b.x) < tol &&
              std::any_of(holeU.begin(), holeU.end(), [&](double u) { return std::abs(std::abs(a.x) - u) < tol * 3; }))
            continue;
        }
        if (tag < 0) continue;
        clipPositive(primToSeg(*p, M, tag), segs, tol);
      }
      if (segs.empty()) continue;
      double vmin = 1e300, vmax = -1e300;
      for (auto& s : segs) for (auto& q : segPolyline(s, 1.0)) { vmin = std::min(vmin, q.y); vmax = std::max(vmax, q.y); }
      ASeg axisSeg;
      axisSeg.a = {0, vmin}; axisSeg.b = {0, vmax}; axisSeg.tag = 2;
      segs.push_back(axisSeg);
      Arrangement A;
      A.build(segs, tol);
      std::vector<char> mat(A.cycles.size(), 0);
      for (size_t ci = 0; ci < A.cycles.size(); ++ci) {
        const auto& cy = A.cycles[ci];
        if (cy.area <= 0) continue;
        if (section) {
          Vec2 ip = A.interiorPoint((int)ci);
          for (auto& h : hatch) if (pointInPolygon(ip, h)) { mat[ci] = 1; break; }
          continue;
        }
        bool touchesAxis = false, anyHidden = false, allHidden = true;
        for (int h : cy.halves) {
          const auto& E = A.edges[A.halves[h].edge];
          bool isAxis = std::find(E.tags.begin(), E.tags.end(), 2) != E.tags.end();
          if (isAxis) { touchesAxis = true; continue; }
          if (!E.seg.arc && std::abs(E.seg.a.y - E.seg.b.y) < tol) continue;   // radial edge (perpendicular to axis)
          bool hid = std::all_of(E.tags.begin(), E.tags.end(), [](int t) { return t == 1; });
          anyHidden |= hid;
          allHidden &= hid;
        }
        mat[ci] = !(touchesAxis && anyHidden && allHidden);
      }
      auto bnd = simplify(materialBoundary(A, mat), tol);
      if (bnd.empty()) {
        res.warn("sketch.noProfile", "Görünüş " + vname + ": kapalı yarım profil bulunamadı", {v.id}, (int)st);
        continue;
      }
      SketchCandidate c;
      c.id = ir.newId();
      c.viewId = v.id;
      c.purpose = "revolve-half-profile";
      c.plane = SketchPlaneKind::XZ;
      c.sketch.plane.kind = SketchPlaneKind::XZ;
      SkBuild B;
      B.tol = tol;
      B.sk.plane.kind = SketchPlaneKind::XZ;
      addSegs(B, bnd);
      c.profileClosed = true;
      int dof0 = B.dof();
      autoConstraints(B, angTol, true);
      // ---- dimensions
      struct Pending { int cid; const Dimension* d; std::string cls; double key; double value; ParamKind kind; std::string factor; };
      std::vector<Pending> pend;
      std::vector<int> redundant;
      M.mirrorU = true;
      for (auto* d : dimsOfView(v.id)) {
        if (!d->driving) continue;
        double val = d->text.nominal;
        std::string ph = "__d" + std::to_string(d->id);
        int cid = -1;
        std::string cls;
        double key = 0;
        // linear diameter: own dimension line (PDF / exploded DXF) or a DXF DIMENSION (lines in its block) whose
        // bound geometry is not a circle (a leader to a circle is the end-view case)
        const Primitive* g0 = d->geomPrimIds.empty() ? nullptr : findPrim(ir.norm, d->geomPrimIds[0]);
        const bool linearDia = d->kind == DimKind::Diameter && d->anchors.size() >= 2 &&
                               (d->dimLineIds.size() || !g0 || (g0->kind != PrimKind::Circle && g0->kind != PrimKind::Arc)) &&
                               distance(d->anchors[0], d->anchors[1]) > tol;
        if (d->kind == DimKind::Linear || d->kind == DimKind::Aligned || linearDia) {
          Vec2 a0 = M.toUV(d->anchors[0]), a1 = M.toUV(d->anchors[1]);
          Vec2 dir = M.toUVnoMirror(d->anchors[1]) - M.toUVnoMirror(d->anchors[0]);
          bool alongU = std::abs(dir.x) > std::abs(dir.y);
          if (d->kind == DimKind::Diameter || d->text.kind == DimKind::Diameter) {
            int p = resolvePoint(B, a0.x >= a1.x ? a0 : a1, 0, tol * 3);
            if (p < 0) continue;
            B.vals[ph] = val;
            cid = B.tryAdd(SkConstraintType::DistanceX, {0, p}, ph + "/2");
            cls = "D";
            key = B.sk.point(p)->y;
          } else {
            int ax = alongU ? 0 : 1;
            int p = resolvePoint(B, a0, ax, tol * 3), q = resolvePoint(B, a1, ax, tol * 3);
            if (p < 0 || q < 0 || p == q) { res.warn("sketch.dimUnresolved", "Ölçü \"" + d->text.raw + "\" sketch noktalarına eşlenemedi", {d->id}, (int)st); continue; }
            auto* P = B.sk.point(p); auto* Q = B.sk.point(q);
            if ((alongU ? Q->x - P->x : Q->y - P->y) < 0) std::swap(p, q);
            B.vals[ph] = val;
            cid = B.tryAdd(alongU ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {p, q}, ph);
            cls = alongU ? "L" : "L";
            key = std::min(B.sk.point(p)->y, B.sk.point(q)->y);
            if (!alongU && std::abs(std::abs(B.sk.point(q)->y - B.sk.point(p)->y) - (vmax - vmin)) < tol * 3) cls = "L_top";
          }
        } else if (d->kind == DimKind::Radius && !d->geomPrimIds.empty()) {
          int eid = -1;
          for (auto& [e, pr] : B.ent2prim) if (pr == d->geomPrimIds[0] && B.sk.entity(e)->type != SkEntityType::Line) eid = e;
          if (eid < 0) continue;
          B.vals[ph] = val;
          cid = B.tryAdd(SkConstraintType::Radius, {eid}, ph);
          cls = "R";
          key = B.sk.point(B.sk.entity(eid)->points[0])->y;
        } else if (d->kind == DimKind::Diameter && !d->geomPrimIds.empty()) {
          // leader to a circle in this profile view (rare) -> handled like the end-view case below
          continue;
        } else {
          continue;
        }
        if (cid < 0) { redundant.push_back(d->id); B.vals.erase(ph); continue; }
        pend.push_back({cid, d, cls, key, val, ParamKind::Length, ""});
      }
      // diameters given on the end view (circles concentric with the axis)
      auto evIt = ir.views.attrs.find("interp.endView." + std::to_string(v.id));
      const ViewRegion* ev = evIt != ir.views.attrs.end() ? findView(ir.layout, std::stoi(evIt->second)) : nullptr;
      if (ev) {
        for (auto* d : dimsOfView(ev->id)) {
          if (!d->driving || d->kind != DimKind::Diameter || d->geomPrimIds.empty() || d->attrs.count("interp.pcd")) continue;
          const Primitive* circ = findPrim(ir.norm, d->geomPrimIds[0]);
          if (!circ || circ->kind != PrimKind::Circle) continue;
          Vec2 cc = transformPoint(ev->sheetToView, circ->center);
          if (length(cc) > tol * 5) continue;   // not concentric: bolt hole, handled by the hole pattern
          double r = circ->radius * viewScaleFactor(*ev);
          int p = -1;
          for (auto& e : B.sk.entities) {
            if (e.type != SkEntityType::Line) continue;
            auto* a = B.sk.point(e.points[0]); auto* b = B.sk.point(e.points[1]);
            if (std::abs(a->x - r) < tol * 3 && std::abs(b->x - r) < tol * 3) { p = a->id; break; }
          }
          if (p < 0) continue;
          std::string ph = "__d" + std::to_string(d->id);
          B.vals[ph] = d->text.nominal;
          int cid = B.tryAdd(SkConstraintType::DistanceX, {0, p}, ph + "/2");
          if (cid < 0) { redundant.push_back(d->id); B.vals.erase(ph); continue; }
          pend.push_back({cid, d, "D", B.sk.point(p)->y, d->text.nominal, ParamKind::Length, ""});
        }
      }
      // ---- names: D by axial position, L by position, L_top, R
      std::stable_sort(pend.begin(), pend.end(), [](auto& a, auto& b) { return a.cls != b.cls ? a.cls < b.cls : a.key < b.key; });
      // diameter key: mid of the axial extent of the line carrying the point
      for (auto& pd : pend) {
        std::string name = pd.cls == "L_top" ? reg.takeExact("L_top") : reg.take(pd.cls);
        std::string ph = "__d" + std::to_string(pd.d->id);
        for (auto& cst : B.sk.constraints) if (cst.id == pd.cid) cst.valueExpr = renameIdentifier(cst.valueExpr, ph, name);
        B.vals.erase(ph);
        B.c2dim[pd.cid] = pd.d->id;
        B.addParam(name, pd.value, pd.kind, ParamTag::FromDimension, dimLabel(*pd.d, ir), pd.d->id, pd.d, useTolMid(*pd.d));
        if (pd.d->text.isMax) B.params.back().rangeMax = pd.value;
      }
      for (int did : redundant) {
        const Dimension* d = findDimension(ir.annot, did);
        std::string name = reg.take("L_ref");
        B.addParam(name, d->text.nominal, ParamKind::Length, ParamTag::Derived,
                   "[T] fazlalık ölçü, referans olarak doğrulanır: " + dimLabel(*d, ir), did, d);
      }
      for (auto* d : dimsOfView(v.id))   // max/min envelope dims -> range rules
        if (d->text.isMax || d->text.isMin) {
          std::string name = reg.take("L_zarf");
          B.addParam(name, d->text.nominal, ParamKind::Length, ParamTag::Derived, "[T] zarf ölçüsü (" + std::string(d->text.isMax ? "max" : "min") + "): " + dimLabel(*d, ir), d->id, d);
          if (d->text.isMax) B.params.back().rangeMax = d->text.nominal; else B.params.back().rangeMin = d->text.nominal;
        }
      int dofDims = B.dof();
      if (opt.closeDofWithMeasured) closeWithMeasured(B, reg, rounding, vname);
      c.attrs["interp.dof0"] = std::to_string(dof0);
      c.attrs["interp.dofAfterDims"] = std::to_string(dofDims);
      c.attrs["interp.strategy"] = section ? "SectionRevolve" : "Revolve";
      c.attrs["interp.axisDir"] = horizontalAxis ? "h" : "v";
      c.attrs["interp.viewMap." + std::to_string(v.id)] = horizontalAxis ? "Z,X" : "X,Z";
      c.attrs["interp.profileVmin"] = fmt(vmin);
      c.attrs["interp.profileVmax"] = fmt(vmax);
      if (ev) c.attrs["interp.viewMap." + std::to_string(ev->id)] = horizontalAxis ? "Y,X" : "X,Y";
      if (!redundant.empty()) {
        std::string s;
        for (int r : redundant) s += (s.empty() ? "" : ",") + std::to_string(r);
        c.attrs["interp.redundant"] = s;
      }
      c.axisEntity = -1;
      finishCandidate(B, c, devTol);
      c.conf = float(std::clamp(ir.views.symmetryScore[v.id], 0.0, 1.0));
      int revolveId = c.id;
      ir.sketches.push_back(std::move(c));

      // ================================================================= bolt holes on the end view
      if (ev) {
        const SketchCandidate& rc = ir.sketches.back();
        std::vector<const Primitive*> holes;
        for (auto* p : viewPrims(ir, *ev, {LineRole::Visible}))
          if (p->kind == PrimKind::Circle && length(transformPoint(ev->sheetToView, p->center)) > tol * 5) holes.push_back(p);
        // group by radius and pitch radius
        std::vector<std::vector<const Primitive*>> groups;
        for (auto* h : holes) {
          bool put = false;
          for (auto& g : groups)
            if (std::abs(g[0]->radius - h->radius) < 0.05 &&
                std::abs(length(transformPoint(ev->sheetToView, g[0]->center)) - length(transformPoint(ev->sheetToView, h->center))) < tol * 5) {
              g.push_back(h); put = true; break;
            }
          if (!put) groups.push_back({h});
        }
        for (auto& g : groups) {
          double fe = viewScaleFactor(*ev);
          double rh = g[0]->radius * fe;
          double pr = length(transformPoint(ev->sheetToView, g[0]->center));
          // part coordinates: X along the profile's radial direction (shared sheet axis), Y the other one
          auto partXY = [&](const Primitive* h) {
            Vec2 q = transformPoint(ev->sheetToView, h->center);
            return horizontalAxis ? Vec2{q.y, q.x} : Vec2{q.x, q.y};
          };
          const Primitive* first = g[0];
          double bestAng = 1e300;
          for (auto* h : g) { Vec2 q = partXY(h); double a = std::abs(std::atan2(q.y, q.x)); if (a < bestAng) { bestAng = a; first = h; } }
          // axial extent from hidden lines of the profile at radius pr +- rh
          double z0 = 1e300, z1 = -1e300;
          for (int id : v.primIds) {
            const Primitive* p = findPrim(ir.norm, id);
            if (!p || p->kind != PrimKind::Line || roleOf(ir, id) != LineRole::Hidden) continue;
            Vec2 a = M.toUV(p->p0), b = M.toUV(p->p1);
            if (std::abs(a.x - b.x) > tol) continue;
            if (std::abs(std::abs(a.x - pr) - rh) < tol * 3 || std::abs(a.x - pr) < rh) {
              z0 = std::min({z0, a.y, b.y}); z1 = std::max({z1, a.y, b.y});
            }
          }
          bool fromHidden = z1 > z0;
          if (!fromHidden) { z0 = vmin; z1 = vmax; }
          SketchCandidate hc;
          hc.id = ir.newId();
          hc.viewId = ev->id;
          hc.purpose = "hole-pattern";
          hc.plane = SketchPlaneKind::XY;
          SkBuild H;
          H.tol = tol;
          H.sk.plane.kind = SketchPlaneKind::XY;
          // plane offset: an axial length parameter of the profile with the same value, else a number
          std::string offExpr = fmt(z1, 6);
          for (auto& pp : rc.params)
            if (pp.tag == ParamTag::FromDimension && pp.param.name[0] == 'L' && std::abs(pp.nominalValue - z1) < tol * 3) { offExpr = pp.param.name; break; }
          H.sk.plane.offsetExpr = offExpr;
          Vec2 c0 = partXY(first);
          int circ = H.sk.addCircle(c0.x, c0.y, rh);
          int cp = H.sk.entity(circ)->points[0];
          H.orig[cp] = c0;
          H.ent2prim[circ] = first->id;
          // diameter / count / pitch circle from dimensions of the end view
          std::string dName, nName, pName;
          int count = (int)g.size();
          for (auto* d : dimsOfView(ev->id)) {
            if (d->geomPrimIds.empty()) continue;
            int gp = d->geomPrimIds[0];
            const bool onGroup = std::any_of(g.begin(), g.end(), [&](auto* h) { return h->id == gp; });
            const double mTol = std::max(tol * 5, 0.02 * d->measuredPart);
            if (pName.empty() && d->kind == DimKind::Diameter && onGroup && g.size() > 1 && std::abs(d->measuredPart - 2 * pr) < mTol &&
                std::abs(d->measuredPart - 2 * rh) > mTol) {   // "Ø90" between two hole centres = pitch circle
              pName = reg.take("D_bc");
              H.addParam(pName, d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*d, ir), d->id, d, useTolMid(*d));
              continue;
            }
            if (dName.empty() && d->kind == DimKind::Diameter && onGroup && std::abs(d->measuredPart - 2 * rh) < mTol) {
              dName = reg.take("Dh");
              H.vals[dName] = d->text.nominal;
              int cid = H.sk.addConstraint(SkConstraintType::Diameter, {circ}, dName);
              H.c2dim[cid] = d->id;
              H.addParam(dName, d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*d, ir), d->id, d, useTolMid(*d));
              if (d->text.count > 1) {
                count = d->text.count;
                if (d->text.count != (int)g.size())
                  res.warn("sketch.holeCount", "\"" + d->text.raw + "\" adedi çizimdeki delik sayısından (" + std::to_string(g.size()) + ") farklı", {d->id}, (int)st);
              }
            }
            if (pName.empty() && d->kind == DimKind::Diameter && d->attrs.count("interp.pcd")) {
              const Primitive* pc = findPrim(ir.norm, gp);
              if (pc && std::abs(pc->radius * fe - pr) < tol * 5) {
                pName = reg.take("D_bc");
                H.addParam(pName, d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*d, ir), d->id, d, useTolMid(*d));
              }
            }
          }
          if (dName.empty()) {
            dName = reg.take("Dh");
            H.vals[dName] = 2 * rh;
            H.sk.addConstraint(SkConstraintType::Diameter, {circ}, dName);
            H.addParam(dName, std::round(2 * rh / rounding) * rounding, ParamKind::Length, ParamTag::Measured, "[Ö] delik çapı ölçüldü, görünüş " + viewNameOf(*ev), -1);
          }
          if (pName.empty()) {
            pName = reg.take("D_bc");
            H.addParam(pName, std::round(2 * pr / rounding) * rounding, ParamKind::Length, ParamTag::Measured, "[Ö] bölüm dairesi çapı ölçüldü, görünüş " + viewNameOf(*ev), -1);
          }
          if (std::abs(c0.y) < tol * 3) {
            H.sk.point(cp)->y = 0;
            H.sk.addConstraint(SkConstraintType::HorizontalPts, {0, cp});
            int cid = H.sk.addConstraint(SkConstraintType::DistanceX, {0, cp}, pName + "/2");
            (void)cid;
          } else {
            H.sk.addConstraint(SkConstraintType::Distance, {0, cp}, pName + "/2");
          }
          nName = reg.take("n_h");
          H.addParam(nName, count, ParamKind::Count, count == (int)g.size() ? ParamTag::FromDimension : ParamTag::Measured,
                     "[D] delik adedi (dairesel desen), görünüş " + viewNameOf(*ev), -1);
          if (opt.closeDofWithMeasured) closeWithMeasured(H, reg, rounding, viewNameOf(*ev));
          hc.attrs["interp.forSketch"] = std::to_string(revolveId);
          hc.attrs["interp.forStrategy"] = "Revolve";
          hc.attrs["hole.diameterParam"] = dName;
          hc.attrs["hole.through"] = "1";
          hc.attrs["pattern.kind"] = count > 1 ? "circular" : "";
          hc.attrs["pattern.countParam"] = nName;
          hc.attrs["pattern.axis"] = "Z";
          hc.attrs["interp.holeZ"] = fmt(z0) + "," + fmt(z1);
          if (!fromHidden) res.warn("sketch.holeDepthAssumed", "Delik boyu profilde gizli çizgiyle gösterilmemiş; boydan boya varsayıldı", {first->id}, (int)st);
          finishCandidate(H, hc, devTol);
          hc.conf = 0.8f;
          ir.sketches.push_back(std::move(hc));
        }
      }
      continue;
    }

    // ================================================================= (b) prismatic: primary profile + holes
    ViewMap M{&v, false, false};
    // adjacent view for depth
    const ViewRegion* adj = nullptr;
    Adjacency adjRel = Adjacency::None;
    for (auto& r : ir.views.relations)
      if (r.a == v.id) {
        const ViewRegion* o = findView(ir.layout, r.b);
        if (o && o->role != ViewRole::Ignore && (!adj || r.conf > 0.5)) { adj = o; adjRel = r.adj; }
      }
    bool depthAlongY = adjRel == Adjacency::Above || adjRel == Adjacency::Below;   // shared x
    // holes: visible circles of the primary view with through/blind evidence in the adjacent view
    std::vector<const Primitive*> circles;
    for (auto* p : viewPrims(ir, v, {LineRole::Visible})) if (p->kind == PrimKind::Circle) circles.push_back(p);
    std::vector<ASeg> segs;
    std::set<int> holeIds;
    for (auto* p : circles) holeIds.insert(p->id);
    for (auto* p : viewPrims(ir, v, {LineRole::Visible}))
      if (!holeIds.count(p->id) && isCurve(*p)) segs.push_back(primToSeg(*p, M, 0));
    Arrangement A;
    A.build(segs, tol);
    std::vector<char> mat(A.cycles.size(), 0);
    for (size_t ci = 0; ci < A.cycles.size(); ++ci) mat[ci] = A.cycles[ci].area > 0;
    // islands (separate inner components) are cut-outs
    for (size_t ci = 0; ci < A.cycles.size(); ++ci) {
      if (A.cycles[ci].area >= 0 || A.cycles[ci].container < 0) continue;
      int comp = A.cycles[ci].component;
      for (size_t cj = 0; cj < A.cycles.size(); ++cj) if (A.cycles[cj].component == comp && A.cycles[cj].area > 0) mat[cj] = 0;
      res.warn("sketch.cutout", "Görünüş " + vname + ": iç kapalı kontur boşluk (boydan boya kesim) kabul edildi", {}, (int)st);
    }
    auto bnd = simplify(materialBoundary(A, mat), tol);
    if (bnd.empty()) {
      res.warn("sketch.noProfile", "Görünüş " + vname + ": kapalı profil bulunamadı", {v.id}, (int)st);
      continue;
    }
    Box2 pb = emptyBox();
    for (auto& s : bnd) for (auto& q : segPolyline(s, 1)) expand(pb, q);
    SketchCandidate c;
    c.id = ir.newId();
    c.viewId = v.id;
    c.purpose = "extrude-profile";
    c.plane = SketchPlaneKind::XY;
    SkBuild B;
    B.tol = tol;
    B.sk.plane.kind = SketchPlaneKind::XY;
    addSegs(B, bnd);
    c.profileClosed = true;
    int dof0 = B.dof();
    autoConstraints(B, angTol, false);
    // hole sketch builder (one per diameter group)
    std::vector<std::vector<const Primitive*>> hg;
    for (auto* h : circles) {
      bool put = false;
      for (auto& g : hg) if (std::abs(g[0]->radius - h->radius) < 0.02) { g.push_back(h); put = true; break; }
      if (!put) hg.push_back({h});
    }
    struct HoleSk { SkBuild B; std::vector<const Primitive*> circ; std::map<int, int> circEnt; bool through = true; double depth = 0; bool evidence = false; };
    std::vector<HoleSk> hs(hg.size());
    for (size_t gi = 0; gi < hg.size(); ++gi) {
      HoleSk& H = hs[gi];
      H.B.tol = tol;
      H.B.sk.plane.kind = SketchPlaneKind::XY;
      H.circ = hg[gi];
      for (auto* h : hg[gi]) {
        Vec2 cc = M.toUV(h->center);
        int e = H.B.sk.addCircle(cc.x, cc.y, h->radius * f);
        H.B.orig[H.B.sk.entity(e)->points[0]] = cc;
        H.B.ent2prim[e] = h->id;
        H.circEnt[h->id] = e;
      }
      // through / blind from hidden lines in the adjacent view
      if (adj) {
        Box2 ab = visibleBox(ir, *adj);
        double ext = depthAlongY ? height(ab) : width(ab);
        double lo = 1e300, hi = -1e300;
        for (auto* p : viewPrims(ir, *adj, {LineRole::Hidden})) {
          if (p->kind != PrimKind::Line) continue;
          for (auto* h : hg[gi]) {
            double cpos = depthAlongY ? h->center.x : h->center.y;
            double lpos = depthAlongY ? p->p0.x : p->p0.y;
            bool parallelDepth = depthAlongY ? std::abs(p->p0.x - p->p1.x) < 0.05 : std::abs(p->p0.y - p->p1.y) < 0.05;
            if (!parallelDepth || std::abs(std::abs(lpos - cpos) - h->radius) > 0.1) continue;
            double a = depthAlongY ? p->p0.y : p->p0.x, b = depthAlongY ? p->p1.y : p->p1.x;
            lo = std::min({lo, a, b}); hi = std::max({hi, a, b});
          }
        }
        if (hi > lo) {
          H.evidence = true;
          H.through = (hi - lo) >= 0.98 * ext;
          H.depth = (hi - lo) * viewScaleFactor(*adj);
        }
      }
      autoConstraints(H.B, angTol, false);
      for (auto& e1 : H.B.sk.entities)
        for (auto& e2 : H.B.sk.entities) {
          if (e1.id >= e2.id) continue;
          auto* p = H.B.sk.point(e1.points[0]); auto* q = H.B.sk.point(e2.points[0]);
          if (std::abs(p->y - q->y) < tol) H.B.tryAdd(SkConstraintType::HorizontalPts, {p->id, q->id});
          if (std::abs(p->x - q->x) < tol) H.B.tryAdd(SkConstraintType::VerticalPts, {p->id, q->id});
        }
    }
    // ---- dimensions of the primary view
    std::vector<int> redundant;
    struct Pending { SkBuild* B; int cid; const Dimension* d; std::string cls; double key; };
    std::vector<Pending> pend;
    for (auto* d : dimsOfView(v.id)) {
      if (!d->driving) continue;
      std::string ph = "__d" + std::to_string(d->id);
      double val = d->text.nominal;
      if (d->kind == DimKind::Diameter && !d->dimLineIds.empty() == false && !d->geomPrimIds.empty()) {
        // leader on a hole circle
        for (auto& H : hs) {
          auto it = H.circEnt.find(d->geomPrimIds[0]);
          if (it == H.circEnt.end()) continue;
          H.B.vals[ph] = val;
          int cid = H.B.tryAdd(SkConstraintType::Diameter, {it->second}, ph);
          if (cid >= 0) pend.push_back({&H.B, cid, d, "Dh", 0});
          else redundant.push_back(d->id);
          break;
        }
        continue;
      }
      if (d->kind == DimKind::Radius && !d->geomPrimIds.empty()) {
        int eid = -1;
        for (auto& [e, pr] : B.ent2prim) if (pr == d->geomPrimIds[0] && B.sk.entity(e)->type != SkEntityType::Line) eid = e;
        if (eid < 0) continue;
        B.vals[ph] = val;
        int cid = B.tryAdd(SkConstraintType::Radius, {eid}, ph);
        if (cid >= 0) pend.push_back({&B, cid, d, "R", 0}); else redundant.push_back(d->id);
        continue;
      }
      if (d->kind != DimKind::Linear && d->kind != DimKind::Aligned) continue;
      Vec2 a0 = M.toUV(d->anchors[0]), a1 = M.toUV(d->anchors[1]);
      bool alongU = std::abs(d->direction.x) > std::abs(d->direction.y);
      int ax = alongU ? 0 : 1;
      // target sketch: profile if both anchors resolve there, else the hole sketch holding one anchor
      SkBuild* T = nullptr;
      int p = resolvePoint(B, a0, ax, tol * 3), q = resolvePoint(B, a1, ax, tol * 3);
      if (p >= 0 && q >= 0 && p != q) T = &B;
      else {
        for (auto& H : hs) {
          int hp = resolvePoint(H.B, a0, ax, tol * 3), hq = resolvePoint(H.B, a1, ax, tol * 3);
          auto originLike = [&](Vec2 a) { return std::abs(ax == 0 ? a.x : a.y) < tol * 3 ? 0 : -1; };
          if (hp < 0) hp = originLike(a0);
          if (hq < 0) hq = originLike(a1);
          if (hp >= 0 && hq >= 0 && hp != hq) { T = &H.B; p = hp; q = hq; break; }
        }
      }
      if (!T) { res.warn("sketch.dimUnresolved", "Ölçü \"" + d->text.raw + "\" sketch noktalarına eşlenemedi", {d->id}, (int)st); continue; }
      auto* P = T->sk.point(p); auto* Q = T->sk.point(q);
      if ((alongU ? Q->x - P->x : Q->y - P->y) < 0) std::swap(p, q);
      T->vals[ph] = val;
      int cid = T->tryAdd(alongU ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {p, q}, ph);
      if (cid < 0) { redundant.push_back(d->id); T->vals.erase(ph); continue; }
      std::string cls = "L";
      if (T == &B && alongU && std::abs(val * 1.0 - width(pb)) < std::max(tol * 3, 0.01 * val) && std::abs(std::abs(Q->x - P->x) - width(pb)) < tol * 3) cls = "W";
      if (T == &B && !alongU && std::abs(std::abs(Q->y - P->y) - height(pb)) < tol * 3) cls = "H";
      pend.push_back({T, cid, d, cls, std::min(alongU ? P->y : P->x, alongU ? Q->y : Q->x)});
    }
    std::stable_sort(pend.begin(), pend.end(), [](auto& a, auto& b) {
      auto rank = [](const std::string& c) { return c == "W" ? 0 : c == "H" ? 1 : c == "L" ? 2 : 3; };
      return rank(a.cls) < rank(b.cls);
    });
    std::map<SkBuild*, std::string> holeDiaName;
    for (auto& pd : pend) {
      std::string name = (pd.cls == "W" || pd.cls == "H") ? reg.take(pd.cls, false) : reg.take(pd.cls);
      std::string ph = "__d" + std::to_string(pd.d->id);
      for (auto& cst : pd.B->sk.constraints) if (cst.id == pd.cid) cst.valueExpr = renameIdentifier(cst.valueExpr, ph, name);
      pd.B->vals.erase(ph);
      pd.B->c2dim[pd.cid] = pd.d->id;
      pd.B->addParam(name, pd.d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*pd.d, ir), pd.d->id, pd.d, useTolMid(*pd.d));
      if (pd.cls == "Dh") holeDiaName[pd.B] = name;
    }
    for (int did : redundant) {
      const Dimension* d = findDimension(ir.annot, did);
      B.addParam(reg.take("L_ref"), d->text.nominal, ParamKind::Length, ParamTag::Derived,
                 "[T] fazlalık ölçü, referans olarak doğrulanır: " + dimLabel(*d, ir), did, d);
    }
    // ---- depth T
    std::string depthName = reg.take("T", false);
    {
      bool got = false;
      if (adj) {
        Box2 ab = visibleBox(ir, *adj);
        double ext = depthAlongY ? height(ab) : width(ab);
        for (auto* d : dimsOfView(adj->id)) {
          if (!d->driving || (d->kind != DimKind::Linear && d->kind != DimKind::Aligned)) continue;
          bool alongY = std::abs(d->direction.y) > std::abs(d->direction.x);
          if (alongY != depthAlongY) continue;
          if (std::abs(d->measuredSheet - ext) > std::max(0.2, 0.02 * ext)) continue;
          B.addParam(depthName, d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*d, ir) + " (derinlik)", d->id, d, useTolMid(*d));
          got = true;
          break;
        }
        if (!got) {   // depth dimensioned on another view (e.g. Top while the depth is read from Left)
          const double meas = ext * viewScaleFactor(*adj);
          for (auto& ov : ir.layout.views) {
            if (ov.id == v.id || ov.id == adj->id || ov.role == ViewRole::Ignore || got) continue;
            for (auto* d : dimsOfView(ov.id)) {
              if (!d->driving || (d->kind != DimKind::Linear && d->kind != DimKind::Aligned)) continue;
              if (std::abs(d->measuredPart - meas) > std::max(tol * 5, 0.005 * meas)) continue;
              B.addParam(depthName, d->text.nominal, ParamKind::Length, ParamTag::FromDimension, dimLabel(*d, ir) + " (derinlik)", d->id, d, useTolMid(*d));
              got = true;
              break;
            }
          }
        }
        if (!got) {
          double val = std::round(ext * viewScaleFactor(*adj) / rounding) * rounding;
          B.addParam(depthName, val, ParamKind::Length, ParamTag::Measured, "[Ö] derinlik komşu görünüşten ölçüldü (" + viewNameOf(*adj) + ")", -1);
          got = true;
        }
        c.attrs["interp.strategy"] = "ExtrudeTwoView";
        c.attrs["interp.adjView"] = std::to_string(adj->id);
        c.attrs["interp.viewMap." + std::to_string(adj->id)] = depthAlongY ? "X,Z" : "Z,Y";
      } else if (ir.layout.attrs.count("interp.thicknessNote")) {
        double t = attrNum(ir.layout.attrs, "interp.thicknessNote", 0);
        B.addParam(depthName, t, ParamKind::Length, ParamTag::FromDimension, "[D] kalınlık notu", -1);
        c.attrs["interp.strategy"] = "ExtrudeSingleView";
        got = true;
      }
      if (!got) {
        double guess = std::round(std::max(1.0, 0.1 * std::max(width(pb), height(pb))));
        B.addParam(depthName, guess, ParamKind::Length, ParamTag::Assumed, "[V] derinlik bilinmiyor (tek görünüş); varsayım", -1);
        c.attrs["interp.strategy"] = "ExtrudeSingleView";
        c.attrs["interp.depthAssumed"] = "1";
        res.warn("sketch.depthAssumed", "Derinlik için komşu görünüş veya kalınlık notu yok; " + fmt(guess) + " mm varsayıldı", {v.id}, (int)st);
      }
    }
    c.attrs["interp.depthParam"] = depthName;
    c.attrs["interp.viewMap." + std::to_string(v.id)] = "X,Y";
    int dofDims = B.dof();
    if (opt.closeDofWithMeasured) closeWithMeasured(B, reg, rounding, vname);
    c.attrs["interp.dof0"] = std::to_string(dof0);
    c.attrs["interp.dofAfterDims"] = std::to_string(dofDims);
    if (!redundant.empty()) {
      std::string s;
      for (int r : redundant) s += (s.empty() ? "" : ",") + std::to_string(r);
      c.attrs["interp.redundant"] = s;
    }
    finishCandidate(B, c, devTol);
    c.conf = 0.8f;
    int profId = c.id;
    std::string strategy = c.attrs["interp.strategy"];
    ir.sketches.push_back(std::move(c));
    for (size_t gi = 0; gi < hs.size(); ++gi) {
      HoleSk& H = hs[gi];
      SketchCandidate hc;
      hc.id = ir.newId();
      hc.viewId = v.id;
      hc.purpose = "hole-pattern";
      hc.plane = SketchPlaneKind::XY;
      H.B.sk.plane.offsetExpr = depthName;
      std::string dName = holeDiaName.count(&H.B) ? holeDiaName[&H.B] : "";
      if (dName.empty()) {
        dName = reg.take("Dh");
        double dv = std::round(2 * H.circ[0]->radius * f / rounding) * rounding;
        H.B.vals[dName] = dv;
        H.B.sk.addConstraint(SkConstraintType::Diameter, {H.circEnt[H.circ[0]->id]}, dName);
        H.B.addParam(dName, dv, ParamKind::Length, ParamTag::Measured, "[Ö] delik çapı ölçüldü, görünüş " + vname, -1);
      }
      if (!H.evidence)
        res.warn("sketch.holeAssumedThrough", "Görünüş " + vname + ": Ø" + fmt(2 * H.circ[0]->radius * f, 3) +
                 " delikleri için komşu görünüşte gizli çizgi yok; boydan boya varsayıldı", {H.circ[0]->id}, (int)st);
      if (opt.closeDofWithMeasured) closeWithMeasured(H.B, reg, rounding, vname);
      hc.attrs["interp.forSketch"] = std::to_string(profId);
      hc.attrs["interp.forStrategy"] = strategy;
      hc.attrs["hole.diameterParam"] = dName;
      hc.attrs["hole.through"] = (!H.evidence || H.through) ? "1" : "0";
      if (H.evidence && !H.through) {
        std::string dn = reg.take("Lh");
        H.B.addParam(dn, std::round(H.depth / rounding) * rounding, ParamKind::Length, ParamTag::Measured, "[Ö] kör delik derinliği (gizli çizgiden)", -1);
        hc.attrs["hole.depthExpr"] = dn;
      }
      finishCandidate(H.B, hc, devTol);
      hc.conf = H.evidence ? 0.85f : 0.6f;
      ir.sketches.push_back(std::move(hc));
    }
  }
  if (ir.sketches.empty()) {
    // Fallback (typ. raster): no closed profile anywhere -> the visible geometry of the primary (else the richest)
    // view as an unconstrained sketch, so the user still gets an editable starting point (SketchOnly).
    const ViewRegion* best = nullptr;
    size_t bestN = 0;
    for (auto& v : ir.layout.views) {
      if (v.role == ViewRole::Ignore) continue;
      size_t n = viewPrims(ir, v, {LineRole::Visible}).size();
      if (v.id == ir.views.primaryView) n += 1000000;
      if (n > bestN) { bestN = n; best = &v; }
    }
    if (best && bestN % 1000000 > 0) {
      const double f = viewScaleFactor(*best);
      ViewMap M{best, false, false};
      std::vector<ASeg> segs;
      for (auto* p : viewPrims(ir, *best, {LineRole::Visible}))
        if (isCurve(*p)) segs.push_back(primToSeg(*p, M, 0));
      SketchCandidate c;
      c.id = ir.newId();
      c.viewId = best->id;
      c.purpose = "raw-geometry";
      c.plane = SketchPlaneKind::XY;
      SkBuild B;
      B.tol = std::max(1e-4, 0.02 * f);
      B.sk.plane.kind = SketchPlaneKind::XY;
      addSegs(B, segs);
      finishCandidate(B, c, std::max(0.05, 0.05 * f));
      c.conf = 0.2f;
      ir.sketches.push_back(std::move(c));
      res.warn("sketch.rawFallback", "Kapalı profil bulunamadı: görünüş " + viewNameOf(*best) +
               " geometrisi kısıtsız sketch olarak verildi (elle düzenleyin)", {best->id}, (int)st);
    } else {
      res.warn("sketch.none", "Hiçbir görünüşten sketch çıkarılamadı", {}, (int)st);
    }
  }
  int dof0n = 0;
  for (auto& c : ir.sketches) dof0n += c.dof == 0;
  res.message = std::to_string(ir.sketches.size()) + " sketch (" + std::to_string(dof0n) + " tam tanımlı)";
  report(prog, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
