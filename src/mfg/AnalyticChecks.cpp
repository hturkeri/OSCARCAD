// PersistCAD mfg — ANALYTIC DfAM / DfM rules (no B-Rep, no regeneration).
//
// Works on the analytic CSG model (MfgInternal.h). Main analyses:
//  * per hole:   Ø limits, standard drill, L/D (through length from the material along the axis),
//                horizontal-hole self-support, blind-hole powder/escape, floor under the drill tip;
//  * per prism:  axial thickness (plates / pocket floors), pocket depth vs usable tool, slot width,
//                accessibility along the tool axis, enclosed cuts;
//  * slab analysis: for every family of parallel prisms/holes the part is cut into axial slabs; in each
//                slab the boundary curves are sampled and 2D rays are cast along the inward normal
//                (wall thickness / ligaments / boss walls / hole-to-hole) and the outward normal (gaps,
//                slot widths), using the history-replaying material classifier. Concave vertices
//                (internal vertical corners) and concave arcs are detected in the same pass;
//  * edge treatments: fillet/chamfer size vs AM min feature, concave vertical fillets vs tool radius;
//  * shell thickness / closed shells; part extents vs build envelope and machine stock;
//  * down-facing planar/curved prism faces (overhang) not resting on the build plate;
//  * turning: axisymmetry.
// Each finding is mapped to its feature, its driving parameter(s) and a suggested value (Suggest.cpp).
#include "MfgInternal.h"

#include "pc/Expression.h"

#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <tuple>

