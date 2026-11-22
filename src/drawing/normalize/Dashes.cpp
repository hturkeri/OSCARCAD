// Dash chaining and collinear-overlap merging (SPEC §2.4). OWNER WP-1.
#include "normalize/NormInternal.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace pc::drawing::norm {
using namespace geom;

namespace {
struct Seg { std::size_t idx; double ang; Vec2 u; double off, t0, t1; };

double median(std::vector<double> v) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  return v[v.size() / 2];
}
}  // namespace

void chainDashes(Work& w) {
  const double angTol = w.opt.angleTolDeg;
  const double offTol = std::max(w.opt.snapTol, 0.02);
  const double maxGap = 4.0, maxDash = 30.0;
  std::vector<Seg> segs;
  for (std::size_t i = 0; i < w.prims.size(); ++i) {
    const Primitive& p = w.prims[i];
    if (p.kind != PrimKind::Line || !p.style.stroked || p.fromDimBlock) continue;
    Vec2 d = p.p1 - p.p0;
    double a = norm360(deg(std::atan2(d.y, d.x)));
    if (a >= 180) a -= 180;
    if (a > 180 - angTol) a -= 180;   // fold near-horizontal
    const Vec2 u = polar({0, 0}, 1, a), n{-u.y, u.x};
    const double t0 = dot(p.p0, u), t1 = dot(p.p1, u);
    segs.push_back({i, a, u, dot((p.p0 + p.p1) * 0.5, n), std::min(t0, t1), std::max(t0, t1)});
  }
  std::sort(segs.begin(), segs.end(), [](const Seg& a, const Seg& b) { return a.ang < b.ang; });
  std::set<std::size_t> consumed;
  std::vector<Primitive> created;
  // cluster by angle, then by offset, then sweep along the line
  std::size_t s = 0;
  while (s < segs.size()) {
    std::size_t e = s + 1;
    while (e < segs.size() && segs[e].ang - segs[e - 1].ang <= angTol * 0.5 && segs[e].ang - segs[s].ang <= angTol) ++e;
    std::vector<Seg> g(segs.begin() + static_cast<long>(s), segs.begin() + static_cast<long>(e));
    // re-express offsets/params in the group's mean direction
    double ma = 0; for (auto& x : g) ma += x.ang; ma /= static_cast<double>(g.size());
    const Vec2 u = polar({0, 0}, 1, ma), n{-u.y, u.x};
    for (auto& x : g) {
      const Primitive& p = w.prims[x.idx];
      x.u = u; x.off = dot((p.p0 + p.p1) * 0.5, n);
      const double t0 = dot(p.p0, u), t1 = dot(p.p1, u);
      x.t0 = std::min(t0, t1); x.t1 = std::max(t0, t1);
    }
    std::sort(g.begin(), g.end(), [](const Seg& a, const Seg& b) { return a.off < b.off; });
    std::size_t a = 0;
    while (a < g.size()) {
      std::size_t b = a + 1;
      while (b < g.size() && g[b].off - g[b - 1].off <= offTol * 0.5 && g[b].off - g[a].off <= offTol) ++b;
      std::vector<Seg> line(g.begin() + static_cast<long>(a), g.begin() + static_cast<long>(b));
      a = b;
      if (line.size() < 2) continue;
      std::sort(line.begin(), line.end(), [](const Seg& x, const Seg& y) { return x.t0 < y.t0 || (x.t0 == y.t0 && x.t1 < y.t1); });
      // split into runs of compatible style with gaps <= maxGap
      std::size_t r0 = 0;
      while (r0 < line.size()) {
        std::size_t r1 = r0 + 1;
        double end = line[r0].t1;
        const LineStyle& st = w.prims[line[r0].idx].style;
        while (r1 < line.size() && line[r1].t0 - end <= maxGap && sameStroke(w.prims[line[r1].idx].style, st)) {
          end = std::max(end, line[r1].t1); ++r1;
        }
        std::vector<Seg> run(line.begin() + static_cast<long>(r0), line.begin() + static_cast<long>(r1));
        r0 = r1;
        if (run.size() < 2) continue;
        // classify: gaps and overlaps
        std::vector<double> dashes, gaps;
        bool overlapOnly = true, anyOverlap = false, allShort = true;
        double cur = run[0].t1;
        dashes.push_back(run[0].t1 - run[0].t0);
        for (std::size_t k = 1; k < run.size(); ++k) {
          const double gap = run[k].t0 - cur;
          if (gap > offTol) { overlapOnly = false; gaps.push_back(gap); dashes.push_back(run[k].t1 - run[k].t0); }
          else if (gap < -offTol) { anyOverlap = true; dashes.back() = std::max(cur, run[k].t1) - (cur - dashes.back()); }
          else { overlapOnly = false; gaps.push_back(0); dashes.push_back(run[k].t1 - run[k].t0); }
          cur = std::max(cur, run[k].t1);
        }
        for (double d : dashes) allShort = allShort && d <= maxDash;
        int realGaps = 0; for (double gp : gaps) realGaps += gp > offTol;
        const bool dashed = realGaps >= 2 || (realGaps == 1 && allShort && dashes.size() == 2 && dashes[0] <= 6 && dashes[1] <= 6);
        if (dashed && !allShort) {
          // long solid pieces with gaps: separate edges, not dashes
          continue;
        }
        if (!dashed && !(overlapOnly && anyOverlap)) continue;   // touching collinear edges stay separate (T-junction info)
        const Primitive& first = w.prims[run[0].idx];
        Primitive m = first;
        m.id = w.nextId++;
        m.mergedFrom.clear();
        double t0 = 1e300, t1 = -1e300;
        for (auto& x : run) { t0 = std::min(t0, x.t0); t1 = std::max(t1, x.t1); m.mergedFrom.push_back(w.prims[x.idx].id); consumed.insert(x.idx); }
        double off = 0; for (auto& x : run) off += x.off; off /= static_cast<double>(run.size());
        m.p0 = u * t0 + n * off; m.p1 = u * t1 + n * off;
        if (dashed) {
          // pattern: long dash, gap[, dot, gap]
          std::vector<double> longs, dots, gs;
          const double md = median(dashes);
          for (double d : dashes) (d < 0.4 * md ? dots : longs).push_back(d);
          for (double gp : gaps) if (gp > offTol) gs.push_back(gp);
          const double lg = median(longs), gp = median(gs);
          if (m.style.dash.empty()) {
            if (!dots.empty() && dots.size() * 3 >= longs.size()) m.style.dash = {lg, gp, median(dots), gp};
            else m.style.dash = {lg, gp};
          }
          m.attrs["norm.dashPieces"] = std::to_string(run.size());
          w.stats["dashedLines"]++;
        } else {
          w.stats["collinearMerged"]++;
        }
        m.conf = std::min(first.conf, dashed ? 0.88f : first.conf);
        created.push_back(m);
      }
    }
    s = e;
  }
  if (consumed.empty()) return;
  std::vector<Primitive> keep;
  for (std::size_t i = 0; i < w.prims.size(); ++i) if (!consumed.count(i)) keep.push_back(std::move(w.prims[i]));
  for (auto& m : created) keep.push_back(std::move(m));
  w.prims = std::move(keep);
}

}  // namespace pc::drawing::norm
