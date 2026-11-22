// PersistCAD sketch — tolerant planar regions (SC-PULLUPTO). See pc/Regions.h.
#include "pc/Regions.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <ShapeFix_Face.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace pc::sketch {

namespace {

constexpr double kPi = 3.14159265358979323846;
struct V2 { double x = 0, y = 0; };
inline V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
inline V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
inline V2 operator*(V2 a, double k) { return {a.x * k, a.y * k}; }
inline double cross(V2 a, V2 b) { return a.x * b.y - a.y * b.x; }
inline double dot(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }
inline double norm(V2 a) { return std::hypot(a.x, a.y); }
inline double dist(V2 a, V2 b) { return norm(a - b); }
double normAngle(double a) {
  a = std::fmod(a, 2 * kPi);
  if (a < 0) a += 2 * kPi;
  return a;
}

enum class CT { Line, Arc, Circle };

// Source curve (after end point snapping). Parameter t in [0,1] (circle: [0,1) periodic).
struct Cv {
  int eid = 0;
  CT type = CT::Line;
  V2 a, b, c;
  double r = 0, a0 = 0, sw = 0;   // arc / circle: start angle, CCW sweep (circle 2pi)
  V2 at(double t) const {
    if (type == CT::Line) return a + (b - a) * t;
    const double ang = a0 + sw * t;
    return c + V2{std::cos(ang), std::sin(ang)} * r;
  }
  // parameter of the projection of p (lines unclamped; arcs: outside the sweep -> < 0 or > 1, nearer end)
  double paramOf(V2 p) const {
    if (type == CT::Line) {
      const V2 d = b - a;
      const double l2 = dot(d, d);
      return l2 > 0 ? dot(p - a, d) / l2 : 0;
    }
    const double ang = normAngle(std::atan2(p.y - c.y, p.x - c.x) - a0);
    if (type == CT::Circle) return ang / (2 * kPi);
    if (ang <= sw) return ang / sw;
    return (ang - sw < 2 * kPi - ang) ? ang / sw : -(2 * kPi - ang) / sw;
  }
  double scale() const { return type == CT::Line ? dist(a, b) : r * sw; }
};

bool circleFrom3(V2 p, V2 q, V2 s, V2& c, double& r) {
  const double d = 2 * (p.x * (q.y - s.y) + q.x * (s.y - p.y) + s.x * (p.y - q.y));
  const double sc = std::max({norm(q - p), norm(s - q), norm(s - p), 1e-12});
  if (std::fabs(d) < 1e-12 * sc * sc) return false;
  const double p2 = dot(p, p), q2 = dot(q, q), s2 = dot(s, s);
  c = {(p2 * (q.y - s.y) + q2 * (s.y - p.y) + s2 * (p.y - q.y)) / d, (p2 * (s.x - q.x) + q2 * (p.x - s.x) + s2 * (q.x - p.x)) / d};
  r = norm(p - c);
  return r > 0 && std::isfinite(r);
}

// Arc through start p, a point m on the arc and end q (CCW or CW decided by m).
struct Arc3 {
  V2 c;
  double r = 0, a0 = 0, sw = 0;   // signed sweep (negative = CW)
  bool ok = false;
};
Arc3 arcThrough(V2 p, V2 m, V2 q) {
  Arc3 A;
  if (!circleFrom3(p, m, q, A.c, A.r)) return A;
  A.a0 = std::atan2(p.y - A.c.y, p.x - A.c.x);
  const double a1 = std::atan2(q.y - A.c.y, q.x - A.c.x), am = std::atan2(m.y - A.c.y, m.x - A.c.x);
  const double ccw = normAngle(a1 - A.a0);
  A.sw = normAngle(am - A.a0) < ccw ? ccw : -(2 * kPi - ccw);
  A.ok = true;
  return A;
}

struct UF {
  std::vector<int> p;
  explicit UF(int n) : p(size_t(n)) { for (int i = 0; i < n; ++i) p[size_t(i)] = i; }
  int f(int x) { while (p[size_t(x)] != x) x = p[size_t(x)] = p[size_t(p[size_t(x)])]; return x; }
  void u(int a, int b) { a = f(a); b = f(b); if (a != b) p[size_t(std::max(a, b))] = std::min(a, b); }
};

// cluster points closer than tol (single linkage, spatial hash)
std::vector<int> cluster(const std::vector<V2>& pts, double tol) {
  const int n = int(pts.size());
  UF uf(n);
  const double cell = std::max(tol, 1e-9);
  std::unordered_map<long long, std::vector<int>> grid;
  auto key = [](long long i, long long j) { return i * 73856093LL ^ j * 19349663LL; };
  for (int i = 0; i < n; ++i) {
    const long long gi = (long long)std::floor(pts[size_t(i)].x / cell), gj = (long long)std::floor(pts[size_t(i)].y / cell);
    for (long long di = -1; di <= 1; ++di)
      for (long long dj = -1; dj <= 1; ++dj) {
        auto it = grid.find(key(gi + di, gj + dj));
        if (it == grid.end()) continue;
        for (int k : it->second)
          if (dist(pts[size_t(k)], pts[size_t(i)]) <= tol) uf.u(i, k);
      }
    grid[key(gi, gj)].push_back(i);
  }
  std::vector<int> root(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) root[size_t(i)] = uf.f(i);
  return root;
}

// intersection points of two curves (tolerance tol), parameters not filtered
void intersect(const Cv& A, const Cv& B, double tol, std::vector<V2>& out) {
  auto onArc = [](const Cv& R, V2 p, double tolAng) {
    if (R.type == CT::Circle) return true;
    const double t = normAngle(std::atan2(p.y - R.c.y, p.x - R.c.x) - R.a0);
    return t <= R.sw + tolAng || t >= 2 * kPi - tolAng;
  };
  auto lineLine = [&](const Cv& P, const Cv& Q) {
    const V2 d1 = P.b - P.a, d2 = Q.b - Q.a;
    const double l1 = norm(d1), l2 = norm(d2), den = cross(d1, d2);
    if (std::fabs(den) <= 1e-12 * l1 * l2) return;   // parallel / collinear: handled by the end point rule
    const double t = cross(Q.a - P.a, d2) / den, u = cross(Q.a - P.a, d1) / den;
    const double et = tol / l1, eu = tol / l2;
    if (t >= -et && t <= 1 + et && u >= -eu && u <= 1 + eu) out.push_back(P.a + d1 * t);
  };
  auto lineRound = [&](const Cv& P, const Cv& R) {
    const V2 d = P.b - P.a;
    const double l = norm(d);
    if (l <= 0) return;
    const V2 u = d * (1.0 / l);
    const double tc = dot(R.c - P.a, u), h = cross(u, R.c - P.a);
    if (std::fabs(h) > R.r + tol) return;
    const double w2 = R.r * R.r - h * h, w = w2 > 0 ? std::sqrt(w2) : 0.0;
    const double ts[2] = {tc - w, tc + w};
    for (int i = 0; i < (w > 0 ? 2 : 1); ++i) {
      if (ts[i] < -tol || ts[i] > l + tol) continue;
      const V2 p = P.a + u * ts[i];
      if (onArc(R, p, tol / R.r)) out.push_back(p);
    }
  };
  auto roundRound = [&](const Cv& P, const Cv& Q) {
    const V2 dc = Q.c - P.c;
    const double d = norm(dc);
    if (d <= tol) return;   // concentric / co-circular: end point rule
    if (d > P.r + Q.r + tol || d < std::fabs(P.r - Q.r) - tol) return;
    const double aa = (P.r * P.r - Q.r * Q.r + d * d) / (2 * d), h2 = P.r * P.r - aa * aa, h = h2 > 0 ? std::sqrt(h2) : 0.0;
    const V2 e = dc * (1.0 / d), m = P.c + e * aa, nrm{-e.y, e.x};
    const V2 cand[2] = {m + nrm * h, m - nrm * h};
    for (int i = 0; i < (h > 0 ? 2 : 1); ++i)
      if (onArc(P, cand[i], tol / P.r) && onArc(Q, cand[i], tol / Q.r)) out.push_back(cand[i]);
  };
  const bool al = A.type == CT::Line, bl = B.type == CT::Line;
  if (al && bl) lineLine(A, B);
  else if (al) lineRound(A, B);
  else if (bl) lineRound(B, A);
  else roundRound(A, B);
}

// one piece of the arrangement (between two nodes, or a whole free circle)
struct Piece {
  int eid = 0;
  int n0 = -1, n1 = -1;
  bool closed = false;     // free circle
  bool arc = false;
  V2 p0, p1, m;            // end points (node positions) and a point inside
  Arc3 A;                  // arc geometry (arc pieces) / closed circle: c, r
  V2 at(double f) const {  // f in [0,1] from p0 to p1
    if (closed) { const double ang = 2 * kPi * f; return A.c + V2{std::cos(ang), std::sin(ang)} * A.r; }
    if (!arc) return p0 + (p1 - p0) * f;
    const double ang = A.a0 + A.sw * f;
    return A.c + V2{std::cos(ang), std::sin(ang)} * A.r;
  }
  int samples() const {
    if (!arc && !closed) return 1;
    const double sw = closed ? 2 * kPi : std::fabs(A.sw);
    return std::max(4, int(std::ceil(sw / (5.0 * kPi / 180.0))));
  }
};

double polyArea(const std::vector<V2>& p) {
  double a = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) a += cross(p[i], p[(i + 1) % n]);
  return 0.5 * a;
}
V2 polyCentroid(const std::vector<V2>& p) {
  double a = 0, cx = 0, cy = 0;
  for (size_t i = 0, n = p.size(); i < n; ++i) {
    const double k = cross(p[i], p[(i + 1) % n]);
    a += k;
    cx += (p[i].x + p[(i + 1) % n].x) * k;
    cy += (p[i].y + p[(i + 1) % n].y) * k;
  }
  if (std::fabs(a) < 1e-300) return p.empty() ? V2{} : p[0];
  return {cx / (3 * a), cy / (3 * a)};
}
bool inPoly(const std::vector<V2>& poly, V2 q) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const V2 &a = poly[i], &b = poly[j];
    if ((a.y > q.y) != (b.y > q.y) && q.x < (b.x - a.x) * (q.y - a.y) / (b.y - a.y) + a.x) in = !in;
  }
  return in;
}

