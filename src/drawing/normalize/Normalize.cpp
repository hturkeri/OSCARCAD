// S2 normalization driver + small passes (cleanup, arrowheads, hatch, snapping). OWNER WP-1. SPEC §2.4.
#include "normalize/NormInternal.h"
#include "normalize/SymbolFonts.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace pc::drawing {
namespace norm {
using namespace geom;

namespace {
// solid vs dashed never merge (extension line over a centre line); patterns compared loosely
bool sameDash(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > 0.05 + 0.05 * std::max(a[i], b[i])) return false;
  return true;
}
}  // namespace

bool sameStroke(const LineStyle& a, const LineStyle& b) {
  return std::abs(a.width - b.width) <= 1e-3 + 0.02 * std::max(a.width, b.width) && a.rgb == b.rgb && a.stroked == b.stroked &&
         a.filled == b.filled && a.layer == b.layer && a.linetype == b.linetype && sameDash(a.dash, b.dash);
}

namespace {
std::string geomKey(const Primitive& p, double q) {
  auto r = [q](double v) { return static_cast<long long>(std::llround(v / q)); };
  std::ostringstream o;
  o << static_cast<int>(p.kind) << ':';
  switch (p.kind) {
    case PrimKind::Line: {
      Vec2 a = p.p0, b = p.p1;
      if (a.x > b.x || (a.x == b.x && a.y > b.y)) std::swap(a, b);
      o << r(a.x) << ',' << r(a.y) << ',' << r(b.x) << ',' << r(b.y);
      break;
    }
    case PrimKind::Arc: case PrimKind::Circle:
      o << r(p.center.x) << ',' << r(p.center.y) << ',' << r(p.radius) << ',' << r(p.a0) << ',' << r(p.a1);
      break;
    default:
      for (auto& v : p.pts) o << r(v.x) << ',' << r(v.y) << ';';
      o << r(p.p0.x) << r(p.p0.y) << r(p.p1.x) << r(p.p1.y);
  }
  return o.str();
}
}  // namespace

void cleanup(Work& w) {
  {
    std::vector<TextRun> keep;
    for (auto& t : w.texts) {
      bool blank = true; for (char c : t.utf8) blank = blank && (c == ' ' || c == '\t');
      if (blank) { w.stats["blankTexts"]++; continue; }
      keep.push_back(std::move(t));
    }
    w.texts = std::move(keep);
  }
  std::vector<Primitive> out;
  std::map<std::string, std::size_t> seen;
  const double q = std::max(1e-4, w.opt.snapTol * 0.1);
  for (auto& p : w.prims) {
    if (p.kind == PrimKind::Line && distance(p.p0, p.p1) < 1e-6) { w.stats["zeroLength"]++; continue; }
    if ((p.kind == PrimKind::Arc || p.kind == PrimKind::Circle) && p.radius < 1e-6) { w.stats["zeroLength"]++; continue; }
    const std::string k = geomKey(p, q);
    auto it = seen.find(k);
    if (it != seen.end()) {
      Primitive& keep = out[it->second];
      // fill + stroke copies of the same outline: keep one, union the flags
      keep.style.filled = keep.style.filled || p.style.filled;
      keep.style.stroked = keep.style.stroked || p.style.stroked;
      keep.style.width = std::max(keep.style.width, p.style.width);
      keep.mergedFrom.push_back(p.id);
      w.stats["duplicates"]++;
      continue;
    }
    seen.emplace(k, out.size());
    out.push_back(std::move(p));
  }
  w.prims = std::move(out);
}

namespace {
bool arrowFromTriangle(const std::array<Vec2, 3>& v, const NormalizeOptions& o, Vec2& tip, Vec2& base) {
  double best = 1e300; int t = 0;
  for (int k = 0; k < 3; ++k) { const double len = distance(v[(k + 1) % 3], v[(k + 2) % 3]); if (len < best) { best = len; t = k; } }
  tip = v[t]; base = (v[(t + 1) % 3] + v[(t + 2) % 3]) * 0.5;
  const double len = distance(tip, base);
  if (len < o.arrowMinMm || len > o.arrowMaxMm) return false;
  const double ratio = best / len;   // base width / length: ISO filled arrows ~ 0.33 (15° half angle 0.27 .. 30° 0.58)
  return ratio >= 0.12 && ratio <= 0.8;
}
}  // namespace

