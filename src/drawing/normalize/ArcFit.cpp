// Arc / circle re-fitting (SPEC §2.4): Bezier chains and flattened line chains -> Arc/Circle. OWNER WP-1.
#include "normalize/NormInternal.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace pc::drawing::norm {
using namespace geom;

namespace {

// Exact test: cubic P0 C1 C2 P1 approximating a circular arc (standard k = 4/3 tan(theta/4) construction).
bool cubicIsArc(const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, Vec2& ctr, double& r, double relTol = 2e-3) {
  const Vec2 t0 = c1 - p0, t1 = p1 - c2;
  if (length(t0) < 1e-12 || length(t1) < 1e-12) return false;
  const Vec2 n0{-t0.y, t0.x}, n1{-t1.y, t1.x};
  const double den = cross(n0, n1);
  if (std::abs(den) < 1e-12 * length(n0) * length(n1)) return false;
  const double s = cross(p1 - p0, n1) / den;
  ctr = p0 + n0 * s;
  const double r0 = distance(ctr, p0), r1 = distance(ctr, p1);
  if (std::abs(r0 - r1) > relTol * std::max(r0, r1)) return false;
  r = 0.5 * (r0 + r1);
  const double th = std::acos(std::clamp(dot(unit(p0 - ctr), unit(p1 - ctr)), -1.0, 1.0));
  const double k = 4.0 / 3.0 * std::tan(th / 4) * r;
  return std::abs(length(t0) - k) <= 0.03 * k + 1e-9 && std::abs(length(t1) - k) <= 0.03 * k + 1e-9;
}

double maxDev(const std::vector<Vec2>& pts, Vec2 c, double r) {
  double m = 0;
  for (auto& p : pts) m = std::max(m, std::abs(distance(p, c) - r));
  return m;
}

// Signed sweep (deg) walking p[0]..p[n-1] around c.
double sweepOf(const std::vector<Vec2>& p, Vec2 c) {
  double s = 0;
  for (std::size_t i = 1; i < p.size(); ++i) {
    double d = deg(std::atan2(p[i].y - c.y, p[i].x - c.x) - std::atan2(p[i - 1].y - c.y, p[i - 1].x - c.x));
    while (d > 180) d -= 360;
    while (d < -180) d += 360;
    s += d;
  }
  return s;
}

Primitive makeArc(const Primitive& tmpl, int id, Vec2 c, double r, Vec2 a, Vec2 b, double sweep, bool full, float conf) {
  Primitive p = tmpl;
  p.id = id; p.pts.clear(); p.closed = false; p.mergedFrom.clear(); p.attrs.erase("dxf.approx");
  p.center = c; p.radius = r; p.conf = conf;
  if (full) { p.kind = PrimKind::Circle; p.a0 = 0; p.a1 = 360; return p; }
  p.kind = PrimKind::Arc;
  double a0 = deg(std::atan2(a.y - c.y, a.x - c.x)), a1 = deg(std::atan2(b.y - c.y, b.x - c.x));
  if (sweep < 0) std::swap(a0, a1);
  a0 = norm360(a0);
  a1 = a0 + std::abs(sweep);
  p.a0 = a0; p.a1 = a1;
  return p;
}

}  // namespace