struct Cycle {
  std::vector<int> he;     // half-edges (2*piece + dir); a free circle: {2*piece} or {2*piece+1}
  std::vector<V2> poly;
  double area = 0;
  int comp = -1;
  int parent = -1;         // negative cycles: region they are a hole of
};

double sketchScale(const Sketch& sk) {
  double mnx = 1e300, mny = 1e300, mxx = -1e300, mxy = -1e300, circ = 0;
  for (const auto& e : sk.entities) {
    if (e.construction) continue;
    for (int pid : e.points)
      if (const SkPoint* p = sk.point(pid)) {
        mnx = std::min(mnx, p->x); mxx = std::max(mxx, p->x); mny = std::min(mny, p->y); mxy = std::max(mxy, p->y);
      }
    if (e.type == SkEntityType::Circle) circ = std::max(circ, 2 * e.radius);
  }
  double s = circ;
  if (mxx >= mnx) s = std::max(s, std::hypot(mxx - mnx, mxy - mny));
  return s;
}

} // namespace

double autoGapTol(const Sketch& sk) {
  const double s = sketchScale(sk);
  return std::min(0.1, std::max(1e-6, 0.005 * s));
}

RegionResult arrangeRegions(const Sketch& sk, const gp_Ax3& frame, double gapTol) {
  RegionResult R;
  const double g = gapTol > 0 ? gapTol : autoGapTol(sk);
  R.gapTol = g;
  const double scale = std::max(1.0, sketchScale(sk));
  const double eps = std::max(1e-9, 1e-10 * scale);

  // ---- source curves
  std::vector<Cv> cv;
  for (const auto& e : sk.entities) {
    if (e.construction) continue;
    Cv c;
    c.eid = e.id;
    bool ok = true;
    for (int pid : e.points) ok = ok && sk.point(pid);
    if (!ok) continue;
    if (e.type == SkEntityType::Line && e.points.size() == 2) {
      c.type = CT::Line;
      c.a = {sk.point(e.points[0])->x, sk.point(e.points[0])->y};
      c.b = {sk.point(e.points[1])->x, sk.point(e.points[1])->y};
      if (dist(c.a, c.b) <= eps) continue;
    } else if (e.type == SkEntityType::Circle && e.points.size() == 1) {
      c.type = CT::Circle;
      c.c = {sk.point(e.points[0])->x, sk.point(e.points[0])->y};
      c.r = e.radius;
      c.sw = 2 * kPi;
      if (!(c.r > g)) continue;
    } else if (e.type == SkEntityType::Arc && e.points.size() == 3) {
      c.type = CT::Arc;
      c.c = {sk.point(e.points[0])->x, sk.point(e.points[0])->y};
      c.a = {sk.point(e.points[1])->x, sk.point(e.points[1])->y};
      c.b = {sk.point(e.points[2])->x, sk.point(e.points[2])->y};
      c.r = dist(c.a, c.c);
      if (!(c.r > eps) || dist(c.a, c.b) <= eps) continue;
      c.a0 = std::atan2(c.a.y - c.c.y, c.a.x - c.c.x);
      c.sw = normAngle(std::atan2(c.b.y - c.c.y, c.b.x - c.c.x) - c.a0);
      if (c.sw <= 0) c.sw = 2 * kPi;
    } else {
      continue;
    }
    cv.push_back(c);
  }

  // ---- 1. end point snapping (gap closing)
  {
    std::vector<V2> ends;
    std::vector<std::pair<int, int>> who;
    for (size_t i = 0; i < cv.size(); ++i)
      if (cv[i].type != CT::Circle) {
        ends.push_back(cv[i].a); who.push_back({int(i), 0});
        ends.push_back(cv[i].b); who.push_back({int(i), 1});
      }
    const std::vector<int> root = cluster(ends, g);
    std::map<int, std::pair<V2, int>> acc;
    for (size_t k = 0; k < ends.size(); ++k) {
      auto& s = acc[root[k]];
      s.first = s.first + ends[k];
      s.second++;
    }
    // a corner of two straight lines is rebuilt at the intersection of their carriers (keeps imported outlines exact:
    // an axis-parallel edge stays axis-parallel), otherwise the group's mean is used
    std::map<int, V2> corner;
    {
      std::map<int, std::vector<size_t>> members;
      for (size_t k = 0; k < ends.size(); ++k) members[root[k]].push_back(k);
      for (auto& [rt, ks] : members) {
        const V2 mean = acc[rt].first * (1.0 / acc[rt].second);
        bool done = false;
        for (size_t x = 0; x < ks.size() && !done; ++x)
          for (size_t y = x + 1; y < ks.size() && !done; ++y) {
            const Cv &A = cv[size_t(who[ks[x]].first)], &B = cv[size_t(who[ks[y]].first)];
            if (A.type != CT::Line || B.type != CT::Line || who[ks[x]].first == who[ks[y]].first) continue;
            const V2 d1 = A.b - A.a, d2 = B.b - B.a;
            const double den = cross(d1, d2);
            if (std::fabs(den) <= 1e-9 * norm(d1) * norm(d2)) continue;   // parallel: mean
            const V2 X = A.a + d1 * (cross(B.a - A.a, d2) / den);
            if (dist(X, mean) <= 2 * g) { corner[rt] = X; done = true; }
          }
      }
    }
    std::set<int> healed;
    for (size_t k = 0; k < ends.size(); ++k) {
      const auto& s = acc[root[k]];
      const auto ct = corner.find(root[k]);
      const V2 m = ct != corner.end() ? ct->second : s.first * (1.0 / s.second);
      if (dist(m, ends[k]) > eps) healed.insert(root[k]);
      Cv& c = cv[size_t(who[k].first)];
      (who[k].second == 0 ? c.a : c.b) = m;
    }
    R.healedGaps = int(healed.size());
    // refit arcs through the moved end points (and their old middle) -> pieces pass exactly through the nodes
    std::vector<Cv> keep;
    for (auto c : cv) {
      if (c.type == CT::Line) {
        if (dist(c.a, c.b) <= g) continue;
      } else if (c.type == CT::Arc) {
        const double am = c.a0 + 0.5 * c.sw;
        const V2 mid = c.c + V2{std::cos(am), std::sin(am)} * c.r;
        if (dist(c.a, c.b) <= g && c.r * c.sw <= 2 * g) continue;
        const Arc3 A = arcThrough(c.a, mid, c.b);
        if (!A.ok) { c.type = CT::Line; }
        else if (A.sw > 0) { c.c = A.c; c.r = A.r; c.a0 = A.a0; c.sw = A.sw; }
        else { c.c = A.c; c.r = A.r; c.a0 = A.a0; c.sw = A.sw; }   // CW after refit: keep (signed sweep)
        if (c.type == CT::Arc && c.sw < 0) {   // store CCW: swap ends
          std::swap(c.a, c.b);
          c.a0 = std::atan2(c.a.y - c.c.y, c.a.x - c.c.x);
          c.sw = -c.sw;
        }
      }
      keep.push_back(c);
    }
    cv.swap(keep);
  }

  // ---- 2. split points: own ends, T-junctions, crossings
  std::vector<std::vector<std::pair<double, V2>>> splits(cv.size());
  for (size_t i = 0; i < cv.size(); ++i)
    if (cv[i].type != CT::Circle) { splits[i].push_back({0.0, cv[i].a}); splits[i].push_back({1.0, cv[i].b}); }
  for (size_t i = 0; i < cv.size(); ++i) {
    if (cv[i].type == CT::Circle) continue;
    for (const V2 P : {cv[i].a, cv[i].b})
      for (size_t k = 0; k < cv.size(); ++k) {
        if (k == i) continue;
        const Cv& K = cv[k];
        if (K.type != CT::Circle && (dist(K.a, P) <= g || dist(K.b, P) <= g)) continue;
        double t = K.paramOf(P);
        if (K.type != CT::Circle && (t <= 0 || t >= 1)) continue;
        const V2 Q = K.at(t);
        if (dist(Q, P) > g) continue;
        splits[k].push_back({t, Q});
        ++R.tJunctions;
      }
  }
  for (size_t i = 0; i < cv.size(); ++i)
    for (size_t j = i + 1; j < cv.size(); ++j) {
      std::vector<V2> xs;
      intersect(cv[i], cv[j], eps, xs);
      for (const V2 X : xs) {
        const double ti = cv[i].paramOf(X), tj = cv[j].paramOf(X);
        auto inner = [&](const Cv& c, double t) {
          if (c.type == CT::Circle) return true;
          return t > -1e-12 && t < 1 + 1e-12 && dist(c.at(t), c.a) > g && dist(c.at(t), c.b) > g;
        };
        if (!inner(cv[i], ti) && !inner(cv[j], tj)) continue;   // shared / snapped end points
        auto clampT = [](const Cv& c, double t) { return c.type == CT::Circle ? t : std::clamp(t, 0.0, 1.0); };
        splits[i].push_back({clampT(cv[i], ti), X});
        splits[j].push_back({clampT(cv[j], tj), X});
        if (inner(cv[i], ti) && inner(cv[j], tj)) ++R.crossings;
      }
    }

  // ---- 3. nodes
  std::vector<V2> sp;
  std::vector<std::pair<int, int>> spWho;
  for (size_t i = 0; i < cv.size(); ++i)
    for (size_t k = 0; k < splits[i].size(); ++k) { sp.push_back(splits[i][k].second); spWho.push_back({int(i), int(k)}); }
  const std::vector<int> root = cluster(sp, g);
  std::map<int, int> nodeOfRoot;
  std::vector<V2> nodePos;
  std::vector<int> nodeCount;
  std::vector<std::vector<int>> splitNode(cv.size());
  for (size_t i = 0; i < cv.size(); ++i) splitNode[i].assign(splits[i].size(), -1);
  for (size_t k = 0; k < sp.size(); ++k) {
    auto it = nodeOfRoot.find(root[k]);
    int nd;
    if (it == nodeOfRoot.end()) {
      nd = int(nodePos.size());
      nodeOfRoot[root[k]] = nd;
      nodePos.push_back({0, 0});
      nodeCount.push_back(0);
    } else nd = it->second;
    nodePos[size_t(nd)] = nodePos[size_t(nd)] + sp[k];
    nodeCount[size_t(nd)]++;
    splitNode[size_t(spWho[k].first)][size_t(spWho[k].second)] = nd;
  }
  for (size_t n = 0; n < nodePos.size(); ++n) nodePos[n] = nodePos[n] * (1.0 / nodeCount[n]);

  // ---- 4. pieces
  std::vector<Piece> pieces;
  std::map<std::pair<int, int>, std::vector<V2>> seen;
  auto addPiece = [&](const Cv& c, int n0, int n1, double t0, double t1) {
    if (n0 == n1) return;
    Piece p;
    p.eid = c.eid;
    p.n0 = n0;
    p.n1 = n1;
    p.p0 = nodePos[size_t(n0)];
    p.p1 = nodePos[size_t(n1)];
    p.m = c.type == CT::Line ? (p.p0 + p.p1) * 0.5 : c.at(0.5 * (t0 + t1));
    if (c.type != CT::Line) {
      p.A = arcThrough(p.p0, p.m, p.p1);
      p.arc = p.A.ok;
    }
    if (dist(p.p0, p.p1) <= eps && !p.arc) return;
    const auto key = std::make_pair(std::min(n0, n1), std::max(n0, n1));
    for (const V2& mm : seen[key])
      if (dist(mm, p.m) <= std::max(g, 1e-6)) { ++R.overlaps; return; }
    seen[key].push_back(p.m);
    pieces.push_back(p);
  };
  for (size_t i = 0; i < cv.size(); ++i) {
    const Cv& c = cv[i];
    std::vector<std::pair<double, int>> s;
    for (size_t k = 0; k < splits[i].size(); ++k) s.push_back({splits[i][k].first, splitNode[i][k]});
    std::sort(s.begin(), s.end(), [](auto& x, auto& y) { return x.first < y.first; });
    if (c.type == CT::Circle) {
      // distinct nodes along the circle
      std::vector<std::pair<double, int>> u;
      for (auto& q : s) if (u.empty() || u.back().second != q.second) u.push_back(q);
      while (u.size() > 1 && u.front().second == u.back().second) u.pop_back();
      if (u.empty()) {
        Piece p;
        p.eid = c.eid;
        p.closed = true;
        p.A.c = c.c;
        p.A.r = c.r;
        p.A.ok = true;
        pieces.push_back(p);
        continue;
      }
      if (u.size() == 1) {   // touches the rest at one point: a second node opposite to it
        const double t2 = std::fmod(u[0].first + 0.5, 1.0);
        const int nd = int(nodePos.size());
        nodePos.push_back(c.at(t2));
        nodeCount.push_back(1);
        u.push_back({t2, nd});
        std::sort(u.begin(), u.end(), [](auto& x, auto& y) { return x.first < y.first; });
      }
      for (size_t k = 0; k < u.size(); ++k) {
        const auto& a = u[k];
        auto b = u[(k + 1) % u.size()];
        double tb = b.first;
        if (k + 1 == u.size()) tb += 1.0;
        addPiece(c, a.second, b.second, a.first, tb);
      }
      continue;
    }
    for (size_t k = 0; k + 1 < s.size(); ++k) {
      if (s[k].second == s[k + 1].second) continue;
      addPiece(c, s[k].second, s[k + 1].second, s[k].first, s[k + 1].first);
    }
  }

  // ---- 5. prune dangling chains (report the open ends)
  const int nn = int(nodePos.size());
  std::vector<std::vector<int>> inc(static_cast<size_t>(nn));
  for (size_t p = 0; p < pieces.size(); ++p)
    if (!pieces[p].closed) { inc[size_t(pieces[p].n0)].push_back(int(p)); inc[size_t(pieces[p].n1)].push_back(int(p)); }
  std::vector<int> deg(static_cast<size_t>(nn));
  for (int n = 0; n < nn; ++n) deg[size_t(n)] = int(inc[size_t(n)].size());
  std::vector<char> removed(pieces.size(), 0);
  std::set<int> openEnt;
  {
    std::vector<int> stack;
    for (int n = 0; n < nn; ++n)
      if (deg[size_t(n)] == 1) {
        stack.push_back(n);
        R.openPoints.emplace_back(nodePos[size_t(n)].x, nodePos[size_t(n)].y);
        for (int p : inc[size_t(n)]) openEnt.insert(pieces[size_t(p)].eid);
      }
    while (!stack.empty()) {
      const int n = stack.back();
      stack.pop_back();
      if (deg[size_t(n)] != 1) continue;
      for (int p : inc[size_t(n)]) {
        if (removed[size_t(p)]) continue;
        removed[size_t(p)] = 1;
        const int o = pieces[size_t(p)].n0 == n ? pieces[size_t(p)].n1 : pieces[size_t(p)].n0;
        --deg[size_t(n)];
        --deg[size_t(o)];
        if (deg[size_t(o)] == 1) stack.push_back(o);
      }
    }
  }
  R.openEntities.assign(openEnt.begin(), openEnt.end());
  for (size_t i = 0; i < R.openPoints.size(); ++i)
    for (size_t j = i + 1; j < R.openPoints.size(); ++j) {
      const double d = R.openPoints[i].Distance(R.openPoints[j]);
      if (R.nearestGap < 0 || d < R.nearestGap) R.nearestGap = d;
    }

  // ---- 6. half-edges, faces
  auto heFrom = [&](int h) { const Piece& p = pieces[size_t(h / 2)]; return h % 2 == 0 ? p.n0 : p.n1; };
  auto heTo = [&](int h) { const Piece& p = pieces[size_t(h / 2)]; return h % 2 == 0 ? p.n1 : p.n0; };
  auto heAt = [&](int h, double f) { const Piece& p = pieces[size_t(h / 2)]; return p.at(h % 2 == 0 ? f : 1 - f); };
  std::vector<std::vector<int>> out(static_cast<size_t>(nn));
  std::vector<double> heAng(pieces.size() * 2, 0);
  for (size_t p = 0; p < pieces.size(); ++p) {
    if (removed[p] || pieces[p].closed) continue;
    for (int d = 0; d < 2; ++d) {
      const int h = int(2 * p) + d;
      const V2 s = nodePos[size_t(heFrom(h))];
      V2 q = heAt(h, 0.02);
      if (dist(q, s) < 1e-12) q = heAt(h, 0.5);
      heAng[size_t(h)] = std::atan2(q.y - s.y, q.x - s.x);
      out[size_t(heFrom(h))].push_back(h);
    }
  }
  for (auto& o : out) std::sort(o.begin(), o.end(), [&](int a, int b) { return heAng[size_t(a)] < heAng[size_t(b)]; });
  std::vector<Cycle> cycles;
  std::vector<char> used(pieces.size() * 2, 0);
  auto sampleHe = [&](int h, std::vector<V2>& poly) {
    const int k = pieces[size_t(h / 2)].samples();
    for (int i = 0; i < k; ++i) poly.push_back(heAt(h, double(i) / k));
  };
  for (size_t p = 0; p < pieces.size(); ++p) {
    if (removed[p] || pieces[p].closed) continue;
    for (int d = 0; d < 2; ++d) {
      const int h0 = int(2 * p) + d;
      if (used[size_t(h0)]) continue;
      Cycle C;
      int h = h0;
      bool okCycle = true;
      for (size_t guard = 0; guard <= pieces.size() * 2 + 2; ++guard) {
        used[size_t(h)] = 1;
        C.he.push_back(h);
        sampleHe(h, C.poly);
        const int v = heTo(h), twin = h ^ 1;
        const auto& o = out[size_t(v)];
        const auto it = std::find(o.begin(), o.end(), twin);
        if (it == o.end()) { okCycle = false; break; }
        const size_t idx = size_t(it - o.begin());
        h = o[(idx + o.size() - 1) % o.size()];
        if (h == h0) break;
        if (used[size_t(h)]) { okCycle = false; break; }
      }
      if (!okCycle) continue;
      C.area = polyArea(C.poly);
      if (std::fabs(C.area) <= eps * scale) continue;
      cycles.push_back(std::move(C));
    }
  }
  // components
  UF comp(nn + int(pieces.size()));
  for (size_t p = 0; p < pieces.size(); ++p)
    if (!removed[p] && !pieces[p].closed) comp.u(pieces[p].n0, pieces[p].n1);
  for (auto& C : cycles) C.comp = comp.f(heFrom(C.he.front()));
  for (size_t p = 0; p < pieces.size(); ++p) {
    if (!pieces[p].closed) continue;
    for (int d = 0; d < 2; ++d) {
      Cycle C;
      C.he.push_back(int(2 * p) + d);
      sampleHe(int(2 * p) + d, C.poly);
      C.area = polyArea(C.poly);
      C.comp = nn + int(p);
      cycles.push_back(std::move(C));
    }
  }
  // holes: the outer (negative) cycle of a component inside the smallest bounded face of another component
  std::map<int, int> outerOf;   // component -> its outer cycle (largest |area| negative)
  for (size_t i = 0; i < cycles.size(); ++i) {
    if (cycles[i].area >= 0) continue;
    auto it = outerOf.find(cycles[i].comp);
    if (it == outerOf.end() || std::fabs(cycles[size_t(it->second)].area) < std::fabs(cycles[i].area)) outerOf[cycles[i].comp] = int(i);
  }
  for (auto& [cm, oi] : outerOf) {
    Cycle& O = cycles[size_t(oi)];
    const V2 probe = O.poly.front();
    int best = -1;
    for (size_t j = 0; j < cycles.size(); ++j) {
      const Cycle& F = cycles[j];
      if (F.area <= 0 || F.comp == cm || F.area <= std::fabs(O.area)) continue;
      if (!inPoly(F.poly, probe)) continue;
      if (best < 0 || F.area < cycles[size_t(best)].area) best = int(j);
    }
    O.parent = best;
  }

  // ---- 7. regions in order
  std::vector<int> regs;
  for (size_t i = 0; i < cycles.size(); ++i) if (cycles[i].area > 0) regs.push_back(int(i));
  std::vector<V2> cent(cycles.size());
  for (int i : regs) cent[size_t(i)] = polyCentroid(cycles[size_t(i)].poly);
  std::stable_sort(regs.begin(), regs.end(), [&](int a, int b) {
    const double A = cycles[size_t(a)].area, B = cycles[size_t(b)].area;
    if (std::fabs(A - B) > 1e-6 * std::max(1.0, std::max(A, B))) return A > B;
    const V2 ca = cent[size_t(a)], cb = cent[size_t(b)];
    if (std::fabs(ca.x - cb.x) > 1e-6) return ca.x < cb.x;
    return ca.y < cb.y;
  });

  // ---- 8. OCCT
  const gp_Pnt O = frame.Location();
  const gp_Dir X = frame.XDirection(), Y = frame.YDirection(), N = frame.Direction();
  const double hand = X.Crossed(Y).Dot(N) >= 0 ? 1.0 : -1.0;
  auto to3d = [&](V2 p) { return gp_Pnt(O.XYZ() + X.XYZ() * p.x + Y.XYZ() * p.y); };
  std::vector<TopoDS_Vertex> verts(nodePos.size());
  auto vertex = [&](int n) -> const TopoDS_Vertex& {
    if (verts[size_t(n)].IsNull()) verts[size_t(n)] = BRepBuilderAPI_MakeVertex(to3d(nodePos[size_t(n)])).Vertex();
    return verts[size_t(n)];
  };
  std::vector<TopoDS_Edge> edges(pieces.size());
  auto edgeOf = [&](int p) -> bool {
    if (!edges[size_t(p)].IsNull()) return true;
    const Piece& pc = pieces[size_t(p)];
    try {
      if (pc.closed) {
        BRepBuilderAPI_MakeEdge me(gp_Circ(gp_Ax2(to3d(pc.A.c), hand > 0 ? N : N.Reversed(), X), pc.A.r));
        if (!me.IsDone()) return false;
        edges[size_t(p)] = me.Edge();
      } else if (pc.arc) {
        GC_MakeArcOfCircle mk(to3d(pc.p0), to3d(pc.m), to3d(pc.p1));
        if (!mk.IsDone()) return false;
        BRepBuilderAPI_MakeEdge me(mk.Value(), vertex(pc.n0), vertex(pc.n1));
        if (!me.IsDone()) return false;
        edges[size_t(p)] = me.Edge();
      } else {
        BRepBuilderAPI_MakeEdge me(vertex(pc.n0), vertex(pc.n1));
        if (!me.IsDone()) return false;
        edges[size_t(p)] = me.Edge();
      }
    } catch (const Standard_Failure&) {
      return false;
    }
    return true;
  };
  auto wireOf = [&](const Cycle& C, TopoDS_Wire& w) -> bool {
    BRep_Builder bb;
    bb.MakeWire(w);
    const int n = int(C.he.size());
    for (int q = 0; q < n; ++q) {
      const int h = hand > 0 ? C.he[size_t(q)] : C.he[size_t(n - 1 - q)];
      const int p = h / 2;
      if (!edgeOf(p)) return false;
      bool fwd = h % 2 == 0;
      if (hand < 0) fwd = !fwd;
      bb.Add(w, fwd ? edges[size_t(p)] : TopoDS::Edge(edges[size_t(p)].Reversed()));
    }
    w.Closed(Standard_True);
    return true;
  };
  const gp_Pln pln(frame);
  std::vector<std::string> errs;
  std::set<int> usedPieces;
  for (int ri : regs) {
    const Cycle& C = cycles[size_t(ri)];
    TopoDS_Wire ow;
    if (!wireOf(C, ow)) { errs.push_back("bölge kenarı oluşturulamadı"); continue; }
    BRepBuilderAPI_MakeFace mf(pln, ow, Standard_True);
    double net = C.area;
    int nh = 0;
    std::vector<int> pcs;
    for (int h : C.he) pcs.push_back(h / 2);
    for (size_t j = 0; j < cycles.size() && mf.IsDone(); ++j) {
      if (cycles[j].parent != ri) continue;
      TopoDS_Wire hw;
      if (!wireOf(cycles[j], hw)) continue;
      mf.Add(hw);
      net -= std::fabs(cycles[j].area);
      ++nh;
      for (int h : cycles[j].he) pcs.push_back(h / 2);
    }
    if (!mf.IsDone()) { errs.push_back("bölge yüzü oluşturulamadı"); continue; }
    TopoDS_Face face = mf.Face();
    try {
      if (!BRepCheck_Analyzer(face).IsValid()) {
        ShapeFix_Face fx(face);
        fx.Perform();
        face = fx.Face();
      }
    } catch (...) {
    }
    R.profile.faces.push_back(face);
    R.holes.push_back(nh);
    R.areas.push_back(net);
    for (int p : pcs)
      if (usedPieces.insert(p).second) R.profile.edges.push_back({edges[size_t(p)], pieces[size_t(p)].eid});
  }

  std::ostringstream os;
  if (!R.openPoints.empty()) {
    os << "Açık kontur: " << R.openPoints.size() << " açık uç";
    if (R.nearestGap > 0) os << " (en yakın iki uç arası " << R.nearestGap << " mm > tolerans " << g << " mm)";
    os << ", eğriler:";
    for (int e : R.openEntities) os << " " << e;
  }
  for (auto& e : errs) os << (os.tellp() > 0 ? "; " : "") << e;
  R.profile.message = os.str();
  R.profile.ok = R.openPoints.empty() && errs.empty() && !R.profile.faces.empty();
  return R;
}

