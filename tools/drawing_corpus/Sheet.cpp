// HLR views, first-angle layout, centre lines, ISO dimensions and title block -> GroundTruth. OWNER WP-1.
#include "Corpus.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace corpus {
namespace {
const double kPi = 3.14159265358979323846;
double deg(double r) { return r * 180 / kPi; }
double n360(double a) { a = std::fmod(a, 360); return a < 0 ? a + 360 : a; }

// view frames: Front (X,Z) eye at -Y; Top (X,Y) eye at +Z; Left (-Y,Z) eye at -X
gp_Ax2 viewAx(ViewRole v) {
  if (v == ViewRole::Top) return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  if (v == ViewRole::Left) return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(-1, 0, 0), gp_Dir(0, -1, 0));
  return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
}
int viewDirAxis(ViewRole v) { return v == ViewRole::Top ? 2 : v == ViewRole::Left ? 0 : 1; }
Vec2 proj(ViewRole v, const double p[3]) {
  if (v == ViewRole::Top) return {p[0], p[1]};
  if (v == ViewRole::Left) return {-p[1], p[2]};
  return {p[0], p[2]};
}

struct VPrim { Primitive p; bool hidden = false; };

void addEdge(const TopoDS_Edge& e, bool hidden, std::vector<VPrim>& out) {
  BRepAdaptor_Curve c(e);
  const double u0 = c.FirstParameter(), u1 = c.LastParameter();
  VPrim v; v.hidden = hidden;
  if (c.GetType() == GeomAbs_Line) {
    gp_Pnt a = c.Value(u0), b = c.Value(u1);
    v.p.kind = PrimKind::Line; v.p.p0 = {a.X(), a.Y()}; v.p.p1 = {b.X(), b.Y()};
    if (distance(v.p.p0, v.p.p1) < 1e-6) return;
    out.push_back(v); return;
  }
  if (c.GetType() == GeomAbs_Circle) {
    gp_Circ ci = c.Circle();
    const Vec2 ctr{ci.Location().X(), ci.Location().Y()};
    v.p.center = ctr; v.p.radius = ci.Radius();
    if (std::abs(u1 - u0 - 2 * kPi) < 1e-7) { v.p.kind = PrimKind::Circle; v.p.a0 = 0; v.p.a1 = 360; out.push_back(v); return; }
    auto ang = [&](double u) { gp_Pnt q = c.Value(u); return n360(deg(std::atan2(q.Y() - ctr.y, q.X() - ctr.x))); };
    double as = ang(u0), ae = ang(u1), am = ang((u0 + u1) / 2);
    auto inCcw = [](double s, double e, double m) { return n360(m - s) < n360(e - s); };
    if (!inCcw(as, ae, am)) std::swap(as, ae);
    v.p.kind = PrimKind::Arc; v.p.a0 = as; v.p.a1 = as + n360(ae - as);
    if (v.p.a1 - v.p.a0 < 1e-6) return;
    out.push_back(v); return;
  }
  GCPnts_QuasiUniformDeflection d(c, 0.005);
  if (!d.IsDone() || d.NbPoints() < 2) return;
  v.p.kind = PrimKind::Polyline;
  for (int i = 1; i <= d.NbPoints(); ++i) v.p.pts.push_back({d.Value(i).X(), d.Value(i).Y()});
  // edge-on circles / ellipses come back as degenerate curves: collinear points -> one line (extreme points)
  Vec2 a = v.p.pts.front(), b = a; double far = 0;
  for (auto& q : v.p.pts) if (distance(q, a) > far) { far = distance(q, a); b = q; }
  if (far < 1e-6) return;
  Vec2 lo = a, hi = b; double tlo = 0, thi = far; bool straight = true;
  const Vec2 u = (b - a) * (1 / far);
  for (auto& q : v.p.pts) {
    if (std::abs(cross(u, q - a)) > 1e-5) { straight = false; break; }
    const double t = dot(q - a, u);
    if (t < tlo) { tlo = t; lo = q; }
    if (t > thi) { thi = t; hi = q; }
  }
  if (straight) { v.p.kind = PrimKind::Line; v.p.p0 = lo; v.p.p1 = hi; v.p.pts.clear(); }
  out.push_back(v);
}