void bezierToArcs(Work& w) {
  std::vector<Primitive> out;
  for (auto& p : w.prims) {
    if (p.kind != PrimKind::Bezier || p.pts.size() < 4 || (p.style.filled && !p.style.stroked)) { out.push_back(std::move(p)); continue; }
    // per cubic: exact arc test, else sampled fit
    struct Cub { bool arc; Vec2 c; double r; std::vector<Vec2> samples; };
    std::vector<Cub> cs;
    for (std::size_t i = 0; i + 3 < p.pts.size(); i += 3) {
      Cub cb; cb.arc = false; cb.r = 0;
      for (int k = 0; k <= 8; ++k) cb.samples.push_back(bezier(p.pts[i], p.pts[i + 1], p.pts[i + 2], p.pts[i + 3], k / 8.0));
      if (cubicIsArc(p.pts[i], p.pts[i + 1], p.pts[i + 2], p.pts[i + 3], cb.c, cb.r)) cb.arc = true;
      else if (fitCircle(cb.samples, cb.c, cb.r) && maxDev(cb.samples, cb.c, cb.r) <= w.opt.arcFitTol && cb.r < 5000 &&
               std::abs(sweepOf(cb.samples, cb.c)) > 1.0)
        cb.arc = true;
      cs.push_back(std::move(cb));
    }
    // group consecutive arc cubics on the same circle
    std::vector<Primitive> pieces;
    std::size_t i = 0;
    bool anyArc = false;
    while (i < cs.size()) {
      if (!cs[i].arc) {
        // keep non-circular cubic run as a Bezier
        std::size_t j = i;
        Primitive b = p; b.id = w.nextId++; b.pts = {cs[i].samples.front()}; b.pts[0] = p.pts[3 * i];
        while (j < cs.size() && !cs[j].arc) { for (int k = 1; k <= 3; ++k) b.pts.push_back(p.pts[3 * j + k]); ++j; }
        b.mergedFrom = {p.id}; b.closed = false;
        pieces.push_back(b); i = j; continue;
      }
      std::size_t j = i + 1;
      std::vector<Vec2> all = cs[i].samples;
      Vec2 c = cs[i].c; double r = cs[i].r;
      while (j < cs.size() && cs[j].arc && distance(cs[j].c, c) <= std::max(w.opt.arcFitTol, 1e-3 * r) &&
             std::abs(cs[j].r - r) <= std::max(w.opt.arcFitTol, 1e-3 * r)) {
        all.insert(all.end(), cs[j].samples.begin() + 1, cs[j].samples.end());
        ++j;
      }
      Vec2 fc = c; double fr = r;
      if (j - i > 1 && fitCircle(all, fc, fr) && maxDev(all, fc, fr) <= std::max(w.opt.arcFitTol, maxDev(all, c, r))) { c = fc; r = fr; }
      const double sw = sweepOf(all, c);
      const bool full = std::abs(sw) >= 359.5 && distance(all.front(), all.back()) <= std::max(w.opt.snapTol, 1e-6);
      Primitive a = makeArc(p, w.nextId++, c, r, all.front(), all.back(), sw, full, std::min(p.conf, 0.93f));
      a.mergedFrom = {p.id};
      a.attrs["norm.arcFrom"] = "bezier";
      pieces.push_back(a); anyArc = true;
      i = j;
    }
    if (!anyArc) { out.push_back(std::move(p)); continue; }
    if (pieces.size() == 1) pieces[0].mergedFrom = {p.id};
    w.stats["arcsFromBezier"] += static_cast<int>(pieces.size());
    for (auto& q : pieces) out.push_back(std::move(q));
  }
  w.prims = std::move(out);
}

