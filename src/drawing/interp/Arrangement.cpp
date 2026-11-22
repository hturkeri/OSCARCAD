// WP-3 private: planar arrangement of line/arc segments.
#include "interp/Arrangement.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>

namespace pc::drawing::interp {

namespace {
constexpr double kPI = 3.14159265358979323846;
double normAng(double a) { while (a < 0) a += 2 * kPI; while (a >= 2 * kPI) a -= 2 * kPI; return a; }
}  // namespace

double segSweep(const ASeg& s) {
  if (!s.arc) return 0;
  double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x), a1 = std::atan2(s.b.y - s.c.y, s.b.x - s.c.x);
  double sw = normAng(a1 - a0);
  if (sw < 1e-12) sw = 2 * kPI;
  return s.ccw ? sw : -(2 * kPI - sw == 0 ? 2 * kPI : 2 * kPI - sw);
}
double segLength(const ASeg& s) { return s.arc ? std::abs(segSweep(s)) * s.r : distance(s.a, s.b); }
Vec2 segPoint(const ASeg& s, double t) {
  if (!s.arc) return s.a + (s.b - s.a) * t;
  double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x) + segSweep(s) * t;
  return {s.c.x + s.r * std::cos(a0), s.c.y + s.r * std::sin(a0)};
}
std::vector<Vec2> segPolyline(const ASeg& s, double maxStep) {
  int n = s.arc ? std::max(2, (int)std::ceil(segLength(s) / std::max(1e-6, maxStep))) : 1;
  n = std::min(n, 2000);
  std::vector<Vec2> v;
  for (int i = 0; i <= n; ++i) v.push_back(segPoint(s, double(i) / n));
  return v;
}
double distToSeg(const ASeg& s, Vec2 q) {
  if (!s.arc) {
    Vec2 d = s.b - s.a;
    double L2 = dot(d, d);
    double t = L2 > 1e-30 ? std::clamp(dot(q - s.a, d) / L2, 0.0, 1.0) : 0;
    return distance(q, s.a + d * t);
  }
  double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x), sw = segSweep(s);
  double aq = std::atan2(q.y - s.c.y, q.x - s.c.x);
  double rel = sw >= 0 ? normAng(aq - a0) : normAng(a0 - aq);
  if (rel <= std::abs(sw)) return std::abs(distance(q, s.c) - s.r);
  return std::min(distance(q, s.a), distance(q, s.b));
}

// parameter of a point known to be on the segment
static double paramOf(const ASeg& s, Vec2 q) {
  if (!s.arc) {
    Vec2 d = s.b - s.a;
    double L2 = dot(d, d);
    return L2 > 1e-30 ? dot(q - s.a, d) / L2 : 0;
  }
  double a0 = std::atan2(s.a.y - s.c.y, s.a.x - s.c.x), sw = segSweep(s);
  double aq = std::atan2(q.y - s.c.y, q.x - s.c.x);
  double rel = sw >= 0 ? normAng(aq - a0) : normAng(a0 - aq);
  if (rel > std::abs(sw) + 1e-9) rel = rel > (std::abs(sw) + 2 * kPI) / 2 ? rel - 2 * kPI : rel;
  return rel / std::abs(sw);
}

