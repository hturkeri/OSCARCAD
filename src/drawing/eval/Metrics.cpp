// Evaluation metrics (SPEC §6.2) against corpus ground truth. OWNER WP-1.
// Metric names (Metrics::values):
//   prim.recall / prim.precision      all geometric GT prims vs all IR prims (Hausdorff < tol, one-to-one greedy)
//   prim.coverage                     fraction of GT sample points lying on some IR prim (tolerant to splitting)
//   geom.recall / geom.precision      Visible edges only (precision needs S4 roles; without roles = prim.precision)
//   geom.endpointRms                  RMS endpoint error of matched lines/arcs (mm)
//   arc.recall, arc.centerErrMax, arc.radiusErrMax, arc.radiusErrRms   GT arcs/circles vs IR Arc/Circle
//   text.exact, text.cer              all GT texts vs nearest IR text;  dim.textExact, dim.textCer: dimension texts
//   dim.valueAcc, dim.assocAcc        need S4 dims;  view.roleAcc, scale.exact  need S3
//   sketch.dof0, sketch.paramErr, solid.volErr, auto.success, edits.toSuccess
//   tol (mm) and count.* are informational.
#include "pc/drawing/Eval.h"
#include "pc/drawing/GroundTruth.h"
#include "normalize/GeomUtil.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace pc::drawing {
namespace {
using namespace geom;

bool isGeomKind(PrimKind k) {
  return k == PrimKind::Line || k == PrimKind::Arc || k == PrimKind::Circle || k == PrimKind::Polyline ||
         k == PrimKind::Ellipse || k == PrimKind::Bezier || k == PrimKind::Arrowhead;
}

std::u32string toU32(const std::string& s) {
  std::u32string o;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = s[i];
    char32_t cp; int n;
    if (c < 0x80) { cp = c; n = 1; } else if ((c >> 5) == 6) { cp = c & 0x1f; n = 2; }
    else if ((c >> 4) == 14) { cp = c & 0x0f; n = 3; } else { cp = c & 0x07; n = 4; }
    for (int k = 1; k < n && i + k < s.size(); ++k) cp = (cp << 6) | (s[i + k] & 0x3f);
    o.push_back(cp); i += n;
  }
  return o;
}
std::string squash(const std::string& s) {   // collapse whitespace, trim
  std::string o; bool sp = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { sp = !o.empty(); continue; }
    if (sp) o.push_back(' ');
    sp = false; o.push_back(c);
  }
  return o;
}
std::size_t levenshtein(const std::u32string& a, const std::u32string& b) {
  std::vector<std::size_t> prev(b.size() + 1), cur(b.size() + 1);
  for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = j;
  for (std::size_t i = 1; i <= a.size(); ++i) {
    cur[0] = i;
    for (std::size_t j = 1; j <= b.size(); ++j)
      cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
    std::swap(prev, cur);
  }
  return prev[b.size()];
}

bool boxesNear(const Box2& a, const Box2& b, double tol) {
  return !(a.hi.x + tol < b.lo.x || b.hi.x + tol < a.lo.x || a.hi.y + tol < b.lo.y || b.hi.y + tol < a.lo.y);
}