namespace {
struct Chain { std::vector<std::size_t> prims; std::vector<Vec2> pts; bool closed = false; };

// Greedy recursive fitting along a point chain. Emits arcs for runs of >= 3 segments with consistent small turning.
void fitChain(Work& w, const Chain& ch, std::vector<Primitive>& out, std::set<int>& consumed) {
  const auto& P = ch.pts;
  const std::size_t n = P.size();   // points; segments = n-1
  if (n < 4) return;
  const double tol = w.opt.arcFitTol;
  auto turn = [&](std::size_t k) {   // signed turning angle at interior vertex k (deg)
    const Vec2 a = P[k] - P[k - 1], b = P[k + 1] - P[k];
    return deg(std::atan2(cross(a, b), dot(a, b)));
  };
  auto segLen = [&](std::size_t k) { return distance(P[k], P[k + 1]); };
  std::size_t i = 0;
  std::vector<std::pair<std::size_t, std::size_t>> runs;   // [i, j] point indices
  std::vector<std::pair<Vec2, double>> circ;
  while (i + 3 < n) {
    std::size_t best = 0; Vec2 bc; double br = 0;
    double sign = 0;
    for (std::size_t j = i + 3; j < n; ++j) {
      // turning consistency for vertices i+1..j-1
      bool ok = true;
      for (std::size_t k = std::max(i + 1, j - 1); k <= j - 1; ++k) {
        const double t = turn(k);
        if (std::abs(t) < 0.05 || std::abs(t) > 25) { ok = false; break; }
        if (sign == 0) sign = t > 0 ? 1 : -1;
        if ((t > 0 ? 1 : -1) != sign) { ok = false; break; }
      }
      if (j == i + 3 && ok) {   // also check the first interior vertex
        const double t = turn(i + 1);
        ok = std::abs(t) >= 0.05 && std::abs(t) <= 25 && (t > 0 ? 1 : -1) == sign;
      }
      if (!ok) break;
      // segment length uniformity (flatteners use equal angular steps)
      double lmin = 1e300, lmax = 0;
      for (std::size_t k = i; k < j; ++k) { lmin = std::min(lmin, segLen(k)); lmax = std::max(lmax, segLen(k)); }
      if (lmax > 4.0 * lmin + 1e-9 && j - i > 3) break;
      std::vector<Vec2> sub(P.begin() + static_cast<long>(i), P.begin() + static_cast<long>(j) + 1);
      Vec2 c; double r;
      if (!fitCircle(sub, c, r) || r > 5000) break;
            const double dev = maxDev(sub, c, r);   // vertices lie on the true arc for chord flattening
      if (dev > tol) break;
      best = j; bc = c; br = r;
    }
    if (best) { runs.push_back({i, best}); circ.push_back({bc, br}); i = best; }
    else { ++i; sign = 0; }
  }
  if (runs.empty()) return;
  // adjacent runs on the same circle (greedy fit can stop early on long chains) -> one run
  for (std::size_t r = 0; r + 1 < runs.size();) {
    const bool touch = runs[r].second == runs[r + 1].first;
    const bool same = distance(circ[r].first, circ[r + 1].first) <= 2 * tol && std::abs(circ[r].second - circ[r + 1].second) <= tol;
    if (touch && same) {
      std::vector<Vec2> sub(P.begin() + static_cast<long>(runs[r].first), P.begin() + static_cast<long>(runs[r + 1].second) + 1);
      Vec2 c; double rad;
      if (fitCircle(sub, c, rad) && maxDev(sub, c, rad) <= tol) {
        runs[r].second = runs[r + 1].second; circ[r] = {c, rad};
        runs.erase(runs.begin() + static_cast<long>(r) + 1); circ.erase(circ.begin() + static_cast<long>(r) + 1);
        continue;
      }
    }
    ++r;
  }
  // merge closed chain wrap-around and detect full circles
  for (std::size_t r = 0; r < runs.size(); ++r) {
    const auto [a, b] = runs[r];
    const auto [c, rad] = circ[r];
    std::vector<Vec2> sub(P.begin() + static_cast<long>(a), P.begin() + static_cast<long>(b) + 1);
    const double sw = sweepOf(sub, c);
    const bool full = ch.closed && a == 0 && b == n - 1 && std::abs(sw) >= 359;
    const Primitive& tmpl = w.prims[ch.prims[a]];
    Primitive arc = makeArc(tmpl, w.nextId++, c, rad, P[a], P[b], sw, full, std::min(tmpl.conf, 0.85f));
    arc.kind = full ? PrimKind::Circle : PrimKind::Arc;
    arc.p0 = arc.p1 = Vec2{0, 0};
    for (std::size_t k = a; k < b; ++k) {
      const int pid = w.prims[ch.prims[k]].id;
      if (std::find(arc.mergedFrom.begin(), arc.mergedFrom.end(), pid) == arc.mergedFrom.end()) arc.mergedFrom.push_back(pid);
    }
    arc.attrs["norm.arcFrom"] = "segments";
    arc.attrs["norm.arcSegments"] = std::to_string(b - a);
    for (std::size_t k = a; k < b; ++k) consumed.insert(static_cast<int>(ch.prims[k]));
    out.push_back(arc);
    w.stats[full ? "circlesFromSegments" : "arcsFromSegments"]++;
  }
  // leftover segments of a Polyline source become Lines
  for (std::size_t k = 0; k + 1 < n; ++k) {
    const Primitive& src = w.prims[ch.prims[k]];
    if (src.kind != PrimKind::Polyline) continue;
    bool inRun = false;
    for (auto& [a, b] : runs) inRun |= (k >= a && k < b);
    if (inRun) continue;
    Primitive l = src; l.id = w.nextId++; l.kind = PrimKind::Line; l.p0 = P[k]; l.p1 = P[k + 1]; l.pts.clear(); l.closed = false;
    l.mergedFrom = {src.id}; l.attrs.erase("dxf.approx");
    out.push_back(l);
  }
}
}  // namespace