// merge collinear touching lines and co-circular touching arcs of the same visibility
void mergePieces(std::vector<VPrim>& v) {
  bool changed = true;
  while (changed) {
    changed = false;
    for (std::size_t i = 0; i < v.size() && !changed; ++i)
      for (std::size_t j = i + 1; j < v.size() && !changed; ++j) {
        Primitive &a = v[i].p, &b = v[j].p;
        if (v[i].hidden != v[j].hidden || a.kind != b.kind) continue;
        if (a.kind == PrimKind::Line) {
          const Vec2 d = a.p1 - a.p0; const double L = length(d);
          const Vec2 u = d * (1 / L), n{-u.y, u.x};
          if (std::abs(dot(b.p0 - a.p0, n)) > 1e-6 || std::abs(dot(b.p1 - a.p0, n)) > 1e-6) continue;
          double t[4] = {0, L, dot(b.p0 - a.p0, u), dot(b.p1 - a.p0, u)};
          const double blo = std::min(t[2], t[3]), bhi = std::max(t[2], t[3]);
          if (blo > L + 1e-6 || bhi < -1e-6) continue;
          const double lo = std::min(0.0, blo), hi = std::max(L, bhi);
          const Vec2 o = a.p0;
          a.p0 = o + u * lo; a.p1 = o + u * hi;
          v.erase(v.begin() + static_cast<long>(j)); changed = true;
        } else if (a.kind == PrimKind::Arc) {
          if (distance(a.center, b.center) > 1e-6 || std::abs(a.radius - b.radius) > 1e-6) continue;
          auto meet = [](double x, double y) { const double d = std::abs(n360(x) - n360(y)); return d < 1e-6 || d > 360 - 1e-6; };
          const double span = (a.a1 - a.a0) + (b.a1 - b.a0);
          if (meet(a.a1, b.a0)) a.a1 = a.a0 + span;
          else if (meet(b.a1, a.a0)) { a.a0 = b.a0; a.a1 = b.a0 + span; }
          else continue;
          if (a.a1 - a.a0 >= 360 - 1e-6) { a.kind = PrimKind::Circle; a.a0 = 0; a.a1 = 360; }
          v.erase(v.begin() + static_cast<long>(j)); changed = true;
        }
      }
  }
  // drop hidden pieces that coincide with visible ones (HLR outline duplicates)
  auto same = [](const Primitive& a, const Primitive& b) {
    if (a.kind != b.kind) return false;
    if (a.kind == PrimKind::Line)
      return (distance(a.p0, b.p0) < 1e-6 && distance(a.p1, b.p1) < 1e-6) || (distance(a.p0, b.p1) < 1e-6 && distance(a.p1, b.p0) < 1e-6);
    if (a.kind == PrimKind::Circle || a.kind == PrimKind::Arc)
      return distance(a.center, b.center) < 1e-6 && std::abs(a.radius - b.radius) < 1e-6 && std::abs(a.a0 - b.a0) < 1e-6 && std::abs(a.a1 - b.a1) < 1e-6;
    return false;
  };
  for (std::size_t i = 0; i < v.size(); ++i)
    for (std::size_t j = v.size(); j-- > i + 1;)
      if (same(v[i].p, v[j].p)) v.erase(v.begin() + static_cast<long>(j));
  // hidden lines lying on a visible line are not drawn
  auto onLine = [](const Primitive& L, Vec2 q) {
    const Vec2 e = L.p1 - L.p0; const double len = length(e), t = dot(q - L.p0, e) / (len * len);
    return t > -1e-6 && t < 1 + 1e-6 && std::abs(cross(e, q - L.p0)) / len < 1e-6;
  };
  for (std::size_t j = v.size(); j-- > 0;) {
    if (!v[j].hidden || v[j].p.kind != PrimKind::Line) continue;
    for (auto& q : v)
      if (!q.hidden && q.p.kind == PrimKind::Line && onLine(q.p, v[j].p.p0) && onLine(q.p, v[j].p.p1)) { v.erase(v.begin() + static_cast<long>(j)); break; }
  }
}