static std::vector<Vec2> intersect(const ASeg& s, const ASeg& t) {
  std::vector<Vec2> out;
  auto circleOf = [](const ASeg& x, Vec2& c, double& r) { c = x.c; r = x.r; };
  if (!s.arc && !t.arc) {
    Vec2 d1 = s.b - s.a, d2 = t.b - t.a;
    double den = cross(d1, d2);
    if (std::abs(den) < 1e-14 * (length(d1) * length(d2) + 1e-300)) return out;
    double u = cross(t.a - s.a, d2) / den, v = cross(t.a - s.a, d1) / den;
    if (u >= -1e-9 && u <= 1 + 1e-9 && v >= -1e-9 && v <= 1 + 1e-9) out.push_back(s.a + d1 * u);
    return out;
  }
  std::vector<Vec2> cand;
  if (s.arc != t.arc) {
    const ASeg& L = s.arc ? t : s;
    const ASeg& C = s.arc ? s : t;
    Vec2 d = L.b - L.a, f = L.a - C.c;
    double a = dot(d, d), b = 2 * dot(f, d), c = dot(f, f) - C.r * C.r;
    double disc = b * b - 4 * a * c;
    if (disc < 0) {
      if (disc > -1e-9 * a * C.r * C.r) disc = 0; else return out;
    }
    for (double sg : {-1.0, 1.0}) cand.push_back(L.a + d * ((-b + sg * std::sqrt(disc)) / (2 * a)));
  } else {
    Vec2 c1, c2; double r1, r2;
    circleOf(s, c1, r1); circleOf(t, c2, r2);
    double dd = distance(c1, c2);
    if (dd < 1e-12 || dd > r1 + r2 + 1e-9 || dd < std::abs(r1 - r2) - 1e-9) return out;
    double a = (r1 * r1 - r2 * r2 + dd * dd) / (2 * dd);
    double h2 = r1 * r1 - a * a;
    double h = h2 > 0 ? std::sqrt(h2) : 0;
    Vec2 e = (c2 - c1) * (1 / dd);
    Vec2 m = c1 + e * a;
    cand.push_back({m.x - h * e.y, m.y + h * e.x});
    cand.push_back({m.x + h * e.y, m.y - h * e.x});
  }
  for (auto& q : cand)
    if (distToSeg(s, q) < 1e-7 * (1 + s.r) && distToSeg(t, q) < 1e-7 * (1 + t.r)) out.push_back(q);
  return out;
}

static ASeg subSeg(const ASeg& s, Vec2 p, Vec2 q) {
  ASeg r = s;
  r.a = p; r.b = q;
  return r;
}

