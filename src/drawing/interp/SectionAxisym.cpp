// R2 — axisymmetric section views: hatch families -> parts, centreline -> half profiles, dimensions -> constraints.
// See SectionAxisym.h and docs/drawing_import/R2_SECTION_AXISYM.md.
#include "interp/SectionAxisym.h"
#include "interp/Arrangement.h"
#include "interp/InterpUtil.h"
#include "interp/Transverse.h"   // integration: cross-view diameters / slot bands (R3)
#include "pc/SketchSolver.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <regex>
#include <set>
#include <cstdlib>
#include <iostream>

namespace pc::drawing::r2 {
using namespace interp;

namespace {

struct LineRec { int id = -1; Vec2 a, b; double w = 0; bool dashed = false; bool fromRaw = false; };

double ang180(Vec2 d) { double a = rad2deg(std::atan2(d.y, d.x)); a = std::fmod(a + 360.0, 180.0); return a >= 180 ? a - 180 : a; }
double angDiff180(double a, double b) { double d = std::fmod(std::abs(a - b), 180.0); return std::min(d, 180 - d); }

double distToPoly(Vec2 q, const std::vector<Vec2>& poly) {
  double best = 1e300;
  for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) best = std::min(best, distPointSeg(q, poly[j], poly[i]));
  return best;
}

// faces of an arrangement: bounded cycles; point-in-face honours holes (negative cycles contained in the face)
struct Faces {
  const Arrangement* A = nullptr;
  std::vector<std::vector<int>> holes;   // per cycle: hole cycles
  void init(const Arrangement& a) {
    A = &a;
    holes.assign(a.cycles.size(), {});
    for (size_t i = 0; i < a.cycles.size(); ++i)
      if (a.cycles[i].area < 0 && a.cycles[i].container >= 0) holes[a.cycles[i].container].push_back((int)i);
  }
  bool inside(int ci, Vec2 q) const {
    const auto& c = A->cycles[ci];
    if (c.area <= 0 || !pointInPolygon(q, c.poly)) return false;
    for (int h : holes[ci]) if (pointInPolygon(q, A->cycles[h].poly)) return false;
    return true;
  }
  int faceAt(Vec2 q) const {   // smallest bounded face containing q, -1 = none
    int best = -1;
    double ba = 1e300;
    for (size_t i = 0; i < A->cycles.size(); ++i)
      if (A->cycles[i].area > 0 && A->cycles[i].area < ba && inside((int)i, q)) { ba = A->cycles[i].area; best = (int)i; }
    return best;
  }
  int faceOfHalf(int h) const {   // bounded face on the left of half-edge h (-1 = unbounded)
    int c = A->halves[h].cycle;
    if (c < 0) return -1;
    return A->cycles[c].area > 0 ? c : A->cycles[c].container;
  }
  double net(int ci) const {
    double a = A->cycles[ci].area;
    for (int h : holes[ci]) a += A->cycles[h].area;   // hole areas are negative
    return a;
  }
};

struct FaceHatch { std::vector<int> lines; double angle = 0, pitch = 0; };

HatchFamily familyOf(const std::vector<int>& idx, const std::vector<LineRec>& L) {
  HatchFamily f;
  f.lines = (int)idx.size();
  if (idx.empty()) return f;
  // circular mean of doubled angles
  double sx = 0, sy = 0;
  for (int i : idx) { double a = deg2rad(2 * ang180(L[i].b - L[i].a)); sx += std::cos(a); sy += std::sin(a); }
  f.angleDeg = std::fmod(rad2deg(std::atan2(sy, sx)) / 2 + 180, 180);
  Vec2 n{-std::sin(deg2rad(f.angleDeg)), std::cos(deg2rad(f.angleDeg))};
  std::vector<double> off;
  for (int i : idx) off.push_back(dot((L[i].a + L[i].b) * 0.5, n));
  std::sort(off.begin(), off.end());
  std::vector<double> d;
  for (size_t i = 1; i < off.size(); ++i) if (off[i] - off[i - 1] > 0.05) d.push_back(off[i] - off[i - 1]);
  if (!d.empty()) {
    std::sort(d.begin(), d.end());
    double m = d[d.size() / 2];
    std::vector<double> e;   // drop gaps that are multiples (lines missing / clipped by holes)
    for (double x : d) if (x < 1.5 * m) e.push_back(x);
    f.pitch = e[e.size() / 2];
  }
  return f;
}

bool sameFamily(const HatchFamily& a, const HatchFamily& b) {
  if (angDiff180(a.angleDeg, b.angleDeg) > 4.0) return false;
  if (a.pitch <= 0 || b.pitch <= 0) return true;
  return std::abs(a.pitch - b.pitch) <= 0.2 * std::max(a.pitch, b.pitch);
}

std::string numTag(double v) {
  std::string s = fmt(v, 3);
  for (auto& c : s) if (c == '.') c = 'p'; else if (c == '-') c = 'm';
  return s;
}

using PSeg = ProfileSeg;

struct Work {
  const DrawingIR& ir;
  AxisymSection S;
  std::vector<LineRec> lines;                 // all stroked lines (norm + raw hatch-region sources)
  std::vector<int> hatchCand;                 // indices into lines
  std::vector<ASeg> profileSegs;              // thick visible edges (sheet)
  double thick = 0;                           // width threshold (lines >= thick are profile edges)
  // first arrangement
  Arrangement A1; Faces F1;
  std::map<int, FaceHatch> hatchOf;           // A1 cycle -> hatch
  std::vector<int> clusterFaces;              // A1 cycles of the chosen section
  explicit Work(const DrawingIR& i) : ir(i) {}
};


void collect(Work& W) {
  const PrimitiveSheet& s = W.ir.norm;
  double minW = 1e300, maxW = 0;
  auto add = [&](const Primitive& p, bool fromRaw) {
    if (p.kind != PrimKind::Line || !p.style.stroked || p.fromDimBlock) return;
    if (distance(p.p0, p.p1) < 0.05) return;
    LineRec r{p.id, p.p0, p.p1, p.style.width, !p.style.dash.empty() || isCenterDash(p), fromRaw};
    W.lines.push_back(r);
    if (r.w > 0) { minW = std::min(minW, r.w); maxW = std::max(maxW, r.w); }
  };
  for (auto& p : s.prims) add(p, false);
  for (auto& p : s.prims)
    if (p.kind == PrimKind::HatchRegion)
      for (int id : p.mergedFrom)
        if (auto* q = findPrim(W.ir.raw, id)) add(*q, true);
  // width classes: hatch / dimension lines are the thinnest class, visible edges are thicker (ISO 128 0.25/0.5)
  W.thick = (maxW > minW * 1.1 && minW < 1e300) ? minW * 1.1 : -1;
  for (size_t i = 0; i < W.lines.size(); ++i) {
    const LineRec& r = W.lines[i];
    if (r.dashed) continue;
    bool thin = W.thick < 0 || r.w < W.thick;
    double a = ang180(r.b - r.a), m = std::fmod(a, 90.0);
    if (thin && m > 15 && m < 75) W.hatchCand.push_back((int)i);
  }
  for (auto& r : W.lines) {
    if (r.dashed || r.fromRaw) continue;
    if (W.thick > 0 && r.w < W.thick) continue;
    ASeg g; g.a = r.a; g.b = r.b; g.src = r.id; W.profileSegs.push_back(g);
  }
  for (auto& p : s.prims) {
    if ((p.kind != PrimKind::Arc && p.kind != PrimKind::Circle) || !p.style.stroked || !p.style.dash.empty()) continue;
    if (W.thick > 0 && p.style.width < W.thick) continue;
    ASeg g; g.arc = true; g.c = p.center; g.r = p.radius; g.src = p.id;
    if (p.kind == PrimKind::Circle) { g.a = g.b = {p.center.x + p.radius, p.center.y}; }
    else { auto e = primEndpoints(p); g.a = e[0]; g.b = e[1]; }
    g.ccw = true;
    W.profileSegs.push_back(g);
  }
}

// hatch lines of each face of A1 (midpoint inside, both ends on the face boundary)
void assignHatch(Work& W) {
  W.F1.init(W.A1);
  for (int i : W.hatchCand) {
    const LineRec& r = W.lines[i];
    Vec2 m = (r.a + r.b) * 0.5;
    int f = W.F1.faceAt(m);
    if (f < 0) continue;
    const double tolE = 0.15;
    auto onB = [&](Vec2 q) {
      if (distToPoly(q, W.A1.cycles[f].poly) < tolE) return true;
      for (int h : W.F1.holes[f]) if (distToPoly(q, W.A1.cycles[h].poly) < tolE) return true;
      return false;
    };
    if (!onB(r.a) || !onB(r.b)) continue;
    W.hatchOf[f].lines.push_back(i);
  }
  for (auto& [f, h] : W.hatchOf) { HatchFamily fam = familyOf(h.lines, W.lines); h.angle = fam.angleDeg; h.pitch = fam.pitch; }
}

Box2 cycleBox(const Arrangement& A, int c) { Box2 b = emptyBox(); for (auto& p : A.cycles[c].poly) expand(b, p); return b; }
bool boxesNear(const Box2& a, const Box2& b, double m) {
  return a.lo.x - m <= b.hi.x && b.lo.x - m <= a.hi.x && a.lo.y - m <= b.hi.y && b.lo.y - m <= a.hi.y;
}

struct AxisCand { int line = -1; Vec2 p; Vec2 dir; bool horizontal = true; double iou = 0, arcFrac = 0, score = 0; };

// mirror IoU of the material of `faces` about the line (p, dir), sampled on a grid over `box`
double mirrorIoU(const Faces& F, const std::vector<int>& faces, const Box2& box, Vec2 p, Vec2 dir) {
  const int N = 90;
  Vec2 n = perp(dir);
  auto mat = [&](Vec2 q) { for (int f : faces) if (F.inside(f, q)) return true; return false; };
  int inter = 0, uni = 0;
  for (int i = 0; i < N; ++i)
    for (int j = 0; j < N; ++j) {
      Vec2 q{box.lo.x + (box.hi.x - box.lo.x) * (i + 0.5) / N, box.lo.y + (box.hi.y - box.lo.y) * (j + 0.5) / N};
      double s = dot(q - p, n);
      if (s < 0) continue;   // count each mirrored pair once (upper sample + its image)
      Vec2 m = q - n * (2 * s);
      bool a = mat(q), b = mat(m);
      if (a || b) ++uni;
      if (a && b) ++inter;
    }
  return uni ? double(inter) / uni : 0;
}

bool findSection(Work& W) {
  // hatched faces: >= 2 clipped hatch lines, or a single one in a face bordering a hatched face of the same angle
  // (thin walls such as a flare carry one or no hatch line)
  std::set<int> hs;
  for (auto& [f, h] : W.hatchOf) if (h.lines.size() >= 2) hs.insert(f);
  for (auto& [f, h] : W.hatchOf) {
    if (h.lines.size() != 1) continue;
    for (int he : W.A1.cycles[f].halves) {
      int o = W.F1.faceOfHalf(he ^ 1);
      if (o >= 0 && hs.count(o) && angDiff180(W.hatchOf[o].angle, ang180(W.lines[h.lines[0]].b - W.lines[h.lines[0]].a)) <= 4) { hs.insert(f); break; }
    }
  }
  for (auto it = W.hatchOf.begin(); it != W.hatchOf.end();) it = hs.count(it->first) ? std::next(it) : W.hatchOf.erase(it);
  std::vector<int> hf(hs.begin(), hs.end());
  if (hf.empty()) { W.S.reason = "taralı bölge bulunamadı"; return false; }
  const bool dbg = std::getenv("R2_DEBUG") != nullptr;
  if (dbg) {
    std::cerr << "R2: lines=" << W.lines.size() << " hatchCand=" << W.hatchCand.size() << " profile=" << W.profileSegs.size()
              << " cycles=" << W.A1.cycles.size() << " hatchedFaces=" << hf.size() << " thick=" << W.thick << "\n";
    for (int f : hf) {
      Box2 b = cycleBox(W.A1, f);
      std::cerr << "    face " << f << " area=" << W.A1.cycles[f].area << " lines=" << W.hatchOf[f].lines.size() << " ang=" << W.hatchOf[f].angle
                << " pitch=" << W.hatchOf[f].pitch << " box " << b.lo.x << "," << b.lo.y << " - " << b.hi.x << "," << b.hi.y << "\n";
    }
  }
  // the view of a centreline = hatched faces lying along it; candidates: dashed horizontal / vertical lines
  double bestScore = 0;
  AxisCand best;
  std::vector<int> bestFaces;
  Box2 bestBox;
  for (size_t i = 0; i < W.lines.size(); ++i) {
    const LineRec& r = W.lines[i];
    if (!r.dashed) continue;
    Vec2 d = r.b - r.a;
    double L = length(d);
    bool hor = std::abs(d.y) < std::sin(deg2rad(1.0)) * L, ver = std::abs(d.x) < std::sin(deg2rad(1.0)) * L;
    if (!hor && !ver) continue;
    Vec2 dir = hor ? Vec2{1, 0} : Vec2{0, 1}, n = perp(dir);
    double s0 = std::min(dot(r.a, dir), dot(r.b, dir)), s1 = std::max(dot(r.a, dir), dot(r.b, dir)), c = dot(r.a, n);
    std::vector<int> faces;
    for (int f : hf) {
      Box2 b = cycleBox(W.A1, f);
      double f0 = dot(b.lo, dir), f1 = dot(b.hi, dir), g0 = dot(b.lo, n), g1 = dot(b.hi, n);
      double ov = std::min(f1, s1 + 1) - std::max(f0, s0 - 1);
      double gap = std::max({0.0, g0 - c, c - g1});
      if (ov >= 0.5 * (f1 - f0) && gap < 0.6 * L) faces.push_back(f);
    }
    if (faces.empty()) continue;
    Box2 B = emptyBox();
    for (int f : faces) { Box2 b = cycleBox(W.A1, f); expand(B, b.lo); expand(B, b.hi); }
    double lo = dot(B.lo, n), hi = dot(B.hi, n);
    if (c < lo + 0.1 * (hi - lo) || c > hi - 0.1 * (hi - lo)) continue;
    double w = B.hi.x - B.lo.x, h = B.hi.y - B.lo.y;
    double arcL = 0, totL = 0, areaW = 0;
    for (int f : faces) {
      areaW += W.F1.net(f);
      for (int he : W.A1.cycles[f].halves) {
        const ASeg& sg = W.A1.edges[W.A1.halves[he].edge].seg;
        double l = segLength(sg);
        totL += l;
        if (sg.arc && sg.r > 0.15 * std::min(w, h)) arcL += l;
      }
    }
    AxisCand a;
    a.line = (int)i;
    a.horizontal = hor;
    a.dir = dir;
    a.p = hor ? Vec2{B.lo.x, c} : Vec2{c, B.lo.y};
    a.iou = mirrorIoU(W.F1, faces, B, a.p, a.dir);
    a.arcFrac = totL > 0 ? arcL / totL : 0;
    a.score = a.iou * (1 - a.arcFrac);
    if (dbg) std::cerr << "  axis cand line " << r.id << (hor ? " h" : " v") << " faces=" << faces.size() << " iou=" << a.iou << " arc=" << a.arcFrac << "\n";
    if (a.score >= 0.5 && a.score * std::sqrt(areaW) > bestScore) {
      bestScore = a.score * std::sqrt(areaW);
      best = a;
      bestFaces = faces;
      bestBox = B;
    }
  }
  if (best.line < 0) { W.S.reason = "taralı kesitte simetri ekseni (eksen çizgisi) bulunamadı"; return false; }
  W.S.found = true;
  W.S.box = bestBox;
  W.S.horizontal = best.horizontal;
  W.S.axisPrim = W.lines[best.line].id;
  W.S.axisPoint = best.p;
  W.S.axisDir = best.dir;
  W.S.sideNormal = perp(best.dir);   // horizontal axis: side 0 = above; vertical: side 0 = left
  W.S.mirrorIoU = best.iou;
  W.clusterFaces = bestFaces;
  return true;
}

// ---- texts
struct TextInfo { const TextRun* t = nullptr; Vec2 c; bool alongAxis = false; DimText d; double dia = 0, chamfer = 0, chamferAng = 45, angle = 0, lin = 0; };

double titleScaleOf(const DrawingIR& ir, std::string* ev) {
  if (ir.layout.title.scale.conf > 0 && ir.layout.title.scale.num > 0) {
    if (ev) *ev = ir.layout.title.scale.evidence;
    return ir.layout.title.scale.factor();
  }
  static const std::regex re(R"(^\s*(\d+(?:[.,]\d+)?)\s*:\s*(\d+(?:[.,]\d+)?)\s*$)");
  for (auto& t : ir.norm.texts) {
    std::smatch m;
    if (std::regex_match(t.utf8, m, re)) {
      double a = std::stod(m[1].str()), b = std::stod(m[2].str());
      if (a > 0 && b > 0) { if (ev) *ev = "antet: " + t.utf8; return b / a; }
    }
  }
  return 0;
}

std::vector<TextInfo> sectionTexts(const Work& W) {
  std::vector<TextInfo> out;
  const Box2& B = W.S.box;
  double m = 0.8 * std::max(B.hi.x - B.lo.x, B.hi.y - B.lo.y);
  static const std::regex reDia(R"((?:Ø|⌀)\s*(\d+(?:[.,]\d+)?))");
  static const std::regex reAng(R"((\d+(?:[.,]\d+)?)\s*°\s*(?:INCL|DAHİL|DAHIL))", std::regex::icase);
  static const std::regex reCh(R"(^\s*(\d+(?:[.,]\d+)?)\s*[xX×]\s*(\d+(?:[.,]\d+)?)\s*°\s*$)");
  auto num = [](std::string s) { for (auto& c : s) if (c == ',') c = '.'; return std::stod(s); };
  for (auto& t : W.ir.norm.texts) {
    Vec2 c = textCentre(t);
    if (c.x < B.lo.x - m || c.x > B.hi.x + m || c.y < B.lo.y - m || c.y > B.hi.y + m) continue;
    if (t.utf8.find(':') != std::string::npos) continue;
    TextInfo ti;
    ti.t = &t;
    ti.c = c;
    Vec2 bd{std::cos(deg2rad(t.rotDeg)), std::sin(deg2rad(t.rotDeg))};
    ti.alongAxis = std::abs(dot(bd, W.S.axisDir)) > 0.95;
    std::smatch mm;
    std::string u = t.utf8;
    if (std::regex_match(u, mm, reCh)) { ti.chamfer = num(mm[1].str()); ti.chamferAng = num(mm[2].str()); out.push_back(ti); continue; }
    if (std::regex_search(u, mm, reDia)) { ti.dia = num(mm[1].str()); out.push_back(ti); continue; }
    if (std::regex_search(u, mm, reAng)) { ti.angle = num(mm[1].str()); out.push_back(ti); continue; }
    ti.d = parseDim(u);
    if (!ti.d.valid) continue;
    if (ti.d.kind == DimKind::Angle) ti.angle = ti.d.nominal;
    else if (ti.d.kind == DimKind::Chamfer) { ti.chamfer = ti.d.nominal; ti.chamferAng = ti.d.chamferAngle > 0 ? ti.d.chamferAngle : 45; }
    else if (ti.d.kind == DimKind::Diameter) ti.dia = ti.d.nominal;
    else if ((ti.d.kind == DimKind::Linear || ti.d.kind == DimKind::Unknown || ti.d.kind == DimKind::Aligned) && ti.d.nominal > 0 &&
             ti.d.remainder.empty() && !ti.d.reference)
      ti.lin = ti.d.nominal;
    else continue;
    out.push_back(ti);
  }
  return out;
}

// leader end point: follow thin lines starting at the text box (<= 3 segments); returns false if none
bool leaderEnd(const Work& W, const TextRun& t, Vec2& end) {
  Box2 tb = textBox(t);
  auto nearBox = [&](Vec2 q) {
    double dx = std::max({tb.lo.x - q.x, 0.0, q.x - tb.hi.x}), dy = std::max({tb.lo.y - q.y, 0.0, q.y - tb.hi.y});
    return std::hypot(dx, dy) < 1.5;
  };
  for (size_t i = 0; i < W.lines.size(); ++i) {
    const LineRec& r = W.lines[i];
    if (r.dashed || r.fromRaw || (W.thick > 0 && r.w >= W.thick)) continue;
    Vec2 cur;
    if (nearBox(r.a)) cur = r.b; else if (nearBox(r.b)) cur = r.a; else continue;
    if (nearBox(cur)) continue;
    std::set<size_t> used{i};
    for (int step = 0; step < 3; ++step) {
      bool moved = false;
      for (size_t j = 0; j < W.lines.size() && !moved; ++j) {
        const LineRec& q = W.lines[j];
        if (used.count(j) || q.dashed || q.fromRaw || (W.thick > 0 && q.w >= W.thick)) continue;
        if (distance(q.a, cur) < 0.1) { cur = q.b; used.insert(j); moved = true; }
        else if (distance(q.b, cur) < 0.1) { cur = q.a; used.insert(j); moved = true; }
      }
      if (!moved) break;
    }
    end = cur;
    return true;
  }
  return false;
}

void determineScale(Work& W, const std::vector<TextInfo>& texts) {
  AxisymSection& S = W.S;
  std::string tev;
  S.titleScale = titleScaleOf(W.ir, &tev);
  // radii (sheet) of the hatched material's vertices and cylinder edges
  std::vector<double> R;
  for (int f : W.clusterFaces)
    for (auto& q : W.A1.cycles[f].poly) {
      double r = std::abs(dot(q - S.axisPoint, S.sideNormal));
      if (r > 0.05) R.push_back(r);
    }
  std::sort(R.begin(), R.end());
  R.erase(std::unique(R.begin(), R.end(), [](double a, double b) { return std::abs(a - b) < 0.01; }), R.end());
  struct K { double k; int t; };
  std::vector<K> ks;
  for (size_t i = 0; i < texts.size(); ++i)
    if (texts[i].dia > 0) for (double r : R) ks.push_back({texts[i].dia / (2 * r), (int)i});
  std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) { return a.k < b.k; });
  int bestN = 0;
  double bestK = 0;
  for (size_t i = 0; i < ks.size(); ++i) {
    std::map<int, double> per;   // text -> k closest to ks[i].k
    for (size_t j = i; j < ks.size() && ks[j].k <= ks[i].k * 1.015; ++j) per.emplace(ks[j].t, ks[j].k);
    if ((int)per.size() > bestN) {
      bestN = (int)per.size();
      std::vector<double> v;
      for (auto& [t, k] : per) v.push_back(k);
      std::sort(v.begin(), v.end());
      bestK = v[v.size() / 2];
    }
  }
  if (bestN >= 2) {
    S.scale = bestK;
    S.scaleEvidence = "ölçüler: " + std::to_string(bestN) + " çap ölçüsü / ölçülen yarıçap, " + fmt(1 / bestK, 4) + " kağıt mm / parça mm";
  } else if (S.titleScale > 0) {
    S.scale = S.titleScale;
    S.scaleEvidence = tev;
  } else if (W.ir.layout.scaleFromDims.conf > 0) {
    S.scale = W.ir.layout.scaleFromDims.factor();
    S.scaleEvidence = W.ir.layout.scaleFromDims.evidence;
  } else {
    S.scale = 1;
    S.scaleEvidence = "varsayım 1:1";
  }
  if (S.titleScale > 0 && std::abs(S.titleScale - S.scale) > 0.02 * S.scale)
    S.notes.push_back("Antet ölçeği (" + tev + ") ölçülerden bulunan ölçekle uyuşmuyor (" + fmt(1 / S.scale, 3) +
                      ":1); sayfa küçültülerek basılmış olabilir — ölçülerden bulunan ölçek kullanıldı");
}

}  // namespace