RegionResult sketchRegions(const Sketch& sk, const gp_Ax3& frame, double gapTol) {
  Profile legacy;
  try { legacy = buildProfile(sk, frame); } catch (...) { legacy = Profile(); }
  RegionResult A = arrangeRegions(sk, frame, gapTol);
  if (legacy.ok && !legacy.faces.empty() && legacy.faces.size() == A.profile.faces.size()) {
    RegionResult L = A;
    L.legacy = true;
    L.profile = legacy;
    L.holes.clear();
    L.areas.clear();
    for (const auto& f : legacy.faces) {
      int w = 0;
      for (TopExp_Explorer ex(f, TopAbs_WIRE); ex.More(); ex.Next()) ++w;
      L.holes.push_back(std::max(0, w - 1));
    }
    L.areas = A.areas;   // same faces, possibly in another order: informative only
    return L;
  }
  A.profile.freePoints = legacy.freePoints;
  A.profile.freePoints2d = legacy.freePoints2d;
  return A;
}

std::string describeRegions(const RegionResult& r) {
  std::ostringstream os;
  const size_t n = r.profile.faces.size();
  int withHoles = 0;
  for (int h : r.holes) if (h > 0) ++withHoles;
  os << n << " bölge";
  if (withHoles) os << " (" << withHoles << " delikli)";
  if (r.healedGaps) os << ", " << r.healedGaps << " boşluk kapatıldı (≤ " << r.gapTol << " mm)";
  if (r.tJunctions) os << ", " << r.tJunctions << " T-birleşim";
  if (r.crossings) os << ", " << r.crossings << " kesişim";
  if (r.overlaps) os << ", " << r.overlaps << " çakışan parça atıldı";
  if (!r.openPoints.empty()) os << "; " << r.profile.message;
  return os.str();
}

} // namespace pc::sketch