namespace pc::mfg::detail {

// ============================================================================ process helpers
bool isConvMilling(const ConvSettings& c) {
  return c.enabled && (c.process == ConvProcess::CNC_Milling3Axis || c.process == ConvProcess::CNC_Milling5Axis);
}
bool amUsesSupports(AmProcess p) {
  return p == AmProcess::LPBF_Metal || p == AmProcess::EBM_Metal || p == AmProcess::DED_Metal ||
         p == AmProcess::FDM_Polymer || p == AmProcess::SLA_Resin;
}
bool amIsPowderOrResin(AmProcess p) {
  return p == AmProcess::LPBF_Metal || p == AmProcess::EBM_Metal || p == AmProcess::SLS_Polymer || p == AmProcess::SLA_Resin;
}
const char* amName(AmProcess p) {
  switch (p) {
    case AmProcess::LPBF_Metal: return "LPBF";
    case AmProcess::EBM_Metal: return "EBM";
    case AmProcess::DED_Metal: return "DED";
    case AmProcess::FDM_Polymer: return "FDM";
    case AmProcess::SLA_Resin: return "SLA";
    case AmProcess::SLS_Polymer: return "SLS";
    default: return "AM";
  }
}
const char* convName(ConvProcess p) {
  switch (p) {
    case ConvProcess::CNC_Milling3Axis: return "3 eksen CNC freze";
    case ConvProcess::CNC_Milling5Axis: return "5 eksen CNC freze";
    case ConvProcess::CNC_Turning: return "CNC torna";
    default: return "CNC";
  }
}
double amBridgeLimit(AmProcess p) {
  switch (p) {
    case AmProcess::FDM_Polymer: return 10.0;  // bridging of extruded strands
    case AmProcess::LPBF_Metal: return 2.0;    // short down-skin spans survive without support
    case AmProcess::EBM_Metal: return 3.0;
    case AmProcess::SLA_Resin: return 1.0;
    default: return 0.0;
  }
}

namespace {

// ============================================================================ context
struct Ctx {
  const Document& doc;
  const ParamValues& v;
  const MfgSettings& s;
  const AModel& m;
  std::vector<Issue> out;
  bool am = false, conv = false, milling = false, turning = false, five = false;
  gp_Dir b{0, 0, 1}, k{0, 0, 1};
  double zmin = 0, zmax = 0;
  bool hasBody = false;
  std::map<std::string, double> bodyZmin;   // v3: multi-body: lowest point of each body along b (own build plate)
  std::string amTag, convTag;
  Ctx(const Document& d, const ParamValues& vv, const MfgSettings& ss, const AModel& mm) : doc(d), v(vv), s(ss), m(mm) {}
};

std::string unitOf(const Document& doc, const std::string& p) {
  const Parameter* P = doc.params.find(p);
  if (!P) return "";
  return P->kind == ParamKind::Length ? " mm" : P->kind == ParamKind::Angle ? "°" : "";
}

std::string suggestionText(const Ctx& c, const Suggestion& sg) {
  if (!sg.value || sg.params.empty()) return "";
  const std::string& p = sg.params[0];
  const char* rel = sg.relation > 0 ? " ≥ " : sg.relation < 0 ? " ≤ " : " = ";
  return " " + p + rel + fmt(*sg.value) + unitOf(c.doc, p) + " önerilir.";
}

void emit(Ctx& c, Severity sev, Domain dom, const std::string& rule, const std::string& fid, double actual,
          double limit, const std::string& msg, const Suggestion& sg, std::vector<std::string> faces = {},
          std::vector<std::string> edges = {}) {
  Issue is;
  is.severity = sev;
  is.domain = dom;
  is.ruleId = rule;
  is.featureId = fid;
  is.actual = actual;
  is.limit = limit;
  is.params = sg.params;
  is.suggestedValue = sg.value;
  is.message = msg + suggestionText(c, sg);
  is.faces = std::move(faces);
  is.edges = std::move(edges);
  c.out.push_back(std::move(is));
}

gp_Dir perpendicular(const gp_Dir& t) {
  gp_Vec a = std::fabs(t.X()) < 0.9 ? gp_Vec(1, 0, 0) : gp_Vec(0, 1, 0);
  gp_Vec u = gp_Vec(t) ^ a;
  return gp_Dir(u);
}

bool parallel(const gp_Dir& a, const gp_Dir& b, double tolDeg = 0.5) {
  return std::fabs(a.Dot(b)) > std::cos(tolDeg * kPi / 180.0);
}

gp_Pnt P3(const gp_Vec& v) { return gp_Pnt(v.XYZ()); }

double matFraction(const AModel& m, const gp_Pnt& p, const gp_Dir& t, double eps, int N = 48) {
  gp_Dir u = perpendicular(t);
  gp_Dir w = t.Crossed(u);
  int n = 0;
  for (int i = 0; i < N; ++i) {
    double a = 2 * kPi * (i + 0.5) / N;
    gp_Pnt q = p.Translated(gp_Vec(u) * (eps * std::cos(a)) + gp_Vec(w) * (eps * std::sin(a)));
    if (m.material(q)) ++n;
  }
  return double(n) / N;
}

// ============================================================================ 1D material intervals along a line
struct Interval {
  double a, b;
};

std::vector<Interval> lineIntervals(const AModel& m, const gp_Pnt& p0, const gp_Dir& d, double smin, double smax,
                                    int skipHole = -1) {
  std::vector<double> br{smin, smax};
  gp_Vec dv(d);
  std::vector<double> ts;
  for (auto& P : m.prisms) {
    double w;
    V2 uv0 = P.toLocal(p0, &w);
    double dw = dv.Dot(gp_Vec(P.frame.Direction()));
    V2 duv{dv.Dot(gp_Vec(P.frame.XDirection())), dv.Dot(gp_Vec(P.frame.YDirection()))};
    if (std::fabs(dw) > 1e-12) {
      br.push_back((P.w0 - w) / dw);
      br.push_back((P.w1 - w) / dw);
    }
    double L = norm(duv);
    if (L > 1e-9) {
      V2 dd = duv * (1.0 / L);
      ts.clear();
      for (auto& sg : P.segs) sg.intersectRay(uv0, dd, -1e9, ts);
      for (double t : ts) br.push_back(t / L);
    }
  }
  for (int hi = 0; hi < int(m.holes.size()); ++hi) {
    if (hi == skipHole) continue;
    const HoleLoc& H = m.holes[hi];
    gp_Vec ax(H.axis);
    gp_Vec rel(H.top, p0);
    double s0 = rel.Dot(ax), da = dv.Dot(ax);
    gp_Vec r0 = rel - ax * s0, dr = dv - ax * da;
    double sEnd = H.through ? kBig : H.depth + H.tipLen;
    if (std::fabs(da) > 1e-12)
      for (double sb : {0.0, H.cbDepth, H.csLen, H.through ? kBig : H.depth, sEnd}) br.push_back((sb - s0) / da);
    double A = dr.Dot(dr), B = r0.Dot(dr), C0 = r0.Dot(r0);
    for (double rr : {H.r, H.cbR, H.csR}) {
      if (rr <= 0) continue;
      if (A > 1e-18) {
        double disc = B * B - A * (C0 - rr * rr);
        if (disc >= 0) {
          double sq = std::sqrt(disc);
          br.push_back((-B - sq) / A);
          br.push_back((-B + sq) / A);
        }
      }
    }
    // cones (drill tip / countersink): along a parallel line the radius is constant
    if (A <= 1e-18 && std::fabs(da) > 1e-12) {
      double rho = std::sqrt(C0);
      if (!H.through && H.tipLen > 0 && rho < H.r) br.push_back((H.depth + H.tipLen * (1 - rho / H.r) - s0) / da);
      if (H.kind == 2 && H.csLen > 0 && rho > H.r && rho < H.csR)
        br.push_back((H.csLen * (H.csR - rho) / (H.csR - H.r) - s0) / da);
    } else if (std::fabs(da) > 1e-12) {
      for (int i = 1; i < 8; ++i) {
        if (H.tipLen > 0) br.push_back((H.depth + H.tipLen * i / 8.0 - s0) / da);
        if (H.csLen > 0) br.push_back((H.csLen * i / 8.0 - s0) / da);
      }
    }
  }
  std::vector<double> t;
  for (double x : br)
    if (std::isfinite(x) && x >= smin && x <= smax) t.push_back(x);
  std::sort(t.begin(), t.end());
  std::vector<Interval> out;
  for (size_t i = 0; i + 1 < t.size(); ++i) {
    if (t[i + 1] - t[i] < 1e-9) continue;
    double mid = 0.5 * (t[i] + t[i + 1]);
    if (!m.material(p0.Translated(dv * mid), skipHole)) continue;
    if (!out.empty() && std::fabs(out.back().b - t[i]) < 1e-9) out.back().b = t[i + 1];
    else out.push_back({t[i], t[i + 1]});
  }
  return out;
}

// length of the material interval that contains s (0 if none)
double intervalAround(const std::vector<Interval>& iv, double s, Interval* hit = nullptr) {
  for (auto& i : iv)
    if (s >= i.a - 1e-9 && s <= i.b + 1e-9) {
      if (hit) *hit = i;
      return i.b - i.a;
    }
  return 0;
}

// ============================================================================ entity identities
struct EntId {
  bool hole = false;
  std::string fid;
  int instance = 0;
  int eid = -1;          // sketch entity id (prism) or sketch point id (hole)
  double holeS = 0;      // hole: axial position used for the radius (counterbore zone etc.)
  bool operator<(const EntId& o) const {
    return std::tie(hole, fid, instance, eid) < std::tie(o.hole, o.fid, o.instance, o.eid);
  }
  bool operator==(const EntId& o) const {
    return hole == o.hole && fid == o.fid && instance == o.instance && eid == o.eid;
  }
};

const Prism* findPrism(const AModel& m, const std::string& fid, int inst) {
  for (auto& P : m.prisms)
    if (P.fid == fid && P.instance == inst) return &P;
  return nullptr;
}
const HoleLoc* findHole(const AModel& m, const std::string& fid, int inst, int pid) {
  for (auto& H : m.holes)
    if (H.fid == fid && H.instance == inst && H.pointId == pid) return &H;
  return nullptr;
}
const Seg* findSeg(const Prism& P, int eid) {
  for (auto& s : P.segs)
    if (s.entityId == eid) return &s;
  return nullptr;
}

struct Basis {
  gp_Dir a;
  gp_Vec e1, e2;
};

Basis makeBasis(const gp_Dir& a) {
  Basis B;
  B.a = a;
  gp_Vec A(a);
  gp_Vec u = gp_Vec(1, 0, 0) - A * a.X();
  if (u.Magnitude() < 1e-6) u = gp_Vec(0, 1, 0) - A * a.Y();
  u.Normalize();
  B.e1 = u;
  B.e2 = A ^ u;
  return B;
}

V2 proj(const Basis& B, const gp_Pnt& p) {
  gp_Vec v(p.XYZ());
  return {v.Dot(B.e1), v.Dot(B.e2)};
}

Seg segToBasis(const Seg& s, const Prism& P, const Basis& B) {
  auto map = [&](V2 uv) { return proj(B, P.toWorld(uv, 0)); };
  Seg o = s;
  if (s.kind == SegKind::Line) {
    o.a = map(s.a);
    o.b = map(s.b);
    return o;
  }
  o.c = map(s.c);
  V2 st = map(s.point(0));
  o.a0 = std::atan2(st.y - o.c.y, st.x - o.c.x);
  gp_Vec X(P.frame.XDirection()), Y(P.frame.YDirection());
  double det = X.Dot(B.e1) * Y.Dot(B.e2) - Y.Dot(B.e1) * X.Dot(B.e2);
  o.sweep = det < 0 ? -s.sweep : s.sweep;
  o.a = map(s.a);
  o.b = map(s.b);
  return o;
}

// range along basis axis (clipped to [lo,hi])
bool prismRange(const Prism& P, const gp_Dir& a, double lo, double hi, double& r0, double& r1) {
  double wO = gp_Vec(P.frame.Location().XYZ()).Dot(gp_Vec(a));
  double sg = gp_Vec(P.frame.Direction()).Dot(gp_Vec(a));
  r0 = wO + sg * P.w0;
  r1 = wO + sg * P.w1;
  if (r0 > r1) std::swap(r0, r1);
  r0 = std::max(r0, lo);
  r1 = std::min(r1, hi);
  return r1 > r0 + 1e-9;
}
bool holeRange(const HoleLoc& H, const gp_Dir& a, double lo, double hi, double& r0, double& r1) {
  double t0 = gp_Vec(H.top.XYZ()).Dot(gp_Vec(a));
  double sg = gp_Vec(H.axis).Dot(gp_Vec(a));
  r0 = t0;
  r1 = t0 + sg * H.totalLen();
  if (r0 > r1) std::swap(r0, r1);
  r0 = std::max(r0, lo);
  r1 = std::min(r1, hi);
  return r1 > r0 + 1e-9;
}

// 2D curve of an entity in the basis plane (rebuilt model)
std::optional<Seg> entSeg(const AModel& m, const EntId& e, const Basis& B) {
  if (e.hole) {
    const HoleLoc* H = findHole(m, e.fid, e.instance, e.eid);
    if (!H) return std::nullopt;
    double r = H->radiusAt(e.holeS);
    if (r <= 0) r = H->r;
    Seg s;
    s.kind = SegKind::Circle;
    s.c = proj(B, H->top);
    s.r = r;
    s.a0 = 0;
    s.sweep = 2 * kPi;
    s.a = s.b = s.point(0);
    return s;
  }
  const Prism* P = findPrism(m, e.fid, e.instance);
  if (!P) return std::nullopt;
  const Seg* s = findSeg(*P, e.eid);
  if (!s) return std::nullopt;
  return segToBasis(*s, *P, B);
}

std::optional<double> entWidth(const AModel& m, const EntId& S, const EntId& H, const Basis& B) {
  auto s = entSeg(m, S, B);
  auto h = entSeg(m, H, B);
  if (!s || !h) return std::nullopt;
  if (S == H) {
    if (s->kind == SegKind::Line) return std::nullopt;
    return 2 * s->r;
  }
  return curveDistance(*s, *h);
}

std::optional<double> entHeight(const AModel& m, const EntId& S, const EntId& H, const gp_Dir& a) {
  double lo, hi;
  if (!m.extents(a, lo, hi)) return std::nullopt;
  auto range = [&](const EntId& e, double& r0, double& r1) -> bool {
    if (e.hole) {
      const HoleLoc* h = findHole(m, e.fid, e.instance, e.eid);
      return h && holeRange(*h, a, lo, hi, r0, r1);
    }
    const Prism* p = findPrism(m, e.fid, e.instance);
    return p && prismRange(*p, a, lo, hi, r0, r1);
  };
  double a0, a1, b0, b1;
  if (!range(S, a0, a1) || !range(H, b0, b1)) return std::nullopt;
  double h = std::min(a1, b1) - std::max(a0, b0);
  return h > 0 ? std::optional<double>(h) : std::nullopt;
}

int featIndexOf(const Document& doc, const std::string& fid) { return doc.indexOf(fid); }

std::vector<std::string> entExprs(const AModel& m, const EntId& e) {
  std::vector<std::string> ex;
  const Feature* f = m.doc->feature(e.fid);
  if (e.hole) {
    const HoleLoc* H = findHole(m, e.fid, e.instance, e.eid);
    if (H) {
      ex.push_back(H->diameterExpr);
      if (H->kind == 1 && f) ex.push_back(f->prop("cbDiameter"));
      if (const Feature* src = m.doc->feature(H->srcFid)) {
        if (src != f) ex.push_back(src->prop("diameter"));
      }
      if (H->sketch >= 0)
        for (auto& x : m.sketches[H->sketch].exprs()) ex.push_back(x);
    }
  } else {
    const Prism* P = findPrism(m, e.fid, e.instance);
    if (P) {
      if (P->sketch >= 0)
        for (auto& x : m.sketches[P->sketch].exprs()) ex.push_back(x);
      ex.push_back(P->distanceExpr);
    }
  }
  if (f && f->type == FeatureType::Pattern) {
    ex.push_back(f->prop("spacing"));
    ex.push_back(f->prop("angle"));
    ex.push_back(f->prop("count"));
  }
  return ex;
}

std::string describe(const AModel& m, const EntId& e) {
  if (e.hole) {
    const HoleLoc* H = findHole(m, e.fid, e.instance, e.eid);
    return e.fid + " delik Ø" + (H ? fmt(2 * H->r) : std::string("?"));
  }
  const Prism* P = findPrism(m, e.fid, e.instance);
  const Seg* s = P ? findSeg(*P, e.eid) : nullptr;
  std::string what = !s ? "kenar" : s->kind == SegKind::Line ? "kenar" : s->kind == SegKind::Circle ? "daire Ø" + fmt(2 * s->r) : "yay R" + fmt(s->r);
  return e.fid + " " + what + " (e" + std::to_string(e.eid) + ")";
}

std::vector<std::string> entFaces(const AModel& m, const EntId& e) {
  if (e.hole) {
    const HoleLoc* H = findHole(m, e.fid, e.instance, e.eid);
    if (H) return {H->faceName("wall")};
    return {};
  }
  const Prism* P = findPrism(m, e.fid, e.instance);
  if (P) return {P->faceName("side.e" + std::to_string(e.eid))};
  return {};
}

std::string edgeName(const std::string& a, const std::string& b) {
  return a < b ? "edge(" + a + "|" + b + ")" : "edge(" + b + "|" + a + ")";
}

std::string stripEdgeSuffix(const std::string& r) {
  auto p = r.rfind(')');
  if (p == std::string::npos) return r;
  return r.substr(0, p + 1);
}

bool edgeCovered(const AModel& m, const std::string& fa, const std::string& fb) {
  std::string e = edgeName(fa, fb);
  for (auto& t : m.edgeTreats)
    for (auto& r : t.refs) {
      if (r == "face:" + fa || r == "face:" + fb) return true;
      if (stripEdgeSuffix(r) == e) return true;
    }
  return false;
}

// ============================================================================ slab analysis
struct BEnt {
  Seg seg;
  EntId id;
  int featIndex = 0;
};

struct WallRec {
  EntId S, H;
  double t = std::numeric_limits<double>::infinity();
  Basis B;
  int featIndex = 0;
  std::string fid;
};

struct CornerRec {
  std::string fid, edge;
  std::vector<std::string> faces;
};

struct ArcRec {
  EntId id;
  double r = 0;
  bool fullCircle = false;
  std::string fid;
  std::vector<std::string> faces;
};

struct SlabOut {
  std::map<std::pair<EntId, EntId>, WallRec> walls, gaps;
  std::map<std::string, CornerRec> corners;
  std::map<EntId, ArcRec> arcs;
};

std::vector<V2> curveIntersections(const Seg& s, const Seg& h) {
  std::vector<V2> out;
  if (s.kind == SegKind::Line || h.kind == SegKind::Line) {
    const Seg& L = s.kind == SegKind::Line ? s : h;
    const Seg& O = s.kind == SegKind::Line ? h : s;
    double len = L.length();
    V2 d = normalized(L.b - L.a);
    std::vector<double> ts;
    O.intersectRay(L.a, d, -1e-12, ts);
    for (double t : ts)
      if (t <= len + 1e-12) out.push_back(L.a + d * t);
    return out;
  }
  double d = norm(h.c - s.c);
  if (d < 1e-12 || d > s.r + h.r || d < std::fabs(s.r - h.r)) return out;
  double a = (s.r * s.r - h.r * h.r + d * d) / (2 * d);
  double hh = std::sqrt(std::max(0.0, s.r * s.r - a * a));
  V2 e = (h.c - s.c) * (1.0 / d);
  V2 p = s.c + e * a;
  for (double sg : {-1.0, 1.0}) {
    V2 q{p.x - sg * hh * e.y, p.y + sg * hh * e.x};
    if (s.angleInSpan(std::atan2(q.y - s.c.y, q.x - s.c.x)) && h.angleInSpan(std::atan2(q.y - h.c.y, q.x - h.c.x)))
      out.push_back(q);
  }
  return out;
}

void slabAnalysis(Ctx& c, SlabOut& so) {
  const AModel& m = c.m;
  // ---- axis groups
  std::vector<gp_Dir> axes;
  auto addAxis = [&](const gp_Dir& d) {
    for (auto& a : axes)
      if (parallel(a, d, 1e-4)) return;
    axes.push_back(d);
  };
  for (auto& P : m.prisms)
    if (P.op == BodyOp::New || P.op == BodyOp::Join) addAxis(P.frame.Direction());
  const double gapCap = std::max({c.am ? c.s.am.minGap : 0.0, c.milling ? c.s.conv.minToolDiameter : 0.0}) * 1.5 + 1e-6;
  std::vector<double> ts;
  std::vector<std::pair<double, int>> hits;
  for (auto& a0 : axes) {
    gp_Dir a = a0;
    Basis B = makeBasis(a);
    double lo, hi;
    if (!m.extents(a, lo, hi)) continue;
    std::vector<int> gp, gh;
    std::vector<double> br{lo, hi};
    for (int i = 0; i < int(m.prisms.size()); ++i) {
      double r0, r1;
      if (!parallel(m.prisms[i].frame.Direction(), a, 1e-4)) continue;
      if (!prismRange(m.prisms[i], a, lo, hi, r0, r1)) continue;
      gp.push_back(i);
      br.push_back(r0);
      br.push_back(r1);
    }
    for (int i = 0; i < int(m.holes.size()); ++i) {
      const HoleLoc& H = m.holes[i];
      double r0, r1;
      if (!parallel(H.axis, a, 1e-4) || !holeRange(H, a, lo, hi, r0, r1)) continue;
      gh.push_back(i);
      br.push_back(r0);
      br.push_back(r1);
      double t0 = gp_Vec(H.top.XYZ()).Dot(gp_Vec(a)), sg = gp_Vec(H.axis).Dot(gp_Vec(a));
      for (double s : {H.cbDepth, H.csLen, H.through ? -1.0 : H.depth})
        if (s > 0) br.push_back(t0 + sg * s);
    }
    std::sort(br.begin(), br.end());
    br.erase(std::unique(br.begin(), br.end(), [](double x, double y) { return std::fabs(x - y) < 1e-7; }), br.end());
    // prism curves in the basis plane (once per group)
    std::map<int, std::vector<Seg>> pseg;
    for (int i : gp)
      for (auto& s : m.prisms[i].segs) pseg[i].push_back(segToBasis(s, m.prisms[i], B));

    for (size_t si = 0; si + 1 < br.size(); ++si) {
      double z0 = br[si], z1 = br[si + 1];
      if (z1 < lo - 1e-9 || z0 > hi + 1e-9 || z1 - z0 < 1e-6) continue;
      double zm = 0.5 * (z0 + z1);
      gp_Vec A = gp_Vec(a) * zm;
      auto world = [&](V2 p) { return P3(B.e1 * p.x + B.e2 * p.y + A); };
      auto mat = [&](V2 p) { return m.material(world(p)); };
      std::vector<BEnt> ents;
      bool anyBody = false;
      for (int i : gp) {
        const Prism& P = m.prisms[i];
        double r0, r1;
        prismRange(P, a, lo, hi, r0, r1);
        if (zm <= r0 || zm >= r1) continue;
        if (P.op == BodyOp::New || P.op == BodyOp::Join) anyBody = true;
        auto& segs = pseg[i];
        for (size_t j = 0; j < segs.size(); ++j) {
          BEnt be;
          be.seg = segs[j];
          be.id = {false, P.fid, P.instance, P.segs[j].entityId, 0};
          be.featIndex = P.featIndex;
          ents.push_back(be);
        }
      }
      if (!anyBody) continue;
      for (int i : gh) {
        const HoleLoc& H = m.holes[i];
        double t0 = gp_Vec(H.top.XYZ()).Dot(gp_Vec(a)), sg = gp_Vec(H.axis).Dot(gp_Vec(a));
        double s = (zm - t0) * sg;
        double r = H.radiusAt(s);
        if (r <= 0) continue;
        BEnt be;
        be.seg.kind = SegKind::Circle;
        be.seg.c = proj(B, H.top);
        be.seg.r = r;
        be.seg.a0 = 0;
        be.seg.sweep = 2 * kPi;
        be.seg.a = be.seg.b = be.seg.point(0);
        be.id = {true, H.fid, H.instance, H.pointId, s};
        be.featIndex = H.featIndex;
        ents.push_back(be);
      }
      const double dl = 1e-6 * std::max(1.0, m.scale);
      // ---- wall / gap rays
      for (size_t ei = 0; ei < ents.size(); ++ei) {
        const BEnt& E = ents[ei];
        int N = E.seg.kind == SegKind::Line ? 12 : 24;
        for (int i = 0; i < N; ++i) {
          double t = (i + 0.5) / N;
          V2 q = E.seg.point(t), n = E.seg.normal(t);
          bool mp = mat(q + n * dl), mm = mat(q - n * dl);
          if (mp == mm) continue;
          for (int pass = 0; pass < 2; ++pass) {
            bool gapPass = pass == 1;
            if (gapPass && (E.id.hole || gapCap <= 1e-6)) continue;
            V2 d = (mp != gapPass) ? n : n * -1.0;  // wall: into material, gap: into void
            hits.clear();
            for (size_t hj = 0; hj < ents.size(); ++hj) {
              ts.clear();
              ents[hj].seg.intersectRay(q, d, 1e3 * dl, ts);
              for (double tt : ts) hits.push_back({tt, int(hj)});
            }
            std::sort(hits.begin(), hits.end());
            double prev = 0;
            int found = -1;
            double ft = 0;
            for (size_t h = 0; h < hits.size(); ++h) {
              double tn = h + 1 < hits.size() ? hits[h + 1].first : hits[h].first + 10 * dl + 1.0;
              if (tn - hits[h].first < 1e-9) { prev = hits[h].first; continue; }
              double mid = 0.5 * (hits[h].first + tn);
              bool mt = mat(q + d * mid);
              if (gapPass ? mt : !mt) { found = hits[h].second; ft = hits[h].first; break; }
              prev = hits[h].first;
              if (gapPass && prev > gapCap) break;
            }
            (void)prev;
            if (found < 0) continue;
            if (gapPass && ft > gapCap) continue;
            const BEnt& Hh = ents[found];
            V2 hp = q + d * ft;
            V2 hn = Hh.seg.kind == SegKind::Line ? Hh.seg.normal(0) : normalized(hp - Hh.seg.c);
            if (std::fabs(dot(hn, d)) < 0.5) continue;
            // canonical pair: later feature first
            EntId S = E.id, H = Hh.id;
            int fs = E.featIndex, fh = Hh.featIndex;
            if (fh > fs || (fh == fs && H < S)) { std::swap(S, H); std::swap(fs, fh); }
            auto key = std::make_pair(S, H);
            auto& rec = (gapPass ? so.gaps : so.walls)[key];
            if (ft < rec.t) {
              rec.t = ft;
              rec.S = S;
              rec.H = H;
              rec.B = B;
              rec.featIndex = fs;
              rec.fid = S.fid;
            }
          }
        }
      }
      // ---- concave arcs (milled internal radii) and sharp internal vertical corners
      if (!(c.milling && (c.five || parallel(a, c.k, 1.0)))) continue;
      for (auto& E : ents) {
        if (E.id.hole || E.seg.kind == SegKind::Line) continue;
        V2 q = E.seg.point(0.5);
        V2 tc = normalized(E.seg.c - q);
        if (!mat(q + tc * dl * 10) && mat(q - tc * dl * 10)) {
          auto& ar = so.arcs[E.id];
          ar.id = E.id;
          ar.r = E.seg.r;
          ar.fullCircle = E.seg.kind == SegKind::Circle;
          ar.fid = E.id.fid;
          ar.faces = entFaces(m, E.id);
        }
      }
      std::vector<V2> verts;
      for (size_t i = 0; i < ents.size(); ++i) {
        if (ents[i].id.hole) continue;
        if (ents[i].seg.kind != SegKind::Circle) {
          verts.push_back(ents[i].seg.a);
          verts.push_back(ents[i].seg.b);
        }
        for (size_t j = i + 1; j < ents.size(); ++j) {
          if (ents[j].id.hole) continue;
          if (ents[j].id.fid == ents[i].id.fid && ents[j].id.instance == ents[i].id.instance) continue;
          for (auto& p : curveIntersections(ents[i].seg, ents[j].seg)) verts.push_back(p);
        }
      }
      const double vt = 1e-6 * std::max(1.0, m.scale);
      std::vector<V2> uniq;
      for (auto& p : verts) {
        bool dup = false;
        for (auto& u : uniq)
          if (norm(u - p) < vt * 10) { dup = true; break; }
        if (!dup) uniq.push_back(p);
      }
      const double eps = std::max(1e-4, 1e-5 * m.scale);
      for (auto& vtx : uniq) {
        std::vector<const BEnt*> at;
        bool nearHole = false;
        for (auto& E : ents)
          if (E.seg.distance(vtx) < vt * 100) {
            if (E.id.hole) nearHole = true;
            else at.push_back(&E);
          }
        if (nearHole || at.size() < 2) continue;
        int n = 0;
        const int NS = 48;
        for (int i = 0; i < NS; ++i) {
          double ang = 2 * kPi * (i + 0.5) / NS;
          if (mat(vtx + V2{std::cos(ang), std::sin(ang)} * eps)) ++n;
        }
        double frac = double(n) / NS;
        if (frac < 0.56 || frac > 0.97) continue;
        // pick the two entities with the most different tangents
        const BEnt *ea = at[0], *eb = at[1];
        auto tangentAt = [&](const BEnt* e) {
          if (e->seg.kind == SegKind::Line) return normalized(e->seg.b - e->seg.a);
          V2 r = normalized(vtx - e->seg.c);
          return V2{-r.y, r.x};
        };
        double best = 1;
        for (size_t i = 0; i < at.size(); ++i)
          for (size_t j = i + 1; j < at.size(); ++j) {
            double cs = std::fabs(dot(tangentAt(at[i]), tangentAt(at[j])));
            if (cs < best) { best = cs; ea = at[i]; eb = at[j]; }
          }
        if (best > std::cos(5 * kPi / 180)) continue;  // tangent continuous
        auto fa = entFaces(m, ea->id), fb = entFaces(m, eb->id);
        if (fa.empty() || fb.empty()) continue;
        if (edgeCovered(m, fa[0], fb[0])) continue;
        std::string en = edgeName(fa[0], fb[0]);
        auto& cr = so.corners[en];
        cr.edge = en;
        cr.fid = ea->featIndex >= eb->featIndex ? ea->id.fid : eb->id.fid;
        cr.faces = {fa[0], fb[0]};
      }
    }
  }
}

// exact thickness of the found pairs (the sampled ray only locates the pair)
void refinePairs(const AModel& m, SlabOut& so) {
  for (auto* mp : {&so.walls, &so.gaps})
    for (auto& [k, w] : *mp) {
      auto e = entWidth(m, w.S, w.H, w.B);
      if (e && *e > 1e-9 && *e < w.t) w.t = *e;
    }
}

// ============================================================================ rules
std::string amLimitText(const Ctx& c, const char* what, double lim) {
  return std::string(amName(c.s.am.process)) + " " + what + " " + fmt(lim) + " mm (" + c.s.am.material + ")";
}
std::string convLimitText(const Ctx& c, const char* what, double lim) {
  return std::string(convName(c.s.conv.process)) + " " + what + " " + fmt(lim) + " mm (" + c.s.conv.material + ")";
}

struct WallEval {
  const WallRec* w = nullptr;
  double value = 0;     // thickness or ratio
  double height = 0;
};

std::vector<std::string> pairFaces(const AModel& m, const WallRec& w) {
  std::vector<std::string> faces = entFaces(m, w.S);
  for (auto& f : entFaces(m, w.H))
    if (std::find(faces.begin(), faces.end(), f) == faces.end()) faces.push_back(f);
  return faces;
}

std::vector<std::string> pairExprs(const AModel& m, const WallRec& w) {
  std::vector<std::string> ex = entExprs(m, w.S);
  for (auto& x : entExprs(m, w.H)) ex.push_back(x);
  return ex;
}

// group violations by feature, keep the worst (smallest for min-rules, largest for max-rules)
std::map<std::string, std::pair<WallEval, std::vector<std::string>>> worstByFeature(const AModel& m,
                                                                                    const std::vector<WallEval>& v,
                                                                                    bool smallestIsWorst) {
  std::map<std::string, std::pair<WallEval, std::vector<std::string>>> g;
  for (auto& e : v) {
    auto it = g.find(e.w->fid);
    if (it == g.end()) {
      g[e.w->fid] = {e, pairFaces(m, *e.w)};
      continue;
    }
    bool worse = smallestIsWorst ? e.value < it->second.first.value : e.value > it->second.first.value;
    for (auto& f : pairFaces(m, *e.w))
      if (std::find(it->second.second.begin(), it->second.second.end(), f) == it->second.second.end())
        it->second.second.push_back(f);
    if (worse) it->second.first = e;
  }
  return g;
}

void wallRules(Ctx& c, const SlabOut& so) {
  std::vector<WallEval> amWall, cncWall, amRatio, cncRatio, amGap, cncSlot;
  for (auto& [key, w] : so.walls) {
    if (!std::isfinite(w.t)) continue;
    if (c.am && w.t < c.s.am.minWallThickness - 1e-9) amWall.push_back({&w, w.t, 0});
    if (c.conv && w.t < c.s.conv.minWallThickness - 1e-9) cncWall.push_back({&w, w.t, 0});
    auto h = entHeight(c.m, w.S, w.H, w.B.a);
    if (!h || w.t <= 1e-9) continue;
    double ratio = *h / w.t;
    if (c.am && ratio > c.s.am.maxAspectRatio + 1e-9) amRatio.push_back({&w, ratio, *h});
    if (c.conv && ratio > c.s.conv.maxWallHeightToThickness + 1e-9) cncRatio.push_back({&w, ratio, *h});
  }
  for (auto& [key, w] : so.gaps) {
    if (!std::isfinite(w.t)) continue;
    if (c.am && w.t < c.s.am.minGap - 1e-9) amGap.push_back({&w, w.t, 0});
    if (c.milling && (c.five || parallel(w.B.a, c.k, 1.0)) && w.t < c.s.conv.minToolDiameter - 1e-9)
      cncSlot.push_back({&w, w.t, 0});
  }
  auto widthMetric = [](const WallRec& w) -> Metric {
    EntId S = w.S, H = w.H;
    Basis B = w.B;
    return [S, H, B](const AModel& mm) { return entWidth(mm, S, H, B); };
  };
  auto ratioMetric = [](const WallRec& w) -> Metric {
    EntId S = w.S, H = w.H;
    Basis B = w.B;
    gp_Dir ax = w.B.a;
    return [S, H, B, ax](const AModel& mm) -> std::optional<double> {
      auto ww = entWidth(mm, S, H, B);
      auto hh = entHeight(mm, S, H, ax);
      if (!ww || !hh || *ww <= 1e-9) return std::nullopt;
      return *hh / *ww;
    };
  };
  auto desc = [&](const WallRec& w) {
    return w.S == w.H ? describe(c.m, w.S) : describe(c.m, w.S) + " ↔ " + describe(c.m, w.H);
  };
  // min wall
  auto wallEmit = [&](const std::vector<WallEval>& v, double lim, Domain dom, const char* rule, const std::string& lt) {
    for (auto& [fid, gw] : worstByFeature(c.m, v, true)) {
      const WallRec& w = *gw.first.w;
      Suggestion sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, pairExprs(c.m, w)), widthMetric(w), lim, Want::AtLeast);
      emit(c, Severity::Error, dom, rule, fid, w.t, lim, "Et kalınlığı " + fmt(w.t) + " mm < " + lt + " [" + desc(w) + "].", sg,
           gw.second);
    }
  };
  wallEmit(amWall, c.s.am.minWallThickness, Domain::Additive, "AM.MIN_WALL", amLimitText(c, "min. et kalınlığı", c.s.am.minWallThickness));
  wallEmit(cncWall, c.s.conv.minWallThickness, Domain::Conventional, "CNC.MIN_WALL",
           convLimitText(c, "min. et kalınlığı", c.s.conv.minWallThickness));
  // slenderness: height parameters first (the height is usually what makes a pin slender)
  auto ratioCands = [&](const WallRec& w) {
    std::vector<std::string> hx;
    for (const EntId* e : {&w.S, &w.H}) {
      if (e->hole) {
        if (auto* Ho = findHole(c.m, e->fid, e->instance, e->eid)) hx.push_back(Ho->depthExpr);
      } else if (auto* P = findPrism(c.m, e->fid, e->instance)) {
        hx.push_back(P->distanceExpr);
      }
    }
    for (auto& x : pairExprs(c.m, w)) hx.push_back(x);
    return orderCandidates(c.doc, hx);
  };
  for (auto& [fid, gw] : worstByFeature(c.m, amRatio, false)) {
    const WallRec& w = *gw.first.w;
    double ratio = gw.first.value, h = gw.first.height;
    Suggestion sg = suggestByProbe(c.doc, c.v, ratioCands(w), ratioMetric(w), c.s.am.maxAspectRatio, Want::AtMost);
    emit(c, Severity::Warning, Domain::Additive, "AM.ASPECT_RATIO", fid, ratio, c.s.am.maxAspectRatio,
         "Narin duvar/pim: yükseklik/kalınlık = " + fmt(h) + "/" + fmt(w.t) + " = " + fmt(ratio, 1) + " > " +
             amName(c.s.am.process) + " sınırı " + fmt(c.s.am.maxAspectRatio, 1) + " (çarpılma, recoater çarpması riski) [" +
             desc(w) + "].",
         sg, gw.second);
  }
  for (auto& [fid, gw] : worstByFeature(c.m, cncRatio, false)) {
    const WallRec& w = *gw.first.w;
    double ratio = gw.first.value, h = gw.first.height;
    Suggestion sg = suggestByProbe(c.doc, c.v, ratioCands(w), ratioMetric(w), c.s.conv.maxWallHeightToThickness, Want::AtMost);
    emit(c, Severity::Warning, Domain::Conventional, "CNC.WALL_RATIO", fid, ratio, c.s.conv.maxWallHeightToThickness,
         "İnce duvar: yükseklik/kalınlık = " + fmt(h) + "/" + fmt(w.t) + " = " + fmt(ratio, 1) + " > " +
             convName(c.s.conv.process) + " sınırı " + fmt(c.s.conv.maxWallHeightToThickness, 1) + " (titreşim/esneme) [" +
             desc(w) + "].",
         sg, gw.second);
  }
  for (auto& [fid, gw] : worstByFeature(c.m, amGap, true)) {
    const WallRec& w = *gw.first.w;
    Suggestion sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, pairExprs(c.m, w)), widthMetric(w), c.s.am.minGap, Want::AtLeast);
    emit(c, Severity::Warning, Domain::Additive, "AM.MIN_GAP", fid, w.t, c.s.am.minGap,
         "Boşluk " + fmt(w.t) + " mm < " + amLimitText(c, "min. boşluk", c.s.am.minGap) +
             " (yüzeyler kaynayabilir, toz/reçine temizlenemez) [" + desc(w) + "].",
         sg, gw.second);
  }
  for (auto& [fid, gw] : worstByFeature(c.m, cncSlot, true)) {
    const WallRec& w = *gw.first.w;
    Suggestion sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, pairExprs(c.m, w)), widthMetric(w),
                                   c.s.conv.minToolDiameter, Want::AtLeast);
    emit(c, Severity::Error, Domain::Conventional, "CNC.SLOT_WIDTH", fid, w.t, c.s.conv.minToolDiameter,
         "Kanal/boşluk genişliği " + fmt(w.t) + " mm < en küçük freze Ø" + fmt(c.s.conv.minToolDiameter) +
             " mm; takım giremez (" + convName(c.s.conv.process) + ") [" + desc(w) + "].",
         sg, gw.second);
  }
  for (auto& [en, cr] : so.corners) {
    double rt = c.s.conv.minToolDiameter / 2;
    emit(c, Severity::Error, Domain::Conventional, "CNC.SHARP_INTERNAL_CORNER", cr.fid, 0, rt,
         "Keskin iç dikey köşe (R = 0): Ø" + fmt(c.s.conv.minToolDiameter) + " mm freze köşede en az R" + fmt(rt) +
             " mm bırakır (" + convName(c.s.conv.process) + "). Köşeye ≥ R" + fmt(rt) + " mm yuvarlatma (Fillet) ekleyin.",
         Suggestion{}, cr.faces, {cr.edge});
  }
  for (auto& [id, ar] : so.arcs) {
    double rt = c.s.conv.minToolDiameter / 2;
    if (ar.r >= rt - 1e-9) continue;
    EntId e = ar.id;
    const Prism* P = findPrism(c.m, e.fid, e.instance);
    std::vector<std::string> ex;
    if (P && P->sketch >= 0) ex = c.m.sketches[P->sketch].exprs();
    auto cands = orderCandidates(c.doc, ex);
    Basis B = makeBasis(P ? P->frame.Direction() : c.k);
    Metric rm = [e, B](const AModel& mm) -> std::optional<double> {
      auto s = entSeg(mm, e, B);
      if (!s || s->kind == SegKind::Line) return std::nullopt;
      return s->r;
    };
    Suggestion sg = suggestByProbe(c.doc, c.v, cands, rm, rt, Want::AtLeast);
    std::string what = ar.fullCircle ? "Frezelenen dairesel cep Ø" + fmt(2 * ar.r) + " mm" : "İç köşe yayı R" + fmt(ar.r) + " mm";
    emit(c, Severity::Error, Domain::Conventional, "CNC.CORNER_RADIUS", ar.fid, ar.r, rt,
         what + " < freze yarıçapı R" + fmt(rt) + " mm (en küçük freze Ø" + fmt(c.s.conv.minToolDiameter) + " mm, " +
             convName(c.s.conv.process) + ").",
         sg, ar.faces);
  }
}