bool pointInPolygon(Vec2 q, const std::vector<Vec2>& poly) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
    if (((poly[i].y > q.y) != (poly[j].y > q.y)) &&
        (q.x < (poly[j].x - poly[i].x) * (q.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
      in = !in;
  return in;
}
double polygonArea(const std::vector<Vec2>& p) {
  double A = 0;
  for (size_t i = 0; i < p.size(); ++i) A += cross(p[i], p[(i + 1) % p.size()]);
  return A / 2;
}

ASeg Arrangement::halfSeg(int h) const {
  const Half& H = halves[h];
  ASeg s = edges[H.edge].seg;
  if (!H.fwd) { std::swap(s.a, s.b); s.ccw = !s.ccw; }
  return s;
}

void Arrangement::build(const std::vector<ASeg>& in0, double tol, bool removeDangling) {
  verts.clear(); edges.clear(); halves.clear(); cycles.clear();
  std::vector<ASeg> in;
  for (auto& s : in0) {
    if (s.arc && distance(s.a, s.b) < tol) {   // full circle -> two half arcs
      ASeg h1 = s, h2 = s;
      Vec2 opp = s.c * 2.0 - s.a;
      h1.b = opp; h2.a = opp; h1.ccw = h2.ccw = true;
      in.push_back(h1); in.push_back(h2);
    } else {
      in.push_back(s);
    }
  }
  // ---- 1. split points
  std::vector<std::vector<Vec2>> cuts(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    for (size_t j = 0; j < in.size(); ++j) {
      if (i == j) continue;
      if (j > i) for (auto& q : intersect(in[i], in[j])) { cuts[i].push_back(q); cuts[j].push_back(q); }
      for (Vec2 e : {in[j].a, in[j].b}) if (distToSeg(in[i], e) < tol) cuts[i].push_back(e);
    }
  }
  // ---- 2. vertex clustering
  std::vector<Vec2> pts;
  auto vid = [&](Vec2 p) {
    for (size_t k = 0; k < pts.size(); ++k) if (distance(pts[k], p) < tol) return (int)k;
    pts.push_back(p);
    return (int)pts.size() - 1;
  };
  struct Piece { ASeg s; int v0, v1; };
  std::vector<Piece> pieces;
  for (size_t i = 0; i < in.size(); ++i) {
    const ASeg& s = in[i];
    if (segLength(s) < tol) continue;
    std::vector<std::pair<double, Vec2>> ps{{0, s.a}, {1, s.b}};
    for (auto& q : cuts[i]) {
      double t = std::clamp(paramOf(s, q), 0.0, 1.0);
      ps.push_back({t, q});
    }
    std::sort(ps.begin(), ps.end(), [](auto& x, auto& y) { return x.first < y.first; });
    // vertex ids along the segment
    std::vector<int> ids;
    for (auto& [t, q] : ps) {
      int v = vid(q);
      if (ids.empty() || ids.back() != v) ids.push_back(v);
    }
    for (size_t k = 1; k < ids.size(); ++k) {
      if (ids[k] == ids[k - 1]) continue;
      ASeg sub = subSeg(s, pts[ids[k - 1]], pts[ids[k]]);
      if (segLength(sub) < tol * 0.5) continue;
      pieces.push_back({sub, ids[k - 1], ids[k]});
    }
  }
  // ---- 3. dedupe (same end vertices + same midpoint)
  for (auto& p : pieces) {
    Vec2 m = segPoint(p.s, 0.5);
    int found = -1;
    for (size_t e = 0; e < edges.size(); ++e) {
      const Edge& E = edges[e];
      bool same = (E.v0 == p.v0 && E.v1 == p.v1) || (E.v0 == p.v1 && E.v1 == p.v0);
      if (same && distance(segPoint(E.seg, 0.5), m) < tol * 2) { found = (int)e; break; }
    }
    if (found >= 0) {
      edges[found].srcs.push_back(p.s.src);
      edges[found].tags.push_back(p.s.tag);
      continue;
    }
    Edge E;
    E.seg = p.s; E.v0 = p.v0; E.v1 = p.v1; E.srcs = {p.s.src}; E.tags = {p.s.tag};
    edges.push_back(E);
  }
  // ---- 4. dangling removal
  std::vector<char> alive(edges.size(), 1);
  if (removeDangling) {
    bool changed = true;
    while (changed) {
      changed = false;
      std::vector<int> deg(pts.size(), 0);
      for (size_t e = 0; e < edges.size(); ++e) if (alive[e]) { deg[edges[e].v0]++; deg[edges[e].v1]++; }
      for (size_t e = 0; e < edges.size(); ++e)
        if (alive[e] && (deg[edges[e].v0] < 2 || deg[edges[e].v1] < 2)) { alive[e] = 0; changed = true; }
    }
  }
  std::vector<Edge> kept;
  for (size_t e = 0; e < edges.size(); ++e) if (alive[e]) kept.push_back(edges[e]);
  edges = kept;
  // re-index vertices
  std::vector<int> remap(pts.size(), -1);
  for (auto& e : edges)
    for (int* v : {&e.v0, &e.v1}) {
      if (remap[*v] < 0) { remap[*v] = (int)verts.size(); verts.push_back({pts[*v], {}}); }
      *v = remap[*v];
    }
  // ---- 5. half-edges
  for (size_t e = 0; e < edges.size(); ++e) {
    for (int k = 0; k < 2; ++k) {
      Half h;
      h.edge = (int)e; h.fwd = k == 0;
      h.from = k == 0 ? edges[e].v0 : edges[e].v1;
      h.to = k == 0 ? edges[e].v1 : edges[e].v0;
      halves.push_back(h);
    }
  }
  for (size_t h = 0; h < halves.size(); ++h) {
    ASeg s = halfSeg((int)h);
    double L = segLength(s);
    double t = std::min(0.25, std::max(1e-4, 4 * tol / std::max(L, 1e-12)));
    Vec2 q = segPoint(s, t);
    halves[h].ang = std::atan2(q.y - s.a.y, q.x - s.a.x);
    verts[halves[h].from].out.push_back((int)h);
  }
  for (auto& v : verts)
    std::sort(v.out.begin(), v.out.end(), [&](int a, int b) { return halves[a].ang < halves[b].ang; });
  // next(h): at dest vertex, the outgoing half-edge that comes just BEFORE twin(h) in CCW order (clockwise next)
  for (size_t h = 0; h < halves.size(); ++h) {
    int tw = twin((int)h);
    const auto& out = verts[halves[h].to].out;
    auto it = std::find(out.begin(), out.end(), tw);
    size_t idx = it - out.begin();
    halves[h].next = out[(idx + out.size() - 1) % out.size()];
  }
  // ---- 6. cycles
  for (size_t h = 0; h < halves.size(); ++h) {
    if (halves[h].cycle >= 0) continue;
    Cycle c;
    int cur = (int)h;
    int guard = 0;
    while (halves[cur].cycle < 0 && guard++ < 100000) {
      halves[cur].cycle = (int)cycles.size();
      c.halves.push_back(cur);
      auto pl = segPolyline(halfSeg(cur), 0.05 * (1 + edges[halves[cur].edge].seg.r * 0.02));
      c.poly.insert(c.poly.end(), pl.begin(), pl.end() - 1);
      cur = halves[cur].next;
    }
    c.area = polygonArea(c.poly);
    cycles.push_back(std::move(c));
  }
  // ---- 7. components + islands
  std::vector<int> par(verts.size());
  std::iota(par.begin(), par.end(), 0);
  std::function<int(int)> f = [&](int x) { return par[x] == x ? x : par[x] = f(par[x]); };
  for (auto& e : edges) par[f(e.v0)] = f(e.v1);
  for (auto& c : cycles) c.component = c.halves.empty() ? -1 : f(halves[c.halves[0]].from);
  for (size_t i = 0; i < cycles.size(); ++i) {
    if (cycles[i].area >= 0) continue;
    double bestA = 1e300;
    Vec2 probe = cycles[i].poly.empty() ? Vec2{} : cycles[i].poly[0];
    for (size_t j = 0; j < cycles.size(); ++j) {
      if (cycles[j].area <= 0 || cycles[j].component == cycles[i].component) continue;
      if (cycles[j].area < bestA && pointInPolygon(probe, cycles[j].poly)) { bestA = cycles[j].area; cycles[i].container = (int)j; }
    }
  }
}

Vec2 Arrangement::interiorPoint(int ci) const {
  const auto& poly = cycles[ci].poly;
  if (poly.size() < 3) return poly.empty() ? Vec2{} : poly[0];
  Box2 b = emptyBox();
  for (auto& p : poly) expand(b, p);
  // horizontal scanlines: widest interior interval midpoint
  Vec2 best = (b.lo + b.hi) * 0.5;
  double bestW = -1;
  for (int k = 1; k < 16; ++k) {
    double y = b.lo.y + (b.hi.y - b.lo.y) * (k + 0.37) / 16.0;
    std::vector<double> xs;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
      if ((poly[i].y > y) != (poly[j].y > y))
        xs.push_back(poly[i].x + (y - poly[i].y) * (poly[j].x - poly[i].x) / (poly[j].y - poly[i].y));
    std::sort(xs.begin(), xs.end());
    for (size_t i = 0; i + 1 < xs.size(); i += 2)
      if (xs[i + 1] - xs[i] > bestW) { bestW = xs[i + 1] - xs[i]; best = {(xs[i] + xs[i + 1]) / 2, y}; }
  }
  return best;
}

}  // namespace pc::drawing::interp