void fitArcs(Work& w) {
  const double jt = std::max(1e-6, std::min(w.opt.snapTol * 0.5, 0.02));
  std::vector<Primitive> out;
  std::set<int> consumed;   // indices into w.prims
  // (1) stroked polylines (DXF spline approximations, raster chains): one chain each
  for (std::size_t i = 0; i < w.prims.size(); ++i) {
    const Primitive& p = w.prims[i];
    if (p.kind != PrimKind::Polyline || !p.style.stroked || (p.style.filled && !p.style.stroked) || p.pts.size() < 4) continue;
    Chain ch; ch.pts = p.pts; ch.closed = p.closed;
    if (p.closed) ch.pts.push_back(p.pts.front());
    ch.prims.assign(ch.pts.size() - 1, i);
    std::vector<Primitive> res; std::set<int> cons;
    fitChain(w, ch, res, cons);
    if (!cons.empty()) {
      consumed.insert(static_cast<int>(i));
      for (auto& q : res) { q.mergedFrom = {p.id}; out.push_back(std::move(q)); }
    }
  }
  // (2) line chains through degree-2 vertices with identical stroke style
  std::vector<std::size_t> lines;
  for (std::size_t i = 0; i < w.prims.size(); ++i)
    if (w.prims[i].kind == PrimKind::Line && w.prims[i].style.dash.empty() && w.prims[i].style.stroked && !w.prims[i].fromDimBlock)
      lines.push_back(i);
  auto keyOf = [jt](Vec2 p) { return std::make_pair(static_cast<long long>(std::floor(p.x / jt)), static_cast<long long>(std::floor(p.y / jt))); };
  std::map<std::pair<long long, long long>, std::vector<std::pair<std::size_t, int>>> grid;   // (line index, end)
  for (std::size_t li : lines) { grid[keyOf(w.prims[li].p0)].push_back({li, 0}); grid[keyOf(w.prims[li].p1)].push_back({li, 1}); }
  auto neighbours = [&](std::size_t li, int end) {
    const Vec2 p = end == 0 ? w.prims[li].p0 : w.prims[li].p1;
    std::vector<std::pair<std::size_t, int>> r;
    const auto k = keyOf(p);
    for (long long dx = -1; dx <= 1; ++dx)
      for (long long dy = -1; dy <= 1; ++dy) {
        auto it = grid.find({k.first + dx, k.second + dy});
        if (it == grid.end()) continue;
        for (auto& e : it->second) {
          if (e.first == li) continue;
          const Vec2 q = e.second == 0 ? w.prims[e.first].p0 : w.prims[e.first].p1;
          if (distance(p, q) <= jt) r.push_back(e);
        }
      }
    return r;
  };
  std::set<std::size_t> visited;
  for (std::size_t li : lines) {
    if (visited.count(li)) continue;
    // walk back to a chain start
    std::size_t start = li; int startEnd = 0;
    {
      std::set<std::size_t> seen{li};
      std::size_t cur = li; int curEnd = 0;   // walk from cur's end `curEnd`
      while (true) {
        auto nb = neighbours(cur, curEnd);
        if (nb.size() != 1 || seen.count(nb[0].first) || !sameStroke(w.prims[nb[0].first].style, w.prims[li].style)) break;
        if (neighbours(nb[0].first, nb[0].second).size() != 1) break;
        cur = nb[0].first; curEnd = 1 - nb[0].second; seen.insert(cur);
      }
      start = cur; startEnd = curEnd;
    }
    Chain ch;
    std::size_t cur = start; int fromEnd = startEnd;   // chain enters `cur` at `fromEnd`
    ch.pts.push_back(fromEnd == 0 ? w.prims[cur].p0 : w.prims[cur].p1);
    while (true) {
      visited.insert(cur);
      ch.prims.push_back(cur);
      const int outEnd = 1 - fromEnd;
      ch.pts.push_back(outEnd == 0 ? w.prims[cur].p0 : w.prims[cur].p1);
      auto nb = neighbours(cur, outEnd);
      if (nb.size() != 1 || !sameStroke(w.prims[nb[0].first].style, w.prims[li].style)) break;
      if (neighbours(nb[0].first, nb[0].second).size() != 1) break;
      if (visited.count(nb[0].first)) { if (nb[0].first == start) ch.closed = true; break; }
      cur = nb[0].first; fromEnd = nb[0].second;
    }
    if (ch.prims.size() < 3) continue;
    std::vector<Primitive> res; std::set<int> cons;
    fitChain(w, ch, res, cons);
    for (int c : cons) consumed.insert(c);
    for (auto& q : res) out.push_back(std::move(q));
  }
  if (!consumed.empty()) {
    std::vector<Primitive> keep;
    for (std::size_t i = 0; i < w.prims.size(); ++i) if (!consumed.count(static_cast<int>(i))) keep.push_back(std::move(w.prims[i]));
    for (auto& q : out) keep.push_back(std::move(q));
    w.prims = std::move(keep);
  }
  mergeCocircularArcs(w);
}