// true if `bodies` is not empty and every named body is created by a Revolve feature (turned part whose
// material the analytic classifier cannot replay)
bool bodiesAreRevolved(const Document& doc, const std::vector<std::string>& bodies) {
  if (bodies.empty()) return false;
  for (auto& b : bodies) {
    bool rev = false;
    for (auto& f : doc.features) {
      if (f.suppressed || f.type != FeatureType::Revolve) continue;
      const std::string op = f.prop("op");
      const std::string name = f.prop("bodyName").empty() ? f.id : f.prop("bodyName");
      if (name == b && (op == "new" || op.empty())) rev = true;
    }
    if (!rev) return false;
  }
  return true;
}

// ---------------------------------------------------------------------------- micro cuts in turned bodies
// An Extrude CUT into a revolved (turned) body is invisible to the slab analysis (no analytic material around
// it). Its tool is still known exactly: the cut is as wide as the smaller of the extrusion length and the
// profile's own minimum width. That is the blade thickness a slitting saw / the wire + spark gap of a wire-EDM /
// the cutter diameter of an end mill needs, so it is checked against conv.minToolDiameter (CNC.SLOT_WIDTH) and
// am.minGap (AM.MIN_GAP), with a suggestion on the parameter that drives the width (e.g. w_slot).
void microCutRules(Ctx& c) {
  const AModel& m = c.m;
  for (auto& P : m.prisms) {
    if (P.op != BodyOp::Cut || P.instance != 0 || P.through) continue;
    if (!bodiesAreRevolved(c.doc, P.bodies)) continue;
    const double len = std::fabs(P.w1 - P.w0);
    const double own = regionMinThickness(P.segs);
    const bool byLen = len <= own;
    const double w = byLen ? len : own;
    if (!(w > 0) || !std::isfinite(w)) continue;
    std::vector<std::string> ex;
    if (byLen) ex.push_back(P.distanceExpr);
    else if (P.sketch >= 0 && P.sketch < int(m.sketches.size())) ex = m.sketches[P.sketch].exprs();
    auto suggest = [&](double lim) {
      if (byLen) return suggestByExpr(c.doc, c.v, P.distanceExpr, lim, Want::AtLeast);
      Suggestion sg;
      sg.params = orderCandidates(c.doc, ex);
      return sg;
    };
    std::vector<std::string> faces{P.faceName("start"), P.faceName("end")};
    if (c.conv && w < c.s.conv.minToolDiameter - 1e-9) {
      emit(c, Severity::Error, Domain::Conventional, "CNC.SLOT_WIDTH", P.fid, w, c.s.conv.minToolDiameter,
           "Kanal genişliği " + fmt(w) + " mm < en küçük takım (testere bıçağı / tel + kıvılcım aralığı / freze) " +
               fmt(c.s.conv.minToolDiameter) + " mm; kanal açılamaz (" + convName(c.s.conv.process) + ").",
           suggest(c.s.conv.minToolDiameter), faces);
    }
    if (c.am && amIsPowderOrResin(c.s.am.process) && c.s.am.minGap > 0 && w < c.s.am.minGap - 1e-9) {
      emit(c, Severity::Warning, Domain::Additive, "AM.MIN_GAP", P.fid, w, c.s.am.minGap,
           "Kanal genişliği " + fmt(w) + " mm < " + amLimitText(c, "min. boşluk", c.s.am.minGap) +
               " (kanal kaynayabilir, toz/reçine temizlenemez).",
           suggest(c.s.am.minGap), faces);
    }
  }
}

