// SC-UX (WP-B) — sketch trim (pure logic). See TrimLogic.h.
#include "TrimLogic.h"
#include <algorithm>
#include <cmath>
#include <set>

using namespace pc;

namespace pcd {

namespace {
constexpr double kEps = 1e-9;
struct P2 { double x = 0, y = 0; };
P2 pt(const Sketch& s, int id) { const SkPoint* p = s.point(id); return p ? P2{p->x, p->y} : P2{}; }
double norm2pi(double a) { while (a < 0) a += 2 * M_PI; while (a >= 2 * M_PI) a -= 2 * M_PI; return a; }

struct Curve {
  int id = -1;
  SkEntityType type = SkEntityType::Line;
  P2 a, b;            // line ends
  P2 c; double r = 0; // circle / arc
  double a0 = 0, sweep = 2 * M_PI;   // arc: start angle, CCW sweep
};
Curve curveOf(const Sketch& s, const SkEntity& e) {
  Curve c;
  c.id = e.id;
  c.type = e.type;
  if (e.type == SkEntityType::Line && e.points.size() >= 2) { c.a = pt(s, e.points[0]); c.b = pt(s, e.points[1]); }
  else if (e.type == SkEntityType::Circle && !e.points.empty()) { c.c = pt(s, e.points[0]); c.r = e.radius; }
  else if (e.type == SkEntityType::Arc && e.points.size() >= 3) {
    c.c = pt(s, e.points[0]);
    const P2 st = pt(s, e.points[1]), en = pt(s, e.points[2]);
    c.r = std::hypot(st.x - c.c.x, st.y - c.c.y);
    c.a0 = norm2pi(std::atan2(st.y - c.c.y, st.x - c.c.x));
    c.sweep = norm2pi(std::atan2(en.y - c.c.y, en.x - c.c.x) - c.a0);
    if (c.sweep < kEps) c.sweep = 2 * M_PI;
  }
  return c;
}
bool onArc(const Curve& c, double ang) {
  if (c.type != SkEntityType::Arc) return true;
  return norm2pi(ang - c.a0) <= c.sweep + 1e-9;
}
// all intersection points of two curves
std::vector<P2> intersect(const Curve& A, const Curve& B) {
  std::vector<P2> out;
  auto lineLine = [&](const Curve& L1, const Curve& L2) {
    const double dx1 = L1.b.x - L1.a.x, dy1 = L1.b.y - L1.a.y, dx2 = L2.b.x - L2.a.x, dy2 = L2.b.y - L2.a.y;
    const double den = dx1 * dy2 - dy1 * dx2;
    if (std::fabs(den) < 1e-12) return;
    const double t = ((L2.a.x - L1.a.x) * dy2 - (L2.a.y - L1.a.y) * dx2) / den;
    const double s = ((L2.a.x - L1.a.x) * dy1 - (L2.a.y - L1.a.y) * dx1) / den;
    if (t < -1e-9 || t > 1 + 1e-9 || s < -1e-9 || s > 1 + 1e-9) return;
    out.push_back({L1.a.x + t * dx1, L1.a.y + t * dy1});
  };
  auto lineCircle = [&](const Curve& L, const Curve& C) {
    const double dx = L.b.x - L.a.x, dy = L.b.y - L.a.y, fx = L.a.x - C.c.x, fy = L.a.y - C.c.y;
    const double a = dx * dx + dy * dy, b = 2 * (fx * dx + fy * dy), c = fx * fx + fy * fy - C.r * C.r;
    if (a < 1e-18) return;
    double disc = b * b - 4 * a * c;
    if (disc < -1e-12) return;
    disc = std::sqrt(std::max(0.0, disc));
    for (double t : {(-b - disc) / (2 * a), (-b + disc) / (2 * a)}) {
      if (t < -1e-9 || t > 1 + 1e-9) continue;
      const P2 p{L.a.x + t * dx, L.a.y + t * dy};
      if (onArc(C, std::atan2(p.y - C.c.y, p.x - C.c.x))) out.push_back(p);
      if (disc < 1e-12) break;
    }
  };
  auto circleCircle = [&](const Curve& C1, const Curve& C2) {
    const double dx = C2.c.x - C1.c.x, dy = C2.c.y - C1.c.y, d = std::hypot(dx, dy);
    if (d < 1e-12 || d > C1.r + C2.r + 1e-9 || d < std::fabs(C1.r - C2.r) - 1e-9) return;
    const double a = (C1.r * C1.r - C2.r * C2.r + d * d) / (2 * d);
    const double h = std::sqrt(std::max(0.0, C1.r * C1.r - a * a));
    const double mx = C1.c.x + a * dx / d, my = C1.c.y + a * dy / d;
    for (int sgn : {1, -1}) {
      const P2 p{mx + sgn * h * dy / d, my - sgn * h * dx / d};
      if (onArc(C1, std::atan2(p.y - C1.c.y, p.x - C1.c.x)) && onArc(C2, std::atan2(p.y - C2.c.y, p.x - C2.c.x))) out.push_back(p);
      if (h < 1e-12) break;
    }
  };
  const bool al = A.type == SkEntityType::Line, bl = B.type == SkEntityType::Line;
  if (al && bl) lineLine(A, B);
  else if (al) lineCircle(A, B);
  else if (bl) lineCircle(B, A);
  else circleCircle(A, B);
  return out;
}
// parameter of p on curve c: line t, circle/arc angle (absolute, [0,2pi))
double paramOf(const Curve& c, const P2& p) {
  if (c.type == SkEntityType::Line) {
    const double dx = c.b.x - c.a.x, dy = c.b.y - c.a.y, l2 = dx * dx + dy * dy;
    return l2 > 0 ? ((p.x - c.a.x) * dx + (p.y - c.a.y) * dy) / l2 : 0;
  }
  return norm2pi(std::atan2(p.y - c.c.y, p.x - c.c.x));
}

struct Cut { double t; int by; };   // parameter + id of the cutting entity
std::vector<Cut> cuts(const Sketch& sk, int entityId) {
  std::vector<Cut> out;
  const SkEntity* e = sk.entity(entityId);
  if (!e) return out;
  const Curve me = curveOf(sk, *e);
  for (const auto& o : sk.entities) {
    if (o.id == entityId) continue;
    for (const P2& p : intersect(me, curveOf(sk, o))) out.push_back({paramOf(me, p), o.id});
  }
  return out;
}

void removeEntity(Sketch& s, int id) {
  const SkEntity* e = s.entity(id);
  if (!e) return;
  const std::vector<int> pts = e->points;
  s.entities.erase(std::remove_if(s.entities.begin(), s.entities.end(), [&](const SkEntity& x) { return x.id == id; }), s.entities.end());
  std::set<int> dead{id};
  for (int pid : pts) {
    if (pid == 0) continue;
    bool used = false;
    for (const auto& en : s.entities) if (std::find(en.points.begin(), en.points.end(), pid) != en.points.end()) used = true;
    if (!used) dead.insert(pid);
  }
  s.constraints.erase(std::remove_if(s.constraints.begin(), s.constraints.end(), [&](const SkConstraint& c) {
                        for (int r : c.refs) if (dead.count(r)) return true;
                        return false;
                      }), s.constraints.end());
  s.points.erase(std::remove_if(s.points.begin(), s.points.end(), [&](const SkPoint& p) { return p.id != 0 && dead.count(p.id); }), s.points.end());
}

// constraints on `id` that stay meaningful after its length / sweep changed
void dropChangedConstraints(Sketch& s, int id, bool keepHV, std::vector<SkConstraint>* hvOut) {
  std::vector<SkConstraint> keep;
  for (const auto& c : s.constraints) {
    const bool refs = std::find(c.refs.begin(), c.refs.end(), id) != c.refs.end();
    if (!refs) { keep.push_back(c); continue; }
    const bool hv = c.type == SkConstraintType::Horizontal || c.type == SkConstraintType::Vertical;
    const bool radial = c.type == SkConstraintType::Radius || c.type == SkConstraintType::Diameter ||
                        c.type == SkConstraintType::Concentric || c.type == SkConstraintType::Tangent;
    if ((hv && keepHV) || radial) { keep.push_back(c); if (hv && hvOut) hvOut->push_back(c); }
  }
  s.constraints = keep;
}
} // namespace

std::vector<double> trimCutParameters(const Sketch& sk, int entityId) {
  std::vector<double> out;
  for (const auto& c : cuts(sk, entityId)) out.push_back(c.t);
  std::sort(out.begin(), out.end());
  return out;
}

bool trimSketchEntity(const Sketch& in, int entityId, double u, double v, Sketch& out, QString* msg) {
  out = in;
  const SkEntity* e = in.entity(entityId);
  if (!e) { if (msg) *msg = "eleman bulunamadı"; return false; }
  const Curve me = curveOf(in, *e);
  const P2 click{u, v};
  std::vector<Cut> cs = cuts(in, entityId);

  if (e->type == SkEntityType::Line) {
    const double tc = std::clamp(paramOf(me, click), 0.0, 1.0);
    double lo = 0, hi = 1;
    int byLo = -1, byHi = -1;
    for (const auto& c : cs) {
      if (c.t > 1e-7 && c.t < tc && c.t > lo) { lo = c.t; byLo = c.by; }
      if (c.t < 1 - 1e-7 && c.t > tc && c.t < hi) { hi = c.t; byHi = c.by; }
    }
    if (byLo < 0 && byHi < 0) {
      removeEntity(out, entityId);
      if (msg) *msg = "Kesişimi olmayan çizgi silindi.";
      return true;
    }
    const int p0 = e->points[0], p1 = e->points[1];
    const bool construction = e->construction;
    std::vector<SkConstraint> hv;
    dropChangedConstraints(out, entityId, true, &hv);
    auto at = [&](double t) { return P2{me.a.x + t * (me.b.x - me.a.x), me.a.y + t * (me.b.y - me.a.y)}; };
    bool firstKept = false;
    if (byLo >= 0) {
      const P2 q = at(lo);
      const int np = out.addPoint(q.x, q.y);
      out.entity(entityId)->points = {p0, np};
      out.addConstraint(SkConstraintType::PointOnEntity, {np, byLo});
      firstKept = true;
    }
    if (byHi >= 0) {
      const P2 q = at(hi);
      const int np = out.addPoint(q.x, q.y);
      int lid;
      if (!firstKept) { out.entity(entityId)->points = {np, p1}; lid = entityId; }
      else {
        lid = out.addLine(np, p1, construction);
        for (const auto& c : hv) out.addConstraint(c.type, {lid});
      }
      out.addConstraint(SkConstraintType::PointOnEntity, {np, byHi});
    }
    // the far end point that is no longer used (the trimmed piece touched a free end)
    for (int pid : {p0, p1}) {
      if (pid == 0) continue;
      bool used = false;
      for (const auto& en : out.entities) if (std::find(en.points.begin(), en.points.end(), pid) != en.points.end()) used = true;
      if (!used) {
        out.constraints.erase(std::remove_if(out.constraints.begin(), out.constraints.end(), [&](const SkConstraint& c) {
                                return std::find(c.refs.begin(), c.refs.end(), pid) != c.refs.end();
                              }), out.constraints.end());
        out.points.erase(std::remove_if(out.points.begin(), out.points.end(), [&](const SkPoint& p) { return p.id == pid; }), out.points.end());
      }
    }
    if (msg) *msg = "Çizgi parçası kırpıldı.";
    return true;
  }

  // circle / arc: angles relative to the start (arc) or to the click (circle)
  const double ac = norm2pi(std::atan2(click.y - me.c.y, click.x - me.c.x));
  if (e->type == SkEntityType::Circle) {
    if (cs.size() < 2) {
      if (cs.empty()) { removeEntity(out, entityId); if (msg) *msg = "Kesişimi olmayan çember silindi."; return true; }
      if (msg) *msg = "Çemberi kırpmak için en az iki kesişim gerekir.";
      return false;
    }
    // nearest cut CCW after the click (hi) and CW before it (lo)
    double best1 = 1e9, best2 = 1e9;
    Cut hiC{0, -1}, loC{0, -1};
    for (const auto& c : cs) {
      const double fwd = norm2pi(c.t - ac), bwd = norm2pi(ac - c.t);
      if (fwd > 1e-9 && fwd < best1) { best1 = fwd; hiC = c; }
      if (bwd > 1e-9 && bwd < best2) { best2 = bwd; loC = c; }
    }
    if (hiC.by < 0 || loC.by < 0) { if (msg) *msg = "kesişim bulunamadı"; return false; }
    const int center = e->points[0];
    const P2 ps{me.c.x + me.r * std::cos(hiC.t), me.c.y + me.r * std::sin(hiC.t)};
    const P2 pe{me.c.x + me.r * std::cos(loC.t), me.c.y + me.r * std::sin(loC.t)};
    const int sId = out.addPoint(ps.x, ps.y), eId = out.addPoint(pe.x, pe.y);
    SkEntity* ne = out.entity(entityId);
    ne->type = SkEntityType::Arc;
    ne->points = {center, sId, eId};
    out.addConstraint(SkConstraintType::PointOnEntity, {sId, hiC.by});
    out.addConstraint(SkConstraintType::PointOnEntity, {eId, loC.by});
    if (msg) *msg = "Çember yaya dönüştürüldü (parça kırpıldı).";
    return true;
  }

  // arc
  const double rc = norm2pi(ac - me.a0);
  double lo = 0, hi = me.sweep;
  int byLo = -1, byHi = -1;
  for (const auto& c : cs) {
    const double rel = norm2pi(c.t - me.a0);
    if (rel > 1e-7 && rel < rc && rel > lo) { lo = rel; byLo = c.by; }
    if (rel < me.sweep - 1e-7 && rel > rc && rel < hi) { hi = rel; byHi = c.by; }
  }
  if (byLo < 0 && byHi < 0) { removeEntity(out, entityId); if (msg) *msg = "Kesişimi olmayan yay silindi."; return true; }
  const int center = e->points[0], st = e->points[1], en = e->points[2];
  const bool construction = e->construction;
  dropChangedConstraints(out, entityId, false, nullptr);
  auto at = [&](double rel) { return P2{me.c.x + me.r * std::cos(me.a0 + rel), me.c.y + me.r * std::sin(me.a0 + rel)}; };
  bool firstKept = false;
  if (byLo >= 0) {
    const P2 q = at(lo);
    const int np = out.addPoint(q.x, q.y);
    out.entity(entityId)->points = {center, st, np};
    out.addConstraint(SkConstraintType::PointOnEntity, {np, byLo});
    firstKept = true;
  }
  if (byHi >= 0) {
    const P2 q = at(hi);
    const int np = out.addPoint(q.x, q.y);
    if (!firstKept) out.entity(entityId)->points = {center, np, en};
    else out.addArc(center, np, en, construction);
    out.addConstraint(SkConstraintType::PointOnEntity, {np, byHi});
  }
  for (int pid : {st, en}) {
    if (pid == 0) continue;
    bool used = false;
    for (const auto& x : out.entities) if (std::find(x.points.begin(), x.points.end(), pid) != x.points.end()) used = true;
    if (!used) {
      out.constraints.erase(std::remove_if(out.constraints.begin(), out.constraints.end(), [&](const SkConstraint& c) {
                              return std::find(c.refs.begin(), c.refs.end(), pid) != c.refs.end();
                            }), out.constraints.end());
      out.points.erase(std::remove_if(out.points.begin(), out.points.end(), [&](const SkPoint& p) { return p.id == pid; }), out.points.end());
    }
  }
  if (msg) *msg = "Yay parçası kırpıldı.";
  return true;
}

} // namespace pcd