AxisymSection analyzeAxisymSection(const DrawingIR& ir) {
  Work W(ir);
  collect(W);
  if (W.profileSegs.empty()) { W.S.reason = "görünür kenar yok"; return W.S; }
  W.A1.build(W.profileSegs, 0.05);
  assignHatch(W);
  if (!findSection(W)) return W.S;
  AxisymSection& S = W.S;
  const Box2 B = S.box;
  // hatch line ids of the section, profile prims
  std::set<int> hl;
  for (int f : W.clusterFaces) for (int i : W.hatchOf[f].lines) hl.insert(W.lines[i].id);
  S.hatchLineIds.assign(hl.begin(), hl.end());
  // ---- axial origin: front face (min axial coordinate of the material)
  double a0 = 1e300;
  for (int f : W.clusterFaces) for (auto& q : W.A1.cycles[f].poly) a0 = std::min(a0, dot(q - S.axisPoint, S.axisDir));
  S.axial0 = a0;
  std::vector<TextInfo> texts = sectionTexts(W);
  determineScale(W, texts);
  // ---- second arrangement: section edges + axis
  std::vector<ASeg> segs;
  Box2 Bm{{B.lo.x - 0.5, B.lo.y - 0.5}, {B.hi.x + 0.5, B.hi.y + 0.5}};
  std::set<int> prof;
  for (auto& s : W.profileSegs) {
    Box2 b = emptyBox();
    for (auto& q : segPolyline(s, 0.5)) expand(b, q);
    if (!boxesNear(b, Bm, 0)) continue;
    segs.push_back(s);
    segs.back().tag = 0;
    prof.insert(s.src);
  }
  S.profilePrimIds.assign(prof.begin(), prof.end());
  ASeg ax;
  double e0 = dot(B.lo - S.axisPoint, S.axisDir) - 1, e1 = dot(B.hi - S.axisPoint, S.axisDir) + 1;
  ax.a = S.axisPoint + S.axisDir * e0;
  ax.b = S.axisPoint + S.axisDir * e1;
  ax.tag = 2;
  ax.src = S.axisPrim;
  segs.push_back(ax);
  Arrangement A2;
  A2.build(segs, 0.05);
  Faces F2;
  F2.init(A2);
  // ---- face -> family (via the hatched face of A1 containing it) -> part
  std::vector<HatchFamily> fams;   // per cluster face
  std::map<int, int> famOfA1;
  for (int f : W.clusterFaces) {
    HatchFamily h = familyOf(W.hatchOf[f].lines, W.lines);
    int k = -1;
    for (size_t i = 0; i < fams.size(); ++i) if (sameFamily(fams[i], h)) { k = (int)i; break; }
    if (k < 0) { fams.push_back(h); k = (int)fams.size() - 1; }
    else if (fams[k].pitch <= 0 && h.pitch > 0) { fams[k].pitch = h.pitch; }
    famOfA1[f] = k;
  }
  // family adjacency merge: faces of the same hatch angle that touch (share a boundary edge) belong to one part
  // even if the pitch differs (local re-hatching of thin walls); different angles touching = different parts.
  {
    std::vector<int> par(fams.size());
    std::iota(par.begin(), par.end(), 0);
    std::function<int(int)> fd = [&](int x) { return par[x] == x ? x : par[x] = fd(par[x]); };
    for (size_t e = 0; e < W.A1.edges.size(); ++e) {
      int fa = W.F1.faceOfHalf(2 * (int)e), fb = W.F1.faceOfHalf(2 * (int)e + 1);
      if (fa < 0 || fb < 0 || !famOfA1.count(fa) || !famOfA1.count(fb)) continue;
      int ka = famOfA1[fa], kb = famOfA1[fb];
      if (ka != kb && angDiff180(fams[ka].angleDeg, fams[kb].angleDeg) <= 4.0) {
        if (segLength(W.A1.edges[e].seg) > 0.05) {
          S.notes.push_back("Aynı açılı farklı aralıklı taramalar bitişik: tek parça sayıldı (" + fmt(fams[ka].pitch, 3) + " / " + fmt(fams[kb].pitch, 3) + " mm)");
          par[fd(ka)] = fd(kb);
        }
      }
    }
    std::map<int, int> remap;
    for (auto& [f, k] : famOfA1) { int r = fd(k); if (!remap.count(r)) remap[r] = (int)remap.size(); k = remap[r]; }
    std::vector<HatchFamily> nf(remap.size());
    for (auto& [r, k] : remap) nf[k] = fams[r];
    fams = nf;
  }
  const int nParts = (int)fams.size();
  std::vector<int> partOf(A2.cycles.size(), -1), sideOf(A2.cycles.size(), 0);
  for (size_t c = 0; c < A2.cycles.size(); ++c) {
    if (A2.cycles[c].area <= 0) continue;
    Vec2 ip = A2.interiorPoint((int)c);
    int f1 = -1;
    for (int f : W.clusterFaces) if (W.F1.inside(f, ip)) { f1 = f; break; }
    if (f1 < 0) continue;
    partOf[c] = famOfA1[f1];
    sideOf[c] = dot(ip - S.axisPoint, S.sideNormal) > 0 ? 0 : 1;
  }
  // ---- per part: half profiles
  const double det = S.sideNormal.x * S.axisDir.y - S.sideNormal.y * S.axisDir.x;   // orientation of (r, axial)
  std::vector<std::vector<PSeg>> partSegs(nParts);
  for (int p = 0; p < nParts; ++p) {
    AxisymPart P;
    P.index = p;
    P.family = fams[p];
    for (int f : W.clusterFaces) if (famOfA1[f] == p) for (int i : W.hatchOf[f].lines) P.hatchLineIds.push_back(W.lines[i].id);
    for (size_t c = 0; c < A2.cycles.size(); ++c)
      if (partOf[c] == p) P.area[sideOf[c]] += F2.net((int)c) * S.scale * S.scale;
    // mirror IoU of this part
    {
      std::vector<int> fs;
      for (int f : W.clusterFaces) if (famOfA1[f] == p) fs.push_back(f);
      P.mirrorIoU = mirrorIoU(W.F1, fs, B, S.axisPoint, S.axisDir);
    }
    P.side = P.area[1] > P.area[0] * 1.001 ? 1 : 0;
    auto inSet = [&](int face) { return face >= 0 && partOf[face] == p && sideOf[face] == P.side; };
    std::vector<int> bh;   // boundary half-edges (material on the left)
    for (size_t e = 0; e < A2.edges.size(); ++e) {
      int h0 = 2 * (int)e, h1 = h0 + 1;
      bool m0 = inSet(F2.faceOfHalf(h0)), m1 = inSet(F2.faceOfHalf(h1));
      if (m0 == m1) continue;
      bh.push_back(m0 ? h0 : h1);
    }
    // chain into loops
    std::map<int, std::vector<int>> byFrom;
    for (int h : bh) byFrom[A2.halves[h].from].push_back(h);
    std::set<int> usedH;
    const bool flip = (det < 0) != (P.side == 1);
    std::vector<PSeg>& PS = partSegs[p];
    for (int h0 : bh) {
      if (usedH.count(h0)) continue;
      std::vector<PSeg> loop;
      int h = h0;
      while (h >= 0 && !usedH.count(h)) {
        usedH.insert(h);
        ASeg s = A2.halfSeg(h);
        PSeg q;
        q.arc = s.arc;
        q.a = S.toPart(s.a);
        q.b = S.toPart(s.b);
        if (s.arc) { q.c = S.toPart(s.c); q.r = s.r * S.scale; q.ccw = flip ? !s.ccw : s.ccw; }
        const auto& E = A2.edges[A2.halves[h].edge];
        for (size_t k = 0; k < E.srcs.size(); ++k) if (E.tags[k] == 0) { q.src = E.srcs[k]; break; }
        loop.push_back(q);
        int nxt = -1;
        for (int c : byFrom[A2.halves[h].to]) if (!usedH.count(c)) { nxt = c; break; }
        h = nxt;
      }
      // merge collinear consecutive lines
      bool changed = true;
      while (changed && loop.size() > 3) {
        changed = false;
        for (size_t i = 0; i < loop.size(); ++i) {
          PSeg& a = loop[i];
          PSeg& b = loop[(i + 1) % loop.size()];
          if (a.arc || b.arc) continue;
          Vec2 u = a.b - a.a, v = b.b - b.a;
          if (std::abs(cross(unit(u), unit(v))) < 1e-4 && dot(u, v) > 0) {
            a.b = b.b;
            loop.erase(loop.begin() + (long)((i + 1) % loop.size()));
            changed = true;
            break;
          }
        }
      }
      std::vector<Vec2> poly;
      for (auto& q : loop) {
        if (!q.arc) { poly.push_back(q.a); continue; }
        ASeg t; t.arc = true; t.a = q.a; t.b = q.b; t.c = q.c; t.r = q.r; t.ccw = q.ccw;
        auto pl = segPolyline(t, 0.02);
        poly.insert(poly.end(), pl.begin(), pl.end() - 1);
      }
      if (poly.size() >= 3) P.loops.push_back(poly);
      PS.insert(PS.end(), loop.begin(), loop.end());
    }
    P.segs = PS;
    // Pappus: V = 2 pi * |first moment about the axis| (x = radius)
    double mx = 0;
    for (auto& poly : P.loops)
      for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        mx += (poly[j].x + poly[i].x) * (poly[j].x * poly[i].y - poly[i].x * poly[j].y) / 6.0;
    P.pappusVolume = 2 * kPi * std::abs(mx);   // Mx = integral of x dA (shoelace first moment)
    P.name = "Part" + std::to_string(p + 1);
    S.parts.push_back(P);
  }
  // ---- part names from "(n) NAME" leader labels
  static const std::regex reLabel(R"(^\s*\((\d+)\)\s*(.+?)\s*$)");
  for (auto& t : ir.norm.texts) {
    std::smatch m;
    if (!std::regex_match(t.utf8, m, reLabel)) continue;
    Vec2 e;
    if (!leaderEnd(W, t, e)) continue;
    int f = -1;
    for (int c : W.clusterFaces) if (W.F1.inside(c, e)) { f = c; break; }
    if (f < 0) {   // arrow tip on the boundary: nearest hatched face
      double bd = 0.5;
      for (int c : W.clusterFaces) { double d = distToPoly(e, W.A1.cycles[c].poly); if (d < bd) { bd = d; f = c; } }
    }
    if (f < 0) continue;
    std::string nm = m[2].str();
    std::string id;
    for (char ch : nm) id += std::isalnum((unsigned char)ch) ? ch : '_';
    S.parts[famOfA1[f]].name = id;
    S.parts[famOfA1[f]].family.lines = (int)S.parts[famOfA1[f]].hatchLineIds.size();
  }
  for (auto& P : S.parts) P.family.lines = (int)P.hatchLineIds.size();
  // order parts by axial start, then radius
  {
    std::vector<int> ord(S.parts.size());
    std::iota(ord.begin(), ord.end(), 0);
    auto key = [&](const AxisymPart& P) {
      double a = 1e300, r = 1e300;
      for (auto& l : P.loops) for (auto& q : l) { a = std::min(a, q.y); r = std::min(r, q.x); }
      return std::make_pair(std::round(a * 100), r);
    };
    std::stable_sort(ord.begin(), ord.end(), [&](int x, int y) { return key(S.parts[x]) < key(S.parts[y]); });
    std::vector<AxisymPart> np;
    for (int i : ord) { np.push_back(S.parts[i]); np.back().index = (int)np.size() - 1; }
    S.parts = np;
  }
  for (auto& P : S.parts)
    if (P.mirrorIoU < 0.97)
      S.notes.push_back(P.name + ": kesitin iki yarısı farklı (ayna IoU " + fmt(P.mirrorIoU, 3) + "); daha dolu yarı (" +
                        (P.side == 0 ? std::string("üst/sol") : std::string("alt/sağ")) +
                        ") döndürüldü — eksenel simetrik olmayan kanal/yiv/oluklar ayrı özellik olarak eklenmeli");
  return S;
}