void detectArrowheads(Work& w) {
  if (!w.opt.detectVectorArrowheads) return;
  // Reduced prints (an A3 sheet printed on a smaller page, e.g. the real samples SWC-RR-001 / ATM-PS-001: text cap
  // height ~1.2 mm, arrows ~0.9 mm): when the median text height is below the ISO 3098 minimum of 2.5 mm, scale the
  // lower arrow-length bound with it (ISO 129: arrow length ~ text height). Sheets with normal text are unaffected.
  NormalizeOptions eff = w.opt;
  {
    std::vector<double> hs;
    for (auto& t : w.texts) if (t.height > 0) hs.push_back(t.height);
    if (hs.size() >= 5) {
      std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
      const double h = hs[hs.size() / 2];
      if (h < 2.5) eff.arrowMinMm = std::min(w.opt.arrowMinMm, std::max(0.3, 0.5 * h));
      if (eff.arrowMinMm != w.opt.arrowMinMm) w.stats["arrowMinScaled"]++;
    }
  }
  std::vector<Primitive> out;
  std::set<int> consumed;
  // (a) fill-only closed polylines with 3 vertices
  for (auto& p : w.prims) {
    if (p.kind == PrimKind::Polyline && p.style.filled && p.closed && p.pts.size() == 3) {
      Vec2 tip, base;
      if (arrowFromTriangle({p.pts[0], p.pts[1], p.pts[2]}, eff, tip, base)) {
        Primitive a = p; a.id = w.nextId++; a.kind = PrimKind::Arrowhead; a.p0 = tip; a.p1 = base; a.pts.clear(); a.closed = false;
        a.mergedFrom = {p.id}; a.conf = std::min(p.conf, 0.9f);
        consumed.insert(p.id); out.push_back(a); w.stats["arrowheads"]++;
      }
    }
  }
  // (b) filled+stroked triangles emitted as 3 lines from one paint op (same provenance)
  std::map<std::string, std::vector<const Primitive*>> byOp;
  for (auto& p : w.prims) if (p.kind == PrimKind::Line && p.style.filled && !p.prov.ref.empty()) byOp[p.prov.ref].push_back(&p);
  for (auto& [ref, v] : byOp) {
    if (v.size() != 3) continue;
    std::array<Vec2, 3> tri{v[0]->p0, v[1]->p0, v[2]->p0};
    bool closed = distance(v[0]->p1, v[1]->p0) < 1e-6 && distance(v[1]->p1, v[2]->p0) < 1e-6 && distance(v[2]->p1, v[0]->p0) < 1e-6;
    Vec2 tip, base;
    if (!closed || !arrowFromTriangle(tri, eff, tip, base)) continue;
    Primitive a = *v[0]; a.id = w.nextId++; a.kind = PrimKind::Arrowhead; a.p0 = tip; a.p1 = base;
    a.mergedFrom = {v[0]->id, v[1]->id, v[2]->id};
    for (auto* q : v) consumed.insert(q->id);
    out.push_back(a); w.stats["arrowheads"]++;
  }
  if (consumed.empty()) return;
  std::vector<Primitive> keep;
  for (auto& p : w.prims) if (!consumed.count(p.id)) keep.push_back(std::move(p));
  for (auto& a : out) keep.push_back(std::move(a));
  w.prims = std::move(keep);
}

