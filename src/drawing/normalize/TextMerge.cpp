// Glyph-run merging (SPEC §2.4): per-glyph / per-show-op runs on a common baseline -> words. OWNER WP-1.
#include "normalize/NormInternal.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>

namespace pc::drawing::norm {
using namespace geom;

namespace {
double emSize(const TextRun& t) {
  auto it = t.attrs.find("pdf.fontSizePt");
  if (it != t.attrs.end()) { const double v = std::atof(it->second.c_str()) * kPtToMm; if (v > 0) return v * 0.7; }
  return t.height;
}
double runWidth(const TextRun& t) { return distance(t.quad[0], t.quad[1]); }
}  // namespace

void mergeTextRuns(Work& w) {
  if (!w.opt.mergeGlyphRuns || w.texts.size() < 2) return;
  const std::size_t n = w.texts.size();
  std::vector<int> parent(n);
  for (std::size_t i = 0; i < n; ++i) parent[i] = static_cast<int>(i);
  std::function<int(int)> find = [&](int x) { return parent[x] == x ? x : parent[x] = find(parent[x]); };
  // candidate pairs: same direction, same baseline, small gap (either order)
  for (std::size_t i = 0; i < n; ++i) {
    const TextRun& a = w.texts[i];
    if (a.fromDimBlock || a.asOutlines) continue;
    const Vec2 u = polar({0, 0}, 1, a.rotDeg), nn{-u.y, u.x};
    const double ha = emSize(a);
    for (std::size_t j = i + 1; j < n; ++j) {
      const TextRun& b = w.texts[j];
      if (b.fromDimBlock || b.asOutlines || a.invisible != b.invisible) continue;
      if (std::abs(norm360(a.rotDeg - b.rotDeg + 180) - 180) > 1.0) continue;
      const double hb = emSize(b);
      const double h = std::min(ha, hb);
      if (h <= 0 || std::max(ha, hb) > 1.35 * h) continue;
      const double perp = dot(b.origin - a.origin, nn);
      if (std::abs(perp) > 0.35 * h) continue;
      const double aS = dot(a.origin, u), aE = aS + runWidth(a), bS = dot(b.origin, u), bE = bS + runWidth(b);
      const double gap = bS >= aS ? bS - aE : aS - bE;
      if (gap > w.opt.glyphGapFactor * h) continue;
      if (gap < -0.5 * h) {
        // overprint into a blank slot ("Ø   max." + "11.8"): one run inside the other, container has spaces there
        const bool bInA = bS >= aS - 0.1 * h && bE <= aE + 0.1 * h, aInB = aS >= bS - 0.1 * h && aE <= bE + 0.1 * h;
        const TextRun& cont = bInA ? a : b;
        if (!(bInA || aInB) || cont.utf8.find(' ') == std::string::npos) continue;
      }
      const int ra = find(static_cast<int>(i)), rb = find(static_cast<int>(j));
      if (ra != rb) parent[rb] = ra;
    }
  }
  std::map<int, std::vector<std::size_t>> groups;
  for (std::size_t i = 0; i < n; ++i) groups[find(static_cast<int>(i))].push_back(i);
  std::vector<TextRun> out;
  for (auto& [root, mem] : groups) {
    if (mem.size() == 1) { out.push_back(std::move(w.texts[mem[0]])); continue; }
    const TextRun& r0 = w.texts[mem[0]];
    const Vec2 u = polar({0, 0}, 1, r0.rotDeg), nn{-u.y, u.x};
    std::sort(mem.begin(), mem.end(), [&](std::size_t x, std::size_t y) {
      const double tx = dot(w.texts[x].origin, u), ty = dot(w.texts[y].origin, u);
      return tx < ty || (tx == ty && x < y);
    });
    TextRun m = w.texts[mem[0]];
    m.id = w.nextId++;
    m.mergedFrom.clear(); m.alternatives.clear();
    // explode runs into characters with baseline positions, interleave (overprinted runs), rebuild with gap spaces
    struct Ch { double pos, end; std::string s, alt; std::size_t run; };
    std::vector<Ch> chars;
    std::string glyphs; bool anyAlt = false;
    double hMax = 0, lo = 1e300, hi = -1e300, plo = 1e300, phi = -1e300, hMin = 1e300;
    for (std::size_t k = 0; k < mem.size(); ++k) {
      const TextRun& t = w.texts[mem[k]];
      const double tS = dot(t.origin, u), tW = runWidth(t);
      std::vector<std::string> cs, as;
      auto split = [](const std::string& x) {
        std::vector<std::string> v;
        for (std::size_t i = 0; i < x.size();) {
          const unsigned char c = x[i];
          const std::size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
          v.push_back(x.substr(i, len)); i += len;
        }
        return v;
      };
      cs = split(t.utf8);
      as = t.alternatives.empty() ? cs : split(t.alternatives.front().utf8);
      if (as.size() != cs.size()) as = cs;
      anyAlt |= !t.alternatives.empty();
      std::vector<double> pos;
      auto gp = t.attrs.find("pdf.glyphPos");
      if (gp != t.attrs.end()) {
        const char* p = gp->second.c_str(); char* e = nullptr;
        while (*p) { pos.push_back(std::strtod(p, &e)); if (e == p) break; p = (*e == ',') ? e + 1 : e; }
      }
      if (pos.size() != cs.size()) { pos.clear(); for (std::size_t c = 0; c < cs.size(); ++c) pos.push_back(tW * c / std::max<std::size_t>(1, cs.size())); }
      for (std::size_t c = 0; c < cs.size(); ++c)
        chars.push_back({tS + pos[c], tS + (c + 1 < cs.size() ? pos[c + 1] : tW), cs[c], as[c], k});
      auto g = t.attrs.find("pdf.glyphNames");
      glyphs += (glyphs.empty() ? "" : " | ") + (g == t.attrs.end() ? t.utf8 : g->second);
      lo = std::min(lo, tS); hi = std::max(hi, tS + tW);
      for (auto& q : t.quad) { plo = std::min(plo, dot(q, nn)); phi = std::max(phi, dot(q, nn)); }
      if (!t.attrs.count("norm.symbolTable")) hMax = std::max(hMax, t.height);
      hMin = std::min(hMin, emSize(t));
      m.conf = std::min(m.conf, t.conf);
      m.mergedFrom.push_back(t.id);
    }
    std::stable_sort(chars.begin(), chars.end(), [](const Ch& a, const Ch& b) { return a.pos < b.pos - 1e-6; });
    std::string s, altS;
    const Ch* prev = nullptr;
    for (auto& c : chars) {
      if (c.s == " ") continue;
      if (prev) {
        if (c.s == prev->s && c.pos < prev->pos + 0.3 * (prev->end - prev->pos) && c.run != prev->run) continue;   // overprint
        if (c.pos - prev->end > 0.3 * hMin) { s += ' '; altS += ' '; }
      }
      s += c.s; altS += c.alt;
      prev = &c;
    }
    // trim
    m.utf8 = s;
    if (anyAlt) m.alternatives.push_back({altS, 0.1f});
    const double base = dot(m.origin, nn);
    m.origin = u * lo + nn * base;
    m.height = hMax > 0 ? hMax : m.height;
    m.quad = {u * lo + nn * std::min(plo, base), u * hi + nn * std::min(plo, base), u * hi + nn * std::max(phi, base + m.height),
              u * lo + nn * std::max(phi, base + m.height)};
    if (glyphs.find('|') != std::string::npos && m.attrs.count("pdf.glyphNames")) m.attrs["pdf.glyphNames"] = glyphs;
    m.attrs["norm.mergedRuns"] = std::to_string(mem.size());
    w.stats["textRunsMerged"] += static_cast<int>(mem.size());
    out.push_back(std::move(m));
  }
  w.texts = std::move(out);
}

}  // namespace pc::drawing::norm
