// PersistCAD mfg — GEOMETRIC DfAM / DfM rules on the regenerated B-Rep (pc::model::ModelResult).
//
//  * model status errors                          -> MODEL.FEATURE_FAILED
//  * bounding box in build / spindle frame        -> AM.BUILD_ENVELOPE, CNC.STOCK_ENVELOPE
//  * solids with more than one shell (voids)      -> AM.TRAPPED_POWDER, CNC.ENCLOSED_VOID ; several solids -> MODEL.MULTI_BODY
//  * wall thickness: sampled points on every face, ray along the inward normal (IntCurvesFace_ShapeIntersector),
//    first exit through a NON-adjacent, facing face (incidence < 60 deg)   -> AM.MIN_WALL, CNC.MIN_WALL
//  * down-facing area flatter than minOverhangAngle, not on the build plate -> AM.OVERHANG
//  * concave horizontal cylinders Ø > self-supporting                       -> AM.HORIZONTAL_HOLE
//  * sharp concave line edges parallel to the spindle (internal vertical corners) -> CNC.SHARP_INTERNAL_CORNER
//  * concave cylinders parallel to the spindle with R < tool radius (fillets / milled radii) -> CNC.FILLET_TOOL_RADIUS /
//    CNC.CORNER_RADIUS
//  * 3-axis accessibility: samples whose normal faces away from both +-toolAxis or whose ray along the
//    approach direction is blocked                                           -> CNC.UNDERCUT
//  * hole walls (Hole features): cylinder length / Ø                         -> CNC.HOLE_LD
// Every issue carries persistent face/edge names, featureId = prefix of the face name, the driving
// parameters of that feature and (where the rule maps onto one property) a suggested value.
#include "MfgInternal.h"

#include "pc/Expression.h"

