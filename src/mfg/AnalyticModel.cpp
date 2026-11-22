// PersistCAD mfg — analytic CSG model (no OCCT booleans): 2D curve math, sketch placement,
// prisms / holes / pattern instances and the history-replaying material classifier.
#include "MfgInternal.h"

#include "pc/Expression.h"
#include "pc/SketchSolver.h"

#include <gp_Ax1.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace pc::mfg::detail {

// ============================================================================ 2D math
double norm(V2 a) { return std::hypot(a.x, a.y); }
V2 normalized(V2 a) {
  double n = norm(a);
  return n < 1e-300 ? V2{1, 0} : V2{a.x / n, a.y / n};
}

static double wrap2pi(double a) {
  a = std::fmod(a, 2 * kPi);
  if (a < 0) a += 2 * kPi;
  return a;
}

V2 Seg::point(double t) const {
  if (kind == SegKind::Line) return a + (b - a) * t;
  double ang = a0 + sweep * t;
  return {c.x + r * std::cos(ang), c.y + r * std::sin(ang)};
}

V2 Seg::tangent(double t) const {
  if (kind == SegKind::Line) return normalized(b - a);
  double ang = a0 + sweep * t;
  double s = sweep >= 0 ? 1.0 : -1.0;
  return {-std::sin(ang) * s, std::cos(ang) * s};
}

V2 Seg::normal(double t) const {
  if (kind == SegKind::Line) {
    V2 d = tangent(t);
    return {-d.y, d.x};
  }
  double ang = a0 + sweep * t;
  return {std::cos(ang), std::sin(ang)};
}

double Seg::length() const { return kind == SegKind::Line ? norm(b - a) : std::fabs(sweep) * r; }

bool Seg::angleInSpan(double ang) const {
  if (kind == SegKind::Circle) return true;
  const double tol = 1e-9;
  if (sweep >= 0) return wrap2pi(ang - a0) <= sweep + tol || wrap2pi(ang - a0) >= 2 * kPi - tol;
  return wrap2pi(a0 - ang) <= -sweep + tol || wrap2pi(a0 - ang) >= 2 * kPi - tol;
}

void Seg::intersectRay(V2 o, V2 d, double tmin, std::vector<double>& out) const {
  if (kind == SegKind::Line) {
    V2 e = b - a;
    double den = cross(d, e);
    if (std::fabs(den) < 1e-14 * std::max(1.0, norm(e))) return;
    V2 ao = a - o;
    double t = cross(ao, e) / den;
    double s = cross(ao, d) / den;
    if (s >= -1e-12 && s <= 1 + 1e-12 && t >= tmin) out.push_back(t);
    return;
  }
  V2 f = o - c;
  double B = dot(f, d), C = dot(f, f) - r * r;
  double disc = B * B - C;
  if (disc < 0) return;
  double sq = std::sqrt(disc);
  for (double t : {-B - sq, -B + sq}) {
    if (t < tmin) continue;
    V2 p = o + d * t;
    if (angleInSpan(std::atan2(p.y - c.y, p.x - c.x))) out.push_back(t);
  }
}

double Seg::distance(V2 p) const {
  if (kind == SegKind::Line) {
    V2 e = b - a;
    double L2 = dot(e, e);
    double t = L2 < 1e-300 ? 0 : std::clamp(dot(p - a, e) / L2, 0.0, 1.0);
    return norm(p - (a + e * t));
  }
  double dc = norm(p - c);
  if (kind == SegKind::Circle || angleInSpan(std::atan2(p.y - c.y, p.x - c.x))) return std::fabs(dc - r);
  return std::min(norm(p - a), norm(p - b));
}

int Seg::crossings(V2 p) const {
  if (kind == SegKind::Line) {
    if ((a.y > p.y) == (b.y > p.y)) return 0;
    double x = a.x + (p.y - a.y) * (b.x - a.x) / (b.y - a.y);
    return x > p.x ? 1 : 0;
  }
  double dy = p.y - c.y;
  if (std::fabs(dy) >= r) return 0;
  double dx = std::sqrt(r * r - dy * dy);
  int n = 0;
  for (double x : {c.x - dx, c.x + dx})
    if (x > p.x && angleInSpan(std::atan2(dy, x - c.x))) ++n;
  return n;
}

bool insideEvenOdd(const std::vector<Seg>& segs, V2 p) {
  V2 q{p.x, p.y + 1.234567e-7};
  int n = 0;
  for (auto& s : segs) n += s.crossings(q);
  return (n & 1) == 1;
}

static bool segsIntersect(const Seg& s, const Seg& h) {
  V2 e = s.b - s.a, f = h.b - h.a;
  double den = cross(e, f);
  if (std::fabs(den) < 1e-14) return false;
  double t = cross(h.a - s.a, f) / den, u = cross(h.a - s.a, e) / den;
  return t >= 0 && t <= 1 && u >= 0 && u <= 1;
}