// ---------------------------------------------------------------------------- holes
struct LocEval {
  int idx = -1;
  double L = 0;           // drilled length in material
  bool floorKnown = false;
  double floorLen = 0;    // material beyond the drill tip apex (negative = tip breaks out)
};

Metric throughLDMetric(const HoleLoc& H) {
  std::string f0 = H.fid;
  int inst = H.instance, pid = H.pointId;
  return [f0, inst, pid](const AModel& mm) -> std::optional<double> {
    const HoleLoc* h = findHole(mm, f0, inst, pid);
    if (!h) return std::nullopt;
    int idx = int(h - mm.holes.data());
    double l, u;
    mm.extents(h->axis, l, u);
    double st = gp_Vec(h->top.XYZ()).Dot(gp_Vec(h->axis));
    auto ivv = lineIntervals(mm, h->top, h->axis, 0, std::max(0.0, u - st) + 1, idx);
    if (ivv.empty()) return std::nullopt;
    return (ivv.back().b - std::max(0.0, ivv.front().a)) / (2 * h->r);
  };
}

Metric floorMetric(const HoleLoc& H) {
  std::string f0 = H.fid;
  int inst = H.instance, pid = H.pointId;
  return [f0, inst, pid](const AModel& mm) -> std::optional<double> {
    const HoleLoc* h = findHole(mm, f0, inst, pid);
    if (!h) return std::nullopt;
    int idx = int(h - mm.holes.data());
    auto ivv = lineIntervals(mm, h->top, h->axis, 0, h->depth + h->tipLen + 2 * mm.scale, idx);
    Interval ht{0, 0};
    if (intervalAround(ivv, h->depth + 1e-7, &ht) <= 0) return std::nullopt;
    return ht.b - (h->depth + h->tipLen);
  };
}