// ================================================================================================= S6 hook
namespace {

constexpr size_t kMaxEntities = 150;

struct NameReg {
  std::set<std::string> used;
  std::string take(const std::string& base) {
    if (!used.count(base)) { used.insert(base); return base; }
    for (int k = 2;; ++k) { std::string n = base + "_" + std::to_string(k); if (!used.count(n)) { used.insert(n); return n; } }
  }
};

struct SB {   // sketch builder for one part
  Sketch sk = Sketch::create();
  ParamValues vals;
  std::map<int, Vec2> orig;
  std::vector<ParamProposal> params;
  std::set<std::string> paramNames;
  std::vector<int> measured;
  std::map<int, int> ent2prim;
  std::map<int, int> c2text;
  std::map<std::string, int> refText;   // reference ([T]) parameter -> dimension text
  double tol = 1e-4;
  int pointAt(Vec2 p) {
    for (auto& q : sk.points) if (std::hypot(q.x - p.x, q.y - p.y) < tol) return q.id;
    int id = sk.addPoint(p.x, p.y);
    orig[id] = p;
    return id;
  }
  int dof() { return sketch::degreesOfFreedom(sk, vals); }
  int tryAdd(SkConstraintType t, std::vector<int> refs, const std::string& expr = "") {
    int before = dof();
    int id = sk.addConstraint(t, std::move(refs), expr);
    int after = dof();
    if (after >= 0 && before >= 0 && after < before) return id;
    sk.constraints.pop_back();
    return -1;
  }
  void addParam(const ParamProposal& pp) {
    vals[pp.param.name] = pp.nominalValue;
    if (paramNames.insert(pp.param.name).second) params.push_back(pp);
  }
  std::vector<const SkEntity*> lines() const {
    std::vector<const SkEntity*> v;
    for (auto& e : sk.entities) if (e.type == SkEntityType::Line && !e.construction) v.push_back(&e);
    return v;
  }
  Vec2 P(int id) const { auto* p = sk.point(id); return {p->x, p->y}; }
};

ParamProposal makeParam(const std::string& name, double v, ParamKind k, ParamTag tag, const std::string& desc) {
  ParamProposal pp;
  pp.param.name = name;
  pp.param.kind = k;
  pp.param.expression = fmt(v, 6);
  pp.param.description = desc;
  pp.tag = tag;
  pp.nominalValue = v;
  pp.tolMidValue = v;
  if (tag == ParamTag::FromDimension) { pp.param.exposed = true; pp.param.group = "Teknik resim"; }
  if (k == ParamKind::Length && v > 0) pp.param.minValue = 0;
  return pp;
}

// ---- integration R2 x R3: diameters dimensioned in the transverse views (B-B / C-C / VIEW B) or named in the notes
struct XDia {
  double d = 0;                     // part mm
  const TextRun* t = nullptr;       // dimension text (nullptr: note)
  std::string src;                  // "SECTION B-B ..." / "not: THROAT"
  double y = kNaN;                  // axial hint (part x), NaN = none
};

std::vector<XDia> crossViewDiameters(const DrawingIR& ir, const TransverseResult& TR, const std::vector<TextInfo>& own) {
  std::vector<XDia> out;
  static const std::regex reDia(R"((?:Ø|⌀)\s*(\d+(?:[.,]\d+)?))");
  std::set<const TextRun*> ownSet;
  for (auto& t : own) ownSet.insert(t.t);
  for (auto& V : TR.views) {
    if (V.radii.empty() || V.factor <= 0) continue;
    const double Rs = V.radii.back() / V.factor;   // sheet
    for (auto& t : ir.norm.texts) {
      if (ownSet.count(&t)) continue;
      Vec2 c = textCentre(t);
      if (distance(c, V.centre) > 1.8 * Rs) continue;
      std::smatch m;
      std::string u = t.utf8;
      if (!std::regex_search(u, m, reDia)) continue;
      std::string ns = m[1].str();
      for (auto& ch : ns) if (ch == ',') ch = '.';
      const double D = std::stod(ns);
      bool circle = false;   // the Ø must be a circle of this view
      for (double r : V.radii) circle |= std::abs(2 * r - D) < 0.015 * D + 0.01;
      if (!circle) continue;
      XDia x{D, &t, V.caption.empty() ? "görünüş " + V.letter : V.caption, V.xMarker};
      for (auto& F : V.families)
        if (F.kind == "slot" && !F.faceRelative && has(F.x0) && std::abs(2 * F.rIn - D) < 0.015 * D + 0.01) x.y = 0.5 * (F.x0 + F.x1);
      out.push_back(x);
    }
  }
  for (auto& [name, D] : TR.notes.namedDiameters) {
    bool dup = false;
    for (auto& x : out) dup |= std::abs(x.d - D) < 1e-6;
    if (!dup && D > 0) out.push_back({D, nullptr, "not: " + name, kNaN});
  }
  return out;
}

// A longitudinal SECTION through tangential slots shows the slot walls where they cross the cutting plane. Inside a
// through-wall slot band (x0..x1 of a transverse view) the bore of the revolve is the view's DIMENSIONED inner circle
// (e.g. SWC-RR-001: A-A line r = 14.14 = slot wall crossing, B-B / note 5: Ø24); the slots (R3) re-create the
// section line. Moves such axial lines (rIn < r < rOut) to rIn; returns a description per change.
std::vector<std::string> correctSlotBands(AxisymSection& S, const TransverseResult& TR, const std::vector<XDia>& xd) {
  std::vector<std::string> notes;
  const double tol = 0.03;
  for (auto& V : TR.views)
    for (auto& F : V.families) {
      if (F.kind != "slot" || F.faceRelative || !has(F.x0) || !has(F.x1) || F.rIn <= 0) continue;
      double dimD = 0;
      for (auto& x : xd) if (x.t && std::abs(x.d - 2 * F.rIn) < 0.015 * x.d + 0.01) dimD = x.d;
      if (dimD <= 0) continue;
      const double rIn = dimD / 2;
      for (auto& P : S.parts) {
        std::vector<std::pair<double, std::pair<double, double>>> moves;   // r -> y range
        for (auto& g : P.segs) {
          if (g.arc || std::abs(g.a.x - g.b.x) > 1e-6) continue;
          const double r = g.a.x, y0 = std::min(g.a.y, g.b.y), y1 = std::max(g.a.y, g.b.y);
          if (r <= rIn + tol || r >= F.rOut - tol || y1 - y0 < tol) continue;
          const double ov = std::min(y1, F.x1) - std::max(y0, F.x0);
          if (ov < 0.5 * (y1 - y0)) continue;
          moves.push_back({r, {y0, y1}});
        }
        for (auto& [r, yr] : moves) {
          auto mv = [&](Vec2& q) { if (std::abs(q.x - r) < 1e-6 && q.y > yr.first - 1e-6 && q.y < yr.second + 1e-6) q.x = rIn; };
          for (auto& g : P.segs) { mv(g.a); mv(g.b); }
          for (auto& loop : P.loops) for (auto& q : loop) mv(q);
          notes.push_back(P.name + ": kesitteki r=" + fmt(r, 4) + " (x " + fmt(yr.first, 4) + ".." + fmt(yr.second, 4) +
                          ") kanal duvarının kesit düzlemiyle kesişimi; döndürme çapı " + V.caption + " ölçüsü Ø" +
                          fmt(dimD, 4) + " alındı (kanallar R3 ile kesilir)");
        }
        if (!moves.empty()) {   // drop zero-length segments, recompute Pappus
          P.segs.erase(std::remove_if(P.segs.begin(), P.segs.end(), [](const ProfileSeg& g) { return !g.arc && distance(g.a, g.b) < 1e-9; }), P.segs.end());
          double mx = 0;
          for (auto& poly : P.loops)
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
              mx += (poly[j].x + poly[i].x) * (poly[j].x * poly[i].y - poly[i].x * poly[j].y) / 6.0;
          P.pappusVolume = 2 * kPi * std::abs(mx);
        }
      }
    }
  return notes;
}

}  // namespace