#include <BRepAlgoAPI_Common.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepTools.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeAnalysis_Surface.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax3.hxx>
#include <gp_Lin.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace pc::mfg::detail {
namespace {

struct Sample {
  gp_Pnt p;
  gp_Dir n;     // outward
  double w = 0; // area weight
};

struct FaceData {
  TopoDS_Face face;
  std::string name, fid;
  std::vector<Sample> samples;
  double area = 0;
  GeomAbs_SurfaceType type = GeomAbs_OtherSurface;
};

std::string fidOf(const std::string& name) {
  auto p = name.find('.');
  return p == std::string::npos ? name : name.substr(0, p);
}

bool outwardNormal(const BRepAdaptor_Surface& s, const TopoDS_Face& f, double u, double v, gp_Pnt& p, gp_Dir& n) {
  gp_Vec du, dv;
  s.D1(u, v, p, du, dv);
  gp_Vec nn = du.Crossed(dv);
  if (nn.Magnitude() < 1e-12) return false;
  n = gp_Dir(nn);
  if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
  return true;
}

struct Geo {
  const Document& doc;
  const model::ModelResult& mr;
  const MfgSettings& s;
  std::vector<Issue> out;
  TopoDS_Shape shape;   // v2: shape under analysis (one body, or the whole part)
  std::string body;     // v2: body name ("" = whole part)
  bool multi = false;   // part has > 1 body
  bool am = false, conv = false, milling = false, turning = false, five = false;
  gp_Dir b{0, 0, 1}, k{0, 0, 1};
  TopTools_IndexedMapOfShape faceMap;
  std::vector<FaceData> faces;  // index = faceMap index - 1
  TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
  std::vector<std::set<int>> adjacent;
  double size = 1;
  Geo(const Document& d, const model::ModelResult& m, const MfgSettings& ss) : doc(d), mr(m), s(ss), shape(m.shape) {}
};

int historyIndex(const Document& doc, const std::string& fid) {
  int i = doc.indexOf(fid);
  return i < 0 ? -1 : i;
}

std::vector<std::string> featureParams(const Document& doc, const std::string& fid, const std::vector<std::string>& keys) {
  const Feature* f = doc.feature(fid);
  if (!f) return {};
  std::vector<std::string> ex;
  for (auto& k : keys) ex.push_back(f->prop(k));
  for (auto& [k, v] : f->props) ex.push_back(v);
  if (const Feature* sk = doc.feature(f->prop("sketch")); sk && sk->sketch)
    for (auto& c : sk->sketch->constraints) ex.push_back(c.valueExpr);
  std::vector<std::string> clean;
  for (auto& e : ex) {
    if (e.empty()) continue;
    try {
      referencedNames(e);
      clean.push_back(e);
    } catch (const std::exception&) {}
  }
  return orderCandidates(doc, clean);
}

void addIssue(Geo& g, Severity sev, Domain dom, const std::string& rule, const std::string& fid, double actual, double limit,
              const std::string& msg, std::vector<std::string> params, std::vector<std::string> faces,
              std::vector<std::string> edges = {}, std::optional<double> sugg = {}) {
  Issue is;
  is.severity = sev;
  is.domain = dom;
  is.ruleId = rule;
  is.featureId = fid;
  is.actual = actual;
  is.limit = limit;
  is.message = msg;
  is.params = std::move(params);
  is.suggestedValue = sugg;
  is.faces = std::move(faces);
  is.edges = std::move(edges);
  g.out.push_back(std::move(is));
}

std::string sugText(const Document& doc, const std::string& p, double v, int rel) {
  const Parameter* P = doc.params.find(p);
  std::string unit = !P ? "" : P->kind == ParamKind::Length ? " mm" : P->kind == ParamKind::Angle ? "°" : "";
  return " " + p + (rel > 0 ? " ≥ " : rel < 0 ? " ≤ " : " = ") + fmt(v) + unit + " önerilir.";
}

// suggestion through a single feature property expression
void propSuggestion(Geo& g, const std::string& fid, const std::string& key, double lim, Want want, Issue& is) {
  const Feature* f = g.doc.feature(fid);
  if (!f || f->prop(key).empty()) return;
  Suggestion sg = suggestByExpr(g.doc, g.mr.params, f->prop(key), lim, want);
  if (!sg.value || sg.params.empty()) return;
  is.params = sg.params;
  is.suggestedValue = sg.value;
  is.message += sugText(g.doc, sg.params[0], *sg.value, sg.relation);
}

// ---------------------------------------------------------------------------- setup
void collectFaces(Geo& g) {
  TopExp::MapShapes(g.shape, TopAbs_FACE, g.faceMap);
  g.faces.resize(g.faceMap.Extent());
  std::map<int, std::string> names;
  for (auto& [n, f] : g.mr.faces) {
    int i = g.faceMap.FindIndex(f);
    if (i > 0) names[i] = n;
  }
  for (int i = 1; i <= g.faceMap.Extent(); ++i) {
    FaceData& fd = g.faces[i - 1];
    fd.face = TopoDS::Face(g.faceMap(i));
    fd.name = names.count(i) ? names[i] : "";
    fd.fid = fidOf(fd.name);
    GProp_GProps gp;
    BRepGProp::SurfaceProperties(fd.face, gp);
    fd.area = gp.Mass();
    BRepAdaptor_Surface sa(fd.face, Standard_True);
    fd.type = sa.GetType();
    double u0, u1, v0, v1;
    BRepTools::UVBounds(fd.face, u0, u1, v0, v1);
    const int n = fd.type == GeomAbs_Plane ? 5 : 6;
    BRepClass_FaceClassifier cls;
    std::vector<Sample> smp;
    for (int a = 0; a < n; ++a)
      for (int c = 0; c < n; ++c) {
        double u = u0 + (u1 - u0) * (a + 0.5) / n, v = v0 + (v1 - v0) * (c + 0.5) / n;
        cls.Perform(fd.face, gp_Pnt2d(u, v), 1e-7);
        if (cls.State() != TopAbs_IN) continue;
        Sample sp;
        if (!outwardNormal(sa, fd.face, u, v, sp.p, sp.n)) continue;
        smp.push_back(sp);
      }
    for (auto& sp : smp) sp.w = fd.area / smp.size();
    fd.samples = std::move(smp);
  }
  TopExp::MapShapesAndAncestors(g.shape, TopAbs_EDGE, TopAbs_FACE, g.edgeFaces);
  g.adjacent.assign(g.faces.size(), {});
  for (int i = 1; i <= g.edgeFaces.Extent(); ++i) {
    std::vector<int> ids;
    for (TopTools_ListIteratorOfListOfShape it(g.edgeFaces(i)); it.More(); it.Next()) {
      int fi = g.faceMap.FindIndex(it.Value());
      if (fi > 0) ids.push_back(fi - 1);
    }
    for (int a : ids)
      for (int c : ids)
        if (a != c) g.adjacent[a].insert(c);
  }
  Bnd_Box bb;
  BRepBndLib::Add(g.shape, bb);
  if (!bb.IsVoid()) {
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    g.size = std::max(1.0, std::max({x1 - x0, y1 - y0, z1 - z0}));
  }
}

// ---------------------------------------------------------------------------- rules
void statusRule(Geo& g) {
  for (auto& st : g.mr.status) {
    if (st.ok) continue;
    std::vector<std::string> params = featureParams(g.doc, st.featureId, {});
    addIssue(g, Severity::Error, Domain::Model, "MODEL.FEATURE_FAILED", st.featureId, 0, 0,
             st.message.empty() ? st.featureId + " oluşturulamadı." : st.message, params, {});
  }
}

bool frameBox(const TopoDS_Shape& sh, const gp_Dir& z, double& sx, double& sy, double& sz, double& zmin) {
  gp_Vec Z(z);
  gp_Vec u = gp_Vec(1, 0, 0) - Z * z.X();
  if (u.Magnitude() < 1e-6) u = gp_Vec(0, 1, 0) - Z * z.Y();
  gp_Ax3 ax(gp_Pnt(0, 0, 0), z, gp_Dir(u));
  gp_Trsf T;
  T.SetTransformation(ax);  // world -> frame coordinates
  BRepBuilderAPI_Transform tr(sh, T, Standard_False);
  Bnd_Box bb;
  BRepBndLib::AddOptimal(tr.Shape(), bb, Standard_False, Standard_False);
  if (bb.IsVoid()) return false;
  double x0, y0, z0, x1, y1, z1;
  bb.Get(x0, y0, z0, x1, y1, z1);
  sx = x1 - x0;
  sy = y1 - y0;
  sz = z1 - z0;
  zmin = z0;
  return true;
}

void envelopeRule(Geo& g) {
  auto run = [&](bool amSide) {
    double sx, sy, sz, zmin;
    if (!frameBox(g.shape, amSide ? g.b : g.k, sx, sy, sz, zmin)) return;
    double BX = amSide ? g.s.am.buildX : g.s.conv.stockX, BY = amSide ? g.s.am.buildY : g.s.conv.stockY,
           BZ = amSide ? g.s.am.buildZ : g.s.conv.stockZ;
    const char* rule = amSide ? "AM.BUILD_ENVELOPE" : "CNC.STOCK_ENVELOPE";
    Domain dom = amSide ? Domain::Additive : Domain::Conventional;
    std::string pre = g.body.empty() && g.multi ? "Tüm yapı: " : "";
    std::string what = amSide ? std::string(amName(g.s.am.process)) + " yapı hacmi" : std::string(convName(g.s.conv.process)) + " tezgah/ham malzeme";
    const double tol = 1e-3;
    if (!amSide && g.turning) return;  // analytic rule handles bar stock
    if (sz > BZ + tol)
      addIssue(g, Severity::Error, dom, rule, "", sz, BZ, pre + "Parça boyutu Z = " + fmt(sz) + " mm > " + what + " " + fmt(BZ) + " mm.", {}, {});
    bool fits = (sx <= BX + tol && sy <= BY + tol) || (sx <= BY + tol && sy <= BX + tol);
    if (!fits)
      addIssue(g, Severity::Error, dom, rule, "", std::max(sx, sy), std::max(BX, BY),
               pre + "Parça taban ölçüleri " + fmt(sx) + " × " + fmt(sy) + " mm > " + what + " " + fmt(BX) + " × " + fmt(BY) + " mm.", {}, {});
  };
  if (g.am) run(true);
  if (g.conv) run(false);
}

void voidRule(Geo& g) {
  for (TopExp_Explorer ex(g.shape, TopAbs_SOLID); ex.More(); ex.Next()) {
    TopoDS_Solid so = TopoDS::Solid(ex.Current());
    TopoDS_Shell outer = BRepClass3d::OuterShell(so);
    for (TopExp_Explorer sh(so, TopAbs_SHELL); sh.More(); sh.Next()) {
      if (!outer.IsNull() && sh.Current().IsSame(outer)) continue;
      std::vector<std::string> fn;
      std::string fid;
      for (TopExp_Explorer fe(sh.Current(), TopAbs_FACE); fe.More(); fe.Next()) {
        int i = g.faceMap.FindIndex(fe.Current());
        if (i > 0 && !g.faces[i - 1].name.empty()) {
          fn.push_back(g.faces[i - 1].name);
          if (fid.empty()) fid = g.faces[i - 1].fid;
        }
      }
      if (g.am && amIsPowderOrResin(g.s.am.process))
        addIssue(g, Severity::Error, Domain::Additive, "AM.TRAPPED_POWDER", fid, 0, g.s.am.minEscapeHoleDia,
                 "Kapalı iç boşluk (gövdede ikinci kabuk): " + std::string(g.s.am.process == AmProcess::SLA_Resin ? "reçine" : "toz") +
                     " hapsolur. En az iki adet ≥ Ø" + fmt(g.s.am.minEscapeHoleDia) + " mm boşaltma deliği ekleyin.",
                 featureParams(g.doc, fid, {}), fn);
      if (g.conv)
        addIssue(g, Severity::Error, Domain::Conventional, "CNC.ENCLOSED_VOID", fid, 0, 0,
                 "Kapalı iç boşluk talaşlı imalatla üretilemez.", featureParams(g.doc, fid, {}), fn);
    }
  }
}

void thicknessRule(Geo& g, IntCurvesFace_ShapeIntersector& inter) {
  struct Best {
    double t = std::numeric_limits<double>::infinity();
    int fa = -1, fb = -1;
  };
  std::vector<Best> best(g.faces.size());
  const double pinf = 1e-4 * std::max(1.0, g.size / 100);
  double cap = std::max(g.am ? g.s.am.minWallThickness : 0.0, g.conv ? g.s.conv.minWallThickness : 0.0) * 1.0001;
  if (cap <= 0) return;
  for (int i = 0; i < int(g.faces.size()); ++i) {
    FaceData& fd = g.faces[i];
    for (auto& sp : fd.samples) {
      gp_Dir d = sp.n.Reversed();
      inter.Perform(gp_Lin(sp.p, d), pinf, cap * 1.5 + pinf);
      if (!inter.IsDone()) continue;
      for (int h = 1; h <= inter.NbPnt(); ++h) {
        double w = inter.WParameter(h);
        if (w <= pinf) continue;
        int fi = g.faceMap.FindIndex(inter.Face(h)) - 1;
        if (fi < 0 || fi == i || g.adjacent[i].count(fi)) continue;
        BRepAdaptor_Surface hs(inter.Face(h), Standard_True);
        gp_Pnt hp;
        gp_Dir hn;
        if (!outwardNormal(hs, inter.Face(h), inter.UParameter(h), inter.VParameter(h), hp, hn)) continue;
        if (hn.Dot(d) < 0.5) continue;  // must exit the material roughly head-on
        if (w < best[i].t) best[i] = {w, i, fi};
      }
    }
  }
  struct Agg {
    double t = std::numeric_limits<double>::infinity();
    std::vector<std::string> faces;
  };
  std::map<std::string, Agg> amA, cncA;
  for (auto& bst : best) {
    if (bst.fa < 0) continue;
    const FaceData& A = g.faces[bst.fa];
    const FaceData& B = g.faces[bst.fb];
    std::string fid = historyIndex(g.doc, A.fid) >= historyIndex(g.doc, B.fid) ? A.fid : B.fid;
    auto add = [&](std::map<std::string, Agg>& m) {
      Agg& a = m[fid];
      a.t = std::min(a.t, bst.t);
      for (auto* n : {&A.name, &B.name})
        if (!n->empty() && std::find(a.faces.begin(), a.faces.end(), *n) == a.faces.end()) a.faces.push_back(*n);
    };
    if (g.am && bst.t < g.s.am.minWallThickness - 1e-6) add(amA);
    if (g.conv && bst.t < g.s.conv.minWallThickness - 1e-6) add(cncA);
  }
  auto emitWall = [&](std::map<std::string, Agg>& m, bool amSide) {
    for (auto& [fid, a] : m) {
      double lim = amSide ? g.s.am.minWallThickness : g.s.conv.minWallThickness;
      std::string lt = amSide ? std::string(amName(g.s.am.process)) + " min. et kalınlığı " + fmt(lim) + " mm (" + g.s.am.material + ")"
                              : std::string(convName(g.s.conv.process)) + " min. et kalınlığı " + fmt(lim) + " mm (" + g.s.conv.material + ")";
      Issue is;
      is.severity = Severity::Error;
      is.domain = amSide ? Domain::Additive : Domain::Conventional;
      is.ruleId = amSide ? "AM.MIN_WALL" : "CNC.MIN_WALL";
      is.featureId = fid;
      is.actual = a.t;
      is.limit = lim;
      is.faces = a.faces;
      is.message = "Ölçülen et kalınlığı " + fmt(a.t) + " mm < " + lt + ".";
      is.params = featureParams(g.doc, fid, {"thickness", "distance", "diameter", "radius", "depth"});
      const Feature* f = g.doc.feature(fid);
      if (f && f->type == FeatureType::Shell) propSuggestion(g, fid, "thickness", lim, Want::AtLeast, is);
      g.out.push_back(is);
    }
  };
  emitWall(amA, true);
  emitWall(cncA, false);
}

void overhangRule(Geo& g, double zmin) {
  if (!g.am || !amUsesSupports(g.s.am.process) || g.s.am.minOverhangAngle <= 0) return;
  const double lim = g.s.am.minOverhangAngle;
  gp_Vec bv(g.b);
  struct Agg {
    double area = 0, minAng = 90;
    std::vector<std::string> faces;
  };
  std::map<std::string, Agg> acc;
  for (auto& fd : g.faces) {
    // horizontal concave cylinders are judged by the hole rule below
    BRepAdaptor_Surface sa(fd.face, Standard_True);
    bool horizHole = false;
    if (fd.type == GeomAbs_Cylinder) {
      gp_Cylinder cy = sa.Cylinder();
      double ang = std::asin(std::min(1.0, std::fabs(cy.Axis().Direction().Dot(g.b)))) * 180 / kPi;
      bool concave = !fd.samples.empty() &&
                     gp_Vec(fd.samples[0].n).Dot(gp_Vec(cy.Axis().Location(), fd.samples[0].p) -
                                                 gp_Vec(cy.Axis().Direction()) *
                                                     gp_Vec(cy.Axis().Location(), fd.samples[0].p).Dot(gp_Vec(cy.Axis().Direction()))) < 0;
      if (concave && ang < lim) horizHole = true;
    }
    if (horizHole) continue;
    double area = 0, minAng = 90;
    for (auto& sp : fd.samples) {
      double nb = gp_Vec(sp.n).Dot(bv);
      if (nb >= -1e-9) continue;
      double ang = std::acos(std::clamp(-nb, -1.0, 1.0)) * 180 / kPi;
      if (ang >= lim - 1e-6) continue;
      if (gp_Vec(sp.p.XYZ()).Dot(bv) - zmin < 1e-3 * std::max(1.0, g.size / 100)) continue;  // on the plate
      area += sp.w;
      minAng = std::min(minAng, ang);
    }
    if (area <= 1e-6) continue;
    Agg& a = acc[fd.fid];
    a.area += area;
    a.minAng = std::min(a.minAng, minAng);
    if (!fd.name.empty()) a.faces.push_back(fd.name);
  }
  for (auto& [fid, a] : acc) {
    if (a.area < 0.5) continue;  // tiny slivers (sampling noise)
    addIssue(g, Severity::Warning, Domain::Additive, "AM.OVERHANG", fid, a.minAng, lim,
             "Destek gerektiren sarkan yüzey(ler): en düşük açı " + fmt(a.minAng, 0) + "° < " + fmt(lim, 0) + "° (" +
                 amName(g.s.am.process) + "), alan ≈ " + fmt(a.area, 1) + " mm². Destek ekleyin, pah/eğim verin veya yönlendirmeyi değiştirin.",
             featureParams(g.doc, fid, {}), a.faces);
  }
}

bool concaveCylinder(const FaceData& fd, gp_Cylinder& cy) {
  if (fd.type != GeomAbs_Cylinder || fd.samples.empty()) return false;
  BRepAdaptor_Surface sa(fd.face, Standard_True);
  cy = sa.Cylinder();
  const Sample& sp = fd.samples[0];
  gp_Vec rel(cy.Axis().Location(), sp.p);
  gp_Vec ax(cy.Axis().Direction());
  gp_Vec radial = rel - ax * rel.Dot(ax);
  return gp_Vec(sp.n).Dot(radial) < 0;
}

void cylinderRules(Geo& g) {
  for (auto& fd : g.faces) {
    gp_Cylinder cy;
    if (!concaveCylinder(fd, cy)) continue;
    double r = cy.Radius();
    const Feature* f = g.doc.feature(fd.fid);
    bool isHole = f && f->type == FeatureType::Hole;
    if (f && f->type == FeatureType::Pattern) {
      const Feature* src = g.doc.feature(f->prop("source"));
      isHole = src && src->type == FeatureType::Hole;
    }
    gp_Dir ax = cy.Axis().Direction();
    // AM horizontal holes
    if (g.am && amUsesSupports(g.s.am.process) && g.s.am.minOverhangAngle > 0) {
      double ang = std::asin(std::min(1.0, std::fabs(ax.Dot(g.b)))) * 180 / kPi;
      if (ang < g.s.am.minOverhangAngle - 1e-6 && 2 * r > g.s.am.maxSelfSupportingHoleDia + 1e-6) {
        Issue is;
        is.severity = Severity::Warning;
        is.domain = Domain::Additive;
        is.ruleId = "AM.HORIZONTAL_HOLE";
        is.featureId = fd.fid;
        is.actual = 2 * r;
        is.limit = g.s.am.maxSelfSupportingHoleDia;
        is.faces = {fd.name};
        is.message = "Yatay delik/silindirik boşluk Ø" + fmt(2 * r) + " mm > " + amName(g.s.am.process) + " kendini taşıyan sınır " +
                     fmt(g.s.am.maxSelfSupportingHoleDia) + " mm; destek ya da damla profili gerekir.";
        is.params = featureParams(g.doc, fd.fid, {"diameter"});
        if (isHole) propSuggestion(g, fd.fid, "diameter", g.s.am.maxSelfSupportingHoleDia, Want::AtMost, is);
        g.out.push_back(is);
      }
    }
    if (!g.conv) continue;
    // hole L/D from the drilled wall
    if (isHole && fd.name.find(".wall") != std::string::npos) {
      double u0, u1, v0, v1;
      BRepTools::UVBounds(fd.face, u0, u1, v0, v1);
      double L = std::fabs(v1 - v0);
      if (L / (2 * r) > g.s.conv.maxHoleDepthToDia + 1e-6) {
        Issue is;
        is.severity = Severity::Error;
        is.domain = Domain::Conventional;
        is.ruleId = "CNC.HOLE_LD";
        is.featureId = fd.fid;
        is.actual = L / (2 * r);
        is.limit = g.s.conv.maxHoleDepthToDia;
        is.faces = {fd.name};
        is.message = "Delik duvarı L/D = " + fmt(L) + "/" + fmt(2 * r) + " = " + fmt(L / (2 * r), 1) + " > matkap sınırı " +
                     fmt(g.s.conv.maxHoleDepthToDia, 1) + ".";
        is.params = featureParams(g.doc, fd.fid, {"diameter", "depth"});
        g.out.push_back(is);
      }
    }
    // milled concave radii along the spindle
    if (!g.milling || isHole) continue;
    if (!(g.five || std::fabs(ax.Dot(g.k)) > std::cos(1 * kPi / 180))) continue;
    double rt = g.s.conv.minToolDiameter / 2;
    if (r >= rt - 1e-6) continue;
    bool fil = f && f->type == FeatureType::Fillet;
    Issue is;
    is.severity = Severity::Error;
    is.domain = Domain::Conventional;
    is.ruleId = fil ? "CNC.FILLET_TOOL_RADIUS" : "CNC.CORNER_RADIUS";
    is.featureId = fd.fid;
    is.actual = r;
    is.limit = rt;
    is.faces = {fd.name};
    is.message = std::string(fil ? "İç köşe yuvarlatması" : "Frezelenen iç yarıçap") + " R" + fmt(r) + " mm < freze yarıçapı R" +
                 fmt(rt) + " mm (en küçük freze Ø" + fmt(g.s.conv.minToolDiameter) + " mm).";
    is.params = featureParams(g.doc, fd.fid, {"radius"});
    if (fil) propSuggestion(g, fd.fid, "radius", rt, Want::AtLeast, is);
    g.out.push_back(is);
  }
}

// normal of face F at a 3D point on it
bool normalAt(const TopoDS_Face& F, const gp_Pnt& p, gp_Dir& n, double tol) {
  Handle(Geom_Surface) S = BRep_Tool::Surface(F);
  if (S.IsNull()) return false;
  ShapeAnalysis_Surface sas(S);
  gp_Pnt2d uv = sas.ValueOfUV(p, tol);
  BRepAdaptor_Surface sa(F, Standard_True);
  gp_Pnt q;
  return outwardNormal(sa, F, uv.X(), uv.Y(), q, n);
}

void sharpEdgeRule(Geo& g) {
  if (!g.milling) return;
  std::map<std::string, std::pair<std::vector<std::string>, std::vector<std::string>>> byFid;  // faces, edges
  for (int i = 1; i <= g.edgeFaces.Extent(); ++i) {
    const TopoDS_Edge& e = TopoDS::Edge(g.edgeFaces.FindKey(i));
    if (BRep_Tool::Degenerated(e)) continue;
    const TopTools_ListOfShape& lst = g.edgeFaces(i);
    if (lst.Extent() != 2) continue;
    BRepAdaptor_Curve cu(e);
    if (cu.GetType() != GeomAbs_Line) continue;
    gp_Dir t = cu.Line().Direction();
    if (!(std::fabs(t.Dot(g.k)) > std::cos(1 * kPi / 180))) continue;
    if (BRep_Tool::IsClosed(e, TopoDS::Face(lst.First()))) continue;
    gp_Pnt p = cu.Value(0.5 * (cu.FirstParameter() + cu.LastParameter()));
    TopoDS_Face f1 = TopoDS::Face(lst.First()), f2 = TopoDS::Face(lst.Last());
    if (f1.IsSame(f2)) continue;
    gp_Dir n1, n2;
    double tol = 1e-6 * g.size;
    if (!normalAt(f1, p, n1, tol) || !normalAt(f2, p, n2, tol)) continue;
    if (std::fabs(n1.Dot(n2)) > std::cos(10 * kPi / 180)) continue;  // tangent / smooth
    // direction from the edge into face 1, perpendicular to the edge
    gp_Vec d1 = gp_Vec(t).Crossed(gp_Vec(n1));
    double delta = 1e-3 * std::max(1.0, g.size / 100);
    BRepClass_FaceClassifier fc(f1, p.Translated(d1 * delta), tol * 10);
    if (fc.State() != TopAbs_IN) {
      d1.Reverse();
      BRepClass_FaceClassifier fc2(f1, p.Translated(d1 * delta), tol * 10);
      if (fc2.State() != TopAbs_IN) continue;
    }
    bool concave = d1.Dot(gp_Vec(n2)) > 1e-6;
    if (!concave) continue;
    int i1 = g.faceMap.FindIndex(f1) - 1, i2 = g.faceMap.FindIndex(f2) - 1;
    if (i1 < 0 || i2 < 0) continue;
    const FaceData& A = g.faces[i1];
    const FaceData& B = g.faces[i2];
    std::string fid = historyIndex(g.doc, A.fid) >= historyIndex(g.doc, B.fid) ? A.fid : B.fid;
    std::string en = g.mr.nameOf(e);
    auto& entry = byFid[fid];
    for (auto* n : {&A.name, &B.name})
      if (!n->empty() && std::find(entry.first.begin(), entry.first.end(), *n) == entry.first.end()) entry.first.push_back(*n);
    if (!en.empty()) entry.second.push_back(en);
  }
  double rt = g.s.conv.minToolDiameter / 2;
  for (auto& [fid, fe] : byFid)
    addIssue(g, Severity::Error, Domain::Conventional, "CNC.SHARP_INTERNAL_CORNER", fid, 0, rt,
             "Keskin iç dikey köşe (R = 0, " + std::to_string(std::max<size_t>(1, fe.second.size())) + " kenar): Ø" +
                 fmt(g.s.conv.minToolDiameter) + " mm freze en az R" + fmt(rt) + " mm bırakır. Köşelere ≥ R" + fmt(rt) +
                 " mm yuvarlatma ekleyin.",
             featureParams(g.doc, fid, {}), fe.first, fe.second);
}

void undercutRule(Geo& g, IntCurvesFace_ShapeIntersector& inter) {
  if (!g.milling || g.five) return;
  const double eps = 1e-3 * std::max(1.0, g.size / 100);
  std::map<std::string, std::pair<double, std::vector<std::string>>> byFid;
  for (auto& fd : g.faces) {
    if (fd.samples.empty()) continue;
    int bad = 0;
    for (auto& sp : fd.samples) {
      double nk = gp_Vec(sp.n).Dot(gp_Vec(g.k));
      bool reach = false;
      for (int sgn : {+1, -1}) {
        if (sgn * nk < -1e-3) continue;  // faces away from this approach direction
        gp_Dir dir = sgn > 0 ? g.k : g.k.Reversed();
        gp_Pnt o = sp.p.Translated(gp_Vec(sp.n) * eps);
        inter.Perform(gp_Lin(o, dir), 0.0, RealLast());
        bool blocked = false;
        if (inter.IsDone())
          for (int h = 1; h <= inter.NbPnt(); ++h)
            if (inter.WParameter(h) > eps * 0.5) { blocked = true; break; }
        if (!blocked) { reach = true; break; }
      }
      if (!reach) ++bad;
    }
    if (bad * 3 < int(fd.samples.size()) || bad == 0) continue;
    auto& e = byFid[fd.fid];
    e.first += fd.area * bad / fd.samples.size();
    if (!fd.name.empty()) e.second.push_back(fd.name);
  }
  for (auto& [fid, e] : byFid)
    addIssue(g, Severity::Warning, Domain::Conventional, "CNC.UNDERCUT", fid, e.first, 0,
             "Takım ekseni (±) boyunca erişilemeyen yüzey(ler), alan ≈ " + fmt(e.first, 1) +
                 " mm²: 3 eksende alttan kesme; T-freze, ek bağlama veya 5 eksen gerekir.",
             featureParams(g.doc, fid, {}), e.second);
}

}  // namespace

