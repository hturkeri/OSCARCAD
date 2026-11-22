// S3 — sheet layout: border, title block (fields, ÖLÇEK/SCALE, projection), notes, view clusters + labels.
#include "interp/InterpUtil.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <functional>
#include <regex>
#include <set>

namespace pc::drawing {
using namespace interp;

namespace {

struct Kw { const char* folded; const char* key; };
const Kw kKeywords[] = {
    {"OLCEK", "scale"}, {"SCALE", "scale"}, {"MALZEME", "material"}, {"MATERIAL", "material"},
    {"RESIM NO", "drawingNo"}, {"DWG NO", "drawingNo"}, {"DRAWING NO", "drawingNo"}, {"PARCA NO", "drawingNo"},
    {"ADI", "title"}, {"TITLE", "title"}, {"BIRIM", "units"}, {"UNITS", "units"}, {"TOLERANS", "generalTolerance"},
    {"TOLERANCE", "generalTolerance"}, {"PROJEKSIYON", "projection"}, {"PROJECTION", "projection"},
    {"TARIH", "date"}, {"DATE", "date"}, {"CIZEN", "author"}, {"DRAWN", "author"}, {"REV", "revision"},
    {"PAFTA", "sheet"}, {"SHEET", "sheet"}};

bool startsWithKw(const std::string& folded, const Kw& k) {
  std::string kw = k.folded;
  if (folded.compare(0, kw.size(), kw) != 0) return false;
  return folded.size() == kw.size() || !std::isalnum((unsigned char)folded[kw.size()]);
}

ProjectionMethod projectionFromText(const std::string& f) {
  if (f.find("ISO-E") != std::string::npos || f.find("ISO E") != std::string::npos || f.find("FIRST ANGLE") != std::string::npos ||
      f.find("1. ACI") != std::string::npos || f.find("BIRINCI ACI") != std::string::npos)
    return ProjectionMethod::FirstAngle;
  if (f.find("ISO-A") != std::string::npos || f.find("ISO A") != std::string::npos || f.find("THIRD ANGLE") != std::string::npos ||
      f.find("3. ACI") != std::string::npos || f.find("UCUNCU ACI") != std::string::npos)
    return ProjectionMethod::ThirdAngle;
  return ProjectionMethod::Unknown;
}

bool horiz(const Primitive& p, double tolRel = 0.01) {
  return p.kind == PrimKind::Line && std::abs(p.p1.y - p.p0.y) <= tolRel * std::max(1e-9, distance(p.p0, p.p1));
}
bool vert(const Primitive& p, double tolRel = 0.01) {
  return p.kind == PrimKind::Line && std::abs(p.p1.x - p.p0.x) <= tolRel * std::max(1e-9, distance(p.p0, p.p1));
}
bool boxesTouch(const Box2& a, const Box2& b, double g) {
  return a.lo.x - g <= b.hi.x && b.lo.x - g <= a.hi.x && a.lo.y - g <= b.hi.y && b.lo.y - g <= a.hi.y;
}
bool inside(const Box2& outer, const Box2& in, double g) {
  return in.lo.x >= outer.lo.x - g && in.hi.x <= outer.hi.x + g && in.lo.y >= outer.lo.y - g && in.hi.y <= outer.hi.y + g;
}
Box2 grow(Box2 b, double g) { b.lo.x -= g; b.lo.y -= g; b.hi.x += g; b.hi.y += g; return b; }
double boxDist(const Box2& a, const Box2& b) {
  double dx = std::max({0.0, a.lo.x - b.hi.x, b.lo.x - a.hi.x});
  double dy = std::max({0.0, a.lo.y - b.hi.y, b.lo.y - a.hi.y});
  return std::hypot(dx, dy);
}

std::string joinIds(const std::vector<int>& v) {
  std::string s;
  for (int i : v) s += (s.empty() ? "" : ",") + std::to_string(i);
  return s;
}

}  // namespace

// Visible-candidate width of the sheet (largest width class carrying >= 15 % of curve length).
static double dominantThickWidth(const std::vector<const Primitive*>& ps) {
  std::map<long, double> lenByW;
  double total = 0;
  for (auto* p : ps) {
    if (!isCurve(*p) || isDashed(*p)) continue;
    double L = primLength(*p);
    lenByW[std::lround(p->style.width * 1000)] += L;
    total += L;
  }
  double best = 0;
  for (auto& [w, L] : lenByW)
    if (L >= 0.15 * total && w / 1000.0 > best) best = w / 1000.0;
  return best;
}

StageResult analyzeLayout(DrawingIR& ir, const InterpretOptions&, ProgressSink* prog) {
  StageResult res = StageResult::success();
  const Stage st = Stage::Layout;
  SheetLayout L;
  const PrimitiveSheet& s = ir.norm;
  if (s.prims.empty() && s.texts.empty()) return StageResult::failure(Status::InvalidInput, "Normalize edilmiş çizim boş");
  report(prog, 0.05, "Çerçeve aranıyor");

  Box2 sheet = s.sheet;
  if (isEmpty(sheet) || width(sheet) <= 0) {
    sheet = emptyBox();
    for (auto& p : s.prims) { Box2 b = primBox(p); if (!isEmpty(b)) { expand(sheet, b.lo); expand(sheet, b.hi); } }
  }
  // ---------------------------------------------------------------- border
  std::vector<int> borderPrims;
  {
    double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
    for (auto& p : s.prims) {
      if (p.fromDimBlock || p.kind != PrimKind::Line) continue;
      double Lp = distance(p.p0, p.p1);
      if (horiz(p) && Lp > 0.5 * width(sheet)) { ymin = std::min(ymin, p.p0.y); ymax = std::max(ymax, p.p0.y); }
      if (vert(p) && Lp > 0.5 * height(sheet)) { xmin = std::min(xmin, p.p0.x); xmax = std::max(xmax, p.p0.x); }
    }
    if (xmax > xmin && ymax > ymin && (xmax - xmin) > 0.5 * width(sheet) && (ymax - ymin) > 0.5 * height(sheet)) {
      L.border = {{xmin, ymin}, {xmax, ymax}};
      L.borderConf = 0.9f;
      for (auto& p : s.prims) {
        if (p.kind != PrimKind::Line) continue;
        double Lp = distance(p.p0, p.p1);
        bool onH = horiz(p) && (std::abs(p.p0.y - ymin) < 1 || std::abs(p.p0.y - ymax) < 1) && Lp > 0.3 * (xmax - xmin);
        bool onV = vert(p) && (std::abs(p.p0.x - xmin) < 1 || std::abs(p.p0.x - xmax) < 1) && Lp > 0.3 * (ymax - ymin);
        if (onH || onV) borderPrims.push_back(p.id);
      }
    } else {
      L.border = sheet;
      L.borderConf = 0.2f;
      res.warn("layout.noBorder", "Çerçeve bulunamadı; sayfa sınırı kullanıldı", {}, (int)st);
    }
  }
  std::set<int> borderSet(borderPrims.begin(), borderPrims.end());
  L.attrs["interp.borderPrims"] = joinIds(borderPrims);

  // ---------------------------------------------------------------- title block
  report(prog, 0.2, "Antet aranıyor");
  const Box2& B = L.border;
  Box2 region = {{B.lo.x + 0.3 * width(B), B.lo.y - 1}, {B.hi.x + 1, B.lo.y + 0.5 * height(B)}};
  std::vector<std::pair<int, const Kw*>> kwTexts;
  for (auto& t : s.texts) {
    Box2 tb = textBox(t);
    if (!inside(region, tb, 0)) continue;
    std::string f = foldUpper(trim(t.utf8));
    for (auto& k : kKeywords) if (startsWithKw(f, k)) { kwTexts.push_back({t.id, &k}); break; }
  }
  Box2 tbox = emptyBox();
  if (!kwTexts.empty()) {
    for (auto& [id, k] : kwTexts) { Box2 b = textBox(*findText(s, id)); expand(tbox, b.lo); expand(tbox, b.hi); }
    tbox = grow(tbox, 2.0);
    for (int iter = 0; iter < 20; ++iter) {
      bool changed = false;
      for (auto& p : s.prims) {
        if (borderSet.count(p.id) || p.kind != PrimKind::Line || !(horiz(p) || vert(p))) continue;
        Box2 pb = primBox(p);
        if (!inside(region, pb, 0.5) || !boxesTouch(tbox, pb, 0.5)) continue;
        if (inside(tbox, pb, 1e-6)) continue;
        expand(tbox, pb.lo); expand(tbox, pb.hi);
        changed = true;
      }
      if (!changed) break;
    }
    L.title.box = tbox;
    L.title.conf = 0.85f;
  } else {
    L.title.box = emptyBox();
    L.title.conf = 0;
    res.warn("layout.noTitleBlock", "Antet bulunamadı", {}, (int)st);
  }
  // fields
  std::vector<const TextRun*> tbTexts;
  if (!isEmpty(L.title.box))
    for (auto& t : s.texts) if (inside(L.title.box, textBox(t), 0.5)) { tbTexts.push_back(&t); L.title.textIds.push_back(t.id); }
  std::set<int> usedAsValue;
  for (auto& [id, k] : kwTexts) {
    const TextRun* t = findText(s, id);
    std::string f = foldUpper(trim(t->utf8));
    // remainder after keyword in the same run (map back by byte count of the folded keyword: fold keeps 1 char per glyph)
    std::string val;
    {
      std::string raw = trim(t->utf8);
      size_t glyphs = std::string(k->folded).size(), i = 0, g = 0;
      while (i < raw.size() && g < glyphs) { unsigned char c = raw[i]; i += c < 0x80 ? 1 : (c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2); ++g; }
      val = trim(raw.substr(std::min(i, raw.size())));
    }
    if (val.empty()) {   // nearest text to the right in the same row (or below)
      Box2 kb = textBox(*t);
      const TextRun* best = nullptr;
      double bd = 1e300;
      for (auto* o : tbTexts) {
        if (o->id == t->id) continue;
        bool isKw = std::any_of(kwTexts.begin(), kwTexts.end(), [&](auto& kk) { return kk.first == o->id; });
        if (isKw) continue;
        Box2 ob = textBox(*o);
        double cy = (ob.lo.y + ob.hi.y) / 2, ky = (kb.lo.y + kb.hi.y) / 2;
        if (std::abs(cy - ky) < std::max(2.5, t->height) && ob.lo.x >= kb.hi.x - 0.5) {
          double d = ob.lo.x - kb.hi.x;
          if (d < bd) { bd = d; best = o; }
        }
      }
      if (best && bd < 80) { val = trim(best->utf8); usedAsValue.insert(best->id); }
    }
    if (!val.empty() && !L.title.fields.count(k->key)) L.title.fields[k->key] = val;
  }
  // scale
  {
    double n = 0, d = 0;
    bool got = false;
    std::string ev;
    if (L.title.fields.count("scale")) {
      std::string f = foldUpper(L.title.fields["scale"]);
      if (f.find("NTS") != std::string::npos || f.find("OLCEKSIZ") != std::string::npos) {
        L.title.scale.notToScale = true; L.title.scale.conf = 0.8f; L.title.scale.evidence = "antet: ölçeksiz"; got = true;
      } else if (parseScaleText(L.title.fields["scale"], n, d)) { got = true; ev = "antet: ÖLÇEK " + fmt(n) + ":" + fmt(d); }
    }
    if (!got)
      for (auto* t : tbTexts)
        if (parseScaleText(t->utf8, n, d)) { got = true; ev = "antet metni: " + t->utf8; break; }
    if (got && !L.title.scale.notToScale) {
      L.title.scale.num = n; L.title.scale.den = d; L.title.scale.conf = 0.85f; L.title.scale.evidence = ev;
    } else if (!got) {
      L.title.scale = Scale{};
      L.title.scale.conf = 0.1f;
      L.title.scale.evidence = "varsayılan 1:1";
      res.warn("layout.noScale", "Antette ölçek okunamadı; 1:1 varsayıldı", {}, (int)st);
    }
  }
  // projection: text, then symbol (trapezoid + concentric circles)
  for (auto* t : tbTexts) {
    ProjectionMethod pm = projectionFromText(foldUpper(t->utf8));
    if (pm != ProjectionMethod::Unknown) { L.title.projection = pm; L.title.fields["projection"] = trim(t->utf8); break; }
  }
  if (L.title.projection == ProjectionMethod::Unknown && !isEmpty(L.title.box)) {
    std::vector<const Primitive*> circles, lines;
    for (auto& p : s.prims) {
      Box2 pb = primBox(p);
      if (!inside(L.title.box, pb, 0.5)) continue;
      if (p.kind == PrimKind::Circle && p.radius < 10) circles.push_back(&p);
      if (p.kind == PrimKind::Line && vert(p) && distance(p.p0, p.p1) < 20) lines.push_back(&p);
    }
    for (size_t i = 0; i < circles.size() && L.title.projection == ProjectionMethod::Unknown; ++i)
      for (size_t j = 0; j < circles.size(); ++j) {
        if (i == j || distance(circles[i]->center, circles[j]->center) > 0.2 || circles[i]->radius <= circles[j]->radius * 1.15) continue;
        const Primitive* big = circles[i];
        // two vertical lines of different length beside the circles, at the same height
        std::vector<const Primitive*> cand;
        for (auto* l : lines) {
          double cy = (l->p0.y + l->p1.y) / 2;
          double dx = std::abs(l->p0.x - big->center.x);
          if (std::abs(cy - big->center.y) < big->radius && dx > big->radius && dx < 6 * big->radius) cand.push_back(l);
        }
        if (cand.size() < 2) continue;
        std::sort(cand.begin(), cand.end(), [](auto* a, auto* b) { return a->p0.x < b->p0.x; });
        const Primitive* a = cand.front();
        const Primitive* b = cand.back();
        double la = distance(a->p0, a->p1), lb = distance(b->p0, b->p1);
        bool left = a->p0.x < big->center.x;
        if (std::abs(la - lb) < 0.1) continue;
        // ISO E: trapezoid LEFT of the circles, small side towards them
        if (left && b->p0.x < big->center.x) L.title.projection = ProjectionMethod::FirstAngle;
        else if (!left) L.title.projection = ProjectionMethod::ThirdAngle;
        if (L.title.projection != ProjectionMethod::Unknown) { L.title.projectionSymbolPrim = big->id; L.title.fields["projection"] = "sembol"; }
        break;
      }
  }

  // ---------------------------------------------------------------- view clusters
  report(prog, 0.5, "Görünüşler kümeleniyor");
  std::vector<const Primitive*> cand;
  std::vector<const Primitive*> nonFrame;
  for (auto& p : s.prims) {
    if (p.fromDimBlock || borderSet.count(p.id)) continue;
    Box2 pb = primBox(p);
    if (isEmpty(pb)) continue;
    if (!isEmpty(L.title.box) && inside(L.title.box, pb, 0.5)) continue;
    nonFrame.push_back(&p);
  }
  double thick = dominantThickWidth(nonFrame);
  bool widthsKnown = thick > 0;
  for (auto* p : nonFrame) {
    if (p->kind == PrimKind::HatchRegion) { cand.push_back(p); continue; }
    if (!isCurve(*p)) continue;
    if (isDashed(*p)) { cand.push_back(p); continue; }
    if (!widthsKnown || p->style.width >= 0.8 * thick) cand.push_back(p);
  }
  L.attrs["interp.visibleWidth"] = fmt(thick);
  // union-find on bbox proximity
  const double gap = std::max(2.0, 0.006 * std::hypot(width(B), height(B)));
  std::vector<int> par(cand.size());
  std::iota(par.begin(), par.end(), 0);
  std::function<int(int)> fnd = [&](int x) { return par[x] == x ? x : par[x] = fnd(par[x]); };
  std::vector<Box2> boxes;
  for (auto* p : cand) boxes.push_back(primBox(*p));
  for (size_t i = 0; i < cand.size(); ++i)
    for (size_t j = i + 1; j < cand.size(); ++j)
      if (boxesTouch(boxes[i], boxes[j], gap)) par[fnd((int)i)] = fnd((int)j);
  std::map<int, std::vector<int>> clusters;
  for (size_t i = 0; i < cand.size(); ++i) clusters[fnd((int)i)].push_back((int)i);
  std::vector<std::pair<Box2, std::vector<int>>> views;
  for (auto& [root, mem] : clusters) {
    bool hasSolid = false;
    Box2 b = emptyBox();
    for (int i : mem) {
      expand(b, boxes[i].lo); expand(b, boxes[i].hi);
      if (!isDashed(*cand[i]) && cand[i]->kind != PrimKind::HatchRegion) hasSolid = true;
    }
    if (!hasSolid || (width(b) < 1 && height(b) < 1)) continue;
    std::vector<int> ids;
    for (int i : mem) ids.push_back(cand[i]->id);
    views.push_back({b, ids});
  }
  // clusters lying inside / overlapping another cluster's box belong to it (holes inside a plate outline)
  for (bool merged = true; merged;) {
    merged = false;
    for (size_t i = 0; i < views.size() && !merged; ++i)
      for (size_t j = 0; j < views.size() && !merged; ++j) {
        if (i == j || !boxesTouch(views[i].first, views[j].first, 0)) continue;
        expand(views[i].first, views[j].first.lo);
        expand(views[i].first, views[j].first.hi);
        views[i].second.insert(views[i].second.end(), views[j].second.begin(), views[j].second.end());
        views.erase(views.begin() + j);
        merged = true;
      }
  }
  std::sort(views.begin(), views.end(), [](auto& a, auto& b) {
    return a.first.lo.x != b.first.lo.x ? a.first.lo.x < b.first.lo.x : a.first.lo.y < b.first.lo.y;
  });
  // view membership: cluster prims + every other non-frame prim whose bbox lies inside the (slightly grown) box
  for (auto& [box, ids] : views) {
    ViewRegion v;
    v.id = ir.newId();
    v.box = box;
    std::set<int> mem(ids.begin(), ids.end());
    for (auto* p : nonFrame)
      if (!mem.count(p->id) && inside(grow(box, 1.0), primBox(*p), 0)) mem.insert(p->id);
    v.primIds.assign(mem.begin(), mem.end());
    for (auto& t : s.texts)
      if (inside(grow(box, 1.0), textBox(t), 0)) v.textIds.push_back(t.id);
    v.scale = L.title.scale;
    L.views.push_back(v);
  }
  if (L.views.empty()) res.warn("layout.noViews", "Görünüş bulunamadı", {}, (int)st);

  // labels ("A-A", "KESİT A-A", "GÖRÜNÜŞ B", "DETAY C (5:1)") above/below a view
  static const std::regex reSec(R"(^(?:KESIT|SECTION)?\s*([A-Z])\s*-\s*([A-Z])$)");
  static const std::regex reDet(R"(^(?:DETAY|DETAIL)\s*([A-Z])\s*(?:\((.*)\))?$)");
  static const std::regex reGor(R"(^(?:GORUNUS|VIEW)\s*([A-Z])$)");
  std::set<int> labelTexts;
  for (auto& t : s.texts) {
    if (!isEmpty(L.title.box) && inside(L.title.box, textBox(t), 0.5)) continue;
    std::string f = foldUpper(trim(t.utf8));
    std::smatch m;
    int kind = 0;
    if (std::regex_match(f, m, reSec) && m[1] == m[2]) kind = 1;
    else if (std::regex_match(f, m, reDet)) kind = 2;
    else if (std::regex_match(f, m, reGor)) kind = 3;
    if (!kind) continue;
    ViewRegion* best = nullptr;
    double bd = 1e300;
    Box2 tb = textBox(t);
    for (auto& v : L.views) {
      double d = boxDist(v.box, tb);
      bool horizontallyOver = tb.hi.x >= v.box.lo.x - 10 && tb.lo.x <= v.box.hi.x + 10;
      if (horizontallyOver && d < bd && d < 30) { bd = d; best = &v; }
    }
    if (!best) continue;
    best->label = trim(t.utf8);
    best->labelTextId = t.id;
    labelTexts.insert(t.id);
    if (kind == 1) { best->role = ViewRole::Section; best->roleConf = 0.8f; }
    if (kind == 2) {
      best->role = ViewRole::Detail; best->roleConf = 0.8f;
      double n, d;
      if (m[2].matched && parseScaleText(m[2], n, d)) { best->scale.num = n; best->scale.den = d; best->scale.evidence = "görünüş etiketi"; best->scale.conf = 0.8f; }
    }
  }
  // hatch -> section
  for (auto& v : L.views) {
    if (v.role != ViewRole::Unknown) continue;
    for (int id : v.primIds) {
      const Primitive* p = findPrim(s, id);
      if (p && p->kind == PrimKind::HatchRegion) { v.role = ViewRole::Section; v.roleConf = 0.6f; break; }
    }
  }
  // ---------------------------------------------------------------- notes (texts far from every view and outside the title block)
  std::vector<const TextRun*> noteT;
  for (auto& t : s.texts) {
    if (labelTexts.count(t.id) || t.fromDimBlock) continue;
    Box2 tb = textBox(t);
    if (!isEmpty(L.title.box) && inside(L.title.box, tb, 0.5)) continue;
    double dmin = 1e300;
    for (auto& v : L.views) dmin = std::min(dmin, boxDist(v.box, tb));
    if (dmin > 30 || L.views.empty()) noteT.push_back(&t);
  }
  std::vector<Box2> blocks;
  for (auto* t : noteT) {
    Box2 tb = textBox(*t);
    L.noteTextIds.push_back(t->id);
    bool merged = false;
    for (auto& b : blocks)
      if (boxesTouch(b, tb, 2 * t->height)) { expand(b, tb.lo); expand(b, tb.hi); merged = true; break; }
    if (!merged) blocks.push_back(tb);
    static const std::regex reT(R"((?:^|\s)(?:T|S)\s*=\s*(\d+(?:[.,]\d+)?))");
    static const std::regex reK(R"((?:KALINLIK|THICKNESS|THK)\s*:?\s*(\d+(?:[.,]\d+)?))");
    std::smatch m;
    std::string f = foldUpper(t->utf8);
    if (std::regex_search(f, m, reT) || std::regex_search(f, m, reK)) {
      std::string v = m[1];
      std::replace(v.begin(), v.end(), ',', '.');
      L.attrs["interp.thicknessNote"] = v;
      L.attrs["interp.thicknessNoteText"] = std::to_string(t->id);
    }
  }
  L.noteBlocks = blocks;

  ir.layout = std::move(L);
  res.message = std::to_string(ir.layout.views.size()) + " görünüş, ölçek " + fmt(ir.layout.title.scale.num) + ":" +
                fmt(ir.layout.title.scale.den);
  report(prog, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