double curveDistance(const Seg& s, const Seg& h) {
  if (s.kind == SegKind::Line && h.kind == SegKind::Line) {
    if (segsIntersect(s, h)) return 0;
    return std::min({s.distance(h.a), s.distance(h.b), h.distance(s.a), h.distance(s.b)});
  }
  if (s.kind == SegKind::Circle && h.kind == SegKind::Circle) {
    double d = norm(s.c - h.c);
    double rmax = std::max(s.r, h.r), rmin = std::min(s.r, h.r);
    if (d >= s.r + h.r) return d - s.r - h.r;
    if (d + rmin <= rmax) return rmax - d - rmin;
    return 0;
  }
  auto lineCircle = [](const Seg& L, const Seg& C) {
    double dc = L.distance(C.c);
    if (dc >= C.r) return dc - C.r;
    double dmax = std::max(norm(L.a - C.c), norm(L.b - C.c));
    if (dmax <= C.r) return C.r - dmax;
    return 0.0;
  };
  if (s.kind == SegKind::Line && h.kind == SegKind::Circle) return lineCircle(s, h);
  if (s.kind == SegKind::Circle && h.kind == SegKind::Line) return lineCircle(h, s);
  // arcs involved: dense sampling of the arc against the exact distance to the other curve
  const Seg& arc = s.kind == SegKind::Arc ? s : h;
  const Seg& other = s.kind == SegKind::Arc ? h : s;
  double best = std::numeric_limits<double>::infinity();
  const int N = 256;
  for (int i = 0; i <= N; ++i) best = std::min(best, other.distance(arc.point(double(i) / N)));
  return best;
}

double regionMinThickness(const std::vector<Seg>& segs) {
  double best = std::numeric_limits<double>::infinity();
  double size = 0;
  for (auto& s : segs) size = std::max(size, s.length());
  const double dl = std::max(1e-6, 1e-6 * size);
  std::vector<double> ts;
  for (auto& s : segs) {
    int N = s.kind == SegKind::Line ? 16 : 32;
    for (int i = 0; i < N; ++i) {
      double t = (i + 0.5) / N;
      V2 q = s.point(t), n = s.normal(t);
      bool ip = insideEvenOdd(segs, q + n * dl), im = insideEvenOdd(segs, q - n * dl);
      if (ip == im) continue;
      V2 d = ip ? n : n * -1.0;
      double tmin = 1e3 * dl;
      double first = std::numeric_limits<double>::infinity();
      const Seg* hitSeg = nullptr;
      for (auto& h : segs) {
        ts.clear();
        h.intersectRay(q, d, tmin, ts);
        for (double tt : ts)
          if (tt < first) { first = tt; hitSeg = &h; }
      }
      if (!hitSeg) continue;
      // facing test: the hit curve must be roughly opposite (incidence < 60 deg)
      V2 hp = q + d * first;
      V2 hn = hitSeg->kind == SegKind::Line ? hitSeg->normal(0) : normalized(hp - hitSeg->c);
      if (std::fabs(dot(hn, d)) < 0.5) continue;
      best = std::min(best, first);
    }
  }
  return best;
}

// ============================================================================ helpers
std::string fmt(double v, int prec) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
  return buf;
}

gp_Dir safeDir(double x, double y, double z, const gp_Dir& def) {
  double n = std::sqrt(x * x + y * y + z * z);
  if (n < 1e-12) return def;
  return gp_Dir(x / n, y / n, z / n);
}

double evalProp(const Feature& f, const std::string& key, const ParamValues& v, double def, bool* ok) {
  auto it = f.props.find(key);
  if (it == f.props.end() || it->second.empty()) return def;
  try {
    double r = evalWith(it->second, v);
    if (!std::isfinite(r)) throw ExprError("NaN");
    return r;
  } catch (const std::exception&) {
    if (ok) *ok = false;
    return def;
  }
}

std::vector<std::string> SketchGeo::exprs() const {
  std::vector<std::string> out;
  for (auto& c : solved.constraints)
    if (!c.valueExpr.empty()) out.push_back(c.valueExpr);
  if (!solved.plane.offsetExpr.empty()) out.push_back(solved.plane.offsetExpr);
  return out;
}

// ============================================================================ Prism / HoleLoc
V2 Prism::toLocal(const gp_Pnt& p, double* w) const {
  gp_Vec d(frame.Location(), p);
  if (w) *w = d.Dot(gp_Vec(frame.Direction()));
  return {d.Dot(gp_Vec(frame.XDirection())), d.Dot(gp_Vec(frame.YDirection()))};
}

gp_Pnt Prism::toWorld(V2 uv, double w) const {
  gp_Pnt o = frame.Location();
  gp_Vec X(frame.XDirection()), Y(frame.YDirection()), N(frame.Direction());
  return o.Translated(X * uv.x + Y * uv.y + N * w);
}

bool Prism::contains(const gp_Pnt& p) const {
  double w;
  V2 uv = toLocal(p, &w);
  if (w < w0 - 1e-9 || w > w1 + 1e-9) return false;
  return inside2d(uv);
}