std::vector<model::BodyInfo> bodiesOf(const Document& doc, const model::ModelResult& mr) {
  if (!mr.bodies.empty()) {
    // Bodies whose creating feature carries the prop mfg=0 are reference geometry (e.g. an extracted fluid
    // volume, a CFD domain): they are not manufactured, so no body rule, interference or setup count sees them.
    std::vector<model::BodyInfo> out;
    for (auto& b : mr.bodies) {
      const Feature* cf = doc.feature(b.createdBy);
      if (cf && cf->prop("mfg", "1") == "0") continue;
      out.push_back(b);
    }
    return out;
  }
  std::vector<model::BodyInfo> out;
  if (mr.shape.IsNull()) return out;
  int k = 0;
  for (TopExp_Explorer ex(mr.shape, TopAbs_SOLID); ex.More(); ex.Next()) {
    model::BodyInfo b;
    b.name = "Body" + std::to_string(++k);
    b.shape = ex.Current();
    TopTools_IndexedMapOfShape fm;
    TopExp::MapShapes(b.shape, TopAbs_FACE, fm);
    for (auto& [n, f] : mr.faces)
      if (fm.FindIndex(f) > 0) b.faces.push_back(n);
    out.push_back(std::move(b));
  }
  if (out.size() == 1) {  // single solid: name it like the analytic simulation does (first body-creating feature)
    for (auto& f : doc.features)
      if (!f.suppressed && (f.type == FeatureType::Extrude || f.type == FeatureType::Revolve)) {
        out[0].name = f.prop("bodyName").empty() ? f.id : f.prop("bodyName");
        out[0].createdBy = f.id;
        break;
      }
  }
  return out;
}