int addAxisymSectionSketches(DrawingIR& ir, const InterpretOptions& opt, StageResult& res) {
  for (auto it = ir.attrs.begin(); it != ir.attrs.end();) it = it->first.rfind("r2.", 0) == 0 && it->first != "r2.fixture" ? ir.attrs.erase(it) : std::next(it);
  AxisymSection S = analyzeAxisymSection(ir);
  if (!S.found) { ir.attrs["r2.section"] = "yok: " + S.reason; return 0; }
  // re-derive the dimension texts (same filter as the analysis)
  Work W(ir);
  W.S = S;
  collect(W);
  std::vector<TextInfo> texts = sectionTexts(W);
  // integration R2 x R3: transverse views give the dimensioned bores of the slot bands and further diameters
  const TransverseResult TR = analyzeTransverse(ir.norm);
  const std::vector<XDia> xdias = crossViewDiameters(ir, TR, texts);
  for (auto& n : correctSlotBands(S, TR, xdias)) S.notes.push_back(n);
  const double sc = S.scale;
  const double tolP = 0.03;   // part mm
  // view of the section (wizard grouping), if S3 found one
  int viewId = -1;
  Vec2 bc = (S.box.lo + S.box.hi) * 0.5;
  for (auto& v : ir.layout.views)
    if (bc.x >= v.box.lo.x && bc.x <= v.box.hi.x && bc.y >= v.box.lo.y && bc.y <= v.box.hi.y) viewId = v.id;
  std::string viewName = "kesit";
  if (const ViewRegion* v = findView(ir.layout, viewId)) viewName = v->label.empty() ? labelTr(v->role) : v->label;
  {   // caption of THIS section: nearest "SECTION .." / "KESİT .." text that is not a transverse view's caption
    double best = 1e300;
    for (auto& t : ir.norm.texts) {
      if (!(t.utf8.rfind("SECTION", 0) == 0 || foldUpper(t.utf8).rfind("KESIT", 0) == 0)) continue;
      bool transverse = false;
      for (auto& V : TR.views) transverse |= !V.caption.empty() && (V.caption == t.utf8 || t.utf8.rfind(V.caption, 0) == 0 || V.caption.rfind(t.utf8, 0) == 0);
      Vec2 c = textCentre(t);
      double d = std::hypot(std::max({S.box.lo.x - c.x, 0.0, c.x - S.box.hi.x}), std::max({S.box.lo.y - c.y, 0.0, c.y - S.box.hi.y}));
      if (transverse) d += 1e6;
      if (d < best) { best = d; viewName = t.utf8; }
    }
  }
  // extension-line candidates for axial dimensions: lines perpendicular to the axis (any width, not hatch)
  std::set<int> hatchSet(S.hatchLineIds.begin(), S.hatchLineIds.end());
  struct Ext { double s, n0, n1; };
  std::vector<Ext> ext;
  for (auto& r : W.lines) {
    if (r.dashed || r.fromRaw || hatchSet.count(r.id)) continue;
    Vec2 d = r.b - r.a;
    if (std::abs(dot(unit(d), S.axisDir)) > std::sin(deg2rad(1.0))) continue;
    double s = dot(r.a - S.axisPoint, S.axisDir), n0 = dot(r.a - S.axisPoint, S.sideNormal), n1 = dot(r.b - S.axisPoint, S.sideNormal);
    ext.push_back({s, std::min(n0, n1), std::max(n0, n1)});
  }
  // ---- build sketches
  NameReg reg;
  std::vector<SB> bs(S.parts.size());
  // reconstruct part segments (same as analysis): re-run the half-profile extraction through loops of S.parts:
  // loops are polygons (arcs sampled); for sketch entities we use the polygon vertices of line runs and
  // recover arcs from the analysis' source primitives (rare in turned sections: spherical tips).
  for (size_t p = 0; p < S.parts.size(); ++p) {
    SB& B = bs[p];
    B.sk.plane.kind = SketchPlaneKind::XZ;
    if (S.parts[p].segs.size() > kMaxEntities) continue;   // not a turned-part profile (guard: DOF analysis is O(n^3))
    for (auto& g : S.parts[p].segs) {
      if (!g.arc) {
        int a = B.pointAt(g.a), b = B.pointAt(g.b);
        if (a != b) B.ent2prim[B.sk.addLine(a, b)] = g.src;
        continue;
      }
      if (distance(g.a, g.b) < 1e-6) { B.ent2prim[B.sk.addCircle(B.pointAt(g.c), g.r)] = g.src; continue; }
      int c = B.pointAt(g.c), st = B.pointAt(g.ccw ? g.a : g.b), en = B.pointAt(g.ccw ? g.b : g.a);
      int e = B.sk.addArc(c, st, en);
      B.sk.entity(e)->radius = g.r;
      B.ent2prim[e] = g.src;
    }
    // H / V and on-axis
    for (auto* e : B.lines()) {
      Vec2 a = B.P(e->points[0]), b = B.P(e->points[1]);
      double L = distance(a, b);
      if (std::abs(a.x - b.x) <= std::sin(deg2rad(0.3)) * L) B.tryAdd(SkConstraintType::Vertical, {e->id});
      else if (std::abs(a.y - b.y) <= std::sin(deg2rad(0.3)) * L) B.tryAdd(SkConstraintType::Horizontal, {e->id});
    }
    for (auto pt : std::vector<SkPoint>(B.sk.points))
      if (pt.id != 0 && std::abs(pt.x) < tolP) B.tryAdd(SkConstraintType::VerticalPts, {0, pt.id});
  }
  // ---- dimensions (order: diameters, chamfers, axial lengths ascending, angles)
  std::vector<int> order(texts.size());
  std::iota(order.begin(), order.end(), 0);
  auto cls = [&](const TextInfo& t) { return t.dia > 0 ? 0 : t.chamfer > 0 ? 1 : t.lin > 0 ? 2 : 3; };
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    int ca = cls(texts[a]), cb = cls(texts[b]);
    if (ca != cb) return ca < cb;
    double va = texts[a].dia + texts[a].lin, vb = texts[b].dia + texts[b].lin;
    return va < vb;
  });
  int bound = 0, unbound = 0, reference = 0;
  std::vector<std::string> unboundTxt;
  std::set<int> angleBoundLines;   // (part<<20 | entity)
  for (int ti : order) {
    const TextInfo& T = texts[ti];
    const TextRun& tr = *T.t;
    std::string desc = "[D] \"" + tr.utf8 + "\" — " + viewName;
    bool any = false;
    int matchedPart = -1;   // geometry found but the constraint was redundant (DOF did not drop) -> reference
    double refValue = 0;
    std::string refBase;
    std::string name;
    auto ensureName = [&](const std::string& base) { if (name.empty()) name = reg.take(base); return name; };
    Vec2 tp = S.toPart(T.c);
    Vec2 lead;
    bool hasLead = leaderEnd(W, tr, lead);
    Vec2 hint = hasLead ? S.toPart(lead) : tp;
    if (T.dia > 0) {
      double r = T.dia / 2, tolR = std::max(tolP, 0.01 * r);
      for (size_t p = 0; p < bs.size(); ++p) {
        SB& B = bs[p];
        int pid = -1;
        double bestD = 1e300;
        for (auto* e : B.lines()) {
          Vec2 a = B.P(e->points[0]), b = B.P(e->points[1]);
          if (std::abs(a.x - r) < tolR && std::abs(b.x - r) < tolR) {
            double d = std::max({0.0, std::min(a.y, b.y) - hint.y, hint.y - std::max(a.y, b.y)});
            if (d < bestD) { bestD = d; pid = e->points[0]; }
          }
        }
        if (pid < 0)
          for (auto& q : B.sk.points)
            if (q.id != 0 && std::abs(q.x - r) < tolR) { double d = std::abs(q.y - hint.y); if (d < bestD) { bestD = d; pid = q.id; } }
        if (pid < 0) continue;
        if (matchedPart < 0) { matchedPart = (int)p; refValue = T.dia; refBase = "D_" + numTag(T.dia); }
        std::string n = ensureName("D_" + numTag(T.dia));
        B.vals[n] = T.dia;
        int cid = B.tryAdd(SkConstraintType::DistanceX, {0, pid}, n + "/2");
        if (cid < 0) { if (!B.paramNames.count(n)) B.vals.erase(n); continue; }
        B.c2text[cid] = tr.id;
        B.addParam(makeParam(n, T.dia, ParamKind::Length, ParamTag::FromDimension, desc + " (yarıçap = " + n + "/2)"));
        any = true;
      }
    } else if (T.chamfer > 0) {
      double c = T.chamfer, cr = c * std::tan(deg2rad(T.chamferAng));
      for (size_t p = 0; p < bs.size(); ++p) {
        SB& B = bs[p];
        for (auto* e : B.lines()) {
          Vec2 a = B.P(e->points[0]), b = B.P(e->points[1]);
          if (std::abs(std::abs(a.y - b.y) - c) > tolP || std::abs(std::abs(a.x - b.x) - cr) > tolP) continue;
          if (matchedPart < 0) { matchedPart = (int)p; refValue = c; refBase = "C_" + numTag(c); }
          std::string n = ensureName("C_" + numTag(c));
          B.vals[n] = c;
          int pa = e->points[0], pb = e->points[1];
          if (B.P(pa).y > B.P(pb).y) std::swap(pa, pb);
          int c1 = B.tryAdd(SkConstraintType::DistanceY, {pa, pb}, n);
          int qa = e->points[0], qb = e->points[1];
          if (B.P(qa).x > B.P(qb).x) std::swap(qa, qb);
          std::string ex = std::abs(T.chamferAng - 45) < 1e-6 ? n : n + "*" + fmt(std::tan(deg2rad(T.chamferAng)), 9);
          int c2 = B.tryAdd(SkConstraintType::DistanceX, {qa, qb}, ex);
          if (c1 < 0 && c2 < 0) { if (!B.paramNames.count(n)) B.vals.erase(n); continue; }
          if (c1 >= 0) B.c2text[c1] = tr.id;
          if (c2 >= 0) B.c2text[c2] = tr.id;
          B.addParam(makeParam(n, c, ParamKind::Length, ParamTag::FromDimension, desc + " (pah)"));
          any = true;
        }
      }
    } else if (T.lin > 0 && T.alongAxis) {
      double L = T.lin;
      double cs = dot(T.c - S.axisPoint, S.axisDir), cn = dot(T.c - S.axisPoint, S.sideNormal);
      std::vector<double> xs;
      for (auto& e : ext) if (std::max({0.0, e.n0 - cn, cn - e.n1}) < 3.0) xs.push_back(e.s);
      std::sort(xs.begin(), xs.end());
      xs.erase(std::unique(xs.begin(), xs.end(), [](double a, double b) { return std::abs(a - b) < 0.02; }), xs.end());
      double bestScore = 1e300, sa = 0, sb = 0;
      for (size_t i = 0; i < xs.size(); ++i)
        for (size_t j = i + 1; j < xs.size(); ++j) {
          double Lm = (xs[j] - xs[i]) * sc;
          if (std::abs(Lm - L) > std::max(0.02 * L, 0.04)) continue;
          double sco = std::max({0.0, xs[i] - cs, cs - xs[j]}) + std::abs(Lm - L) / sc;
          if (sco < bestScore) { bestScore = sco; sa = xs[i]; sb = xs[j]; }
        }
      if (bestScore < 1e299) {
        double va = (sa - S.axial0) * sc, vb = (sb - S.axial0) * sc;
        double tolA = std::max(0.03, 0.02 * sc);
        for (size_t p = 0; p < bs.size(); ++p) {
          SB& B = bs[p];
          int pa = -1, pb = -1;
          for (auto& q : B.sk.points) {
            if (std::abs(q.y - va) < tolA && (pa < 0 || q.id == 0)) pa = q.id;
            if (std::abs(q.y - vb) < tolA && pb < 0) pb = q.id;
          }
          if (pa < 0 || pb < 0 || pa == pb) continue;
          if (matchedPart < 0) { matchedPart = (int)p; refValue = L; refBase = "L_" + numTag(L); }
          std::string n = ensureName("L_" + numTag(L));
          B.vals[n] = L;
          int cid = B.tryAdd(SkConstraintType::DistanceY, {pa, pb}, n);
          if (cid < 0) { if (!B.paramNames.count(n)) B.vals.erase(n); continue; }
          B.c2text[cid] = tr.id;
          B.addParam(makeParam(n, L, ParamKind::Length, ParamTag::FromDimension, desc + " (eksenel)"));
          any = true;
        }
      }
    } else if (T.angle > 0) {
      double A = T.angle, half = A / 2;
      for (size_t p = 0; p < bs.size(); ++p) {
        SB& B = bs[p];
        // nearest unbound line at half-angle to the axis (sketch v axis)
        const SkEntity* best = nullptr;
        double bd = 1e300;
        const SkEntity* ref = nullptr;
        double refL = 0;
        for (auto* e : B.lines()) {
          Vec2 a = B.P(e->points[0]), b = B.P(e->points[1]);
          Vec2 d = b - a;
          double L = length(d);
          if (std::abs(d.x) < std::sin(deg2rad(0.3)) * L && L > refL) { refL = L; ref = e; }
          double tilt = rad2deg(std::atan2(std::abs(d.x), std::abs(d.y)));   // angle to the axis
          if (std::abs(tilt - half) > 1.5 || angleBoundLines.count((int)p << 20 | e->id)) continue;
          double dd = distPointSeg(hint, a, b);
          if (dd < bd) { bd = dd; best = e; }
        }
        if (!best || !ref) continue;
        Vec2 r0 = B.P(ref->points[1]) - B.P(ref->points[0]), c0 = B.P(best->points[1]) - B.P(best->points[0]);
        double th = rad2deg(std::atan2(cross(r0, c0), dot(r0, c0)));
        std::string n = ensureName("A_" + numTag(A));
        std::string ex;
        for (auto& [val, e] : std::vector<std::pair<double, std::string>>{{half, n + "/2"}, {-half, "-" + n + "/2"}, {180 - half, "180-" + n + "/2"}, {half - 180, n + "/2-180"}})
          if (std::abs(th - val) < 2.0) ex = e;
        if (ex.empty()) continue;
        if (matchedPart < 0) { matchedPart = (int)p; refValue = A; refBase = "A_" + numTag(A); }
        B.vals[n] = A;
        int cid = B.tryAdd(SkConstraintType::Angle, {ref->id, best->id}, ex);
        if (cid < 0) { if (!B.paramNames.count(n)) B.vals.erase(n); continue; }
        angleBoundLines.insert((int)p << 20 | best->id);
        B.c2text[cid] = tr.id;
        B.addParam(makeParam(n, A, ParamKind::Angle, ParamTag::FromDimension, desc + " (dahil açı; koni yarı açısı = " + n + "/2)"));
        // parallel walls (thin cones such as a flare): keep them parallel to the bound line
        for (auto* e : B.lines()) {
          if (e->id == best->id) continue;
          Vec2 d = B.P(e->points[1]) - B.P(e->points[0]);
          if (std::abs(cross(unit(d), unit(c0))) < std::sin(deg2rad(0.5))) B.tryAdd(SkConstraintType::Parallel, {best->id, e->id});
        }
        any = true;
      }
    }
    if (any) ++bound;
    else if (matchedPart >= 0) {   // consistent with the geometry but redundant: kept as a checked reference value
      ++reference;
      std::string n = ensureName(refBase);
      bs[matchedPart].addParam(makeParam(n, refValue, refBase[0] == 'A' ? ParamKind::Angle : ParamKind::Length, ParamTag::Derived,
                                         "[T] fazlalık/referans ölçü (diğer ölçülerden çıkar): " + desc));
      bs[matchedPart].refText[n] = tr.id;
      bs[matchedPart].params.back().param.exposed = false;
    } else if (int slotPart = -2; T.lin > 0 && T.alongAxis && [&] {   // axial depth / band height of an R3 slot family
                 for (auto& V : TR.views)
                   for (auto& F : V.families)
                     if (F.kind == "slot" && has(F.height) && std::abs(F.height - T.lin) < 1e-6) { slotPart = V.partNo - 1; return true; }
                 return false;
               }()) {
      ++reference;
      std::string n = reg.take("L_" + numTag(T.lin));
      SB* owner = slotPart >= 0 && slotPart < (int)bs.size() ? &bs[slotPart] : bs.empty() ? nullptr : &bs.front();
      if (owner) {
        owner->refText[n] = tr.id;
        owner->addParam(makeParam(n, T.lin, ParamKind::Length, ParamTag::Derived,
                                     "[T] kesim özelliğinin (R3 yarık) eksenel derinliği; kesitte gösterilen: " + desc));
        owner->params.back().param.exposed = false;
      }
    } else if (T.dia > 0 || T.lin > 0 || T.chamfer > 0 || T.angle > 0) {
      ++unbound;
      unboundTxt.push_back(tr.utf8);
    }
  }
  // ---- diameters of the transverse views / notes (Ø24 B-B, Ø34 C-C, Ø14 THROAT): bound where they still remove a DOF,
  // otherwise kept as checked reference values (never measured [Ö] when the sheet dimensions them somewhere)
  int crossBound = 0, crossRef = 0;
  {
    std::set<double> done;
    for (auto& T : texts) if (T.dia > 0) done.insert(T.dia);
    for (auto& X : xdias) {
      if (done.count(X.d)) continue;
      done.insert(X.d);
      const double r = X.d / 2, tolR = std::max(tolP, 0.01 * r);
      const std::string desc = "[D] \"" + (X.t ? X.t->utf8 : "Ø" + fmt(X.d, 4)) + "\" — " + X.src + " (kesit dışı)";
      std::string name;
      bool any = false;
      int refPart = -1;
      for (size_t p = 0; p < bs.size(); ++p) {
        SB& B = bs[p];
        int pid = -1;
        double bestD = 1e300;
        for (auto* e : B.lines()) {
          Vec2 a = B.P(e->points[0]), b = B.P(e->points[1]);
          if (std::abs(a.x - r) >= tolR || std::abs(b.x - r) >= tolR) continue;
          double d = has(X.y) ? std::max({0.0, std::min(a.y, b.y) - X.y, X.y - std::max(a.y, b.y)}) : 0;
          if (d < bestD) { bestD = d; pid = e->points[0]; }
        }
        if (pid < 0)
          for (auto& q : B.sk.points)
            if (q.id != 0 && std::abs(q.x - r) < tolR) { double d = has(X.y) ? std::abs(q.y - X.y) : 0; if (d < bestD) { bestD = d; pid = q.id; } }
        if (pid < 0) continue;
        if (refPart < 0) refPart = (int)p;
        if (name.empty()) name = reg.take("D_" + numTag(X.d));
        B.vals[name] = X.d;
        int cid = B.tryAdd(SkConstraintType::DistanceX, {0, pid}, name + "/2");
        if (cid < 0) { if (!B.paramNames.count(name)) B.vals.erase(name); continue; }
        if (X.t) B.c2text[cid] = X.t->id;
        B.addParam(makeParam(name, X.d, ParamKind::Length, ParamTag::FromDimension, desc + " (yarıçap = " + name + "/2)"));
        any = true;
      }
      if (any) ++crossBound;
      else if (refPart >= 0) {
        ++crossRef;
        bs[refPart].addParam(makeParam(name, X.d, ParamKind::Length, ParamTag::Derived, "[T] fazlalık/referans ölçü (diğer ölçülerden çıkar): " + desc));
        if (X.t) bs[refPart].refText[name] = X.t->id;
        bs[refPart].params.back().param.exposed = false;
      }
    }
  }
  // ---- close DOF with measured values, solve, emit candidates
  int added = 0;
  for (size_t p = 0; p < bs.size(); ++p) {
    SB& B = bs[p];
    const AxisymPart& P = S.parts[p];
    if (P.segs.size() > kMaxEntities || B.sk.entities.empty()) {
      res.warn("r2.section.tooComplex", P.name + ": profil " + std::to_string(P.segs.size()) + " kenar — eksenel simetrik parça profili sayılmadı", {}, (int)Stage::Sketch);
      continue;
    }
    int dof0 = B.dof();
    if (opt.closeDofWithMeasured && B.dof() > 0) {
      std::vector<SkPoint> pts(B.sk.points.begin(), B.sk.points.end());
      std::sort(pts.begin(), pts.end(), [](auto& a, auto& b) { return a.y != b.y ? a.y < b.y : a.x < b.x; });
      for (auto& pt : pts) {
        if (pt.id == 0) continue;
        for (int axis = 0; axis < 2 && B.dof() > 0; ++axis) {
          double v = axis == 0 ? pt.x : pt.y;
          double rv = opt.measuredRounding > 0 ? std::round(v / opt.measuredRounding) * opt.measuredRounding : v;
          if (std::abs(rv) < 1e-9) {
            int id = B.tryAdd(axis == 0 ? SkConstraintType::VerticalPts : SkConstraintType::HorizontalPts, {0, pt.id});
            if (id >= 0) B.measured.push_back(id);
            continue;
          }
          std::string n = reg.take(std::string(axis == 0 ? "M_R" : "M_X") + numTag(std::abs(rv)));
          B.vals[n] = std::abs(rv);
          int id = B.tryAdd(axis == 0 ? SkConstraintType::DistanceX : SkConstraintType::DistanceY, {0, pt.id}, rv >= 0 ? n : "-" + n);
          if (id < 0) { B.vals.erase(n); reg.used.erase(n); continue; }
          B.measured.push_back(id);
          B.addParam(makeParam(n, std::abs(rv), ParamKind::Length, ParamTag::Measured,
                               std::string("[Ö] geometriden ölçüldü (") + (axis == 0 ? "yarıçap" : "eksenel konum") + "), " + P.name));
        }
      }
    }
    SketchCandidate c;
    c.id = ir.newId();
    c.viewId = viewId;
    c.purpose = kPurpose;
    c.plane = SketchPlaneKind::XZ;
    Sketch solved = B.sk;
    sketch::SolveResult r = sketch::solve(solved, B.vals);
    c.dof = r.dof;
    c.conflicting = r.conflicting;
    double maxDev = 0;
    if (r.converged) {
      for (auto& [pid, o] : B.orig) if (auto* q = solved.point(pid)) maxDev = std::max(maxDev, std::hypot(q->x - o.x, q->y - o.y));
      B.sk = solved;
    }
    c.maxGeomDeviation = maxDev;
    c.sketch = B.sk;
    c.params = B.params;
    c.measuredConstraints = B.measured;
    c.entityToPrimitive = B.ent2prim;
    c.profileClosed = !P.loops.empty();
    c.axisEntity = -1;
    c.conf = float(std::clamp(0.5 + 0.5 * S.mirrorIoU, 0.0, 1.0));
    for (auto& [cid, tid] : B.c2text) c.attrs["r2.constraintText." + std::to_string(cid)] = std::to_string(tid);
    for (auto& [n, tid] : B.refText) c.attrs["r2.referenceText." + n] = std::to_string(tid);
    c.attrs["interp.strategy"] = "SectionRevolve";
    c.attrs["interp.dof0"] = std::to_string(dof0);
    c.attrs["interp.solveStatus"] = r.message;
    c.attrs["interp.converged"] = r.converged ? "1" : "0";
    c.attrs["r2.part"] = std::to_string(p);
    c.attrs["r2.partCount"] = std::to_string(S.parts.size());
    c.attrs["r2.partName"] = P.name;
    c.attrs["r2.side"] = std::to_string(P.side);
    c.attrs["r2.mirrorIoU"] = fmt(P.mirrorIoU, 4);
    c.attrs["r2.hatch"] = fmt(P.family.angleDeg, 2) + "deg/" + fmt(P.family.pitch * sc, 4) + "mm(" + std::to_string(P.family.lines) + ")";
    c.attrs["r2.pappusVolume"] = fmt(P.pappusVolume, 6);
    c.attrs["r2.area"] = fmt(P.area[P.side], 6);
    S.parts[p].sketchId = c.id;
    ir.sketches.push_back(std::move(c));
    ++added;
  }
  ir.attrs["r2.section"] = "bulundu";
  ir.attrs["r2.section.parts"] = std::to_string(S.parts.size());
  ir.attrs["r2.section.scale"] = fmt(S.scale, 6);
  ir.attrs["r2.section.scaleEvidence"] = S.scaleEvidence;
  ir.attrs["r2.section.axis"] = std::string(S.horizontal ? "h" : "v") + ":" + std::to_string(S.axisPrim);
  ir.attrs["r2.section.mirrorIoU"] = fmt(S.mirrorIoU, 4);
  ir.attrs["r2.section.hatchLines"] = std::to_string(S.hatchLineIds.size());
  ir.attrs["r2.section.dimsBound"] = std::to_string(bound);
  ir.attrs["r2.section.dimsUnbound"] = std::to_string(unbound);
  ir.attrs["r2.section.dimsReference"] = std::to_string(reference);
  ir.attrs["r2.section.crossViewBound"] = std::to_string(crossBound);
  ir.attrs["r2.section.crossViewReference"] = std::to_string(crossRef);
  std::string ub;
  for (auto& s : unboundTxt) ub += (ub.empty() ? "" : " | ") + s;
  if (!ub.empty()) ir.attrs["r2.section.unboundTexts"] = ub;
  for (auto& n : S.notes) res.warn("r2.section.note", n, {}, (int)Stage::Sketch);
  if (!ub.empty()) res.warn("r2.section.unbound", "Kesit ölçüleri sketch'e bağlanamadı: " + ub, {}, (int)Stage::Sketch);
  return added;
}

}  // namespace pc::drawing::r2