// Touching arcs on the same circle with the same stroke (a chain broken by a T-junction such as a leader tip) -> one
// arc, or a circle when they close.
void mergeCocircularArcs(Work& w) {
  const double tol = std::max(w.opt.arcFitTol, 1e-4);
  auto ang = [](const Primitive& a, double deg0) { return Vec2{a.center.x + a.radius * std::cos(deg0 * kPi / 180), a.center.y + a.radius * std::sin(deg0 * kPi / 180)}; };
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < w.prims.size() && !changed; ++i) {
      if (w.prims[i].kind != PrimKind::Arc) continue;
      for (std::size_t j = 0; j < w.prims.size() && !changed; ++j) {
        if (i == j || w.prims[j].kind != PrimKind::Arc) continue;
        Primitive &a = w.prims[i], &b = w.prims[j];
        if (distance(a.center, b.center) > tol || std::abs(a.radius - b.radius) > tol || !sameStroke(a.style, b.style)) continue;
        if (distance(ang(a, a.a1), ang(b, b.a0)) > std::max(tol, w.opt.snapTol)) continue;   // a then b (CCW)
        Primitive m = a;
        m.id = w.nextId++;
        const double sweep = (a.a1 - a.a0) + (b.a1 - b.a0);
        m.mergedFrom = {a.id, b.id};
        if (sweep >= 360 - 0.5) { m.kind = PrimKind::Circle; m.a0 = 0; m.a1 = 360; }
        else { m.a1 = m.a0 + sweep; }
        m.center = (a.center + b.center) * 0.5; m.radius = (a.radius + b.radius) / 2;
        m.attrs["norm.arcFrom"] = "mergedArcs";
        const std::size_t hi = std::max(i, j), lo = std::min(i, j);
        w.prims.erase(w.prims.begin() + static_cast<long>(hi)); w.prims.erase(w.prims.begin() + static_cast<long>(lo));
        w.prims.push_back(m);
        w.stats["arcsMerged"]++;
        changed = true;
      }
    }
  }
}

}  // namespace pc::drawing::norm