namespace {

void setupFlags(Geo& g, const MfgSettings& s) {
  g.am = s.am.enabled && s.am.process != AmProcess::None;
  g.conv = s.conv.enabled && s.conv.process != ConvProcess::None;
  g.milling = g.conv && isConvMilling(s.conv);
  g.turning = g.conv && s.conv.process == ConvProcess::CNC_Turning;
  g.five = g.conv && s.conv.process == ConvProcess::CNC_Milling5Axis;
  g.b = safeDir(s.am.buildDirX, s.am.buildDirY, s.am.buildDirZ, gp_Dir(0, 0, 1));
  g.k = safeDir(s.conv.toolAxisX, s.conv.toolAxisY, s.conv.toolAxisZ, gp_Dir(0, 0, 1));
}

// every geometric rule on one shape (a body)
void runBodyRules(Geo& g) {
  collectFaces(g);
  envelopeRule(g);
  voidRule(g);
  IntCurvesFace_ShapeIntersector inter;
  inter.Load(g.shape, 1e-7);
  thicknessRule(g, inter);
  double sx, sy, sz, zmin = 0;
  frameBox(g.shape, g.b, sx, sy, sz, zmin);
  overhangRule(g, zmin);
  cylinderRules(g);
  sharpEdgeRule(g);
  undercutRule(g, inter);
}

void tagIssues(std::vector<Issue>& v, size_t from, const std::string& body, bool multi) {
  for (size_t i = from; i < v.size(); ++i) {
    if (v[i].body.empty()) v[i].body = body;
    if (multi && !body.empty() && v[i].message.find("Gövde ") != 0) v[i].message = "Gövde " + body + ": " + v[i].message;
  }
}

bool intentionalCreator(const Feature& f) {
  if (f.type == FeatureType::Combine) return f.prop("keepTools", "0") == "1";
  return f.prop("op") == "new" || f.prop("keepTool", "0") == "1" || !f.prop("bodyName").empty();
}

}  // namespace