std::vector<VPrim> hlr(const TopoDS_Shape& s, ViewRole role) {
  Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
  algo->Add(s);
  algo->Projector(HLRAlgo_Projector(viewAx(role)));
  algo->Update();
  algo->Hide();
  HLRBRep_HLRToShape h(algo);
  std::vector<VPrim> out;
  auto take = [&](const TopoDS_Shape& c, bool hidden) {
    if (c.IsNull()) return;
    for (TopExp_Explorer ex(c, TopAbs_EDGE); ex.More(); ex.Next()) addEdge(TopoDS::Edge(ex.Current()), hidden, out);
  };
  take(h.VCompound(), false); take(h.OutLineVCompound(), false);
  take(h.HCompound(), true); take(h.OutLineHCompound(), true);
  mergePieces(out);
  return out;
}

void expandPrim(Box2& b, const Primitive& p) {
  if (p.kind == PrimKind::Line) { expand(b, p.p0); expand(b, p.p1); }
  else if (p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) {
    expand(b, p.center + Vec2{p.radius, p.radius}); expand(b, p.center - Vec2{p.radius, p.radius});
  } else for (auto& q : p.pts) expand(b, q);
}

struct Builder {
  GroundTruth& gt;
  int nextId = 1;
  int add(Primitive p, LineRole role) { p.id = nextId++; p.prov.source = SourceKind::VectorPdf; gt.sheet.prims.push_back(p); gt.roles[p.id] = role; return p.id; }
  int line(Vec2 a, Vec2 b, double w, LineRole role, const std::vector<double>& dash = {}) {
    Primitive p; p.kind = PrimKind::Line; p.p0 = a; p.p1 = b; p.style.width = w; p.style.dash = dash; return add(p, role);
  }
  int arrow(Vec2 tip, Vec2 dir) {   // dir: unit vector from tip toward the base
    Primitive p; p.kind = PrimKind::Arrowhead; p.p0 = tip; p.p1 = tip + dir * kArrowLen; p.style.filled = true; p.style.stroked = false;
    return add(p, LineRole::Dimension);
  }
  int text(const std::string& s, Vec2 o, double h, double rot) {
    TextRun t; t.id = nextId++; t.utf8 = s; t.origin = o; t.height = h; t.rotDeg = rot; t.font = fontFamily();
    const double w = textAdvanceMm(s, h), c = std::cos(rot * kPi / 180), sn = std::sin(rot * kPi / 180);
    const Vec2 u{c, sn}, n{-sn, c};
    t.quad = {o, o + u * w, o + u * w + n * h, o + n * h};
    gt.sheet.texts.push_back(t); return t.id;
  }
};
}  // namespace