std::string Prism::faceName(const std::string& rest) const {
  if (instance == 0) return fid + "." + rest;
  return fid + ".i" + std::to_string(instance) + "." + srcFid + "." + rest;
}

double HoleLoc::radiusAt(double s) const {
  if (s < -1e-7) return -1;
  if (!through && s > depth + tipLen) return -1;
  double rr = r;
  if (!through && s > depth) rr = tipLen > 0 ? r * (1 - (s - depth) / tipLen) : -1;
  if (kind == 1 && s <= cbDepth) rr = std::max(rr, cbR);
  if (kind == 2 && s <= csLen && csLen > 0) rr = std::max(rr, csR - (csR - r) * s / csLen);
  return rr;
}

bool HoleLoc::contains(const gp_Pnt& p) const {
  gp_Vec v(top, p);
  double s = v.Dot(gp_Vec(axis));
  double rr = radiusAt(s);
  if (rr <= 0) return false;
  gp_Vec radial = v - gp_Vec(axis) * s;
  return radial.Magnitude() < rr;
}

std::string HoleLoc::faceName(const std::string& rest) const {
  std::string base = "h" + std::to_string(k) + "." + rest;
  if (instance == 0) return fid + "." + base;
  return fid + ".i" + std::to_string(instance) + "." + srcFid + "." + base;
}

// ============================================================================ AModel
static bool opActs(const std::vector<std::string>& tag, const std::string& body) {
  return tag.empty() || std::find(tag.begin(), tag.end(), body) != tag.end();
}

bool AModel::materialOfBody(const gp_Pnt& p, const std::string& body, int skipHole) const {
  return materialOfBodyUpTo(p, body, skipHole, int(ops.size()));
}

bool AModel::materialOfBodyUpTo(const gp_Pnt& p, const std::string& body, int skipHole, int opEnd) const {
  bool m = false;
  auto born = bodyBorn.find(body);
  int first = born == bodyBorn.end() ? 0 : std::max(0, born->second);
  for (int oi = first; oi < opEnd && oi < int(ops.size()); ++oi) {
    const OpRef& op = ops[oi];
    if (op.combine) {
      const CombineOp& C = combines[op.idx];
      if (C.target == body) {
        // tool material is only needed where it can change the result
        bool need = C.op == BodyOp::Join ? !m : m;
        if (!need) continue;
        bool t = false;
        for (auto& tb : C.tools)
          if (tb != body && materialOfBodyUpTo(p, tb, skipHole, oi)) { t = true; break; }
        if (C.op == BodyOp::Join) m = t;
        else if (C.op == BodyOp::Cut) m = !t;
        else m = t;
      } else if (!C.keepTools && std::find(C.tools.begin(), C.tools.end(), body) != C.tools.end()) {
        m = false;  // consumed into the target
      }
      continue;
    }
    if (op.hole) {
      if (op.idx == skipHole || !m || !opActs(holes[op.idx].bodies, body)) continue;
      if (holes[op.idx].contains(p)) m = false;
      continue;
    }
    const Prism& P = prisms[op.idx];
    if (!opActs(P.bodies, body)) continue;
    switch (P.op) {
      case BodyOp::New:
      case BodyOp::Join: if (!m) m = P.contains(p); break;
      case BodyOp::Cut: if (m && P.contains(p)) m = false; break;
      case BodyOp::Intersect: if (m) m = P.contains(p); break;
    }
  }
  return m;
}

bool AModel::material(const gp_Pnt& p, int skipHole) const {
  if (bodyNames.size() > 1) {
    for (auto& b : bodyNames)
      if (materialOfBody(p, b, skipHole)) return true;
    return false;
  }
  bool m = false;
  for (auto& op : ops) {
    if (op.combine) continue;  // needs two bodies
    if (op.hole) {
      if (op.idx == skipHole || !m) continue;
      if (holes[op.idx].contains(p)) m = false;
      continue;
    }
    const Prism& P = prisms[op.idx];
    switch (P.op) {
      case BodyOp::New:
      case BodyOp::Join: if (!m) m = P.contains(p); break;
      case BodyOp::Cut: if (m && P.contains(p)) m = false; break;
      case BodyOp::Intersect: if (m) m = P.contains(p); break;
    }
  }
  return m;
}

const SketchGeo* AModel::sketch(const std::string& fid) const {
  auto it = sketchIndex.find(fid);
  return it == sketchIndex.end() ? nullptr : &sketches[it->second];
}

const Prism* AModel::prismByFid(const std::string& fid) const {
  for (auto& p : prisms)
    if (p.fid == fid && p.instance == 0) return &p;
  return nullptr;
}

static std::string stripSplitSuffix(const std::string& n) {
  auto pos = n.rfind(':');
  if (pos == std::string::npos || pos + 1 >= n.size()) return n;
  for (size_t i = pos + 1; i < n.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(n[i]))) return n;
  return n.substr(0, pos);
}