Report checkBodiesImpl(const Document& doc, const model::ModelResult& mr, const MfgSettings& s) {
  Report rep;
  std::vector<model::BodyInfo> bodies = bodiesOf(doc, mr);
  const int n = int(bodies.size());
  int nsolids = 0;
  if (!mr.shape.IsNull())
    for (TopExp_Explorer ex(mr.shape, TopAbs_SOLID); ex.More(); ex.Next()) ++nsolids;
  // solids of reference bodies (creating feature has mfg=0, see bodiesOf) are not parts
  for (auto& b : mr.bodies) {
    const Feature* cf = doc.feature(b.createdBy);
    if (cf && cf->prop("mfg", "1") == "0" && !b.shape.IsNull())
      for (TopExp_Explorer ex(b.shape, TopAbs_SOLID); ex.More(); ex.Next()) --nsolids;
  }
  nsolids = std::max(nsolids, n);
  if (nsolids < 2) return rep;
  const bool am = s.am.enabled && s.am.process != AmProcess::None;
  const bool conv = s.conv.enabled && s.conv.process != ConvProcess::None;

  // ---- MODEL.MULTI_BODY: intentional (Info) vs unintended (Warning)
  std::string all;
  for (auto& b : bodies) all += (all.empty() ? "" : ", ") + b.name;
  std::vector<std::string> bad;
  std::string badFid;
  int docIntentional = 0;
  bool firstSolidSeen = false;
  for (auto& f : doc.features) {
    if (f.suppressed) continue;
    bool solid = f.type == FeatureType::Extrude || f.type == FeatureType::Revolve;
    if (solid && !firstSolidSeen) { firstSolidSeen = true; continue; }
    if ((solid || f.type == FeatureType::Pattern || f.type == FeatureType::Combine || f.type == FeatureType::Hole) && intentionalCreator(f))
      ++docIntentional;
  }
  // A feature whose own tool has k > 1 disjoint solids (e.g. Pattern op=new, keepTool of a multi-hole tool)
  // creates the bodies "<name>:1..k" by design (BODIES contract): those pieces are not a split.
  auto toolSolids = [&](const std::string& fid) {
    int k = 0;
    for (const auto& fi : mr.info)
      if (fi.featureId == fid && !fi.toolShape.IsNull())
        for (TopExp_Explorer ex(fi.toolShape, TopAbs_SOLID); ex.More(); ex.Next()) ++k;
    return k;
  };
  auto piecesByDesign = [&](const model::BodyInfo& b) {
    const size_t colon = b.name.rfind(':');
    if (colon == std::string::npos || b.createdBy.empty()) return false;
    const Feature* f = doc.feature(b.createdBy);
    if (!f || !intentionalCreator(*f) || f->type == FeatureType::Combine) return false;
    const std::string stem = b.name.substr(0, colon);
    const std::string own = !f->prop("bodyName").empty() ? f->prop("bodyName")
                            : f->prop("op") == "new"     ? f->id
                                                         : f->id + ".tool";
    if (stem != own) return false;
    int same = 0;
    for (const auto& o : bodies)
      if (o.name.rfind(stem + ":", 0) == 0) ++same;
    return same <= toolSolids(f->id);
  };
  for (int i = 0; i < n; ++i) {
    const auto& b = bodies[i];
    bool unintended = b.name.find(':') != std::string::npos && !piecesByDesign(b);
    if (!unintended && i > 0 && !b.createdBy.empty()) {
      const Feature* f = doc.feature(b.createdBy);
      unintended = !(f && intentionalCreator(*f));
    }
    if (unintended) {
      bad.push_back(b.name);
      if (badFid.empty()) badFid = b.createdBy;
    }
  }
  if (bad.empty() && nsolids > n) bad.push_back("(" + std::to_string(nsolids - n) + " ek katı)");
  bool createdByKnown = !mr.bodies.empty();
  if (!createdByKnown && bad.empty() && docIntentional < n - 1) bad.push_back("(bilinçsiz ayrılma)");
  {
    Issue is;
    is.domain = Domain::Model;
    is.ruleId = "MODEL.MULTI_BODY";
    is.actual = nsolids;
    is.limit = 1;
    if (bad.empty()) {
      is.severity = Severity::Info;
      is.message = "Model " + std::to_string(nsolids) + " gövdeden oluşuyor (" + all + "); gövdeler bilinçli olarak ayrı tutulmuş (Yeni gövde / kalan takım / Combine).";
    } else {
      std::string bl;
      for (auto& x : bad) bl += (bl.empty() ? "" : ", ") + x;
      is.severity = Severity::Warning;
      is.featureId = badFid;
      is.message = "Model " + std::to_string(nsolids) + " ayrık gövdeden oluşuyor (" + all + "); plan dışı ayrılan gövde(ler): " + bl +
                   ". Tek parça olarak üretilemez; ayrılmayı önleyin veya gövdeleri Combine ile birleştirin.";
    }
    rep.issues.push_back(is);
  }

  // ---- CNC.SEPARATE_PARTS
  if (conv) {
    Issue is;
    is.severity = Severity::Info;
    is.domain = Domain::Conventional;
    is.ruleId = "CNC.SEPARATE_PARTS";
    is.actual = n;
    is.limit = 1;
    is.message = std::to_string(n) + " gövde her biri ayrı bir parça/bağlama (setup) olarak işlenir: " + all + ".";
    rep.issues.push_back(is);
  }

  // ---- pairwise: interference / gap
  const bool gapRule = am && amIsPowderOrResin(s.am.process) && s.am.minGap > 0;
  double gapLook = gapRule ? s.am.minGap : 0;
  for (int i = 0; i < n; ++i)
    for (int j = i + 1; j < n; ++j) {
      const auto& A = bodies[i];
      const auto& B = bodies[j];
      if (A.shape.IsNull() || B.shape.IsNull()) continue;
      try {
        Bnd_Box ba, bb;
        BRepBndLib::Add(A.shape, ba);
        BRepBndLib::Add(B.shape, bb);
        if (ba.IsVoid() || bb.IsVoid()) continue;
        ba.Enlarge(gapLook + 1e-6);
        if (ba.IsOut(bb)) continue;
        double vi = 0, vj = 0;
        {
          GProp_GProps gp;
          BRepGProp::VolumeProperties(A.shape, gp);
          vi = std::fabs(gp.Mass());
          GProp_GProps gq;
          BRepGProp::VolumeProperties(B.shape, gq);
          vj = std::fabs(gq.Mass());
        }
        const double tolV = std::max(1e-3, 1e-6 * std::min(vi, vj));
        double common = 0;
        {
          BRepAlgoAPI_Common cm(A.shape, B.shape);
          if (cm.IsDone() && !cm.Shape().IsNull()) {
            GProp_GProps gp;
            BRepGProp::VolumeProperties(cm.Shape(), gp);
            common = std::fabs(gp.Mass());
          }
        }
        const std::string& fid = !B.createdBy.empty() ? B.createdBy : A.createdBy;
        if (common > tolV) {
          Issue is;
          is.severity = Severity::Error;
          is.domain = Domain::Model;
          is.ruleId = "MODEL.BODY_INTERFERENCE";
          is.featureId = fid;
          is.actual = common;
          is.limit = tolV;
          is.body = B.name;
          is.params = featureParams(doc, fid, {"distance", "offset", "diameter"});
          is.message = "Gövde " + B.name + " ile " + A.name + " örtüşüyor: ortak hacim " + fmt(common, 3) + " mm³ > " + fmt(tolV, 3) +
                       " mm³. " + (fid.empty() ? std::string() : fid + " özelliğinin konumunu/ölçüsünü (" + (is.params.empty() ? std::string("parametreleri") : is.params[0]) + ") değiştirin") +
                       " ya da gövdeleri Combine ile birleştirin.";
          rep.issues.push_back(is);
          continue;
        }
        if (!gapRule) continue;
        BRepExtrema_DistShapeShape ds(A.shape, B.shape);
        if (!ds.IsDone() || ds.NbSolution() < 1) continue;
        double d = ds.Value();
        if (d >= s.am.minGap - 1e-6) continue;
        std::vector<std::string> faces;
        for (int side = 0; side < 2; ++side) {
          TopoDS_Shape sup = side == 0 ? ds.SupportOnShape1(1) : ds.SupportOnShape2(1);
          if (!sup.IsNull() && sup.ShapeType() == TopAbs_FACE) {
            std::string nm = mr.nameOf(TopoDS::Face(sup));
            if (!nm.empty()) faces.push_back(nm);
          }
        }
        Issue is;
        is.severity = Severity::Warning;
        is.domain = Domain::Additive;
        is.ruleId = "AM.BODY_GAP";
        is.featureId = fid;
        is.actual = d;
        is.limit = s.am.minGap;
        is.body = B.name;
        is.faces = faces;
        is.params = featureParams(doc, fid, {"offset", "distance"});
        is.message = "Gövde " + B.name + " ile " + A.name + " arası boşluk " + fmt(d, 3) + " mm < min. boşluk " + fmt(s.am.minGap) + " mm (" +
                     amName(s.am.process) + "): gövdeler yapı sırasında kaynaşır. Gövdeleri ≥ " + fmt(s.am.minGap) +
                     " mm ayırın ya da tek gövde olarak birleştirin.";
        rep.issues.push_back(is);
      } catch (const Standard_Failure&) {
        Issue is;
        is.severity = Severity::Warning;
        is.domain = Domain::Model;
        is.ruleId = "GEOM.CHECK_FAILED";
        is.body = B.name;
        is.message = "Gövde çifti (" + A.name + ", " + B.name + ") kontrolü tamamlanamadı.";
        rep.issues.push_back(is);
      }
    }
  return rep;
}