void detectHatch(Work& w) {
  if (!w.opt.detectHatch) return;
  std::set<int> consumed;
  std::vector<Primitive> regions;
  // (a) image tiles (scan texture of a section, enjektör lesson §2.8.3)
  {
    std::vector<std::size_t> imgs;
    for (std::size_t i = 0; i < w.prims.size(); ++i) if (w.prims[i].kind == PrimKind::Image) imgs.push_back(i);
    std::vector<int> comp(imgs.size(), -1);
    int nc = 0;
    for (std::size_t i = 0; i < imgs.size(); ++i) {
      if (comp[i] >= 0) continue;
      comp[i] = nc;
      std::vector<std::size_t> stack{i};
      while (!stack.empty()) {
        const std::size_t a = stack.back(); stack.pop_back();
        const Box2 ba = primBox(w.prims[imgs[a]]);
        for (std::size_t b = 0; b < imgs.size(); ++b) {
          if (comp[b] >= 0) continue;
          const Box2 bb = primBox(w.prims[imgs[b]]);
          const double t = 0.5;
          if (ba.lo.x - t <= bb.hi.x && bb.lo.x - t <= ba.hi.x && ba.lo.y - t <= bb.hi.y && bb.lo.y - t <= ba.hi.y) { comp[b] = nc; stack.push_back(b); }
        }
      }
      ++nc;
    }
    for (int c = 0; c < nc; ++c) {
      std::vector<std::size_t> mem;
      for (std::size_t i = 0; i < imgs.size(); ++i) if (comp[i] == c) mem.push_back(imgs[i]);
      if (mem.size() < 2) continue;
      // boundary: convex hull of all tile corners
      std::vector<Vec2> pts;
      for (auto i : mem) for (auto& q : w.prims[i].pts) pts.push_back(q);
      std::sort(pts.begin(), pts.end(), [](Vec2 a, Vec2 b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
      std::vector<Vec2> hull;
      for (int pass = 0; pass < 2; ++pass) {
        const std::size_t base = hull.size();
        for (auto& p : pts) {
          while (hull.size() >= base + 2 && cross(hull[hull.size() - 1] - hull[hull.size() - 2], p - hull[hull.size() - 2]) <= 0) hull.pop_back();
          hull.push_back(p);
        }
        hull.pop_back();
        std::reverse(pts.begin(), pts.end());
      }
      Primitive h; h.id = w.nextId++; h.kind = PrimKind::HatchRegion; h.pts = hull; h.closed = true; h.conf = 0.7f;
      h.style.stroked = false; h.style.filled = true; h.prov = w.prims[mem[0]].prov; h.attrs["norm.hatchFrom"] = "imageTiles";
      for (auto i : mem) { h.mergedFrom.push_back(w.prims[i].id); consumed.insert(w.prims[i].id); }
      regions.push_back(h); w.stats["hatchRegions"]++;
    }
  }
  // (b) families of >= 4 thin parallel lines, 30..60 deg, evenly spaced, overlapping extents
  {
    struct L { std::size_t i; double ang, off, t0, t1; };
    std::vector<L> ls;
    for (std::size_t i = 0; i < w.prims.size(); ++i) {
      const Primitive& p = w.prims[i];
      if (p.kind != PrimKind::Line || !p.style.dash.empty() || p.fromDimBlock) continue;
      Vec2 d = p.p1 - p.p0;
      double a = norm360(deg(std::atan2(d.y, d.x)));
      if (a >= 180) a -= 180;
      const double m = std::fmod(a, 90.0);
      if (m < 25 || m > 65) continue;
      const Vec2 u = polar({0, 0}, 1, a), n{-u.y, u.x};
      const double t0 = dot(p.p0, u), t1 = dot(p.p1, u);
      ls.push_back({i, a, dot(p.p0, n), std::min(t0, t1), std::max(t0, t1)});
    }
    std::sort(ls.begin(), ls.end(), [](const L& a, const L& b) { return a.ang < b.ang || (a.ang == b.ang && a.off < b.off); });
    std::size_t s = 0;
    while (s < ls.size()) {
      std::size_t e = s + 1;
      while (e < ls.size() && std::abs(ls[e].ang - ls[s].ang) < 0.5) ++e;
      // within the angle group, split into evenly spaced runs
      std::vector<L> g(ls.begin() + static_cast<long>(s), ls.begin() + static_cast<long>(e));
      std::size_t a = 0;
      while (a + 3 < g.size() + 0) {
        std::size_t b = a + 1;
        const double pitch = g[a + 1].off - g[a].off;
        if (pitch < 0.3 || pitch > 10) { ++a; continue; }
        while (b + 1 < g.size() && std::abs((g[b + 1].off - g[b].off) - pitch) < 0.15 * pitch &&
               sameStroke(w.prims[g[b + 1].i].style, w.prims[g[a].i].style))
          ++b;
        if (b - a + 1 >= 4) {
          Primitive h; h.id = w.nextId++; h.kind = PrimKind::HatchRegion; h.closed = true; h.conf = 0.75f;
          h.hatchAngleDeg = g[a].ang; h.hatchPitch = pitch; h.style = w.prims[g[a].i].style; h.prov = w.prims[g[a].i].prov;
          Box2 bb = emptyBox();
          for (std::size_t k = a; k <= b; ++k) {
            const Primitive& p = w.prims[g[k].i];
            expand(bb, p.p0); expand(bb, p.p1);
            h.mergedFrom.push_back(p.id); consumed.insert(p.id);
          }
          h.pts = {bb.lo, {bb.hi.x, bb.lo.y}, bb.hi, {bb.lo.x, bb.hi.y}};
          h.attrs["norm.hatchFrom"] = "lines";
          regions.push_back(h); w.stats["hatchRegions"]++;
          a = b + 1;
        } else {
          ++a;
        }
      }
      s = e;
    }
  }
  if (consumed.empty()) return;
  std::vector<Primitive> keep;
  for (auto& p : w.prims) if (!consumed.count(p.id)) keep.push_back(std::move(p));
  for (auto& r : regions) keep.push_back(std::move(r));
  w.prims = std::move(keep);
}

void snapEndpoints(Work& w) {
  const double tol = w.opt.snapTol;
  if (tol <= 0) return;
  struct E { std::size_t prim; int end; Vec2 p; bool fixed; };
  std::vector<E> es;
  for (std::size_t i = 0; i < w.prims.size(); ++i) {
    const Primitive& p = w.prims[i];
    if (p.kind == PrimKind::Line) { es.push_back({i, 0, p.p0, false}); es.push_back({i, 1, p.p1, false}); }
    else if (p.kind == PrimKind::Arc) {
      es.push_back({i, 0, polar(p.center, p.radius, p.a0), true}); es.push_back({i, 1, polar(p.center, p.radius, p.a1), true});
    }
  }
  std::map<std::pair<long long, long long>, std::vector<std::size_t>> grid;
  auto cell = [tol](Vec2 p) { return std::make_pair(static_cast<long long>(std::floor(p.x / tol)), static_cast<long long>(std::floor(p.y / tol))); };
  for (std::size_t k = 0; k < es.size(); ++k) grid[cell(es[k].p)].push_back(k);
  std::vector<int> cl(es.size(), -1);
  int n = 0;
  for (std::size_t k = 0; k < es.size(); ++k) {
    if (cl[k] >= 0) continue;
    cl[k] = n;
    std::vector<std::size_t> stack{k};
    while (!stack.empty()) {
      const std::size_t a = stack.back(); stack.pop_back();
      const auto c = cell(es[a].p);
      for (long long dx = -1; dx <= 1; ++dx)
        for (long long dy = -1; dy <= 1; ++dy) {
          auto it = grid.find({c.first + dx, c.second + dy});
          if (it == grid.end()) continue;
          for (std::size_t b : it->second) if (cl[b] < 0 && distance(es[a].p, es[b].p) <= tol) { cl[b] = n; stack.push_back(b); }
        }
    }
    ++n;
  }
  std::vector<Vec2> sum(n, Vec2{0, 0}), fixedPt(n);
  std::vector<int> cnt(n, 0); std::vector<bool> hasFixed(n, false);
  for (std::size_t k = 0; k < es.size(); ++k) {
    sum[cl[k]] = sum[cl[k]] + es[k].p; cnt[cl[k]]++;
    if (es[k].fixed && !hasFixed[cl[k]]) { hasFixed[cl[k]] = true; fixedPt[cl[k]] = es[k].p; }
  }
  int moved = 0;
  for (std::size_t k = 0; k < es.size(); ++k) {
    if (es[k].fixed || cnt[cl[k]] < 2) continue;
    const Vec2 t = hasFixed[cl[k]] ? fixedPt[cl[k]] : sum[cl[k]] * (1.0 / cnt[cl[k]]);
    Primitive& p = w.prims[es[k].prim];
    Vec2& e = es[k].end == 0 ? p.p0 : p.p1;
    if (distance(e, t) > 1e-12) { e = t; ++moved; }
  }
  if (moved) w.stats["snappedEndpoints"] += moved;
}

void repairSymbolFonts(Work& w) {
  std::map<std::string, std::vector<int>> repaired;   // font -> text ids
  for (auto& t : w.texts) {
    std::string before = t.utf8;
    if (w.opt.repairSymbolFonts) {
      auto gn = t.attrs.find("pdf.glyphNames");
      if (gn != t.attrs.end()) {
        const auto dn = t.attrs.find("pdf.fontDiffNames");
        const bool symbolic = t.attrs.count("pdf.symbolic") > 0;
        const FontTable* tab = chooseSymbolTable(t.font, dn == t.attrs.end() ? std::string() : dn->second, symbolic);
        if (tab) {
          std::istringstream is(gn->second);
          std::string name, rebuilt; bool changed = false;
          // rebuild glyph by glyph: glyph names are 1:1 with the decoded characters of the run
          std::vector<std::string> chars;
          for (std::size_t i = 0; i < t.utf8.size();) {
            const unsigned char c = t.utf8[i];
            const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
            chars.push_back(t.utf8.substr(i, len)); i += len;
          }
          std::size_t k = 0;
          while (is >> name) {
            auto it = tab->glyphToUtf8.find(name);
            if (it != tab->glyphToUtf8.end()) { rebuilt += it->second; changed = true; }
            else if (k < chars.size()) rebuilt += chars[k];
            ++k;
          }
          if (changed && k == chars.size()) {
            t.alternatives.insert(t.alternatives.begin(), TextHypothesis{t.utf8, 0.1f});
            t.utf8 = rebuilt;
            t.conf = std::max(t.conf, 0.9f);
            t.attrs["norm.symbolTable"] = tab->family;
            repaired[t.font].push_back(t.id);
          }
        }
      }
    }
    t.utf8 = normalizeSymbols(t.utf8);
    if (t.utf8 != before) w.stats["symbolTexts"]++;
  }
  for (auto& [font, ids] : repaired) {
    Diagnostic d; d.severity = Severity::Info; d.code = "pdf.font.symbolRepaired"; d.stage = static_cast<int>(Stage::Normalize);
    d.message = font + " sembol yazı tipi glif adlarıyla onarıldı (" + std::to_string(ids.size()) + " metin)"; d.ids = ids;
    w.diags.push_back(d);
  }
}

}  // namespace norm

StageResult normalize(const PrimitiveSheet& raw, const NormalizeOptions& opt, PrimitiveSheet& out, int& nextId, ProgressSink* progress) {
  using namespace norm;
  report(progress, 0.0, "Normalizasyon");
  int id = nextId;
  Work w(opt, id);
  w.prims = raw.prims;
  w.texts = raw.texts;
  struct Pass { const char* msg; void (*fn)(Work&); };
  const Pass passes[] = {{"Temizlik", cleanup},           {"Sembol yazı tipleri", repairSymbolFonts}, {"Ok uçları", detectArrowheads},
                         {"Bezier -> yay", bezierToArcs}, {"Yay uydurma", fitArcs},                   {"Kesik çizgiler", chainDashes},
                         {"Tarama", detectHatch},         {"Yakalama", snapEndpoints},                {"Metin birleştirme", mergeTextRuns}};
  const int np = static_cast<int>(sizeof(passes) / sizeof(passes[0]));
  for (int i = 0; i < np; ++i) {
    if (isCancelled(progress)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    report(progress, static_cast<double>(i) / np, passes[i].msg);
    passes[i].fn(w);
  }
  PrimitiveSheet s;
  s.source = raw.source; s.file = raw.file; s.fileSha256 = raw.fileSha256; s.page = raw.page; s.layout = raw.layout; s.sheet = raw.sheet;
  s.unitToMm = raw.unitToMm; s.raster = raw.raster; s.attrs = raw.attrs; s.nativeDims = raw.nativeDims;
  // deterministic order: by id
  std::sort(w.prims.begin(), w.prims.end(), [](const Primitive& a, const Primitive& b) { return a.id < b.id; });
  std::sort(w.texts.begin(), w.texts.end(), [](const TextRun& a, const TextRun& b) { return a.id < b.id; });
  s.prims = std::move(w.prims);
  s.texts = std::move(w.texts);
  // native dimension block ids may have been merged: remap to survivors
  {
    std::map<int, int> survivor;
    for (auto& p : s.prims) for (int m : p.mergedFrom) survivor[m] = p.id;
    for (auto& t : s.texts) for (int m : t.mergedFrom) survivor[m] = t.id;
    for (auto& d : s.nativeDims) {
      for (auto& v : d.blockPrimitiveIds) { auto it = survivor.find(v); if (it != survivor.end()) v = it->second; }
      for (auto& v : d.blockTextIds) { auto it = survivor.find(v); if (it != survivor.end()) v = it->second; }
      std::sort(d.blockPrimitiveIds.begin(), d.blockPrimitiveIds.end());
      d.blockPrimitiveIds.erase(std::unique(d.blockPrimitiveIds.begin(), d.blockPrimitiveIds.end()), d.blockPrimitiveIds.end());
    }
  }
  StageResult r = StageResult::success();
  r.diagnostics = w.diags;
  std::ostringstream msg, st;
  for (auto& [k, v] : w.stats) { st << (st.tellp() > 0 ? ", " : "") << k << "=" << v; s.attrs["norm." + k] = std::to_string(v); }
  msg << "Normalizasyon: " << raw.prims.size() << " -> " << s.prims.size() << " ilkel, " << raw.texts.size() << " -> " << s.texts.size()
      << " metin" << (st.tellp() > 0 ? " (" + st.str() + ")" : "");
  r.message = msg.str();
  s.diagnostics = raw.diagnostics;
  for (auto& d : w.diags) s.diagnostics.push_back(d);
  out = std::move(s);
  nextId = id;
  report(progress, 1.0, r.message);
  return r;
}

}  // namespace pc::drawing