bool AModel::facePlane(const std::string& faceNameIn, gp_Pnt& p, gp_Dir& n) const {
  std::string name = stripSplitSuffix(faceNameIn);
  auto dot1 = name.find('.');
  if (dot1 == std::string::npos) return false;
  std::string fid = name.substr(0, dot1), rest = name.substr(dot1 + 1);
  if (rest.rfind("tool.", 0) == 0) {  // v3: faces of a kept cut/intersect tool body ("<fid>.tool.<rest>")
    fid += ".tool";
    rest = rest.substr(5);
  }
  int instance = 0;
  if (rest.size() > 1 && rest[0] == 'i' && std::isdigit(static_cast<unsigned char>(rest[1]))) {
    auto d2 = rest.find('.');
    if (d2 == std::string::npos) return false;
    instance = std::atoi(rest.substr(1, d2 - 1).c_str());
    std::string rest2 = rest.substr(d2 + 1);  // "<srcFid>.<rest>"
    auto d3 = rest2.find('.');
    if (d3 == std::string::npos) return false;
    rest = rest2.substr(d3 + 1);
  }
  for (auto& P : prisms) {
    if (P.fid != fid || P.instance != instance) continue;
    const double flip = P.op == BodyOp::Cut ? -1.0 : 1.0;
    gp_Vec N(P.frame.Direction());
    if (rest == "end" || rest == "start") {
      bool end = rest == "end";
      double w = (end == (P.extSign > 0)) ? P.w1 : P.w0;
      double s = (end ? P.extSign : -P.extSign) * flip;
      if (P.through && P.op == BodyOp::Cut) return false;
      p = P.frame.Location().Translated(N * w);
      n = gp_Dir(N * s);
      return true;
    }
    if (rest.rfind("side.e", 0) == 0) {
      int eid = std::atoi(rest.substr(6).c_str());
      for (auto& sg : P.segs) {
        if (sg.entityId != eid || sg.kind != SegKind::Line) continue;
        V2 mid = sg.point(0.5), nn = sg.normal(0.5);
        if (P.inside2d(mid + nn * 1e-6)) nn = nn * -1.0;
        nn = nn * flip;
        p = P.toWorld(mid, P.w0);
        gp_Vec n3 = gp_Vec(P.frame.XDirection()) * nn.x + gp_Vec(P.frame.YDirection()) * nn.y;
        if (n3.Magnitude() < 1e-12) return false;
        n = gp_Dir(n3);
        return true;
      }
    }
    return false;
  }
  // hole floors
  if (rest.size() > 1 && rest[0] == 'h') {
    auto d2 = rest.find('.');
    if (d2 == std::string::npos) return false;
    int k = std::atoi(rest.substr(1, d2 - 1).c_str());
    std::string what = rest.substr(d2 + 1);
    for (auto& H : holes) {
      if (H.fid != fid || H.instance != instance || H.k != k) continue;
      if (what == "bottom" && !H.through && H.tipLen == 0) {
        p = H.top.Translated(gp_Vec(H.axis) * H.depth);
        n = H.axis.Reversed();
        return true;
      }
      if (what == "cb_floor" && H.kind == 1) {
        p = H.top.Translated(gp_Vec(H.axis) * H.cbDepth);
        n = H.axis.Reversed();
        return true;
      }
      return false;
    }
  }
  return false;
}

static void segSamples(const Seg& s, std::vector<V2>& out) {
  if (s.kind == SegKind::Line) {
    out.push_back(s.a);
    out.push_back(s.b);
    return;
  }
  const int N = 72;
  for (int i = 0; i <= N; ++i) out.push_back(s.point(double(i) / N));
}

bool AModel::extents(const gp_Dir& d, double& lo, double& hi) const {
  lo = std::numeric_limits<double>::infinity();
  hi = -lo;
  gp_Vec dv(d);
  std::vector<V2> pts;
  for (auto& P : prisms) {
    if (P.op != BodyOp::New && P.op != BodyOp::Join) continue;
    pts.clear();
    for (auto& s : P.segs) segSamples(s, pts);
    for (auto& uv : pts)
      for (double w : {P.w0, P.w1}) {
        double x = gp_Vec(P.toWorld(uv, w).XYZ()).Dot(dv);
        lo = std::min(lo, x);
        hi = std::max(hi, x);
      }
  }
  // revolves: sample points of the profile swept around the axis
  for (auto& R : revolves) {
    if (R.sketch < 0) continue;
    const SketchGeo& g = sketches[R.sketch];
    if (!g.frameOk) continue;
    gp_Ax1 ax(g.frame.Location(), g.frame.XDirection());
    const Feature* f = doc ? doc->feature(R.fid) : nullptr;
    std::string axis = f ? f->prop("axis", "U") : "U";
    if (axis == "V") ax = gp_Ax1(g.frame.Location(), g.frame.YDirection());
    else if (axis != "U") {
      int eid = std::atoi(axis.c_str());
      if (auto* e = g.solved.entity(eid); e && e->type == SkEntityType::Line && e->points.size() == 2) {
        auto* p1 = g.solved.point(e->points[0]);
        auto* p2 = g.solved.point(e->points[1]);
        if (p1 && p2) {
          gp_Pnt a = g.frame.Location().Translated(gp_Vec(g.frame.XDirection()) * p1->x + gp_Vec(g.frame.YDirection()) * p1->y);
          gp_Pnt b = g.frame.Location().Translated(gp_Vec(g.frame.XDirection()) * p2->x + gp_Vec(g.frame.YDirection()) * p2->y);
          if (a.Distance(b) > 1e-9) ax = gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
        }
      }
    }
    pts.clear();
    for (auto& s : g.segs) segSamples(s, pts);
    const int NA = 36;
    for (auto& uv : pts) {
      gp_Pnt p = g.frame.Location().Translated(gp_Vec(g.frame.XDirection()) * uv.x + gp_Vec(g.frame.YDirection()) * uv.y);
      for (int i = 0; i <= NA; ++i) {
        gp_Pnt q = p.Rotated(ax, R.angle * kPi / 180.0 * i / NA);
        double x = gp_Vec(q.XYZ()).Dot(dv);
        lo = std::min(lo, x);
        hi = std::max(hi, x);
      }
    }
  }
  return lo <= hi;
}