Report checkGeometricImpl(const Document& doc, const model::ModelResult& mr, const MfgSettings& s) {
  Report rep;
  Geo g0(doc, mr, s);
  setupFlags(g0, s);
  statusRule(g0);
  if (mr.shape.IsNull() || (!g0.am && !g0.conv)) {
    rep.issues = g0.out;
    return rep;
  }
  std::vector<Issue> all = g0.out;
  try {
    std::vector<model::BodyInfo> bodies = bodiesOf(doc, mr);
    const bool multi = bodies.size() > 1;
    if (bodies.empty()) {
      model::BodyInfo b;
      b.name = "Body1";
      b.shape = mr.shape;
      bodies.push_back(b);
    }
    for (auto& b : bodies) {
      Geo g(doc, mr, s);
      setupFlags(g, s);
      g.shape = b.shape;
      g.body = b.name;
      g.multi = multi;
      try {
        runBodyRules(g);
      } catch (const Standard_Failure& ex) {
        Issue is;
        is.severity = Severity::Warning;
        is.domain = Domain::Model;
        is.ruleId = "GEOM.CHECK_FAILED";
        is.message = std::string("Geometrik kontrol tamamlanamadı: ") + (ex.GetMessageString() ? ex.GetMessageString() : "OCCT hatası");
        g.out.push_back(is);
      }
      tagIssues(g.out, 0, b.name, multi);
      all.insert(all.end(), g.out.begin(), g.out.end());
    }
    if (multi) {
      // whole build (all bodies together)
      Geo g(doc, mr, s);
      setupFlags(g, s);
      g.multi = true;
      g.shape = mr.shape;
      collectFaces(g);   // only for g.size
      envelopeRule(g);
      all.insert(all.end(), g.out.begin(), g.out.end());
    }
    Report br = checkBodiesImpl(doc, mr, s);
    all.insert(all.end(), br.issues.begin(), br.issues.end());
  } catch (const Standard_Failure& ex) {
    Issue is;
    is.severity = Severity::Warning;
    is.domain = Domain::Model;
    is.ruleId = "GEOM.CHECK_FAILED";
    is.message = std::string("Geometrik kontrol tamamlanamadı: ") + (ex.GetMessageString() ? ex.GetMessageString() : "OCCT hatası");
    all.push_back(is);
  }
  dedupe(all);
  rep.issues = all;
  return rep;
}

}  // namespace pc::mfg::detail