struct Matcher {
  // Greedy one-to-one matching gt -> ir by smallest Hausdorff < tol. Returns gt id -> ir id.
  static std::map<int, int> run(const std::vector<const Primitive*>& gt, const std::vector<const Primitive*>& ir, double tol,
                                std::map<int, double>* dist = nullptr) {
    const double cell = 10.0;
    std::map<std::pair<int, int>, std::vector<std::size_t>> grid;
    std::vector<Box2> irBox(ir.size());
    for (std::size_t i = 0; i < ir.size(); ++i) {
      irBox[i] = primBox(*ir[i]);
      for (int gx = static_cast<int>(std::floor(irBox[i].lo.x / cell)); gx <= static_cast<int>(std::floor(irBox[i].hi.x / cell)); ++gx)
        for (int gy = static_cast<int>(std::floor(irBox[i].lo.y / cell)); gy <= static_cast<int>(std::floor(irBox[i].hi.y / cell)); ++gy)
          grid[{gx, gy}].push_back(i);
    }
    struct Cand { double h; int g; std::size_t i; };
    std::vector<Cand> cands;
    for (std::size_t g = 0; g < gt.size(); ++g) {
      const Box2 gb = primBox(*gt[g]);
      std::set<std::size_t> seen;
      for (int gx = static_cast<int>(std::floor((gb.lo.x - tol) / cell)); gx <= static_cast<int>(std::floor((gb.hi.x + tol) / cell)); ++gx)
        for (int gy = static_cast<int>(std::floor((gb.lo.y - tol) / cell)); gy <= static_cast<int>(std::floor((gb.hi.y + tol) / cell)); ++gy) {
          auto it = grid.find({gx, gy});
          if (it == grid.end()) continue;
          for (std::size_t i : it->second) {
            if (!seen.insert(i).second) continue;
            if ((gt[g]->kind == PrimKind::Arrowhead) != (ir[i]->kind == PrimKind::Arrowhead)) continue;
            // quick reject: bounding boxes must agree within tol
            const Box2& ib = irBox[i];
            if (std::abs(ib.lo.x - gb.lo.x) > tol || std::abs(ib.lo.y - gb.lo.y) > tol || std::abs(ib.hi.x - gb.hi.x) > tol ||
                std::abs(ib.hi.y - gb.hi.y) > tol) continue;
            const double h = hausdorff(*gt[g], *ir[i], std::max(0.05, tol));
            if (h < tol) cands.push_back({h, static_cast<int>(g), i});
          }
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.h < b.h || (a.h == b.h && a.g < b.g); });
    std::map<int, int> m;
    std::set<std::size_t> usedIr; std::set<int> usedG;
    for (auto& c : cands) {
      if (usedG.count(c.g) || usedIr.count(c.i)) continue;
      usedG.insert(c.g); usedIr.insert(c.i);
      m[gt[c.g]->id] = ir[c.i]->id;
      if (dist) (*dist)[gt[c.g]->id] = c.h;
    }
    return m;
  }
};

const TextRun* nearestText(const PrimitiveSheet& s, const TextRun& g, double maxDist) {
  const TextRun* best = nullptr; double bd = maxDist;
  const Vec2 gc = (g.quad[0] + g.quad[2]) * 0.5;
  const bool gq = distance(g.quad[0], g.quad[2]) > 0;
  for (auto& t : s.texts) {
    if (t.invisible) continue;
    double rd = std::abs(norm360(t.rotDeg - g.rotDeg + 180) - 180);
    if (rd > 5 && std::abs(rd - 180) > 5) continue;
    const Vec2 tc = (t.quad[0] + t.quad[2]) * 0.5;
    const double d = (gq && distance(t.quad[0], t.quad[2]) > 0) ? std::min(distance(tc, gc), distance(t.origin, g.origin))
                                                               : distance(t.origin, g.origin);
    if (d < bd) { bd = d; best = &t; }
  }
  return best;
}

double optEq(const std::optional<double>& a, const std::optional<double>& b) {
  return std::abs((a ? *a : 0.0) - (b ? *b : 0.0)) < 1e-6;
}
}  // namespace