// ============================================================================ build
static gp_Ax3 frameFromPlane(const gp_Pnt& p, const gp_Dir& n, double offset) {
  gp_Vec N(n);
  gp_Pnt o(N.XYZ() * gp_Vec(p.XYZ()).Dot(N));  // projection of the world origin onto the plane
  gp_Vec u = gp_Vec(1, 0, 0) - N * N.X();
  if (u.Magnitude() < 1e-6) u = gp_Vec(0, 1, 0) - N * N.Y();
  o.Translate(N * offset);
  return gp_Ax3(o, n, gp_Dir(u));
}

static void buildSegs(SketchGeo& g) {
  const Sketch& sk = g.solved;
  std::set<int> used;
  for (auto& e : sk.entities)
    for (int pid : e.points) used.insert(pid);
  for (auto& e : sk.entities) {
    if (e.construction) continue;
    Seg s;
    s.entityId = e.id;
    if (e.type == SkEntityType::Line && e.points.size() == 2) {
      auto *p1 = sk.point(e.points[0]), *p2 = sk.point(e.points[1]);
      if (!p1 || !p2) continue;
      s.kind = SegKind::Line;
      s.a = {p1->x, p1->y};
      s.b = {p2->x, p2->y};
      if (norm(s.b - s.a) < 1e-12) continue;
    } else if (e.type == SkEntityType::Circle && !e.points.empty()) {
      auto* c = sk.point(e.points[0]);
      if (!c || e.radius <= 0) continue;
      s.kind = SegKind::Circle;
      s.c = {c->x, c->y};
      s.r = e.radius;
      s.a0 = 0;
      s.sweep = 2 * kPi;
      s.a = s.b = s.point(0);
      g.holePoints.push_back({c->id, s.c});
    } else if (e.type == SkEntityType::Arc && e.points.size() == 3) {
      auto *c = sk.point(e.points[0]), *p1 = sk.point(e.points[1]), *p2 = sk.point(e.points[2]);
      if (!c || !p1 || !p2) continue;
      s.kind = SegKind::Arc;
      s.c = {c->x, c->y};
      s.a = {p1->x, p1->y};
      s.b = {p2->x, p2->y};
      s.r = norm(s.a - s.c);
      if (s.r <= 0) continue;
      s.a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x);
      double a1 = std::atan2(s.b.y - s.c.y, s.b.x - s.c.x);
      s.sweep = wrap2pi(a1 - s.a0);
      if (s.sweep < 1e-12) s.sweep = 2 * kPi;
    } else {
      continue;
    }
    g.segs.push_back(s);
  }
  for (auto& p : sk.points)
    if (p.id != 0 && !used.count(p.id)) g.holePoints.push_back({p.id, {p.x, p.y}});
  // same ordering / de-duplication as the model's Hole feature (k index of "<fid>.h<k>.*")
  std::stable_sort(g.holePoints.begin(), g.holePoints.end(), [](auto& A, auto& B) {
    if (std::fabs(A.second.x - B.second.x) > 1e-7) return A.second.x < B.second.x;
    return A.second.y < B.second.y - 1e-7;
  });
  std::vector<std::pair<int, V2>> uniq;
  for (auto& h : g.holePoints)
    if (uniq.empty() || norm(uniq.back().second - h.second) > 1e-6) uniq.push_back(h);
  g.holePoints.swap(uniq);
}

static gp_Trsf patternTrsf(const Feature& f, const ParamValues& v, int k, int count) {
  gp_Trsf T;
  std::string kind = f.prop("kind", "linear");
  if (kind == "circular") {
    double ang = evalProp(f, "angle", v, 360);
    double step = std::fabs(std::fabs(ang) - 360) < 1e-9 ? ang / count : (count > 1 ? ang / (count - 1) : 0);
    std::string ax = f.prop("axis", "Z");
    gp_Dir d = ax == "X" ? gp_Dir(1, 0, 0) : ax == "Y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
    T.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), d), step * k * kPi / 180.0);
  } else {
    double sp = evalProp(f, "spacing", v, 10);
    gp_Dir d = safeDir(evalProp(f, "dirX", v, 1), evalProp(f, "dirY", v, 0), evalProp(f, "dirZ", v, 0), gp_Dir(1, 0, 0));
    T.SetTranslation(gp_Vec(d) * (sp * k));
  }
  return T;
}