void holeRules(Ctx& c) {
  const AModel& m = c.m;
  std::vector<std::string> order;
  std::map<std::string, std::vector<LocEval>> byFid;
  for (int hi = 0; hi < int(m.holes.size()); ++hi) {
    const HoleLoc& H = m.holes[hi];
    double lo, hi2;
    m.extents(H.axis, lo, hi2);
    double sTop = gp_Vec(H.top.XYZ()).Dot(gp_Vec(H.axis));
    double smax = std::max(0.0, hi2 - sTop) + 1.0;
    double smin = std::min(0.0, lo - sTop) - 1.0;
    auto iv = lineIntervals(m, H.top, H.axis, smin, smax, hi);
    double sEnd = H.through ? smax : H.depth;
    double matLen = 0, first = -1, last = -1;
    for (auto& i : iv) {
      double a = std::max(i.a, 0.0), b = std::min(i.b, sEnd);
      if (b > a) {
        matLen += b - a;
        if (first < 0) first = a;
        last = b;
      }
    }
    // A hole whose explicit target bodies are all turned (Revolve) bodies: the analytic material classifier
    // does not know revolved material, so the drilled length is the blind depth itself (e.g. the orifice of a
    // turned nozzle head). Through holes in revolved bodies are left to the geometric pass.
    const bool inRevolved = matLen <= 1e-9 && !H.through && bodiesAreRevolved(c.doc, H.bodies);
    if (matLen <= 1e-9 && !inRevolved) continue;  // hole in the air
    LocEval e;
    e.idx = hi;
    e.L = H.through ? (last - first) : H.depth;
    if (!H.through) {
      Interval hit{0, 0};
      if (intervalAround(iv, H.depth + 1e-7, &hit) > 0 && hit.b > H.depth + 1e-7) {
        e.floorKnown = true;
        e.floorLen = hit.b - (H.depth + H.tipLen);
      }
    }
    if (!byFid.count(H.fid)) order.push_back(H.fid);
    byFid[H.fid].push_back(e);
  }
  for (auto& fid : order) {
    auto& locs = byFid[fid];
    const HoleLoc& H = m.holes[locs.front().idx];
    const double d = 2 * H.r;
    const std::string dexpr = H.diameterExpr;
    std::vector<std::string> faces;
    for (auto& e : locs) faces.push_back(m.holes[e.idx].faceName("wall"));
    if (c.am && d < c.s.am.minHoleDiameter - 1e-9) {
      Suggestion sg = suggestByExpr(c.doc, c.v, dexpr, c.s.am.minHoleDiameter, Want::AtLeast);
      emit(c, Severity::Error, Domain::Additive, "AM.MIN_HOLE", fid, d, c.s.am.minHoleDiameter,
           "Delik Ø" + fmt(d) + " mm < " + amLimitText(c, "min. delik çapı", c.s.am.minHoleDiameter) +
               "; delik kapanır, sonradan delinmeli.",
           sg, faces);
    }
    if (c.conv && d < c.s.conv.minHoleDiameter - 1e-9) {
      Suggestion sg = suggestByExpr(c.doc, c.v, dexpr, c.s.conv.minHoleDiameter, Want::AtLeast);
      emit(c, Severity::Error, Domain::Conventional, "CNC.MIN_HOLE", fid, d, c.s.conv.minHoleDiameter,
           "Delik Ø" + fmt(d) + " mm < " + convLimitText(c, "min. delik çapı", c.s.conv.minHoleDiameter) + ".", sg, faces);
    }
    if (c.conv && c.s.conv.preferStandardDrills && d <= standardDrillSizes().back() + 1e-9) {
      double nd = nearestStandardDrill(d);
      if (std::fabs(nd - d) > 1e-6) {
        Suggestion sg = suggestExactByExpr(c.doc, c.v, dexpr, nd);
        emit(c, Severity::Info, Domain::Conventional, "CNC.STD_DRILL", fid, d, nd,
             "Delik Ø" + fmt(d) + " mm standart matkap ölçüsü değil (en yakın standart Ø" + fmt(nd) +
                 " mm); özel takım/raybalama gerekir.",
             sg, faces);
      }
    }
    // drilling L/D (worst location)
    if (c.conv) {
      const LocEval* worst = nullptr;
      for (auto& e : locs)
        if (!worst || e.L > worst->L) worst = &e;
      double ratio = worst->L / d;
      if (ratio > c.s.conv.maxHoleDepthToDia + 1e-9) {
        const HoleLoc& Hw = m.holes[worst->idx];
        Suggestion sg;
        if (!Hw.through) {
          sg = suggestByExpr(c.doc, c.v, "(" + Hw.depthExpr + ")/(" + dexpr + ")", c.s.conv.maxHoleDepthToDia, Want::AtMost);
        } else {
          std::vector<std::string> ex{dexpr};
          for (auto& P : m.prisms) ex.push_back(P.distanceExpr);
          sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, ex), throughLDMetric(Hw), c.s.conv.maxHoleDepthToDia, Want::AtMost);
        }
        emit(c, Severity::Error, Domain::Conventional, "CNC.HOLE_LD", fid, ratio, c.s.conv.maxHoleDepthToDia,
             "Delik derinlik/çap oranı L/D = " + fmt(worst->L) + "/" + fmt(d) + " = " + fmt(ratio, 1) + " > " +
                 convName(c.s.conv.process) + " matkap sınırı " + fmt(c.s.conv.maxHoleDepthToDia, 1) +
                 " (matkap sapması/talaş tahliyesi; derin delik delme gerekir).",
             sg, faces);
      }
    }
    // accessibility along the spindle (3-axis milling: drill must point along -toolAxis)
    if (c.milling && !c.five) {
      double dd = gp_Vec(H.axis).Dot(gp_Vec(c.k));
      if (dd > std::cos(1 * kPi / 180)) {
        emit(c, Severity::Info, Domain::Conventional, "CNC.ACCESS", fid, 0, 0,
             "Delik takım eksenine ters yönden (alttan) delinmeli: ikinci bağlama gerekir.", Suggestion{}, faces);
      } else if (dd > -std::cos(1 * kPi / 180)) {
        emit(c, Severity::Warning, Domain::Conventional, "CNC.ACCESS", fid, std::acos(std::clamp(-dd, -1.0, 1.0)) * 180 / kPi, 0,
             "Delik ekseni takım eksenine paralel değil: 3 eksende ek bağlama / açılı fikstür veya 4./5. eksen gerekir.",
             Suggestion{}, faces);
      }
    }
    // horizontal (self-supporting) holes
    if (c.am && amUsesSupports(c.s.am.process) && c.s.am.minOverhangAngle > 0) {
      double ang = std::asin(std::min(1.0, std::fabs(gp_Vec(H.axis).Dot(gp_Vec(c.b))))) * 180 / kPi;
      if (ang < c.s.am.minOverhangAngle - 1e-9 && d > c.s.am.maxSelfSupportingHoleDia + 1e-9) {
        Suggestion sg = suggestByExpr(c.doc, c.v, dexpr, c.s.am.maxSelfSupportingHoleDia, Want::AtMost);
        emit(c, Severity::Warning, Domain::Additive, "AM.HORIZONTAL_HOLE", fid, d, c.s.am.maxSelfSupportingHoleDia,
             "Yatay delik Ø" + fmt(d) + " mm > " + amLimitText(c, "kendini taşıyan delik sınırı", c.s.am.maxSelfSupportingHoleDia) +
                 " (eksen yapı plakasına " + fmt(ang, 0) + "°); destek ya da damla (teardrop) profili gerekir.",
             sg, faces);
      }
    }
    if (H.through) continue;
    // blind holes: floor under the drill tip (worst location), powder removal
    const LocEval* wf = nullptr;
    for (auto& e : locs)
      if (e.floorKnown && (!wf || e.floorLen < wf->floorLen)) wf = &e;
    if (wf) {
      const HoleLoc& Hf = m.holes[wf->idx];
      std::vector<std::string> fx{Hf.depthExpr};
      for (auto& P : m.prisms) fx.push_back(P.distanceExpr);
      fx.push_back(dexpr);
      auto depthFirst = orderCandidates(c.doc, {Hf.depthExpr});
      for (auto& x : orderCandidates(c.doc, fx))
        if (std::find(depthFirst.begin(), depthFirst.end(), x) == depthFirst.end()) depthFirst.push_back(x);
      double minW = std::max(c.conv ? c.s.conv.minWallThickness : 0.0, c.am ? c.s.am.minWallThickness : 0.0);
      if (wf->floorLen < -1e-9) {
        Suggestion sg = suggestByProbe(c.doc, c.v, depthFirst, floorMetric(Hf), minW, Want::AtLeast);
        emit(c, Severity::Warning, Domain::Model, "HOLE.TIP_BREAKOUT", fid, wf->floorLen, 0,
             "Kör deliğin matkap ucu (" + fmt(Hf.tipLen) + " mm koni) karşı yüzeyi " + fmt(-wf->floorLen) +
                 " mm aşıyor; istenmeyen açıklık/çapak oluşur (uç dahil taban eti ≥ " + fmt(minW) + " mm olmalı).",
             sg, faces);
      } else {
        auto chk = [&](bool on, double lim, Domain dom, const char* rule, const std::string& lt) {
          if (!on || wf->floorLen >= lim - 1e-9) return;
          Suggestion sg = suggestByProbe(c.doc, c.v, depthFirst, floorMetric(Hf), lim, Want::AtLeast);
          emit(c, Severity::Error, dom, rule, fid, wf->floorLen, lim,
               "Kör delik tabanında (matkap ucunda) kalan et " + fmt(wf->floorLen) + " mm < " + lt + ".", sg, faces);
        };
        chk(c.am, c.s.am.minWallThickness, Domain::Additive, "AM.MIN_WALL", amLimitText(c, "min. et kalınlığı", c.s.am.minWallThickness));
        chk(c.conv, c.s.conv.minWallThickness, Domain::Conventional, "CNC.MIN_WALL",
            convLimitText(c, "min. et kalınlığı", c.s.conv.minWallThickness));
      }
    }
    if (c.am && amIsPowderOrResin(c.s.am.process)) {
      const char* medium = c.s.am.process == AmProcess::SLA_Resin ? "reçine" : "toz";
      if (d < c.s.am.minEscapeHoleDia - 1e-9) {
        Suggestion sg = suggestByExpr(c.doc, c.v, dexpr, c.s.am.minEscapeHoleDia, Want::AtLeast);
        emit(c, Severity::Warning, Domain::Additive, "AM.BLIND_HOLE", fid, d, c.s.am.minEscapeHoleDia,
             "Kör delik Ø" + fmt(d) + " mm < " + amLimitText(c, "min. boşaltma çapı", c.s.am.minEscapeHoleDia) + "; " + medium +
                 " delikten temizlenemez (ya da sonradan delinmeli).",
             sg, faces);
      } else if (H.depth / d > 4) {
        emit(c, Severity::Info, Domain::Additive, "AM.BLIND_HOLE", fid, H.depth / d, 4,
             "Derin kör delik (derinlik/çap = " + fmt(H.depth / d, 1) + "): " + medium +
                 " boşaltma için yönlendirmeye ve temizliğe dikkat edin.",
             Suggestion{}, faces);
      }
    }
  }
}

// ---------------------------------------------------------------------------- prisms
std::vector<V2> regionSamples(const Prism& P, int n) {
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (auto& s : P.segs) {
    for (int i = 0; i <= 16; ++i) {
      V2 p = s.point(i / 16.0);
      x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
  }
  std::vector<V2> out;
  if (x1 < x0) return out;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      V2 p{x0 + (x1 - x0) * (i + 0.5) / n, y0 + (y1 - y0) * (j + 0.5) / n};
      if (P.inside2d(p)) out.push_back(p);
    }
  return out;
}

std::optional<double> prismAxialMin(const AModel& m, const Prism& P, bool floor) {
  double lo, hi;
  m.extents(P.frame.Direction(), lo, hi);
  double wO = gp_Vec(P.frame.Location().XYZ()).Dot(gp_Vec(P.frame.Direction()));
  double best = std::numeric_limits<double>::infinity();
  bool any = false;
  for (auto& uv : regionSamples(P, 7)) {
    gp_Pnt base = P.toWorld(uv, 0);
    auto iv = lineIntervals(m, base, P.frame.Direction(), lo - wO - 1, hi - wO + 1);
    double s;
    if (floor) {
      if (P.through) return std::nullopt;
      double wEnd = P.extSign > 0 ? P.w1 : P.w0;
      s = wEnd + P.extSign * 1e-6;
      Interval h{0, 0};
      if (intervalAround(iv, s, &h) <= 0) continue;
      double len = P.extSign > 0 ? h.b - wEnd : wEnd - h.a;
      if (len <= 0) continue;
      best = std::min(best, len);
      any = true;
    } else {
      s = 0.5 * (std::max(P.w0, lo - wO) + std::min(P.w1, hi - wO));
      double len = intervalAround(iv, s);
      if (len <= 0) continue;
      best = std::min(best, len);
      any = true;
    }
  }
  if (!any) return std::nullopt;
  return best;
}

double regionArea(const Prism& P, int n = 16) {
  double x0 = 1e300, y0 = 1e300, x1 = -1e300, y1 = -1e300;
  for (auto& s : P.segs)
    for (int i = 0; i <= 16; ++i) {
      V2 p = s.point(i / 16.0);
      x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
    }
  if (x1 < x0) return 0;
  int in = 0;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j)
      if (P.inside2d({x0 + (x1 - x0) * (i + 0.5) / n, y0 + (y1 - y0) * (j + 0.5) / n})) ++in;
  return (x1 - x0) * (y1 - y0) * in / double(n * n);
}

// ---------------------------------------------------------------------------- drilled circular cuts (v3)
// material length the drill passes through: material along the axis on a ring just outside the circle,
// within the cut's axial range (clipped to the part)
std::optional<double> drilledCutLength(const AModel& m, const Prism& P) {
  if (P.segs.size() != 1 || P.segs[0].kind != SegKind::Circle) return std::nullopt;
  const Seg& c0 = P.segs[0];
  double lo, hi;
  if (!m.extents(P.frame.Direction(), lo, hi)) return std::nullopt;
  double wO = gp_Vec(P.frame.Location().XYZ()).Dot(gp_Vec(P.frame.Direction()));
  double a = std::max(P.w0, lo - wO), b = std::min(P.w1, hi - wO);
  if (b <= a + 1e-9) return std::nullopt;
  double rr = c0.r * 1.02 + 1e-3, best = 0;
  for (int k = 0; k < 8; ++k) {
    double ang = 2 * kPi * (k + 0.5) / 8;
    V2 uv = c0.c + V2{std::cos(ang), std::sin(ang)} * rr;
    double len = 0;
    for (auto& iv : lineIntervals(m, P.toWorld(uv, 0), P.frame.Direction(), a, b)) len += iv.b - iv.a;
    best = std::max(best, len);
  }
  if (best <= 1e-9) return std::nullopt;
  return best;
}

// diameter expression of the circle (sketch Diameter / Radius dimension), "" if not dimensioned
std::string circleDiameterExpr(const AModel& m, const Prism& P) {
  if (P.sketch < 0 || P.segs.empty()) return "";
  for (auto& k : m.sketches[P.sketch].solved.constraints) {
    if (k.valueExpr.empty() || std::find(k.refs.begin(), k.refs.end(), P.segs[0].entityId) == k.refs.end()) continue;
    if (k.type == SkConstraintType::Diameter) return k.valueExpr;
    if (k.type == SkConstraintType::Radius) return "2*(" + k.valueExpr + ")";
  }
  return "";
}

void drilledCutRules(Ctx& c, const Prism& P) {
  const AModel& m = c.m;
  auto L = drilledCutLength(m, P);
  if (!L) return;  // cut in the air
  const std::string fid = P.fid;
  const double d = 2 * P.segs[0].r;
  const std::string dexpr = circleDiameterExpr(m, P);
  std::vector<std::string> faces{P.faceName("side.e" + std::to_string(P.segs[0].entityId))};
  std::vector<std::string> sx;
  if (P.sketch >= 0) sx = m.sketches[P.sketch].exprs();
  Metric dm = [fid](const AModel& mm) -> std::optional<double> {
    const Prism* q = findPrism(mm, fid, 0);
    if (!q || q->segs.size() != 1 || q->segs[0].kind != SegKind::Circle) return std::nullopt;
    return 2 * q->segs[0].r;
  };
  auto sizeSuggestion = [&](double lim, Want want) {
    if (!dexpr.empty()) return suggestByExpr(c.doc, c.v, dexpr, lim, want);
    return suggestByProbe(c.doc, c.v, orderCandidates(c.doc, sx), dm, lim, want);
  };
  if (c.am && d < c.s.am.minHoleDiameter - 1e-9) {
    emit(c, Severity::Error, Domain::Additive, "AM.MIN_HOLE", fid, d, c.s.am.minHoleDiameter,
         "Delik Ø" + fmt(d) + " mm < " + amLimitText(c, "min. delik çapı", c.s.am.minHoleDiameter) +
             "; delik kapanır, sonradan delinmeli.",
         sizeSuggestion(c.s.am.minHoleDiameter, Want::AtLeast), faces);
  }
  if (!c.conv) return;
  if (d < c.s.conv.minHoleDiameter - 1e-9) {
    emit(c, Severity::Error, Domain::Conventional, "CNC.MIN_HOLE", fid, d, c.s.conv.minHoleDiameter,
         "Delik Ø" + fmt(d) + " mm < " + convLimitText(c, "min. delik çapı", c.s.conv.minHoleDiameter) + ".",
         sizeSuggestion(c.s.conv.minHoleDiameter, Want::AtLeast), faces);
  }
  if (c.s.conv.preferStandardDrills && d <= standardDrillSizes().back() + 1e-9) {
    double nd = nearestStandardDrill(d);
    if (std::fabs(nd - d) > 1e-6) {
      Suggestion sg;
      if (!dexpr.empty()) sg = suggestExactByExpr(c.doc, c.v, dexpr, nd);
      else sg.params = orderCandidates(c.doc, sx);
      emit(c, Severity::Info, Domain::Conventional, "CNC.STD_DRILL", fid, d, nd,
           "Delik Ø" + fmt(d) + " mm standart matkap ölçüsü değil (en yakın standart Ø" + fmt(nd) +
               " mm); özel takım/raybalama gerekir.",
           sg, faces);
    }
  }
  double ratio = *L / d;
  if (ratio > c.s.conv.maxHoleDepthToDia + 1e-9) {
    std::vector<std::string> ex{P.distanceExpr};
    if (!dexpr.empty()) ex.push_back(dexpr);
    for (auto& x : sx) ex.push_back(x);
    for (auto& Q : m.prisms) ex.push_back(Q.distanceExpr);
    Metric lm = [fid](const AModel& mm) -> std::optional<double> {
      const Prism* q = findPrism(mm, fid, 0);
      if (!q) return std::nullopt;
      auto l = drilledCutLength(mm, *q);
      if (!l) return std::nullopt;
      return *l / (2 * q->segs[0].r);
    };
    Suggestion sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, ex), lm, c.s.conv.maxHoleDepthToDia, Want::AtMost);
    emit(c, Severity::Error, Domain::Conventional, "CNC.HOLE_LD", fid, ratio, c.s.conv.maxHoleDepthToDia,
         "Delik derinlik/çap oranı L/D = " + fmt(*L) + "/" + fmt(d) + " = " + fmt(ratio, 1) + " > " +
             convName(c.s.conv.process) + " matkap sınırı " + fmt(c.s.conv.maxHoleDepthToDia, 1) +
             " (matkap sapması/talaş tahliyesi; derin delik delme gerekir).",
         sg, faces);
  }
}