GroundTruth buildSheet(const Part& part, int index) {
  GroundTruth gt;
  gt.family = part.family; gt.caseName = part.name;
  gt.sheet.source = SourceKind::VectorPdf; gt.sheet.sheet = {{0, 0}, {420, 297}}; gt.sheet.unitToMm = 1;
  gt.projection = ProjectionMethod::FirstAngle;
  gt.expectedStrategy = part.strategy;
  gt.params = part.params;
  { GProp_GProps g; BRepGProp::VolumeProperties(part.shape, g); gt.volume = g.Mass(); }

  // 1. HLR per view (part mm)
  std::vector<std::vector<VPrim>> vp;
  std::vector<Box2> vb;
  for (ViewRole r : part.views) {
    vp.push_back(hlr(part.shape, r));
    Box2 b = emptyBox();
    for (auto& p : vp.back()) expandPrim(b, p.p);
    if (isEmpty(b)) throw std::runtime_error("HLR produced no edges");
    vb.push_back(b);
  }
  // 2. scale + first-angle layout: Front top-left, Top below Front, Left right of Front
  const double avW = 380, avH = 205, gap = 45;
  auto idxOf = [&](ViewRole r) { for (std::size_t i = 0; i < part.views.size(); ++i) if (part.views[i] == r) return static_cast<int>(i); return -1; };
  const int iF = idxOf(ViewRole::Front), iT = idxOf(ViewRole::Top), iL = idxOf(ViewRole::Left);
  const double needW = width(vb[iF]) + (iL >= 0 ? width(vb[iL]) : 0), needH = height(vb[iF]) + (iT >= 0 ? height(vb[iT]) : 0);
  static const std::pair<int, int> scales[] = {{5, 1}, {2, 1}, {1, 1}, {1, 2}, {1, 5}};
  double s = 0;
  for (auto [num, den] : scales) {
    const double f = static_cast<double>(num) / den;
    if (needW * f + gap * (iL >= 0 ? 2 : 1) + 20 <= avW && needH * f + gap * (iT >= 0 ? 2 : 1) + 10 <= avH) {
      gt.scale.num = num; gt.scale.den = den; s = f; break;
    }
  }
  if (s == 0) throw std::runtime_error("part does not fit on A3");
  gt.scale.conf = 1;
  gt.scale.evidence = "antet: ÖLÇEK " + std::to_string(static_cast<int>(gt.scale.num)) + ":" + std::to_string(static_cast<int>(gt.scale.den));
  std::vector<Vec2> org(part.views.size());
  const double left = 20 + gap * 0.6, top = 287 - gap * 0.6;
  org[iF] = {left - s * vb[iF].lo.x, top - s * vb[iF].hi.y};
  const double fBottom = top - s * height(vb[iF]), fRight = left + s * width(vb[iF]);
  if (iT >= 0) org[iT] = {org[iF].x, fBottom - gap - s * vb[iT].hi.y};
  if (iL >= 0) org[iL] = {fRight + gap - s * vb[iL].lo.x, org[iF].y};
  auto S = [&](int v, Vec2 p) { return org[v] + p * s; };

  Builder B{gt};
  std::vector<Box2> sheetBox(part.views.size());
  // 3. geometry
  for (std::size_t v = 0; v < part.views.size(); ++v) {
    Box2 sb = emptyBox();
    for (auto& q : vp[v]) {
      Primitive p = q.p;
      p.p0 = S(v, p.p0); p.p1 = S(v, p.p1); p.center = S(v, p.center); p.radius *= s;
      for (auto& x : p.pts) x = S(v, x);
      p.style.width = q.hidden ? kThinW : kVisW;
      if (q.hidden) p.style.dash = kHiddenDash;
      p.attrs["corpus.view"] = std::to_string(v);
      B.add(p, q.hidden ? LineRole::Hidden : LineRole::Visible);
      expandPrim(sb, p);
    }
    sheetBox[v] = sb;
    GtView gv; gv.role = part.views[v]; gv.box = {sb.lo - Vec2{3, 3}, sb.hi + Vec2{3, 3}};
    gv.label = toString(part.views[v]);
    gv.sheetToView = {1 / s, 0, 0, 1 / s, -org[v].x / s, -org[v].y / s};
    gt.views.push_back(gv);
  }
  // 4. centre lines
  for (std::size_t v = 0; v < part.views.size(); ++v) {
    const ViewRole r = part.views[v];
    std::vector<std::pair<Vec2, Vec2>> lines;   // axis-parallel centre lines, merged when collinear + overlapping
    auto cl = [&](Vec2 a, Vec2 b) {
      const bool hz = std::abs(a.y - b.y) < 1e-9;
      if (hz && a.x > b.x) std::swap(a, b);
      if (!hz && a.y > b.y) std::swap(a, b);
      for (auto& [x, y] : lines) {
        const bool lh = std::abs(x.y - y.y) < 1e-9;
        if (lh != hz) continue;
        if (hz && std::abs(x.y - a.y) < 1e-6 && a.x <= y.x + 1e-6 && b.x >= x.x - 1e-6) { x.x = std::min(x.x, a.x); y.x = std::max(y.x, b.x); return; }
        if (!hz && std::abs(x.x - a.x) < 1e-6 && a.y <= y.y + 1e-6 && b.y >= x.y - 1e-6) { x.y = std::min(x.y, a.y); y.y = std::max(y.y, b.y); return; }
      }
      lines.push_back({a, b});
    };
    for (auto& c : part.cyls) {
      const Vec2 ctr = S(v, proj(r, c.c));
      if (c.axis == viewDirAxis(r)) {
        const double h = c.r * s + 3;
        cl(ctr - Vec2{h, 0}, ctr + Vec2{h, 0}); cl(ctr - Vec2{0, h}, ctr + Vec2{0, h});
      } else {
        double p0[3] = {c.c[0], c.c[1], c.c[2]}, p1[3] = {c.c[0], c.c[1], c.c[2]};
        p0[c.axis] += c.t0; p1[c.axis] += c.t1;
        Vec2 a = S(v, proj(r, p0)), b = S(v, proj(r, p1));
        const Vec2 u = (b - a) * (1 / distance(a, b));
        cl(a - u * 3, b + u * 3);
      }
    }
    for (auto& [a, b] : lines) B.line(a, b, kThinW, LineRole::Center, kCenterDash);
  }
  // 5. dimensions
  for (auto& d : part.dims) {
    const int v = d.view, firstId = B.nextId;
    std::string def;   // DXF DIMENSION recipe for the DXF writer: "type;angle;x10,y10;x13,y13;x14,y14;x15,y15"
    auto P2 = [](Vec2 p) { char b[64]; std::snprintf(b, sizeof b, "%.6f,%.6f", p.x, p.y); return std::string(b); };
    GtDimension g; g.kind = d.kind; g.nominal = d.nominal; g.tolPlus = d.tolPlus; g.tolMinus = d.tolMinus; g.viewIndex = v;
    g.paramName = d.param; g.driving = d.driving; g.text = d.text;
    const double tw = textAdvanceMm(d.text, kTextH);
    auto bind = [&](Vec2 pt, Vec2 dimDir, bool radialCircle, double r) {
      for (auto& p : gt.sheet.prims) {
        auto it = p.attrs.find("corpus.view");
        if (it == p.attrs.end() || it->second != std::to_string(v)) continue;
        if (p.kind == PrimKind::Line && !radialCircle) {
          const Vec2 e = p.p1 - p.p0; const double L = length(e);
          const double t = dot(pt - p.p0, e) / (L * L);
          if (t < -1e-6 || t > 1 + 1e-6 || std::abs(cross(e, pt - p.p0)) / L > 0.01) continue;
          if (std::abs(dot(e * (1 / L), dimDir)) > 0.05) continue;
          g.geomPrimIds.push_back(p.id);
        } else if (p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) {
          if (distance(p.center, pt) < 0.01 && (!radialCircle || std::abs(p.radius - r) < 0.01)) g.geomPrimIds.push_back(p.id);
        }
      }
    };
    if (d.type == DimType::Linear) {
      Vec2 A = S(v, d.a), Bp = S(v, d.b);
      const Box2& vbx = sheetBox[v];
      if (d.horiz) {
        const double y = d.inside ? A.y : d.offset > 0 ? vbx.hi.y + d.offset : vbx.lo.y + d.offset;
        for (Vec2 P : {A, Bp}) {
          const double sg = y > P.y ? 1 : -1;
          if (std::abs(y - P.y) > 1.5) B.line({P.x, P.y + sg * 1}, {P.x, y + sg * 2}, kThinW, LineRole::Extension);
        }
        const double x0 = std::min(A.x, Bp.x), x1 = std::max(A.x, Bp.x);
        const bool out = x1 - x0 < 2 * kArrowLen + 1;
        B.line({out ? x0 - 2 * kArrowLen : x0, y}, {out ? x1 + 2 * kArrowLen : x1, y}, kThinW, LineRole::Dimension);
        B.arrow({x0, y}, {out ? -1.0 : 1.0, 0}); B.arrow({x1, y}, {out ? 1.0 : -1.0, 0});
        const bool fits = x1 - x0 > tw + 2;
        // text that does not fit between the arrows goes above the dimension line (stacked, no collision with chains)
        g.textId = B.text(d.text, {(x0 + x1) / 2 - tw / 2, fits ? y + 1 : y + 1 + kTextH + 2}, kTextH, 0);
        bind(A, {1, 0}, false, 0); bind(Bp, {1, 0}, false, 0);
        def = "0;0;" + P2({Bp.x, y}) + ";" + P2(A) + ";" + P2(Bp) + ";0,0";
      } else {
        const double x = d.inside ? A.x : d.offset > 0 ? vbx.hi.x + d.offset : vbx.lo.x + d.offset;
        for (Vec2 P : {A, Bp}) {
          const double sg = x > P.x ? 1 : -1;
          if (std::abs(x - P.x) > 1.5) B.line({P.x + sg * 1, P.y}, {x + sg * 2, P.y}, kThinW, LineRole::Extension);
        }
        const double y0 = std::min(A.y, Bp.y), y1 = std::max(A.y, Bp.y);
        const bool out = y1 - y0 < 2 * kArrowLen + 1;
        B.line({x, out ? y0 - 2 * kArrowLen : y0}, {x, out ? y1 + 2 * kArrowLen : y1}, kThinW, LineRole::Dimension);
        B.arrow({x, y0}, {0, out ? -1.0 : 1.0}); B.arrow({x, y1}, {0, out ? 1.0 : -1.0});
        const bool fits = y1 - y0 > tw + 2;
        g.textId = B.text(d.text, {fits ? x - 1 : x - 1 - kTextH - 2, (y0 + y1) / 2 - tw / 2}, kTextH, 90);
        bind(A, {0, 1}, false, 0); bind(Bp, {0, 1}, false, 0);
        def = "0;90;" + P2({x, Bp.y}) + ";" + P2(A) + ";" + P2(Bp) + ";0,0";
      }
    } else {
      const Vec2 C = S(v, d.c);
      const double r = d.r * s, a = d.angDeg * kPi / 180;
      const Vec2 u{std::cos(a), std::sin(a)};
      const Vec2 P = C + u * r, Q = P + u * (kArrowLen + 9);
      B.arrow(P, u);
      B.line(P, Q, kThinW, LineRole::Leader);
      const double dir = u.x >= 0 ? 1 : -1;
      const Vec2 R = Q + Vec2{dir * (tw + 2), 0};
      B.line(Q, R, kThinW, LineRole::Leader);
      g.textId = B.text(d.text, {dir > 0 ? Q.x + 1 : R.x + 1, Q.y + 1}, kTextH, 0);
      if (d.type == DimType::Radial) {
        bind(C, {0, 0}, true, r);
        def = d.kind == DimKind::Radius ? "4;0;" + P2(C) + ";0,0;0,0;" + P2(P) : "3;0;" + P2(C - u * r) + ";0,0;0,0;" + P2(P);
      }
      else {   // chamfer leader: lines through the point
        for (auto& p : gt.sheet.prims)
          if (p.kind == PrimKind::Line && gt.roles[p.id] == LineRole::Visible && std::abs(cross(p.p1 - p.p0, P - p.p0)) / length(p.p1 - p.p0) < 0.01 &&
              dot(P - p.p0, p.p1 - p.p0) > 0 && dot(P - p.p1, p.p0 - p.p1) > 0)
            g.geomPrimIds.push_back(p.id);
      }
    }
    std::string ids;
    for (auto& p : gt.sheet.prims) if (p.id >= firstId) ids += (ids.empty() ? "" : ",") + std::to_string(p.id);
    for (auto& t : gt.sheet.texts)
      if (t.id == g.textId) { t.attrs["corpus.dimPrims"] = ids; if (!def.empty()) t.attrs["corpus.dxfDim"] = def; }
    gt.dims.push_back(g);
  }
  // 6. border + title block
  B.line({10, 10}, {410, 10}, 0.7, LineRole::Border); B.line({410, 10}, {410, 287}, 0.7, LineRole::Border);
  B.line({410, 287}, {10, 287}, 0.7, LineRole::Border); B.line({10, 287}, {10, 10}, 0.7, LineRole::Border);
  B.line({230, 10}, {230, 50}, 0.5, LineRole::TitleBlock); B.line({230, 50}, {410, 50}, 0.5, LineRole::TitleBlock);
  B.line({230, 30}, {410, 30}, 0.35, LineRole::TitleBlock); B.line({320, 10}, {320, 50}, 0.35, LineRole::TitleBlock);
  B.text("PARÇA: " + part.name, {234, 37}, kTextH, 0);
  B.text("MALZEME: " + part.material, {234, 17}, kTextH, 0);
  B.text("ÖLÇEK " + std::to_string(static_cast<int>(gt.scale.num)) + ":" + std::to_string(static_cast<int>(gt.scale.den)), {324, 37}, kTextH, 0);
  char no[64]; std::snprintf(no, sizeof no, "RESİM NO: PC-%s-%02d", part.family.c_str(), index);
  B.text(no, {324, 17}, 2.5, 0);
  for (auto& p : gt.sheet.prims) p.attrs.erase("corpus.view");
  // overlapping collinear annotation lines of identical style (two extension lines from one point) are one line in S2
  {
    auto& P = gt.sheet.prims;
    for (std::size_t i = 0; i < P.size(); ++i)
      for (std::size_t j = P.size(); j-- > i + 1;) {
        Primitive &a = P[i], &b = P[j];
        if (a.kind != PrimKind::Line || b.kind != PrimKind::Line || gt.roles[a.id] != gt.roles[b.id] || a.style.dash != b.style.dash) continue;
        const Vec2 e = a.p1 - a.p0; const double L = length(e); const Vec2 u = e * (1 / L);
        if (std::abs(cross(u, b.p0 - a.p0)) > 1e-6 || std::abs(cross(u, b.p1 - a.p0)) > 1e-6) continue;
        const double t0 = dot(b.p0 - a.p0, u), t1 = dot(b.p1 - a.p0, u);
        if (std::max(t0, t1) <= 1e-6 || std::min(t0, t1) >= L - 1e-6) continue;   // touching or apart: keep
        const Vec2 o = a.p0;
        a.p0 = o + u * std::min({0.0, t0, t1}); a.p1 = o + u * std::max({L, t0, t1});
        const int gone = b.id;
        gt.roles.erase(gone);
        for (auto& d : gt.dims) d.geomPrimIds.erase(std::remove(d.geomPrimIds.begin(), d.geomPrimIds.end(), gone), d.geomPrimIds.end());
        for (auto& t : gt.sheet.texts) {
          auto it = t.attrs.find("corpus.dimPrims");
          if (it == t.attrs.end()) continue;
          std::string ids, tok; std::istringstream is(it->second);
          while (std::getline(is, tok, ',')) if (std::stoi(tok) != gone) ids += (ids.empty() ? "" : ",") + tok;
          it->second = ids;
        }
        P.erase(P.begin() + static_cast<long>(j));
      }
  }
  gt.attrs["corpus.generator"] = "pc_drawing_corpus 1";
  gt.attrs["corpus.sheet"] = "A3";
  return gt;
}
}  // namespace corpus