std::vector<std::string> splitNames(const std::string& csv) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    size_t a = cur.find_first_not_of(" \t"), b = cur.find_last_not_of(" \t");
    if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
    cur.clear();
  };
  for (char ch : csv) {
    if (ch == ',') flush();
    else cur += ch;
  }
  flush();
  return out;
}

AModel buildModel(const Document& doc, const ParamValues& values) {
  AModel m;
  m.doc = &doc;
  m.values = values;
  bool hasBody = false;
  // v2 body simulation (BODIES contract in pc/Feature.h): which body does each feature act on?
  auto addBody = [&](const std::string& n) {
    if (std::find(m.bodyNames.begin(), m.bodyNames.end(), n) == m.bodyNames.end()) {
      m.bodyNames.push_back(n);
      m.bodyBorn[n] = int(m.ops.size());  // index of the op about to be appended
    }
  };
  const FrameProvider* provider = currentFrameProvider();
  auto bodyOfRef = [&](const std::string& ref) -> std::string {
    // "edge(Extrude1.side.e3|Extrude2.end)", "face:Extrude1.end", "Extrude1.end"
    std::string r = ref;
    if (r.rfind("face:", 0) == 0) r = r.substr(5);
    if (r.rfind("edge(", 0) == 0) r = r.substr(5);
    auto dot = r.find('.');
    if (dot == std::string::npos) return "";
    auto it = m.featureBody.find(r.substr(0, dot));
    return it == m.featureBody.end() ? "" : it->second;
  };
  auto noteFeature = [&](const Feature& f, const std::vector<std::string>& tag) {
    std::string b;
    if (tag.size() == 1) b = tag[0];
    else if (tag.empty() && m.bodyNames.size() == 1) b = m.bodyNames[0];
    m.featureBody[f.id] = b;
  };
  for (int fi = 0; fi < int(doc.features.size()); ++fi) {
    const Feature& f = doc.features[fi];
    if (f.suppressed) continue;
    switch (f.type) {
      case FeatureType::Sketch: {
        if (!f.sketch) break;
        SketchGeo g;
        g.fid = f.id;
        g.featIndex = fi;
        g.solved = *f.sketch;
        try {
          sketch::SolveResult r = sketch::solve(g.solved, values);
          g.solveOk = r.ok();
          g.solveMsg = r.message;
        } catch (const std::exception& ex) {
          g.solveOk = false;
          g.solveMsg = ex.what();
        }
        if (!g.solveOk) {
          g.solved = *f.sketch;  // fall back to the stored (last solved) coordinates
          m.unsolvedSketches.push_back(f.id);
        }
        double off = 0;
        try {
          off = f.sketch->plane.offsetExpr.empty() ? 0 : evalWith(f.sketch->plane.offsetExpr, values);
        } catch (const std::exception&) {}
        if (f.sketch->plane.kind == SketchPlaneKind::OnFace) {
          gp_Pnt p;
          gp_Dir n;
          if (m.facePlane(f.sketch->plane.faceRef, p, n)) {
            g.frame = frameFromPlane(p, n, off);
            g.frameOk = true;
          } else if (provider && provider->frame(f, values, m, g.frame)) {
            g.frameOk = true;  // v3 phase 2: face of a datum-plane feature / Combine result, from the B-Rep
          }
        } else if (f.sketch->plane.kind == SketchPlaneKind::OnPlane) {
          // Datum planes are defined on B-Rep edges/faces, which the geometry-free model does not have.
          // Phase 1 (no provider): frameOk stays false, features on them are skipped. Phase 2: the frame comes
          // from the regenerated model's datum plane (+ sketch offset), as in pc::model::sketchFrame.
          if (provider && provider->frame(f, values, m, g.frame)) g.frameOk = true;
        } else {
          g.frame = sketch::standardFrame(f.sketch->plane.kind, off);
          g.frameOk = true;
        }
        buildSegs(g);
        m.sketchIndex[f.id] = int(m.sketches.size());
        m.sketches.push_back(std::move(g));
        break;
      }
      case FeatureType::Extrude: {
        auto it = m.sketchIndex.find(f.prop("sketch"));
        if (it == m.sketchIndex.end()) break;
        const SketchGeo& g = m.sketches[it->second];
        if (!g.frameOk || g.segs.empty()) break;
        bool ok = true;
        double d = evalProp(f, "distance", values, 10, &ok);
        if (!ok) break;
        Prism P;
        P.fid = P.srcFid = f.id;
        P.featIndex = fi;
        P.sketch = it->second;
        P.frame = g.frame;
        P.segs = g.segs;
        P.distanceExpr = f.prop("distance");
        P.draft = evalProp(f, "draft", values, 0);
        std::string op = f.prop("op", hasBody ? "join" : "new");
        P.op = op == "cut" ? BodyOp::Cut : op == "intersect" ? BodyOp::Intersect : op == "new" ? BodyOp::New : BodyOp::Join;
        P.through = P.op == BodyOp::Cut && f.prop("through", "0") == "1";
        std::string dir = f.prop("direction", "normal");
        double L = P.through ? kBig : std::fabs(d);
        if (dir == "symmetric") { P.w0 = -L / 2; P.w1 = L / 2; P.extSign = 1; if (P.through) { P.w0 = -kBig; P.w1 = kBig; } }
        else if (dir == "reverse") { P.w0 = -L; P.w1 = 0; P.extSign = -1; }
        else { P.w0 = 0; P.w1 = L; P.extSign = 1; }
        if (d < 0 && !P.through && dir != "symmetric") { std::swap(P.w0, P.w1); P.w0 = -P.w0; P.w1 = -P.w1; P.extSign = -P.extSign; }
        if (P.op == BodyOp::New || P.op == BodyOp::Join) hasBody = true;
        if (P.op == BodyOp::New) {
          std::string bn = f.prop("bodyName").empty() ? f.id : f.prop("bodyName");
          addBody(bn);
          P.bodies = {bn};
        } else {
          P.bodies = splitNames(f.prop("targets"));
        }
        noteFeature(f, P.bodies);
        // v3: keepTool=1 (cut/intersect) keeps a copy of the tool as a new body "<bodyName|fid.tool>"; its faces
        // are "<fid>.tool.<rest>" (the copy is a prism with fid "<fid>.tool"; issues are reported on <fid>)
        std::optional<Prism> kept;
        if ((P.op == BodyOp::Cut || P.op == BodyOp::Intersect) && f.prop("keepTool", "0") == "1" && !P.through) {
          kept = P;
          kept->fid = kept->srcFid = f.id + ".tool";
          kept->op = BodyOp::New;
        }
        m.ops.push_back({false, int(m.prisms.size())});
        m.prisms.push_back(std::move(P));
        if (kept) {
          std::string bn = f.prop("bodyName").empty() ? f.id + ".tool" : f.prop("bodyName");
          addBody(bn);
          kept->bodies = {bn};
          m.featureBody[kept->fid] = bn;
          m.ops.push_back({false, int(m.prisms.size())});
          m.prisms.push_back(std::move(*kept));
        }
        break;
      }
      case FeatureType::Hole: {
        auto it = m.sketchIndex.find(f.prop("sketch"));
        if (it == m.sketchIndex.end()) break;
        const SketchGeo& g = m.sketches[it->second];
        if (!g.frameOk) break;
        bool ok = true;
        double dia = evalProp(f, "diameter", values, 0, &ok);
        if (!ok || dia <= 0) break;
        HoleLoc base;
        base.fid = base.srcFid = f.id;
        base.featIndex = fi;
        base.sketch = it->second;
        base.r = dia / 2;
        base.through = f.prop("through", "0") == "1";
        base.depth = base.through ? kBig : evalProp(f, "depth", values, 10);
        base.diameterExpr = f.prop("diameter");
        base.depthExpr = f.prop("depth");
        std::string kind = f.prop("kind", "simple");
        base.kind = kind == "counterbore" ? 1 : kind == "countersink" ? 2 : 0;
        if (base.kind == 1) {
          base.cbR = evalProp(f, "cbDiameter", values, dia * 1.8) / 2;
          base.cbDepth = evalProp(f, "cbDepth", values, dia);
        }
        if (base.kind == 2) {
          base.csR = evalProp(f, "csDiameter", values, dia * 2) / 2;
          double csA = evalProp(f, "csAngle", values, 90);
          base.csLen = csA > 0 && base.csR > base.r ? (base.csR - base.r) / std::tan(csA * kPi / 360.0) : 0;
        }
        double tip = evalProp(f, "tipAngle", values, 118);
        base.tipLen = (!base.through && tip > 0 && tip < 180) ? base.r / std::tan(tip * kPi / 360.0) : 0;
        base.axis = g.frame.Direction().Reversed();
        base.bodies = splitNames(f.prop("targets"));
        noteFeature(f, base.bodies);
        int k = 0;
        for (auto& [pid, uv] : g.holePoints) {
          HoleLoc H = base;
          H.k = k++;
          H.pointId = pid;
          H.uv = uv;
          H.top = g.frame.Location().Translated(gp_Vec(g.frame.XDirection()) * uv.x + gp_Vec(g.frame.YDirection()) * uv.y);
          m.ops.push_back({true, int(m.holes.size())});
          m.holes.push_back(H);
        }
        break;
      }
      case FeatureType::Fillet:
      case FeatureType::Chamfer: {
        EdgeTreat e;
        e.fid = f.id;
        e.chamfer = f.type == FeatureType::Chamfer;
        std::string key = e.chamfer ? "distance" : "radius";
        bool ok = true;
        e.size = evalProp(f, key, values, 0, &ok);
        if (!ok) break;
        e.expr = f.prop(key);
        e.refs = f.refs;
        m.featureBody[f.id] = f.refs.empty() ? "" : bodyOfRef(f.refs[0]);
        if (m.featureBody[f.id].empty() && m.bodyNames.size() == 1) m.featureBody[f.id] = m.bodyNames[0];
        m.edgeTreats.push_back(e);
        break;
      }
      case FeatureType::Shell: {
        ShellInfo s;
        s.fid = f.id;
        bool ok = true;
        s.thickness = evalProp(f, "thickness", values, 0, &ok);
        if (!ok) break;
        s.expr = f.prop("thickness");
        s.refs = f.refs;
        s.outward = f.prop("direction", "inward") == "outward";
        m.featureBody[f.id] = f.refs.empty() ? "" : bodyOfRef(f.refs[0]);
        if (m.featureBody[f.id].empty() && m.bodyNames.size() == 1) m.featureBody[f.id] = m.bodyNames[0];
        m.shells.push_back(s);
        break;
      }
      case FeatureType::Revolve: {
        RevolveInfo r;
        r.fid = f.id;
        auto it = m.sketchIndex.find(f.prop("sketch"));
        r.sketch = it == m.sketchIndex.end() ? -1 : it->second;
        r.angle = evalProp(f, "angle", values, 360);
        m.revolves.push_back(r);
        {
          std::string rop = f.prop("op", hasBody ? "join" : "new");
          std::vector<std::string> tag;
          if (rop == "new") {
            std::string bn = f.prop("bodyName").empty() ? f.id : f.prop("bodyName");
            addBody(bn);
            tag = {bn};
          } else {
            tag = splitNames(f.prop("targets"));
          }
          noteFeature(f, tag);
        }
        hasBody = true;
        break;
      }
      case FeatureType::Pattern: {
        std::string src = f.prop("source");
        int count = int(std::lround(evalProp(f, "count", values, 1)));
        if (count < 2 || count > 1000) break;
        std::vector<std::string> patTag;
        if (f.prop("op") == "new") {
          std::string bn = f.prop("bodyName").empty() ? f.id : f.prop("bodyName");
          addBody(bn);
          patTag = {bn};
        }
        {
          auto sb = m.featureBody.find(src);
          m.featureBody[f.id] = !patTag.empty() ? patTag[0] : (sb == m.featureBody.end() ? "" : sb->second);
        }
        std::vector<Prism> srcP;
        std::vector<HoleLoc> srcH;
        for (auto& P : m.prisms)
          if (P.fid == src && P.instance == 0) srcP.push_back(P);
        for (auto& H : m.holes)
          if (H.fid == src && H.instance == 0) srcH.push_back(H);
        for (int k = 1; k < count; ++k) {
          gp_Trsf T = patternTrsf(f, values, k, count);
          for (auto P : srcP) {
            P.fid = f.id;
            P.instance = k;
            P.featIndex = fi;
            P.frame.Transform(T);
            if (!patTag.empty()) P.bodies = patTag;
            m.ops.push_back({false, int(m.prisms.size())});
            m.prisms.push_back(std::move(P));
          }
          for (auto H : srcH) {
            H.fid = f.id;
            H.instance = k;
            H.featIndex = fi;
            H.top.Transform(T);
            H.axis.Transform(T);
            if (!patTag.empty()) H.bodies = patTag;
            m.ops.push_back({true, int(m.holes.size())});
            m.holes.push_back(H);
          }
        }
        break;
      }
      case FeatureType::Plane:
        break;  // datum plane: no material
      case FeatureType::Transform:
        break;  // rigid body motion (CR-3): not replayed analytically; geometric checks see the moved bodies
      case FeatureType::Combine: {
        // v3: body-to-body boolean replayed by the per-body material classifier
        m.featureBody[f.id] = f.prop("target");
        CombineOp C;
        C.fid = f.id;
        std::string op = f.prop("op", "join");
        C.op = op == "cut" ? BodyOp::Cut : op == "intersect" ? BodyOp::Intersect : BodyOp::Join;
        C.target = f.prop("target");
        C.tools = splitNames(f.prop("tools"));
        C.keepTools = f.prop("keepTools", "0") == "1";
        if (C.target.empty() || C.tools.empty()) break;
        if (std::find(m.bodyNames.begin(), m.bodyNames.end(), C.target) == m.bodyNames.end()) break;
        m.ops.push_back({false, int(m.combines.size()), true});
        m.combines.push_back(std::move(C));
        break;
      }
    }
  }
  double lo, hi, size = 0;
  for (gp_Dir d : {gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), gp_Dir(0, 0, 1)})
    if (m.extents(d, lo, hi)) size = std::max(size, hi - lo);
  m.scale = std::max(1.0, size);
  return m;
}

}  // namespace pc::mfg::detail