Metrics evaluate(const DrawingIR& ir, const GroundTruth& gt) {
  Metrics m;
  auto& V = m.values;
  const PrimitiveSheet& S = ir.norm.prims.empty() && ir.norm.texts.empty() ? ir.raw : ir.norm;
  const bool raster = S.source == SourceKind::RasterImage || S.source == SourceKind::ScannedPdf || S.source == SourceKind::Photo;
  const double tol = raster ? 0.5 : 0.05;
  V["tol"] = tol;

  auto roleOfGt = [&](int id) { auto it = gt.roles.find(id); return it == gt.roles.end() ? LineRole::Unknown : it->second; };
  std::vector<const Primitive*> gAll, gVis, iAll, iVis;
  for (auto& p : gt.sheet.prims) if (isGeomKind(p.kind)) { gAll.push_back(&p); if (roleOfGt(p.id) == LineRole::Visible) gVis.push_back(&p); }
  const bool haveRoles = !ir.annot.roles.empty();
  for (auto& p : S.prims) if (isGeomKind(p.kind)) {
    iAll.push_back(&p);
    if (haveRoles) { auto it = ir.annot.roles.find(p.id); if (it != ir.annot.roles.end() && it->second == LineRole::Visible) iVis.push_back(&p); }
  }
  V["count.gtPrims"] = static_cast<double>(gAll.size());
  V["count.irPrims"] = static_cast<double>(iAll.size());

  std::map<int, double> dist;
  const auto match = Matcher::run(gAll, iAll, tol, &dist);
  V["prim.recall"] = gAll.empty() ? 1.0 : static_cast<double>(match.size()) / gAll.size();
  V["prim.precision"] = iAll.empty() ? 0.0 : static_cast<double>(match.size()) / iAll.size();
  for (auto* g : gAll)
    if (!match.count(g->id) && m.notes.size() < 200) {
      std::ostringstream o; o << "prim gt#" << g->id << " " << toString(g->kind) << " [" << toString(roleOfGt(g->id)) << "] eşleşmedi";
      const Box2 b = primBox(*g); o << " @(" << b.lo.x << "," << b.lo.y << ")-(" << b.hi.x << "," << b.hi.y << ")";
      m.notes.push_back(o.str());
    }

  // coverage: sample GT, distance to any IR prim
  {
    std::size_t tot = 0, hit = 0;
    for (auto* g : gAll) {
      const Box2 gb = primBox(*g);
      std::vector<const Primitive*> near;
      for (auto* p : iAll) if (boxesNear(gb, primBox(*p), tol)) near.push_back(p);
      for (auto& q : samplePrim(*g, 0.5)) {
        ++tot;
        for (auto* p : near) if (distPointPrim(q, *p) < tol) { ++hit; break; }
      }
    }
    V["prim.coverage"] = tot ? static_cast<double>(hit) / tot : 1.0;
  }

  // Visible edges
  std::size_t visMatched = 0;
  for (auto* g : gVis) if (match.count(g->id)) ++visMatched;
  V["geom.recall"] = gVis.empty() ? 1.0 : static_cast<double>(visMatched) / gVis.size();
  if (haveRoles) {
    const auto mv = Matcher::run(gVis, iVis, tol);
    V["geom.precision"] = iVis.empty() ? 0.0 : static_cast<double>(mv.size()) / iVis.size();
  } else {
    V["geom.precision"] = V["prim.precision"];
  }
  {
    double se = 0; std::size_t n = 0;
    for (auto& [gid, iid] : match) {
      const Primitive* g = findPrim(gt.sheet, gid); const Primitive* p = findPrim(S, iid);
      if (!g || !p) continue;
      if (g->kind == PrimKind::Line && p->kind == PrimKind::Line) {
        const double d1 = distance(g->p0, p->p0) + distance(g->p1, p->p1), d2 = distance(g->p0, p->p1) + distance(g->p1, p->p0);
        const bool sw = d2 < d1;
        const double e0 = distance(g->p0, sw ? p->p1 : p->p0), e1 = distance(g->p1, sw ? p->p0 : p->p1);
        se += e0 * e0 + e1 * e1; n += 2;
      }
    }
    V["geom.endpointRms"] = n ? std::sqrt(se / n) : 0.0;
  }

  // arcs / circles
  {
    std::vector<const Primitive*> gA, iA;
    for (auto* g : gAll) if (g->kind == PrimKind::Arc || g->kind == PrimKind::Circle) gA.push_back(g);
    for (auto* p : iAll) if (p->kind == PrimKind::Arc || p->kind == PrimKind::Circle) iA.push_back(p);
    const auto ma = Matcher::run(gA, iA, tol);
    double cMax = 0, rMax = 0, rSe = 0;
    for (auto& [gid, iid] : ma) {
      const Primitive* g = findPrim(gt.sheet, gid); const Primitive* p = findPrim(S, iid);
      const double ce = distance(g->center, p->center), re = std::abs(g->radius - p->radius);
      cMax = std::max(cMax, ce); rMax = std::max(rMax, re); rSe += re * re;
    }
    V["count.gtArcs"] = static_cast<double>(gA.size());
    V["arc.recall"] = gA.empty() ? 1.0 : static_cast<double>(ma.size()) / gA.size();
    V["arc.centerErrMax"] = cMax; V["arc.radiusErrMax"] = rMax;
    V["arc.radiusErrRms"] = ma.empty() ? 0.0 : std::sqrt(rSe / ma.size());
  }

  // texts
  {
    std::size_t n = 0, exact = 0, chars = 0, edits = 0, dn = 0, dexact = 0, dchars = 0, dedits = 0;
    std::set<int> dimTextIds;
    for (auto& d : gt.dims) { if (d.textId >= 0) dimTextIds.insert(d.textId); for (int t : d.tolTextIds) dimTextIds.insert(t); }
    for (auto& g : gt.sheet.texts) {
      if (g.utf8.empty()) continue;
      const TextRun* t = nearestText(S, g, std::max(3.0, 2.0 * g.height) + (raster ? 2.0 : 0.0));
      const std::u32string gu = toU32(squash(g.utf8)), tu = toU32(t ? squash(t->utf8) : std::string());
      const std::size_t e = std::min(levenshtein(gu, tu), gu.size());
      const bool ok = gu == tu;
      ++n; exact += ok; chars += gu.size(); edits += e;
      if (dimTextIds.count(g.id)) { ++dn; dexact += ok; dchars += gu.size(); dedits += e; }
      if (!ok && m.notes.size() < 400)
        m.notes.push_back("text gt#" + std::to_string(g.id) + " '" + g.utf8 + "' -> '" + (t ? t->utf8 : std::string("<yok>")) + "'");
    }
    V["count.gtTexts"] = static_cast<double>(n);
    V["text.exact"] = n ? static_cast<double>(exact) / n : 1.0;
    V["text.cer"] = chars ? static_cast<double>(edits) / chars : 0.0;
    V["dim.textExact"] = dn ? static_cast<double>(dexact) / dn : 1.0;
    V["dim.textCer"] = dchars ? static_cast<double>(dedits) / dchars : 0.0;
  }

  // dimensions (S4)
  if (!ir.annot.dims.empty() && !gt.dims.empty()) {
    std::size_t val = 0, assoc = 0, n = 0;
    std::set<int> usedDims;
    for (auto& gd : gt.dims) {
      ++n;
      const TextRun* gtText = gd.textId >= 0 ? findText(gt.sheet, gd.textId) : nullptr;
      const Dimension* best = nullptr; double bd = 1e300;
      for (auto& d : ir.annot.dims) {
        if (usedDims.count(d.id)) continue;
        double dd = 1e300;
        const TextRun* t = d.textId >= 0 ? findText(S, d.textId) : nullptr;
        if (gtText && t) dd = distance(gtText->origin, t->origin);
        else if (std::abs(d.text.nominal - gd.nominal) < 1e-6) dd = 1e6;
        if (dd < bd) { bd = dd; best = &d; }
      }
      if (!best || (gtText && bd > 10 && bd < 1e6) || bd >= 1e300) continue;
      usedDims.insert(best->id);
      const bool vOk = std::abs(best->text.nominal - gd.nominal) < 1e-6 && optEq(best->text.tolPlus, gd.tolPlus) &&
                       optEq(best->text.tolMinus, gd.tolMinus);
      val += vOk;
      bool aOk = !gd.geomPrimIds.empty();
      for (int gid : gd.geomPrimIds) {
        auto it = match.find(gid);
        if (it == match.end() || std::find(best->geomPrimIds.begin(), best->geomPrimIds.end(), it->second) == best->geomPrimIds.end()) {
          aOk = false; break;
        }
      }
      assoc += aOk;
    }
    V["dim.valueAcc"] = n ? static_cast<double>(val) / n : 0.0;
    V["dim.assocAcc"] = n ? static_cast<double>(assoc) / n : 0.0;
  }

  // views / scale (S3)
  if (!ir.layout.views.empty() && !gt.views.empty()) {
    std::size_t ok = 0;
    for (auto& gv : gt.views) {
      double bestIoU = 0; const ViewRegion* bv = nullptr;
      for (auto& v : ir.layout.views) {
        const double ix = std::max(0.0, std::min(gv.box.hi.x, v.box.hi.x) - std::max(gv.box.lo.x, v.box.lo.x));
        const double iy = std::max(0.0, std::min(gv.box.hi.y, v.box.hi.y) - std::max(gv.box.lo.y, v.box.lo.y));
        const double inter = ix * iy, uni = width(gv.box) * height(gv.box) + width(v.box) * height(v.box) - inter;
        const double iou = uni > 0 ? inter / uni : 0;
        if (iou > bestIoU) { bestIoU = iou; bv = &v; }
      }
      ok += (bv && bestIoU > 0.5 && bv->role == gv.role);
    }
    V["view.roleAcc"] = static_cast<double>(ok) / gt.views.size();
  }
  if (ir.lastStage >= static_cast<int>(Stage::Layout)) {
    const Scale& s = ir.layout.title.scale.conf > 0 ? ir.layout.title.scale : ir.layout.scaleFromDims;
    V["scale.exact"] = (std::abs(s.factor() - gt.scale.factor()) < 1e-6 && s.notToScale == gt.scale.notToScale) ? 1.0 : 0.0;
  }

  // sketches / params / solid
  if (!ir.sketches.empty()) {
    std::size_t d0 = 0;
    std::map<std::string, double> vals;
    for (auto& s : ir.sketches) {
      d0 += s.dof == 0;
      for (auto& p : s.params) vals.emplace(p.param.name, p.nominalValue);
    }
    V["sketch.dof0"] = static_cast<double>(d0) / ir.sketches.size();
    double err = 0; std::size_t n = 0;
    for (auto& p : gt.params) {
      auto it = vals.find(p.name);
      if (it == vals.end()) continue;
      err += std::abs(it->second - p.value) / std::max(1.0, std::abs(p.value)); ++n;
    }
    V["sketch.paramErr"] = n ? err / n : 1.0;
    V["sketch.paramFound"] = gt.params.empty() ? 1.0 : static_cast<double>(n) / gt.params.size();
  }
  if (!ir.synthesis.empty()) {
    const SynthesisCandidate* c = &ir.synthesis.front();
    for (auto& s : ir.synthesis) if (s.id == ir.chosenSynthesis) c = &s;
    const double volErr = (gt.volume > 0 && c->volume > 0) ? std::abs(c->volume - gt.volume) / gt.volume : 1.0;
    V["solid.volErr"] = volErr;
    bool paramsOk = true;
    std::map<std::string, double> vals;
    for (auto& s : ir.sketches) for (auto& p : s.params) vals.emplace(p.param.name, p.nominalValue);
    for (auto& p : gt.params) {
      if (p.tag != ParamTag::FromDimension) continue;
      auto it = vals.find(p.name);
      if (it == vals.end() || std::abs(it->second - p.value) > 1e-6 * std::max(1.0, std::abs(p.value))) { paramsOk = false; break; }
    }
    V["auto.success"] = (volErr < 0.005 && paramsOk && ir.edits.empty()) ? 1.0 : 0.0;
  }
  V["edits.toSuccess"] = static_cast<double>(ir.edits.size());
  return m;
}

}  // namespace pc::drawing