void prismRules(Ctx& c) {
  const AModel& m = c.m;
  for (auto& P : m.prisms) {
    if (P.instance != 0) continue;
    std::string fid = P.fid;
    std::vector<std::string> pex;
    pex.push_back(P.distanceExpr);
    if (P.sketch >= 0)
      for (auto& x : m.sketches[P.sketch].exprs()) pex.push_back(x);
    std::vector<std::string> allDist;
    for (auto& Q : m.prisms) allDist.push_back(Q.distanceExpr);
    if (P.op == BodyOp::New || P.op == BodyOp::Join) {
      auto t = prismAxialMin(m, P, false);
      if (t) {
        std::vector<std::string> ex = pex;
        for (auto& x : allDist) ex.push_back(x);
        auto cands = orderCandidates(c.doc, ex);
        Metric mt = [fid](const AModel& mm) -> std::optional<double> {
          const Prism* q = findPrism(mm, fid, 0);
          return q ? prismAxialMin(mm, *q, false) : std::nullopt;
        };
        std::vector<std::string> faces{P.faceName("start"), P.faceName("end")};
        auto chk = [&](bool on, double lim, Domain dom, const char* rule, const std::string& lt) {
          if (!on || *t >= lim - 1e-9) return;
          Suggestion sg = suggestByProbe(c.doc, c.v, cands, mt, lim, Want::AtLeast);
          emit(c, Severity::Error, dom, rule, fid, *t, lim, "Et kalınlığı " + fmt(*t) + " mm < " + lt + " (" + fid + " ekstrüzyon yönünde).",
               sg, faces);
        };
        chk(c.am, c.s.am.minWallThickness, Domain::Additive, "AM.MIN_WALL", amLimitText(c, "min. et kalınlığı", c.s.am.minWallThickness));
        chk(c.conv, c.s.conv.minWallThickness, Domain::Conventional, "CNC.MIN_WALL",
            convLimitText(c, "min. et kalınlığı", c.s.conv.minWallThickness));
      }
    }
    if (P.op != BodyOp::Cut) continue;
    // ---- cuts: floor thickness, pocket depth, slot width, accessibility, enclosure
    auto floorT = prismAxialMin(m, P, true);
    if (floorT) {
      std::vector<std::string> ex = pex;
      for (auto& x : allDist) ex.push_back(x);
      auto cands = orderCandidates(c.doc, ex);
      Metric mt = [fid](const AModel& mm) -> std::optional<double> {
        const Prism* q = findPrism(mm, fid, 0);
        return q ? prismAxialMin(mm, *q, true) : std::nullopt;
      };
      std::vector<std::string> faces{P.faceName("end")};
      auto chk = [&](bool on, double lim, Domain dom, const char* rule, const std::string& lt) {
        if (!on || *floorT >= lim - 1e-9) return;
        Suggestion sg = suggestByProbe(c.doc, c.v, cands, mt, lim, Want::AtLeast);
        emit(c, Severity::Error, dom, rule, fid, *floorT, lim, "Cep tabanı kalınlığı " + fmt(*floorT) + " mm < " + lt + ".", sg, faces);
      };
      chk(c.am, c.s.am.minWallThickness, Domain::Additive, "AM.MIN_WALL", amLimitText(c, "min. et kalınlığı", c.s.am.minWallThickness));
      chk(c.conv, c.s.conv.minWallThickness, Domain::Conventional, "CNC.MIN_WALL",
          convLimitText(c, "min. et kalınlığı", c.s.conv.minWallThickness));
    }
    // v3: a through cut whose profile is one full circle is a drilled hole (e.g. a plug hole on a datum plane):
    // the drill rules of Hole features apply (min Ø, standard drill sizes, L/D)
    if (!floorT && P.segs.size() == 1 && P.segs[0].kind == SegKind::Circle) drilledCutRules(c, P);
    // material removed by this cut along its axis (entry -> floor)
    auto samples = regionSamples(P, 7);
    double lo, hi;
    m.extents(P.frame.Direction(), lo, hi);
    double wO = gp_Vec(P.frame.Location().XYZ()).Dot(gp_Vec(P.frame.Direction()));
    double depth = 0;
    bool blockedBelow = true, blockedAbove = true, enclosed = !samples.empty();
    for (auto& uv : samples) {
      gp_Pnt base = P.toWorld(uv, 0);
      double a = std::max(P.w0, lo - wO), b = std::min(P.w1, hi - wO);
      if (b <= a) { enclosed = false; continue; }
      depth = std::max(depth, b - a);
      bool ma = m.material(P.toWorld(uv, b + 1e-6)), mb = m.material(P.toWorld(uv, a - 1e-6));
      if (!ma) blockedAbove = false;
      if (!mb) blockedBelow = false;
      if (!ma || !mb) enclosed = false;
      (void)base;
    }
    if (enclosed) {
      for (auto& s : P.segs) {
        V2 q = s.point(0.5), n = s.normal(0.5);
        if (P.inside2d(q + n * 1e-5)) n = n * -1.0;
        if (!m.material(P.toWorld(q + n * 1e-4, 0.5 * (P.w0 + P.w1)))) { enclosed = false; break; }
      }
    }
    if (enclosed) {
      if (c.am && amIsPowderOrResin(c.s.am.process))
        emit(c, Severity::Error, Domain::Additive, "AM.TRAPPED_POWDER", fid, 0, c.s.am.minEscapeHoleDia,
             "Kapalı iç boşluk: " + std::string(c.s.am.process == AmProcess::SLA_Resin ? "reçine" : "toz") +
                 " içeride hapsolur. En az iki adet ≥ Ø" + fmt(c.s.am.minEscapeHoleDia) + " mm boşaltma deliği ekleyin.",
             Suggestion{}, {P.faceName("side.e" + std::to_string(P.segs.empty() ? 0 : P.segs[0].entityId))});
      if (c.conv)
        emit(c, Severity::Error, Domain::Conventional, "CNC.ENCLOSED_VOID", fid, 0, 0,
             "Kapalı iç boşluk talaşlı imalatla üretilemez (" + std::string(convName(c.s.conv.process)) + ").", Suggestion{});
      continue;
    }
    bool axisOk = c.five || parallel(P.frame.Direction(), c.k, 1.0);
    if (c.milling && !axisOk && !c.five) {
      emit(c, Severity::Warning, Domain::Conventional, "CNC.ACCESS", fid, 0, 0,
           "Kesim doğrultusu takım eksenine paralel değil: 3 eksende ek bağlama / açılı fikstür gerekir.", Suggestion{},
           {P.faceName("end")});
    }
    if (c.milling && axisOk && !c.five && blockedAbove && blockedBelow && !samples.empty()) {
      emit(c, Severity::Error, Domain::Conventional, "CNC.UNDERCUT", fid, 0, 0,
           "Kesim takım ekseni boyunca iki yönden de malzeme ile kapalı (alttan kesme): 3 eksende işlenemez.", Suggestion{},
           {P.faceName("end")});
    }
    if (c.milling && axisOk && depth > 0) {
      double width = regionMinThickness(P.segs);
      double dt = width;
      for (auto& s : P.segs)
        if (s.kind != SegKind::Line) {
          V2 q = s.point(0.5);
          if (P.inside2d(q + normalized(s.c - q) * 1e-5)) dt = std::min(dt, 2 * s.r);
        }
      double fr = std::numeric_limits<double>::infinity();
      for (auto& t : m.edgeTreats) {
        if (t.chamfer) continue;
        for (auto& r : t.refs)
          if (r.find(fid + ".side") != std::string::npos) fr = std::min(fr, 2 * t.size);
      }
      dt = std::min(dt, fr);
      if (std::isfinite(dt)) {
        double tool = std::max(c.s.conv.minToolDiameter, dt);
        double lim = c.s.conv.maxToolLengthToDia * tool;
        if (depth > lim + 1e-9) {
          Suggestion sg;
          if (!P.through) sg = suggestByExpr(c.doc, c.v, P.distanceExpr, lim, Want::AtMost);
          emit(c, Severity::Warning, Domain::Conventional, "CNC.POCKET_DEPTH", fid, depth, lim,
               "Cep derinliği " + fmt(depth) + " mm > " + fmt(c.s.conv.maxToolLengthToDia, 1) + " × Ø" + fmt(tool) +
                   " mm kullanılabilir freze = " + fmt(lim) + " mm (uzun takım: titreşim/sapma, " + convName(c.s.conv.process) + ").",
               sg, {P.faceName("end")});
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------- edge treatments
struct FaceRef {
  enum Kind { Unknown, Side, Cap, HoleWall } kind = Unknown;
  const Prism* P = nullptr;
  const Seg* seg = nullptr;
  const HoleLoc* H = nullptr;
  std::string name;
};

FaceRef parseFace(const AModel& m, const std::string& nameIn) {
  FaceRef r;
  r.name = nameIn;
  std::string name = nameIn;
  auto pc = name.rfind(':');
  if (pc != std::string::npos) name = name.substr(0, pc);
  auto d1 = name.find('.');
  if (d1 == std::string::npos) return r;
  std::string fid = name.substr(0, d1), rest = name.substr(d1 + 1);
  int inst = 0;
  if (rest.size() > 1 && rest[0] == 'i' && std::isdigit(static_cast<unsigned char>(rest[1]))) {
    auto d2 = rest.find('.');
    if (d2 == std::string::npos) return r;
    inst = std::atoi(rest.substr(1, d2 - 1).c_str());
    rest = rest.substr(d2 + 1);
    auto d3 = rest.find('.');
    if (d3 == std::string::npos) return r;
    rest = rest.substr(d3 + 1);
  }
  if (const Prism* P = findPrism(m, fid, inst)) {
    r.P = P;
    if (rest == "end" || rest == "start") r.kind = FaceRef::Cap;
    else if (rest.rfind("side.e", 0) == 0) {
      r.seg = findSeg(*P, std::atoi(rest.substr(6).c_str()));
      if (r.seg) r.kind = FaceRef::Side;
    }
    return r;
  }
  if (rest.size() > 1 && rest[0] == 'h') {
    auto d2 = rest.find('.');
    if (d2 != std::string::npos && rest.substr(d2 + 1) == "wall") {
      int k = std::atoi(rest.substr(1, d2 - 1).c_str());
      for (auto& H : m.holes)
        if (H.fid == fid && H.instance == inst && H.k == k) { r.H = &H; r.kind = FaceRef::HoleWall; }
    }
  }
  return r;
}

gp_Vec seg3(const Prism& P, V2 d) { return gp_Vec(P.frame.XDirection()) * d.x + gp_Vec(P.frame.YDirection()) * d.y; }

// convexity of the edge between faces A and B: +1 convex, -1 concave, 0 unknown. `t` = edge tangent.
int classifyEdge(const Ctx& c, const std::string& A, const std::string& B, gp_Dir& t, bool& wallWall) {
  const AModel& m = c.m;
  FaceRef fa = parseFace(m, A), fb = parseFace(m, B);
  wallWall = false;
  gp_Pnt p;
  bool have = false;
  if (fa.kind == FaceRef::Side && fb.kind == FaceRef::Side && fa.P == fb.P) {
    const Prism& P = *fa.P;
    std::optional<V2> shared;
    for (V2 x : {fa.seg->a, fa.seg->b})
      for (V2 y : {fb.seg->a, fb.seg->b})
        if (fa.seg->kind != SegKind::Circle && fb.seg->kind != SegKind::Circle && norm(x - y) < 1e-6) shared = x;
    if (!shared) return 0;
    double lo, hi;
    m.extents(P.frame.Direction(), lo, hi);
    double wO = gp_Vec(P.frame.Location().XYZ()).Dot(gp_Vec(P.frame.Direction()));
    double w = 0.5 * (std::max(P.w0, lo - wO) + std::min(P.w1, hi - wO));
    p = P.toWorld(*shared, w);
    t = P.frame.Direction();
    wallWall = true;
    have = true;
  } else {
    const FaceRef* side = fa.kind == FaceRef::Side ? &fa : fb.kind == FaceRef::Side ? &fb : nullptr;
    const FaceRef* hole = fa.kind == FaceRef::HoleWall ? &fa : fb.kind == FaceRef::HoleWall ? &fb : nullptr;
    const std::string& other = (side == &fa || hole == &fa) ? B : A;
    gp_Pnt q;
    gp_Dir n;
    if ((side || hole) && m.facePlane(other, q, n)) {
      if (side) {
        const Prism& P = *side->P;
        V2 mid = side->seg->point(0.5);
        gp_Pnt base = P.toWorld(mid, 0);
        double den = gp_Vec(P.frame.Direction()).Dot(gp_Vec(n));
        if (std::fabs(den) < 1e-9) return 0;
        double w = gp_Vec(base, q).Dot(gp_Vec(n)) / den;
        p = P.toWorld(mid, w);
        gp_Vec tv = seg3(P, side->seg->tangent(0.5));
        if (tv.Magnitude() < 1e-12) return 0;
        t = gp_Dir(tv);
        have = true;
      } else {
        const HoleLoc& H = *hole->H;
        double den = gp_Vec(H.axis).Dot(gp_Vec(n));
        if (std::fabs(den) < 1e-9) return 0;
        double s = gp_Vec(H.top, q).Dot(gp_Vec(n)) / den;
        gp_Dir u = perpendicular(H.axis);
        double r = H.radiusAt(s + 1e-6);
        if (r <= 0) r = H.r;
        p = H.top.Translated(gp_Vec(H.axis) * s + gp_Vec(u) * r);
        t = H.axis.Crossed(u);
        have = true;
      }
    }
  }
  if (!have) return 0;
  double f = matFraction(m, p, t, std::max(1e-4, 1e-5 * m.scale));
  if (f > 0.55 && f < 0.97) return -1;
  if (f < 0.45 && f > 0.03) return +1;
  return 0;
}

std::vector<std::pair<std::string, std::string>> expandRef(const AModel& m, const std::string& ref) {
  std::vector<std::pair<std::string, std::string>> out;
  if (ref.rfind("edge(", 0) == 0) {
    std::string r = stripEdgeSuffix(ref);
    auto bar = r.find('|');
    if (bar == std::string::npos || r.size() < 7) return out;
    out.push_back({r.substr(5, bar - 5), r.substr(bar + 1, r.size() - bar - 2)});
    return out;
  }
  if (ref.rfind("face:", 0) == 0) {
    std::string fname = ref.substr(5);
    FaceRef f = parseFace(m, fname);
    if (f.kind == FaceRef::Side) {
      for (auto& s : f.P->segs) {
        if (&s == f.seg || s.kind == SegKind::Circle || f.seg->kind == SegKind::Circle) continue;
        bool adj = false;
        for (V2 x : {f.seg->a, f.seg->b})
          for (V2 y : {s.a, s.b})
            if (norm(x - y) < 1e-6) adj = true;
        if (adj) out.push_back({fname, f.P->faceName("side.e" + std::to_string(s.entityId))});
      }
      out.push_back({fname, f.P->faceName("end")});
      out.push_back({fname, f.P->faceName("start")});
    } else if (f.kind == FaceRef::Cap) {
      for (auto& s : f.P->segs) out.push_back({fname, f.P->faceName("side.e" + std::to_string(s.entityId))});
    }
  }
  return out;
}

void edgeTreatRules(Ctx& c) {
  for (auto& t : c.m.edgeTreats) {
    const char* what = t.chamfer ? "Pah" : "Yuvarlatma";
    if (c.am && t.size < c.s.am.minFeatureSize - 1e-9 && t.size > 0) {
      Suggestion sg = suggestByExpr(c.doc, c.v, t.expr, c.s.am.minFeatureSize, Want::AtLeast);
      emit(c, Severity::Warning, Domain::Additive, "AM.MIN_FEATURE", t.fid, t.size, c.s.am.minFeatureSize,
           std::string(what) + " " + fmt(t.size) + " mm < " + amLimitText(c, "min. detay boyutu", c.s.am.minFeatureSize) +
               "; detay basılamaz.",
           sg);
    }
    if (t.chamfer || !c.milling) continue;
    double rt = c.s.conv.minToolDiameter / 2;
    if (t.size >= rt - 1e-9) continue;
    std::vector<std::string> concave;
    for (auto& ref : t.refs)
      for (auto& [A, B] : expandRef(c.m, ref)) {
        gp_Dir tan;
        bool ww = false;
        int cv = classifyEdge(c, A, B, tan, ww);
        if (cv >= 0) continue;
        if (c.five ? !ww : !parallel(tan, c.k, 1.0)) continue;  // only internal vertical (wall-wall) corners
        concave.push_back(ref.rfind("edge(", 0) == 0 ? ref : edgeName(A, B));
      }
    if (concave.empty()) continue;
    Suggestion sg = suggestByExpr(c.doc, c.v, t.expr, rt, Want::AtLeast);
    emit(c, Severity::Error, Domain::Conventional, "CNC.FILLET_TOOL_RADIUS", t.fid, t.size, rt,
         "İç köşe yuvarlatması R" + fmt(t.size) + " mm < freze yarıçapı R" + fmt(rt) + " mm (en küçük freze Ø" +
             fmt(c.s.conv.minToolDiameter) + " mm, " + convName(c.s.conv.process) + "); köşe işlenemez.",
         sg, {}, concave);
  }
}

// ---------------------------------------------------------------------------- shells
void shellRules(Ctx& c) {
  for (auto& s : c.m.shells) {
    auto chk = [&](bool on, double lim, Domain dom, const char* rule, const std::string& lt) {
      if (!on || s.thickness >= lim - 1e-9) return;
      Suggestion sg = suggestByExpr(c.doc, c.v, s.expr, lim, Want::AtLeast);
      emit(c, Severity::Error, dom, rule, s.fid, s.thickness, lim, "Kabuk et kalınlığı " + fmt(s.thickness) + " mm < " + lt + ".", sg);
    };
    chk(c.am, c.s.am.minWallThickness, Domain::Additive, "AM.MIN_WALL", amLimitText(c, "min. et kalınlığı", c.s.am.minWallThickness));
    chk(c.conv, c.s.conv.minWallThickness, Domain::Conventional, "CNC.MIN_WALL",
        convLimitText(c, "min. et kalınlığı", c.s.conv.minWallThickness));
    if (s.refs.empty()) {
      if (c.am && amIsPowderOrResin(c.s.am.process))
        emit(c, Severity::Error, Domain::Additive, "AM.TRAPPED_POWDER", s.fid, 0, c.s.am.minEscapeHoleDia,
             "Kabuk kapalı (açık yüz yok): iç boşlukta " + std::string(c.s.am.process == AmProcess::SLA_Resin ? "reçine" : "toz") +
                 " hapsolur. En az iki adet ≥ Ø" + fmt(c.s.am.minEscapeHoleDia) + " mm boşaltma deliği ekleyin.",
             Suggestion{});
      if (c.conv)
        emit(c, Severity::Error, Domain::Conventional, "CNC.ENCLOSED_VOID", s.fid, 0, 0,
             "Kapalı kabuk iç boşluğu talaşlı imalatla üretilemez.", Suggestion{});
    }
  }
}

// ---------------------------------------------------------------------------- envelope
std::vector<std::string> allBodyExprs(const AModel& m) {
  std::vector<std::string> ex;
  for (auto& P : m.prisms) {
    if (P.op != BodyOp::New && P.op != BodyOp::Join) continue;
    if (P.sketch >= 0)
      for (auto& x : m.sketches[P.sketch].exprs()) ex.push_back(x);
    ex.push_back(P.distanceExpr);
  }
  return ex;
}

void envelopeRule(Ctx& c, bool amSide) {
  gp_Dir z = amSide ? c.b : c.k;
  Basis B = makeBasis(z);
  gp_Dir x(B.e1), y(B.e2);
  double lx, hx, ly, hy, lz, hz;
  if (!c.m.extents(x, lx, hx) || !c.m.extents(y, ly, hy) || !c.m.extents(z, lz, hz)) return;
  double sx = hx - lx, sy = hy - ly, sz = hz - lz;
  double BX = amSide ? c.s.am.buildX : c.s.conv.stockX, BY = amSide ? c.s.am.buildY : c.s.conv.stockY,
         BZ = amSide ? c.s.am.buildZ : c.s.conv.stockZ;
  const char* rule = amSide ? "AM.BUILD_ENVELOPE" : "CNC.STOCK_ENVELOPE";
  Domain dom = amSide ? Domain::Additive : Domain::Conventional;
  std::string proc = amSide ? amName(c.s.am.process) : convName(c.s.conv.process);
  std::string env = amSide ? "yapı hacmi" : "tezgah/ham malzeme";
  auto cands = orderCandidates(c.doc, allBodyExprs(c.m));
  auto report = [&](const gp_Dir& d, double size, double lim, const char* axis) {
    Metric mt = [d](const AModel& mm) -> std::optional<double> {
      double l, h;
      if (!mm.extents(d, l, h)) return std::nullopt;
      return h - l;
    };
    Suggestion sg = suggestByProbe(c.doc, c.v, cands, mt, lim, Want::AtMost);
    emit(c, Severity::Error, dom, rule, "", size, lim,
         std::string("Parça boyutu ") + axis + " = " + fmt(size) + " mm > " + proc + " " + env + " " + fmt(lim) + " mm.", sg);
  };
  if (!amSide && c.turning) {
    // bar stock: diameter = 2 x max radial distance from the spindle axis
    double R = 0;
    gp_Dir rd;
    for (int i = 0; i < 36; ++i) {
      double a = 2 * kPi * i / 36;
      gp_Dir d(B.e1 * std::cos(a) + B.e2 * std::sin(a));
      double l, h;
      if (c.m.extents(d, l, h) && h > R) { R = h; rd = d; }
    }
    if (2 * R > BX + 1e-9) {
      Metric mt = [rd](const AModel& mm) -> std::optional<double> {
        double l, h;
        if (!mm.extents(rd, l, h)) return std::nullopt;
        return 2 * h;
      };
      Suggestion sg = suggestByProbe(c.doc, c.v, cands, mt, BX, Want::AtMost);
      emit(c, Severity::Error, dom, rule, "", 2 * R, BX,
           "Tornalama çapı Ø" + fmt(2 * R) + " mm > çubuk malzeme Ø" + fmt(BX) + " mm.", sg);
    }
    if (sz > BZ + 1e-9) report(z, sz, BZ, "(eksenel)");
    return;
  }
  if (sz > BZ + 1e-9) report(z, sz, BZ, amSide ? "Z (yapı yönü)" : "Z (takım ekseni)");
  bool fits = (sx <= BX + 1e-9 && sy <= BY + 1e-9) || (sx <= BY + 1e-9 && sy <= BX + 1e-9);
  if (!fits) {
    if (sx > std::max(BX, BY) + 1e-9 || (sx > BX + 1e-9 && sy > BX + 1e-9)) report(x, sx, sx > BX ? BX : BY, "X");
    if (sy > std::max(BX, BY) + 1e-9 || (sy > BY + 1e-9 && sx > BY + 1e-9)) report(y, sy, sy > BY ? BY : BX, "Y");
    if (sx <= std::max(BX, BY) + 1e-9 && sy <= std::max(BX, BY) + 1e-9 && !(sx > BX + 1e-9 && sy > BX + 1e-9) &&
        !(sy > BY + 1e-9 && sx > BY + 1e-9))
      report(x, sx, BX, "X");
  }
}

// ---------------------------------------------------------------------------- overhang
// body a prism belongs to ("" if not determinable)
std::string prismBody(const AModel& m, const Prism& P) {
  if (P.bodies.size() == 1) return P.bodies[0];
  auto it = m.featureBody.find(P.fid);
  return it == m.featureBody.end() ? "" : it->second;
}

// v3: build plate per body (as the geometric pass does): each body of a multi-body part sits on its own z_min
void computeBodyZmin(Ctx& c) {
  if (!c.m.multiBody()) return;
  gp_Vec bv(c.b);
  std::vector<V2> pts;
  for (auto& P : c.m.prisms) {
    if (P.op != BodyOp::New && P.op != BodyOp::Join) continue;
    std::string b = prismBody(c.m, P);
    if (b.empty()) continue;
    pts.clear();
    for (auto& s : P.segs)
      for (int i = 0; i <= 16; ++i) pts.push_back(s.point(i / 16.0));
    double lo = std::numeric_limits<double>::infinity();
    for (auto& uv : pts)
      for (double w : {P.w0, P.w1}) lo = std::min(lo, gp_Vec(P.toWorld(uv, w).XYZ()).Dot(bv));
    auto it = c.bodyZmin.find(b);
    if (it == c.bodyZmin.end()) c.bodyZmin[b] = lo;
    else it->second = std::min(it->second, lo);
  }
}

double plateOf(const Ctx& c, const Prism& P) {
  if (c.bodyZmin.empty()) return c.zmin;
  auto it = c.bodyZmin.find(prismBody(c.m, P));
  return it == c.bodyZmin.end() ? c.zmin : std::max(c.zmin, it->second);
}

void overhangRule(Ctx& c) {
  if (!c.am || !amUsesSupports(c.s.am.process) || c.s.am.minOverhangAngle <= 0) return;
  const AModel& m = c.m;
  const double lim = c.s.am.minOverhangAngle;
  const double dl = 1e-5 * std::max(1.0, m.scale);
  gp_Vec bv(c.b);
  struct Acc {
    double area = 0, minAng = 90;
    std::vector<std::string> faces;
    bool capOnly = true;
    double capWidth = 0;
    EntId worstSide;
    bool haveSide = false;
  };
  std::map<std::string, Acc> acc;
  for (auto& P : m.prisms) {
    if (P.op == BodyOp::Intersect) continue;
    const double flip = P.op == BodyOp::Cut ? -1.0 : 1.0;
    gp_Vec N(P.frame.Direction());
    double lo, hi;
    m.extents(P.frame.Direction(), lo, hi);
    double wO = gp_Vec(P.frame.Location().XYZ()).Dot(N);
    double wa = std::max(P.w0, lo - wO), wb = std::min(P.w1, hi - wO);
    if (wb <= wa) continue;
    const double zPlate = plateOf(c, P);
    // caps
    for (int cap = 0; cap < 2; ++cap) {
      double w = cap == 0 ? P.w0 : P.w1;
      if (w < lo - wO - 1e-9 || w > hi - wO + 1e-9) continue;
      gp_Vec n = N * (cap == 0 ? -1.0 : 1.0) * flip;
      double nb = n.Dot(bv);
      if (nb >= -1e-9) continue;
      double ang = std::acos(std::clamp(-nb, -1.0, 1.0)) * 180 / kPi;
      if (ang >= lim - 1e-9) continue;
      auto samples = regionSamples(P, 8);
      int exposed = 0;
      for (auto& uv : samples) {
        gp_Pnt p = P.toWorld(uv, w);
        if (gp_Vec(p.XYZ()).Dot(bv) - zPlate < 1e-3) continue;  // on the build plate
        if (!m.material(p.Translated(n * dl)) && m.material(p.Translated(n * -dl))) ++exposed;
      }
      if (!exposed || samples.empty()) continue;
      bool isEnd = (cap == 1) == (P.extSign > 0);
      auto& A = acc[P.fid];
      A.area += regionArea(P) * exposed / double(samples.size());
      A.minAng = std::min(A.minAng, ang);
      A.faces.push_back(P.faceName(isEnd ? "end" : "start"));
      A.capWidth = std::max(A.capWidth, regionMinThickness(P.segs));
    }
    // sides (lines + curves of joins; lines only for cuts — curved cuts are holes)
    for (auto& s : P.segs) {
      if (P.op == BodyOp::Cut && s.kind != SegKind::Line) continue;
      const int NT = s.kind == SegKind::Line ? 6 : 24, NW = 5;
      double segArea = 0, segMin = 90;
      int expo = 0;
      for (int i = 0; i < NT; ++i) {
        double t = (i + 0.5) / NT;
        V2 q = s.point(t), n2 = s.normal(t);
        if (P.inside2d(q + n2 * 1e-6)) n2 = n2 * -1.0;
        gp_Vec n = seg3(P, n2) * flip;
        double nb = n.Dot(bv);
        if (nb >= -1e-9) continue;
        double ang = std::acos(std::clamp(-nb, -1.0, 1.0)) * 180 / kPi;
        if (ang >= lim - 1e-9) continue;
        for (int j = 0; j < NW; ++j) {
          double w = wa + (wb - wa) * (j + 0.5) / NW;
          gp_Pnt p = P.toWorld(q, w);
          if (gp_Vec(p.XYZ()).Dot(bv) - zPlate < 1e-3) continue;
          if (!m.material(p.Translated(n * dl)) && m.material(p.Translated(n * -dl))) {
            ++expo;
            segArea += s.length() / NT * (wb - wa) / NW;
            segMin = std::min(segMin, ang);
          }
        }
      }
      if (!expo) continue;
      auto& A = acc[P.fid];
      A.area += segArea;
      if (segMin < A.minAng || !A.haveSide) {
        if (s.kind == SegKind::Line) {
          A.worstSide = {false, P.fid, P.instance, s.entityId, 0};
          A.haveSide = true;
        }
      }
      A.minAng = std::min(A.minAng, segMin);
      A.capOnly = false;
      A.faces.push_back(P.faceName("side.e" + std::to_string(s.entityId)));
    }
  }
  for (auto& [fid, A] : acc) {
    if (A.area < 1e-3) continue;
    bool bridge = A.capOnly && A.capWidth <= amBridgeLimit(c.s.am.process) + 1e-9;
    Suggestion sg;
    if (A.haveSide && A.minAng > 0.5) {
      EntId e = A.worstSide;
      gp_Vec bb = bv;
      Metric am = [e, bb](const AModel& mm) -> std::optional<double> {
        const Prism* P = findPrism(mm, e.fid, e.instance);
        if (!P) return std::nullopt;
        const Seg* s = findSeg(*P, e.eid);
        if (!s) return std::nullopt;
        V2 q = s->point(0.5), n2 = s->normal(0.5);
        if (P->inside2d(q + n2 * 1e-6)) n2 = n2 * -1.0;
        gp_Vec n = seg3(*P, n2) * (P->op == BodyOp::Cut ? -1.0 : 1.0);
        return std::acos(std::clamp(-n.Dot(bb), -1.0, 1.0)) * 180 / kPi;
      };
      const Prism* P = findPrism(c.m, e.fid, e.instance);
      std::vector<std::string> ex;
      if (P && P->sketch >= 0) ex = c.m.sketches[P->sketch].exprs();
      sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, ex), am, lim, Want::AtLeast);
    }
    std::string msg = bridge ? "Kısa köprü (≤ " + fmt(amBridgeLimit(c.s.am.process), 1) + " mm) yatay aşağı bakan yüzey, alan ≈ " +
                                   fmt(A.area, 1) + " mm²: " + amName(c.s.am.process) + " ile desteksiz basılabilir, yüzey kalitesi düşer."
                             : "Destek gerektiren sarkan yüzey: en düşük açı " + fmt(A.minAng, 0) + "° < " + fmt(lim, 0) +
                                   "° (" + amName(c.s.am.process) + "), alan ≈ " + fmt(A.area, 1) +
                                   " mm². Destek ekleyin, yüzeyi ≥ " + fmt(lim, 0) + "° eğimli yapın (pah) veya baskı yönünü değiştirin.";
    emit(c, bridge ? Severity::Info : Severity::Warning, Domain::Additive, "AM.OVERHANG", fid, A.minAng, lim, msg, sg, A.faces);
  }
}

// ---------------------------------------------------------------------------- horizontal cylindrical cuts (AM)
void horizontalCutRule(Ctx& c) {
  if (!c.am || !amUsesSupports(c.s.am.process) || c.s.am.minOverhangAngle <= 0) return;
  for (auto& P : c.m.prisms) {
    if (P.op != BodyOp::Cut || P.instance != 0) continue;
    double ang = std::asin(std::min(1.0, std::fabs(gp_Vec(P.frame.Direction()).Dot(gp_Vec(c.b))))) * 180 / kPi;
    if (ang >= c.s.am.minOverhangAngle - 1e-9) continue;
    for (auto& s : P.segs) {
      if (s.kind != SegKind::Circle || 2 * s.r <= c.s.am.maxSelfSupportingHoleDia + 1e-9) continue;
      EntId e{false, P.fid, 0, s.entityId, 0};
      Basis B = makeBasis(P.frame.Direction());
      Metric dm = [e, B](const AModel& mm) -> std::optional<double> {
        auto sg = entSeg(mm, e, B);
        if (!sg || sg->kind == SegKind::Line) return std::nullopt;
        return 2 * sg->r;
      };
      std::vector<std::string> ex;
      if (P.sketch >= 0) ex = c.m.sketches[P.sketch].exprs();
      Suggestion sg = suggestByProbe(c.doc, c.v, orderCandidates(c.doc, ex), dm, c.s.am.maxSelfSupportingHoleDia, Want::AtMost);
      emit(c, Severity::Warning, Domain::Additive, "AM.HORIZONTAL_HOLE", P.fid, 2 * s.r, c.s.am.maxSelfSupportingHoleDia,
           "Yatay dairesel kesim Ø" + fmt(2 * s.r) + " mm > " +
               amLimitText(c, "kendini taşıyan delik sınırı", c.s.am.maxSelfSupportingHoleDia) +
               "; destek ya da damla (teardrop) profili gerekir.",
           sg, {P.faceName("side.e" + std::to_string(s.entityId))});
    }
  }
}

// ---------------------------------------------------------------------------- turning
void turningRule(Ctx& c) {
  if (!c.turning) return;
  gp_Ax1 axis(gp_Pnt(0, 0, 0), c.k);
  for (auto& P : c.m.prisms) {
    if (P.instance != 0 && P.fid == P.srcFid) continue;
    bool ok = parallel(P.frame.Direction(), c.k, 0.5);
    if (ok)
      for (auto& s : P.segs) {
        if (s.kind != SegKind::Circle) { ok = false; break; }
        gp_Pnt cc = P.toWorld(s.c, 0);
        gp_Vec rel(gp_Pnt(0, 0, 0), cc);
        gp_Vec radial = rel - gp_Vec(c.k) * rel.Dot(gp_Vec(c.k));
        if (radial.Magnitude() > 1e-6) { ok = false; break; }
      }
    if (!ok)
      emit(c, Severity::Warning, Domain::Conventional, "CNC.TURN_AXISYMMETRY", P.fid, 0, 0,
           P.fid + " iş mili eksenine göre dönel simetrik değil: tornalamada üretilemez, canlı takım/frezeleme gerekir.",
           Suggestion{});
  }
  for (auto& H : c.m.holes) {
    gp_Vec rel(gp_Pnt(0, 0, 0), H.top);
    gp_Vec radial = rel - gp_Vec(c.k) * rel.Dot(gp_Vec(c.k));
    if (!parallel(H.axis, c.k, 0.5) || radial.Magnitude() > 1e-6)
      emit(c, Severity::Info, Domain::Conventional, "CNC.TURN_AXISYMMETRY", H.fid, 0, 0,
           H.fid + " deliği eksen dışında: tornada canlı takım (C ekseni) veya ayrı delme operasyonu gerekir.", Suggestion{},
           {H.faceName("wall")});
  }
}

}  // namespace

// ============================================================================ entry
Report checkAnalyticImpl(const Document& doc, const ParamValues& values, const MfgSettings& s) {
  Report rep;
  AModel m = buildModel(doc, values);
  Ctx c(doc, values, s, m);
  c.am = s.am.enabled && s.am.process != AmProcess::None;
  c.conv = s.conv.enabled && s.conv.process != ConvProcess::None;
  c.milling = c.conv && isConvMilling(s.conv);
  c.turning = c.conv && s.conv.process == ConvProcess::CNC_Turning;
  c.five = c.conv && s.conv.process == ConvProcess::CNC_Milling5Axis;
  c.b = safeDir(s.am.buildDirX, s.am.buildDirY, s.am.buildDirZ, gp_Dir(0, 0, 1));
  c.k = safeDir(s.conv.toolAxisX, s.conv.toolAxisY, s.conv.toolAxisZ, gp_Dir(0, 0, 1));
  c.hasBody = m.extents(c.b, c.zmin, c.zmax);
  computeBodyZmin(c);
  for (auto& id : m.unsolvedSketches) {
    Issue is;
    is.severity = Severity::Info;
    is.domain = Domain::Model;
    is.ruleId = "ANALYTIC.SKETCH_UNSOLVED";
    is.featureId = id;
    const SketchGeo* g = m.sketch(id);
    is.message = id + " çizimi çözülemedi" + (g && !g->solveMsg.empty() ? " (" + g->solveMsg + ")" : std::string()) +
                 "; analitik kontroller kayıtlı koordinatlarla yapıldı.";
    rep.issues.push_back(is);
  }
  if (!c.hasBody || (!c.am && !c.conv)) return rep;
  SlabOut so;
  slabAnalysis(c, so);
  refinePairs(m, so);
  wallRules(c, so);
  microCutRules(c);
  holeRules(c);
  prismRules(c);
  edgeTreatRules(c);
  shellRules(c);
  if (c.am) envelopeRule(c, true);
  if (c.conv) envelopeRule(c, false);
  overhangRule(c);
  horizontalCutRule(c);
  turningRule(c);
  dedupe(c.out);
  for (auto& i : c.out) {
    if (i.body.empty() && !i.featureId.empty()) {
      auto it = m.featureBody.find(i.featureId);
      if (it != m.featureBody.end()) i.body = it->second;
    }
    // v3: the kept tool of a cut/intersect (keepTool=1) is modelled as prism "<fid>.tool": report it on <fid>
    if (i.featureId.size() > 5 && i.featureId.compare(i.featureId.size() - 5, 5, ".tool") == 0) {
      std::string base = i.featureId.substr(0, i.featureId.size() - 5);
      for (size_t pos; (pos = i.message.find(i.featureId + " ")) != std::string::npos;)
        i.message.replace(pos, i.featureId.size(), base);
      i.featureId = base;
    }
    if (m.multiBody() && !i.body.empty() && i.message.find("Gövde ") != 0) i.message = "Gövde " + i.body + ": " + i.message;
    rep.issues.push_back(i);
  }
  return rep;
}

}  // namespace pc::mfg::detail
