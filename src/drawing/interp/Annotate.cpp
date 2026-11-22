// S4 — line roles (width / dash / layer / colour / context) and dimension assembly
// (dimension line + arrows + extension lines + text -> Dimension bound to geometry), scale from dimensions.
#include "interp/InterpUtil.h"
#include <algorithm>
#include <cmath>
#include <regex>
#include <set>

namespace pc::drawing {
using namespace interp;

namespace {

struct Ctx {
  const DrawingIR& ir;
  const PrimitiveSheet& s;
  std::set<int> border, titleBlock;
  double thick = 0;
  double tol = 0.3;            // sheet mm, arrow tip / endpoint coincidence
};

bool parallel(Vec2 a, Vec2 b, double tolDeg) {
  double c = std::abs(cross(unit(a), unit(b)));
  return c < std::sin(deg2rad(tolDeg));
}
Vec2 lineDir(const Primitive& p) { return unit(p.p1 - p.p0); }

std::set<int> idSet(const std::string& csv) {
  std::set<int> s;
  size_t a = 0;
  while (a < csv.size()) {
    size_t b = csv.find(',', a);
    if (b == std::string::npos) b = csv.size();
    try { s.insert(std::stoi(csv.substr(a, b - a))); } catch (...) {}
    a = b + 1;
  }
  return s;
}

bool pointInPoly(Vec2 q, const std::vector<Vec2>& poly) {
  bool in = false;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    if (((poly[i].y > q.y) != (poly[j].y > q.y)) &&
        (q.x < (poly[j].x - poly[i].x) * (q.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x))
      in = !in;
  }
  return in;
}

const double kStdScales[] = {1, 2, 2.5, 3, 4, 5, 10, 20, 50, 100, 0.5, 0.4, 1.0 / 3, 0.25, 0.2, 0.1, 0.05, 0.02, 0.01};

// arrow tips sitting on an end of line `p` (arrow body pointing along the line, inward)
std::vector<const Primitive*> arrowsOn(const Primitive& p, const std::vector<const Primitive*>& arrows, double tol, Vec2* ends = nullptr) {
  std::vector<const Primitive*> out;
  if (p.kind == PrimKind::Line) {
    Vec2 d = lineDir(p);
    for (auto* a : arrows) {
      Vec2 ad = unit(a->p1 - a->p0);
      if (!parallel(ad, d, 8)) continue;
      if (distance(a->p0, p.p0) < tol && dot(ad, p.p1 - p.p0) > 0) out.push_back(a);
      else if (distance(a->p0, p.p1) < tol && dot(ad, p.p0 - p.p1) > 0) out.push_back(a);
      // CAD exports that shorten the dimension line by the arrow length: the arrow BASE sits on the line end and
      // the tip continues the line outward
      else if (distance(a->p1, p.p0) < tol && distance(a->p0, p.p0) > tol && dot(ad, p.p1 - p.p0) > 0) out.push_back(a);
      else if (distance(a->p1, p.p1) < tol && distance(a->p0, p.p1) > tol && dot(ad, p.p0 - p.p1) > 0) out.push_back(a);
    }
  } else if (p.kind == PrimKind::Arc) {
    auto e = primEndpoints(p);
    for (auto* a : arrows)
      for (auto& q : e) if (distance(a->p0, q) < tol) { out.push_back(a); break; }
  }
  (void)ends;
  return out;
}

}  // namespace

StageResult annotate(DrawingIR& ir, const InterpretOptions& opt, ProgressSink* prog) {
  StageResult res = StageResult::success();
  const Stage st = Stage::Annotate;
  Annotation A;
  Ctx c{ir, ir.norm};
  c.border = idSet(ir.layout.attrs.count("interp.borderPrims") ? ir.layout.attrs.at("interp.borderPrims") : "");
  c.thick = attrNum(ir.layout.attrs, "interp.visibleWidth", 0);
  const Box2 tbox = ir.layout.title.box;
  auto inTitle = [&](const Box2& b) {
    return !isEmpty(tbox) && b.lo.x >= tbox.lo.x - 0.5 && b.hi.x <= tbox.hi.x + 0.5 && b.lo.y >= tbox.lo.y - 0.5 && b.hi.y <= tbox.hi.y + 0.5;
  };
  report(prog, 0.05, "Çizgi rolleri");

  // ---------------------------------------------------------------- 1. intrinsic roles
  std::vector<const Primitive*> arrows, thinCont;
  // Layered drawings without line widths (DXF R12, exploded dimensions): when dimensions live on their own layer,
  // geometry on other layers is never a dimension / extension / leader line.
  bool hasDimLayer = false;
  for (auto& p : c.s.prims) {
    std::string l = foldUpper(p.style.layer);
    if (!p.fromDimBlock && (l.find("DIM") != std::string::npos || l.find("OLCU") != std::string::npos)) { hasDimLayer = true; break; }
  }
  for (auto& p : c.s.prims) {
    LineRole r = LineRole::Unknown;
    float conf = 0.5f;
    std::string lay = foldUpper(p.style.layer + " " + p.style.linetype);
    if (p.fromDimBlock) { r = LineRole::Dimension; conf = 1; }
    else if (c.border.count(p.id)) { r = LineRole::Border; conf = 0.95f; }
    else if (inTitle(primBox(p))) { r = LineRole::TitleBlock; conf = 0.9f; }
    else if (p.kind == PrimKind::Arrowhead) { r = LineRole::Dimension; conf = 0.9f; arrows.push_back(&p); }
    else if (p.kind == PrimKind::HatchRegion) { r = LineRole::Hatch; conf = 0.95f; }
    else if (p.kind == PrimKind::Image) { r = LineRole::Unknown; conf = 0.2f; }
    else if (lay.find("CENTER") != std::string::npos || lay.find("EKSEN") != std::string::npos || lay.find("AXIS") != std::string::npos ||
             lay.find("DASHDOT") != std::string::npos) { r = LineRole::Center; conf = 0.95f; }
    else if (lay.find("HIDDEN") != std::string::npos || lay.find("GIZLI") != std::string::npos || lay.find("DASHED") != std::string::npos) {
      r = LineRole::Hidden; conf = 0.95f;
    } else if (lay.find("HATCH") != std::string::npos || lay.find("TARAMA") != std::string::npos) { r = LineRole::Hatch; conf = 0.9f; }
    else if (lay.find("PHANTOM") != std::string::npos) { r = LineRole::Phantom; conf = 0.9f; }
    else if (isDashed(p)) { r = isCenterDash(p) ? LineRole::Center : LineRole::Hidden; conf = 0.85f; }
    else if (isCurve(p)) {
      bool thickLine = c.thick <= 0 || p.style.width >= 0.8 * c.thick;
      bool dimLayer = lay.find("DIM") != std::string::npos || lay.find("OLCU") != std::string::npos;
      if (thickLine && !dimLayer) { r = LineRole::Visible; conf = c.thick > 0 ? 0.85f : 0.6f; }
      if (!thickLine || dimLayer || (c.thick <= 0 && !hasDimLayer)) thinCont.push_back(&p);
    }
    A.roles[p.id] = r;
    A.roleConf[p.id] = conf;
  }
  // ---------------------------------------------------------------- 2. context: dimension / leader / extension / hatch
  std::map<int, std::vector<const Primitive*>> arrowsOfLine;
  for (auto* p : thinCont) {
    auto a = arrowsOn(*p, arrows, c.tol);
    if (a.empty()) continue;
    arrowsOfLine[p->id] = a;
    A.roles[p->id] = (a.size() >= 2 || p->kind == PrimKind::Arc) ? LineRole::Dimension : LineRole::Leader;
    A.roleConf[p->id] = 0.85f;
  }
  // shoulders of leaders + extension lines
  for (auto* p : thinCont) {
    if (arrowsOfLine.count(p->id) || p->kind != PrimKind::Line) continue;
    LineRole cur = A.roles[p->id];
    if (cur != LineRole::Visible && cur != LineRole::Unknown) continue;
    // extension: an arrow tip of a dimension line lies on this line and the line is perpendicular to it
    bool ext = false;
    for (auto& [lid, as] : arrowsOfLine) {
      const Primitive* dl = findPrim(c.s, lid);
      if (dl->kind != PrimKind::Line || A.roles[lid] != LineRole::Dimension || parallel(lineDir(*dl), lineDir(*p), 80)) continue;
      for (auto* a : as)
        if (distPointSeg(a->p0, p->p0, p->p1) < c.tol) { ext = true; break; }
      if (ext) break;
    }
    if (ext) { A.roles[p->id] = LineRole::Extension; A.roleConf[p->id] = 0.8f; continue; }
    for (auto& [lid, as] : arrowsOfLine) {
      if (A.roles[lid] != LineRole::Leader) continue;
      const Primitive* ll = findPrim(c.s, lid);
      Vec2 tail = distance(as[0]->p0, ll->p0) < distance(as[0]->p0, ll->p1) ? ll->p1 : ll->p0;
      if (distance(tail, p->p0) < c.tol || distance(tail, p->p1) < c.tol) { A.roles[p->id] = LineRole::Leader; A.roleConf[p->id] = 0.8f; break; }
    }
  }
  // hatch lines: thin lines inside a hatch region; remaining thin continuous -> thin visible (thread) inside views
  std::vector<const Primitive*> hatchRegions;
  for (auto& p : c.s.prims) if (p.kind == PrimKind::HatchRegion) hatchRegions.push_back(&p);
  for (auto* p : thinCont) {
    if (A.roles[p->id] != LineRole::Visible && A.roles[p->id] != LineRole::Unknown) continue;
    if (c.thick <= 0 && A.roles[p->id] == LineRole::Visible) continue;   // widths unknown: keep visible
    bool inHatch = false;
    for (auto* h : hatchRegions) if (pointInPoly(primMid(*p), h->pts)) { inHatch = true; break; }
    if (inHatch && p->kind == PrimKind::Line) {
      double ang = std::fmod(std::abs(rad2deg(std::atan2(p->p1.y - p->p0.y, p->p1.x - p->p0.x))), 90.0);
      if (ang > 15 && ang < 75) { A.roles[p->id] = LineRole::Hatch; A.roleConf[p->id] = 0.75f; continue; }
    }
    A.roles[p->id] = LineRole::Unknown;
    A.roleConf[p->id] = 0.3f;
  }
  // user-added primitives with a role
  for (auto& p : c.s.prims) {
    auto it = p.attrs.find("edit.role");
    if (it != p.attrs.end()) { LineRole r; if (fromString(it->second, r)) { A.roles[p.id] = r; A.roleConf[p.id] = kConfUser; } }
  }
  for (auto& p : c.s.prims)
    if (A.roles[p.id] == LineRole::Center && p.kind == PrimKind::Line) A.centerlineIds.push_back(p.id);

  // ---------------------------------------------------------------- 3. dimensions
  report(prog, 0.4, "Ölçüler bağlanıyor");
  auto roleIs = [&](int id, LineRole r) { auto it = A.roles.find(id); return it != A.roles.end() && it->second == r; };
  std::vector<const Primitive*> visible, centerCircles;
  for (auto& p : c.s.prims) {
    if (roleIs(p.id, LineRole::Visible)) visible.push_back(&p);
    if (roleIs(p.id, LineRole::Center) && (p.kind == PrimKind::Circle || p.kind == PrimKind::Arc)) centerCircles.push_back(&p);
  }
  std::set<int> excludedTexts(ir.layout.noteTextIds.begin(), ir.layout.noteTextIds.end());
  for (int id : ir.layout.title.textIds) excludedTexts.insert(id);
  for (auto& v : ir.layout.views) if (v.labelTextId >= 0) excludedTexts.insert(v.labelTextId);
  auto viewOf = [&](Vec2 q) {
    int best = -1;
    double bd = 1e300;
    for (auto& v : ir.layout.views) {
      double dx = std::max({0.0, v.box.lo.x - q.x, q.x - v.box.hi.x}), dy = std::max({0.0, v.box.lo.y - q.y, q.y - v.box.hi.y});
      double d = std::hypot(dx, dy);
      if (d < bd) { bd = d; best = v.id; }
    }
    return best;
  };
  std::set<int> usedLines, consumedTexts;
  std::vector<Dimension> dims;

  // 3a. native DXF dimensions
  for (auto& nd : c.s.nativeDims) {
    Dimension d;
    d.id = ir.newId();
    d.nativeDimId = nd.id;
    std::string txt = nd.textOverride;
    bool measuredText = txt.empty() || txt.find("<>") != std::string::npos;
    double val = nd.measured;
    std::string shown = measuredText ? std::regex_replace(txt.empty() ? std::string("<>") : txt, std::regex("<>"), fmt(val, 4)) : txt;
    if (nd.kind == NativeDimKind::Diameter && shown.find("Ø") == std::string::npos) shown = "Ø" + shown;
    if (nd.kind == NativeDimKind::Radius && shown.rfind("R", 0) != 0) shown = "R" + shown;
    d.text = parseDim(shown);
    for (int t : nd.blockTextIds) consumedTexts.insert(t);
    switch (nd.kind) {
      case NativeDimKind::Linear: case NativeDimKind::Aligned: {
        d.kind = nd.kind == NativeDimKind::Linear ? DimKind::Linear : DimKind::Aligned;
        d.anchors = {nd.defpts[1], nd.defpts[2]};
        d.direction = nd.kind == NativeDimKind::Linear ? Vec2{std::cos(deg2rad(nd.rotDeg)), std::sin(deg2rad(nd.rotDeg))}
                                                       : unit(nd.defpts[2] - nd.defpts[1]);
        d.measuredSheet = std::abs(dot(d.anchors[1] - d.anchors[0], d.direction));
        if (d.text.kind == DimKind::Diameter) d.kind = DimKind::Diameter;
        break;
      }
      case NativeDimKind::Diameter: d.kind = DimKind::Diameter; d.anchors = {nd.defpts[0], nd.defpts[3]};
        d.measuredSheet = distance(nd.defpts[0], nd.defpts[3]); break;
      case NativeDimKind::Radius: d.kind = DimKind::Radius; d.anchors = {nd.defpts[0], nd.defpts[3]};
        d.measuredSheet = distance(nd.defpts[0], nd.defpts[3]); break;
      default: d.kind = DimKind::Angle; d.measuredSheet = nd.measured; d.anchors = {nd.defpts[0], nd.defpts[1]}; break;
    }
    d.conf = nd.conf;
    for (auto* p : visible)
      for (auto& an : d.anchors)
        if (distToPrim(*p, an) < c.tol || ((p->kind == PrimKind::Circle || p->kind == PrimKind::Arc) && distance(p->center, an) < c.tol)) {
          if (std::find(d.geomPrimIds.begin(), d.geomPrimIds.end(), p->id) == d.geomPrimIds.end()) d.geomPrimIds.push_back(p->id);
        }
    d.viewId = viewOf((d.anchors[0] + d.anchors[1]) * 0.5);
    d.attrs["interp.source"] = "native";
    dims.push_back(std::move(d));
  }

  // anchor search along an extension line (from its geometry end, away from the dimension line)
  auto anchorAlongExt = [&](const Primitive& ext, Vec2 tip, Vec2& anchor) -> bool {
    Vec2 g = distance(ext.p0, tip) > distance(ext.p1, tip) ? ext.p0 : ext.p1;
    Vec2 farFromTip = g;
    double t0;
    distPointSeg(tip, ext.p0, ext.p1, &t0);
    Vec2 foot = ext.p0 + (ext.p1 - ext.p0) * t0;
    Vec2 d = unit(farFromTip - foot);
    double bestCen = 1e300, bestGeo = 1e300;
    Vec2 aCen{}, aGeo{};
    auto consider = [&](Vec2 q, bool cen) {
      double along = dot(q - g, d);
      double off = std::abs(cross(d, q - g));
      if (off > c.tol) return;
      if (cen) { if (along > -0.5 && along < 12 && along < bestCen) { bestCen = along; aCen = q; } }
      else if (along > -0.5 && along < 6 && along < bestGeo) { bestGeo = along; aGeo = q; }
    };
    for (auto* p : visible) {
      if (p->kind == PrimKind::Circle || p->kind == PrimKind::Arc) consider(p->center, true);
      for (auto& e : primEndpoints(*p)) consider(e, false);
      for (double t : rayHits(*p, g, d)) consider(g + d * t, false);
    }
    for (auto* p : centerCircles) for (double t : rayHits(*p, g, d)) consider(g + d * t, false);
    if (bestCen < 1e299) { anchor = aCen; return true; }
    if (bestGeo < 1e299) { anchor = aGeo; return true; }
    anchor = g;
    return false;
  };
  auto bindGeometry = [&](Dimension& d) {
    for (int k = 0; k < 2; ++k) {
      Vec2 an = d.anchors[k];
      for (auto* p : visible) {
        bool hit = distToPrim(*p, an) < c.tol ||
                   ((p->kind == PrimKind::Circle || p->kind == PrimKind::Arc) && distance(p->center, an) < c.tol);
        if (hit && std::find(d.geomPrimIds.begin(), d.geomPrimIds.end(), p->id) == d.geomPrimIds.end()) d.geomPrimIds.push_back(p->id);
      }
    }
  };

  // 3b. text-driven assembly
  std::vector<const TextRun*> cands;
  for (auto& t : c.s.texts) {
    if (excludedTexts.count(t.id) || consumedTexts.count(t.id) || t.fromDimBlock || t.invisible) continue;
    if (inTitle(textBox(t))) continue;
    cands.push_back(&t);
  }
  static const std::regex reTol(R"(^\s*([+\-±])\s*(\d+(?:[.,]\d+)?)\s*$)");
  for (auto* t : cands) {
    if (consumedTexts.count(t->id)) continue;
    std::smatch mt;
    if (std::regex_match(t->utf8, mt, reTol)) continue;   // stacked tolerance line: bound by its main text
    DimTextContext ctx;
    ctx.fromOcr = c.s.source == SourceKind::RasterImage || c.s.source == SourceKind::ScannedPdf || c.s.source == SourceKind::Photo;
    DimText dt;
    if (!t->alternatives.empty()) {
      std::vector<TextHypothesis> hyps{{t->utf8, t->conf}};
      hyps.insert(hyps.end(), t->alternatives.begin(), t->alternatives.end());
      int k = rankDimTextHypotheses(hyps, ctx, &dt);
      if (k < 0 || !dt.valid) dt = parseDim(t->utf8, ctx);
    } else {
      dt = parseDim(t->utf8, ctx);
    }
    if (!dt.valid || dt.kind == DimKind::Note || dt.kind == DimKind::Unknown) continue;
    Dimension d;
    d.text = dt;
    d.textId = t->id;
    d.conf = std::min(t->conf, dt.conf > 0 ? dt.conf : 0.8f);
    Box2 tb = textBox(*t);
    Vec2 tc = textCentre(*t);
    Vec2 tdir{std::cos(deg2rad(t->rotDeg)), std::sin(deg2rad(t->rotDeg))};
    // stacked tolerance runs right of the text
    for (auto* o : cands) {
      if (o == t || consumedTexts.count(o->id)) continue;
      std::smatch m;
      if (!std::regex_match(o->utf8, m, reTol)) continue;
      Vec2 oc = textCentre(*o);
      double along = dot(oc - tc, tdir), across = std::abs(cross(tdir, oc - tc));
      double halfW = 0.3 * t->height * t->utf8.size();
      if (along > 0 && along < halfW + 3 * t->height && across < 1.2 * t->height) {
        std::string v = m[2];
        std::replace(v.begin(), v.end(), ',', '.');
        double x = std::stod(v);
        std::string sg = m[1];
        if (sg == "+") d.text.tolPlus = x;
        else if (sg == "-") d.text.tolMinus = -x;
        else { d.text.tolPlus = x; d.text.tolMinus = -x; }
        d.tolTextIds.push_back(o->id);
        consumedTexts.insert(o->id);
      }
    }
    if (d.text.tolPlus && !d.text.tolMinus) d.text.tolMinus = 0.0;
    if (d.text.tolMinus && !d.text.tolPlus) d.text.tolPlus = 0.0;
    // (i) two-arrow dimension line parallel to the text
    const Primitive* best = nullptr;
    double bd = 1e300;
    for (auto& [lid, as] : arrowsOfLine) {
      if (usedLines.count(lid) || A.roles[lid] != LineRole::Dimension) continue;
      const Primitive* dl = findPrim(c.s, lid);
      if (dl->kind == PrimKind::Line) {
        Vec2 ld = lineDir(*dl);
        if (!parallel(ld, tdir, 3)) continue;
        double t0 = dot(dl->p0 - tc, ld), t1 = dot(dl->p1 - tc, ld);
        if (std::min(t0, t1) > 2 || std::max(t0, t1) < -2) continue;
        double off = std::abs(cross(ld, tc - dl->p0));
        if (off > 1.5 * t->height + 2) continue;
        if (off < bd) { bd = off; best = dl; }
      } else if (dl->kind == PrimKind::Arc && dt.kind == DimKind::Angle) {
        double off = std::abs(distance(tc, dl->center) - dl->radius);
        if (off < 3 * t->height && off < bd) { bd = off; best = dl; }
      }
    }
    if (best && best->kind == PrimKind::Line) {
      usedLines.insert(best->id);
      d.dimLineIds.push_back(best->id);
      for (auto* a : arrowsOfLine[best->id]) d.arrowIds.push_back(a->id);
      Vec2 tips[2] = {best->p0, best->p1};
      for (int k = 0; k < 2; ++k)   // shortened dimension line: measure from the arrow tips
        for (auto* a : arrowsOfLine[best->id])
          if (distance(a->p1, tips[k]) < c.tol && distance(a->p0, tips[k]) > c.tol) { tips[k] = a->p0; break; }
      for (int k = 0; k < 2; ++k) {
        d.anchors[k] = tips[k];
        for (auto* e : thinCont) {
          if (A.roles[e->id] != LineRole::Extension || e->kind != PrimKind::Line) continue;
          if (distPointSeg(tips[k], e->p0, e->p1) > c.tol) continue;
          Vec2 an;
          anchorAlongExt(*e, tips[k], an);
          d.anchors[k] = an;
          d.extLineIds.push_back(e->id);
          break;
        }
      }
      d.direction = unit(best->p1 - best->p0);
      if (d.direction.x < -1e-9 || (std::abs(d.direction.x) < 1e-9 && d.direction.y < 0)) d.direction = d.direction * -1.0;
      d.measuredSheet = std::abs(dot(d.anchors[1] - d.anchors[0], d.direction));
      if (dt.kind == DimKind::Diameter) d.kind = DimKind::Diameter;
      else if (dt.kind == DimKind::Radius) d.kind = DimKind::Radius;
      else if (std::abs(d.direction.x) > 0.9999 || std::abs(d.direction.y) > 0.9999) d.kind = DimKind::Linear;
      else d.kind = DimKind::Aligned;
      bindGeometry(d);
    } else if (best && best->kind == PrimKind::Arc) {
      usedLines.insert(best->id);
      d.dimLineIds.push_back(best->id);
      for (auto* a : arrowsOfLine[best->id]) d.arrowIds.push_back(a->id);
      d.kind = DimKind::Angle;
      d.measuredSheet = best->a1 - best->a0;
      auto e = primEndpoints(*best);
      d.anchors = {e[0], e[1]};
      for (auto* p : visible)
        if (p->kind == PrimKind::Line && (distPointSeg(best->center, p->p0, p->p1) < 0.5 ||
                                          std::abs(cross(lineDir(*p), best->center - p->p0)) < 0.5))
          for (auto& q : e)
            if (std::abs(cross(lineDir(*p), q - best->center)) < 0.6 * best->radius * 0.1 + 0.5) { d.geomPrimIds.push_back(p->id); break; }
    } else {
      // (ii) leader: shoulder under / beside the text -> arrowed leader -> geometry at the tip
      const Primitive* leader = nullptr;
      Vec2 tip{};
      double bl = 1e300;
      for (auto& [lid, as] : arrowsOfLine) {
        if (usedLines.count(lid)) continue;
        const Primitive* ll = findPrim(c.s, lid);
        if (ll->kind != PrimKind::Line || as.empty()) continue;
        Vec2 tp = as[0]->p0;
        Vec2 tail = distance(tp, ll->p0) < distance(tp, ll->p1) ? ll->p1 : ll->p0;
        double dd = 1e300;
        // shoulder connected to tail
        for (auto* sh : thinCont) {
          if (sh->kind != PrimKind::Line || A.roles[sh->id] != LineRole::Leader || arrowsOfLine.count(sh->id)) continue;
          if (distance(sh->p0, tail) > c.tol && distance(sh->p1, tail) > c.tol) continue;
          double off = std::abs(tb.lo.y - sh->p0.y);
          bool xov = tb.hi.x >= std::min(sh->p0.x, sh->p1.x) - 1 && tb.lo.x <= std::max(sh->p0.x, sh->p1.x) + 1;
          if (xov && off < 2.5) dd = off;
        }
        if (dd > 1e299) {   // no shoulder: tail close to the text box
          Box2 g = tb;
          double dx = std::max({0.0, g.lo.x - tail.x, tail.x - g.hi.x}), dy = std::max({0.0, g.lo.y - tail.y, tail.y - g.hi.y});
          if (std::hypot(dx, dy) < 2.5) dd = std::hypot(dx, dy);
        }
        if (dd < bl) { bl = dd; leader = ll; tip = tp; }
      }
      if (!leader) { continue; }
      usedLines.insert(leader->id);
      d.leaderIds.push_back(leader->id);
      for (auto* a : arrowsOfLine[leader->id]) d.arrowIds.push_back(a->id);
      const Primitive* g = nullptr;
      double gd = 0.5;
      for (auto* p : visible)
        if ((p->kind == PrimKind::Circle || p->kind == PrimKind::Arc) && distToPrim(*p, tip) < gd) { gd = distToPrim(*p, tip); g = p; }
      if (!g)
        for (auto* p : centerCircles)
          if (distToPrim(*p, tip) < gd) { gd = distToPrim(*p, tip); g = p; d.attrs["interp.pcd"] = "1"; }
      if (!g) {
        d.kind = DimKind::Note;
        d.anchors = {tip, tip};
        d.driving = false;
        res.warn("annot.leaderNoGeometry", "Kılavuz çizgisi ucunda daire/yay yok: \"" + t->utf8 + "\"", {t->id}, (int)st);
      } else {
        d.geomPrimIds.push_back(g->id);
        d.anchors = {g->center, tip};
        if (dt.kind == DimKind::Radius) { d.kind = DimKind::Radius; d.measuredSheet = g->radius; }
        else { d.kind = DimKind::Diameter; d.measuredSheet = 2 * g->radius; }
        d.direction = unit(tip - g->center);
      }
    }
    d.id = ir.newId();
    consumedTexts.insert(t->id);
    d.viewId = viewOf(d.kind == DimKind::Diameter || d.kind == DimKind::Radius ? d.anchors[0] : (d.anchors[0] + d.anchors[1]) * 0.5);
    if (d.geomPrimIds.empty() && d.kind != DimKind::Note) {
      d.conf *= 0.5f;
      res.warn("annot.unbound", "Ölçü geometriye bağlanamadı: \"" + t->utf8 + "\"", {t->id}, (int)st);
    }
    d.driving = !(d.text.reference || d.text.isMax || d.text.isMin) && d.kind != DimKind::Note && d.kind != DimKind::Thread &&
                d.kind != DimKind::Chamfer;
    dims.push_back(std::move(d));
  }

  // ---------------------------------------------------------------- 4. scale from dimensions (SPEC §2.5)
  report(prog, 0.75, "Ölçek doğrulanıyor");
  std::vector<double> ratios;
  for (auto& d : dims) {
    if (!d.text.valid || d.text.isMax || d.text.isMin || d.text.reference || d.measuredSheet <= 1e-6) continue;
    if (d.kind != DimKind::Linear && d.kind != DimKind::Aligned && d.kind != DimKind::Diameter && d.kind != DimKind::Radius) continue;
    double ref = d.text.nominal;
    double mid = d.text.tolMid();
    ratios.push_back(ref / d.measuredSheet);
    if (std::abs(mid - ref) > 1e-9) ratios.back() = 0.5 * (ref + mid) / d.measuredSheet;
  }
  SheetLayout& L = ir.layout;
  L.scaleResiduals.clear();
  if (!ratios.empty()) {
    std::vector<double> r = ratios;
    std::sort(r.begin(), r.end());
    double med = r[r.size() / 2];
    if (r.size() % 2 == 0) med = 0.5 * (r[r.size() / 2 - 1] + r[r.size() / 2]);
    double snapped = med;
    for (double s : kStdScales) if (std::abs(med / s - 1) < 0.015) { snapped = s; break; }
    int inl = 0;
    for (double x : ratios) { L.scaleResiduals.push_back(x / snapped - 1); if (std::abs(x / snapped - 1) < 0.02) ++inl; }
    Scale sc;
    if (snapped >= 1) { sc.num = 1; sc.den = snapped; } else { sc.num = 1 / snapped; sc.den = 1; }
    if (std::abs(sc.num - std::round(sc.num)) < 1e-6) sc.num = std::round(sc.num);
    if (std::abs(sc.den - std::round(sc.den)) < 1e-6) sc.den = std::round(sc.den);
    sc.conf = float(0.5 + 0.45 * double(inl) / ratios.size());
    sc.evidence = "ölçüler: " + std::to_string(ratios.size()) + " ölçü, medyan oran " + fmt(med, 4) + " (" + std::to_string(inl) + " uyumlu)";
    L.scaleFromDims = sc;
    const bool userScale = L.attrs.count("interp.userScale") > 0;
    const Scale& ts = L.title.scale;
    double tf = ts.factor();
    if (!userScale && !ts.notToScale && inl >= 2) {
      bool conflict = ts.conf >= 0.5f && std::abs(sc.factor() / tf - 1) > 0.02;
      bool missing = ts.conf < 0.5f;
      if (conflict)
        res.warn("layout.scaleConflict", "Antet ölçeği " + fmt(ts.num) + ":" + fmt(ts.den) + " ölçülerle çelişiyor; ölçülerden gelen " +
                 fmt(sc.num) + ":" + fmt(sc.den) + " kullanıldı", {}, (int)st);
      if (conflict || missing)
        for (auto& v : L.views)
          if (v.role != ViewRole::Detail && !v.userEdited) v.scale = sc;
    }
  }
  // ---------------------------------------------------------------- 5. part values, residuals
  const double snapTol = 0.05;
  for (auto& d : dims) {
    const ViewRegion* v = findView(L, d.viewId);
    double f = v ? viewScaleFactor(*v) : L.title.scale.factor();
    if (v && v->scale.notToScale) f = 0;
    d.measuredPart = d.kind == DimKind::Angle ? d.measuredSheet : d.measuredSheet * f;
    if (!d.text.valid || f == 0) continue;
    double nom = d.text.nominal, mid = d.text.tolMid();
    double rn = d.measuredPart - nom, rm = d.measuredPart - mid;
    d.residualVsTolMid = std::abs(rm) < std::abs(rn) - 1e-12;
    d.residual = d.residualVsTolMid ? rm : rn;
    double band = 0;
    if (d.text.tolPlus || d.text.tolMinus) band = std::abs(d.text.tolPlus.value_or(0) - d.text.tolMinus.value_or(0));
    band = std::max({band, 0.005 * std::abs(nom), snapTol * std::max(f, 1e-9) * (d.kind == DimKind::Angle ? 0 : 1),
                     d.kind == DimKind::Angle ? 0.5 : 0.0}) * opt.tolMatchFactor;
    d.outOfBand = !d.text.isMax && !d.text.isMin && std::abs(d.residual) > band;
    if (d.outOfBand)
      res.warn("dim.outOfBand", "Ölçü \"" + d.text.raw + "\" geometriyle uyuşmuyor: ölçülen " + fmt(d.measuredPart, 3), {d.id}, (int)st);
  }
  A.dims = std::move(dims);
  for (auto& t : c.s.texts)
    if (!consumedTexts.count(t.id) && !excludedTexts.count(t.id) && !inTitle(textBox(t))) A.unusedTextIds.push_back(t.id);
  ir.annot = std::move(A);
  int nb = 0;
  for (auto& d : ir.annot.dims) nb += !d.geomPrimIds.empty();
  res.message = std::to_string(ir.annot.dims.size()) + " ölçü (" + std::to_string(nb) + " bağlı)";
  report(prog, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
