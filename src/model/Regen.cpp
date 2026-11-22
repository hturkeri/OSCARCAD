// PersistCAD model — history-based regeneration engine with persistent naming.
#include "pc/Model.h"
#include "pc/ModelNaming.h"
#include "pc/Expression.h"
#include "pc/SimdState.h"
#include "pc/Regions.h"   // SC-PULLUPTO
#include "pc/UpTo.h"      // SC-PULLUPTO

#include <chrono>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <GProp_GProps.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_DraftAngle.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepSweep_Revol.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Geom2d_Curve.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <Bnd_Box.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeBuild_ReShape.hxx>
#include <ShapeFix_Shape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pc::model {

using naming::Candidate;
using naming::NamedShape;
using naming::NameMap;

namespace {

struct FeatureError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct ToolRecord {
  NamedShape tool;
  std::string op;
};

// One body of a multi-body part (BODIES contract, pc/Feature.h). Always a single solid.
struct BodyRec {
  std::string name;
  std::string createdBy;
  TopoDS_Shape solid;
};

struct Ctx {
  const Document& doc;
  const ParamValues& vals;
  ModelResult& res;
  std::vector<BodyRec> bodies;   // creation order
  NameMap faces;                 // every face of every body -> globally unique persistent name
  int featIndex = 0;             // index of the feature being regenerated
  std::map<std::string, gp_Ax3> frames;
  std::map<std::string, gp_Ax3> datums;   // Plane feature id -> evaluated datum frame
  std::map<std::string, sketch::Profile> profiles;
  std::map<std::string, ToolRecord> tools;
  std::vector<std::string> log;
  // named selections: current face name -> every name it carried / inherited (see ModelResult::faceLineage)
  std::map<std::string, std::set<std::string>> lineage;
  std::map<std::string, std::string> removedBy;
  bool hasBody() const { return !bodies.empty(); }
};

std::string fmt(double v) { return formatNumber(v); }

double evalExpr(const Ctx& c, const std::string& expr, const std::string& key) {
  try {
    return evalWith(expr, c.vals);
  } catch (const std::exception& e) {
    throw FeatureError("'" + key + "' ifadesi değerlendirilemedi (" + expr + "): " + e.what());
  }
}

// Numeric property: required if no default is given.
double num(const Ctx& c, const Feature& f, const std::string& key, std::optional<double> def = std::nullopt) {
  auto it = f.props.find(key);
  if (it == f.props.end() || it->second.empty()) {
    if (def) return *def;
    throw FeatureError("'" + key + "' değeri eksik");
  }
  return evalExpr(c, it->second, key);
}

TopoDS_Shape simplify(const TopoDS_Shape& s) {
  if (s.IsNull()) return s;
  if (s.ShapeType() == TopAbs_SOLID) return s;
  int n = 0;
  TopoDS_Shape last;
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) {
    ++n;
    last = ex.Current();
  }
  if (n == 1) return last;
  return s;
}

int countSolids(const TopoDS_Shape& s) {
  int n = 0;
  if (s.IsNull()) return 0;
  for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) ++n;
  return n;
}

bool frameFromFace(const TopoDS_Face& F, double offset, gp_Ax3& out) {
  BRepAdaptor_Surface s(F, Standard_False);
  if (s.GetType() != GeomAbs_Plane) return false;
  gp_Pln pl = s.Plane();
  gp_Dir n = pl.Axis().Direction();
  if (F.Orientation() == TopAbs_REVERSED) n.Reverse();
  gp_XYZ N = n.XYZ();
  gp_XYZ O = pl.Location().XYZ();
  // projection of world origin onto plane
  gp_XYZ org = N * (O.Dot(N));
  auto proj = [&](const gp_XYZ& a) { return a - N * a.Dot(N); };
  gp_XYZ u = proj(gp_XYZ(1, 0, 0));
  if (u.Modulus() < 1e-6) u = proj(gp_XYZ(0, 1, 0));
  u.Normalize();
  org += N * offset;
  out = gp_Ax3(gp_Pnt(org), n, gp_Dir(u));
  return true;
}

double throughLength(const TopoDS_Shape& body, const gp_Pnt& from) {
  Bnd_Box b;
  BRepBndLib::Add(body, b);
  if (b.IsVoid()) return 1000;
  double x0, y0, z0, x1, y1, z1;
  b.Get(x0, y0, z0, x1, y1, z1);
  gp_Pnt c((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
  double diag = gp_Pnt(x0, y0, z0).Distance(gp_Pnt(x1, y1, z1));
  return 2.0 * (diag + c.Distance(from)) + 10.0;
}

gp_Pnt edgeMid(const TopoDS_Edge& e) {
  BRepAdaptor_Curve c(e);
  return c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
}

// Entity id of a profile face edge (after an optional move `loc`).
int entityOf(const TopoDS_Edge& e, const sketch::Profile& p, const TopLoc_Location& loc) {
  for (const auto& pe : p.edges)
    if (pe.edge.Moved(loc).IsSame(e)) return pe.entityId;
  // geometric fallback (profile builder may have copied edges)
  gp_Pnt m = edgeMid(e);
  int best = -1;
  double bd = 1e-5;
  for (const auto& pe : p.edges) {
    double d = edgeMid(TopoDS::Edge(pe.edge.Moved(loc))).Distance(m);
    if (d < bd) {
      bd = d;
      best = pe.entityId;
    }
  }
  return best;
}

void addFaces(const TopoDS_Shape& s, const std::string& name, int prio, std::vector<Candidate>& out) {
  if (s.IsNull()) return;
  if (s.ShapeType() == TopAbs_FACE) {
    out.push_back({s, name, prio});
    return;
  }
  for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) out.push_back({ex.Current(), name, prio});
}

void addGenerated(BRepBuilderAPI_MakeShape& mk, const TopoDS_Shape& from, const std::string& name,
                  std::vector<Candidate>& out) {
  const TopTools_ListOfShape& g = mk.Generated(from);
  for (TopTools_ListIteratorOfListOfShape it(g); it.More(); it.Next())
    if (it.Value().ShapeType() == TopAbs_FACE) out.push_back({it.Value(), name, 0});
}

// MakeRevol::Generated() misses planar faces swept by edges perpendicular to the axis (full
// revolutions, OCCT 7.6) -> fall back to the underlying sweep.
void addRevolGenerated(BRepPrimAPI_MakeRevol& mk, const TopoDS_Shape& from, const std::string& name,
                       std::vector<Candidate>& out) {
  size_t n0 = out.size();
  addGenerated(mk, from, name, out);
  if (out.size() != n0) return;
  try {
    TopoDS_Shape s = const_cast<BRepSweep_Revol&>(mk.Revol()).Shape(from);
    if (!s.IsNull() && s.ShapeType() == TopAbs_FACE) out.push_back({s, name, 0});
  } catch (...) {
  }
}

// ================================================================ bodies (multi-body state)

std::vector<TopoDS_Shape> solidsOf(const TopoDS_Shape& s) {
  std::vector<TopoDS_Shape> v;
  if (s.IsNull()) return v;
  TopTools_IndexedMapOfShape m;
  TopExp::MapShapes(s, TopAbs_SOLID, m);
  for (int i = 1; i <= m.Extent(); ++i) v.push_back(m(i));
  return v;
}

// Single solid for one body, compound of solids otherwise (null when there is no body).
TopoDS_Shape shapeOf(const std::vector<BodyRec>& bs) {
  if (bs.empty()) return {};
  if (bs.size() == 1) return bs[0].solid;
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  for (auto& b : bs) bb.Add(comp, b.solid);
  return comp;
}

NamedShape namedAll(const Ctx& c) {
  NamedShape n;
  n.shape = shapeOf(c.bodies);
  n.faces = c.faces;
  return n;
}

// The named faces of one body only.
NamedShape namedBody(const Ctx& c, size_t i) {
  NamedShape n;
  n.shape = c.bodies[i].solid;
  for (TopExp_Explorer ex(n.shape, TopAbs_FACE); ex.More(); ex.Next())
    if (const std::string* nm = c.faces.Seek(ex.Current())) n.faces.Bind(ex.Current(), *nm);
  return n;
}

gp_Pnt volumeCentroid(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  if (std::abs(g.Mass()) > 1e-12) return g.CentreOfMass();
  return naming::faceCentroid(s);
}

// Body pieces of `s`: one solid keeps `name`, several disjoint solids become "<name>:k" (centroid order).
std::vector<BodyRec> piecesOf(const std::string& name, const std::string& by, const TopoDS_Shape& s) {
  auto sol = solidsOf(s);
  std::vector<BodyRec> out;
  if (sol.size() == 1) {
    out.push_back({name, by, sol[0]});
    return out;
  }
  std::vector<std::pair<gp_Pnt, TopoDS_Shape>> v;
  for (auto& x : sol) v.emplace_back(volumeCentroid(x), x);
  std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return naming::pointLess(a.first, b.first); });
  for (size_t k = 0; k < v.size(); ++k) out.push_back({name + ":" + std::to_string(k + 1), by, v[k].second});
  return out;
}

// Install a new body list and re-derive globally unique face names. `cands` = names proposed by the
// operation; faces that already existed (IsSame) keep their name with top priority.
void commit(Ctx& c, std::vector<BodyRec> nb, std::vector<Candidate> cands, const std::string& fid) {
  TopoDS_Shape all = shapeOf(nb);
  if (!all.IsNull()) {
    TopTools_IndexedMapOfShape rf;
    TopExp::MapShapes(all, TopAbs_FACE, rf);
    for (int i = 1; i <= rf.Extent(); ++i)
      if (const std::string* n = c.faces.Seek(rf(i))) cands.push_back({rf(i), *n, -1});
    c.faces = naming::finalize(all, cands, fid, &c.log);
  } else {
    c.faces.Clear();
  }
  // Face lineage (named selections): a face inherits the lineage of EVERY candidate that proposed a name for it
  // (not only the winner), so splits, merges and shared faces never lose a name. Names that vanish are recorded
  // with the feature that removed them.
  {
    std::map<std::string, std::set<std::string>> nl;
    for (const auto& cd : cands) {
      const std::string* nn = c.faces.Seek(cd.face);
      if (!nn) continue;
      auto& dst = nl[*nn];
      auto it = c.lineage.find(cd.name);
      if (it != c.lineage.end()) dst.insert(it->second.begin(), it->second.end());
      else dst.insert(cd.name);
    }
    for (NameMap::Iterator it(c.faces); it.More(); it.Next()) nl[it.Value()].insert(it.Value());
    std::set<std::string> before, after;
    for (auto& [n, s] : c.lineage) before.insert(s.begin(), s.end());
    for (auto& [n, s] : nl) after.insert(s.begin(), s.end());
    for (auto& n : before)
      if (!after.count(n)) c.removedBy[n] = fid;
    for (auto& n : after) c.removedBy.erase(n);
    c.lineage = std::move(nl);
  }
  // body names stay unique (defensive: a split suffix could collide with a user name)
  std::map<std::string, int> cnt;
  for (auto& b : nb) cnt[b.name]++;
  std::map<std::string, int> seen;
  for (auto& b : nb)
    if (cnt[b.name] > 1) b.name += ":d" + std::to_string(++seen[b.name]);
  c.bodies = std::move(nb);
}

std::string trimStr(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

std::vector<std::string> splitList(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream is(s);
  while (std::getline(is, cur, ',')) {
    cur = trimStr(cur);
    if (!cur.empty()) out.push_back(cur);
  }
  return out;
}

// Body indices for names: exact match, else every split piece "<name>:k" (warning). Sorted, unique.
std::vector<size_t> resolveBodies(const Ctx& c, const std::vector<std::string>& names, std::string& warn) {
  std::set<size_t> out;
  for (auto& n : names) {
    bool hit = false;
    for (size_t i = 0; i < c.bodies.size(); ++i)
      if (c.bodies[i].name == n) {
        out.insert(i);
        hit = true;
      }
    if (hit) continue;
    std::string pieces;
    for (size_t i = 0; i < c.bodies.size(); ++i)
      if (c.bodies[i].name.rfind(n + ":", 0) == 0) {
        out.insert(i);
        pieces += (pieces.empty() ? "" : ", ") + c.bodies[i].name;
      }
    if (pieces.empty()) throw FeatureError("gövde bulunamadı '" + n + "'");
    warn += std::string(warn.empty() ? "" : "; ") + "gövde '" + n + "' bölünmüş, kullanılan: " + pieces;
  }
  return {out.begin(), out.end()};
}

// Name for a body created by feature f (prop bodyName, else `def`); must be new and usable in lists.
std::string newBodyName(const Ctx& c, const Feature& f, const std::string& def) {
  std::string n = trimStr(f.prop("bodyName"));
  if (n.empty()) n = def;
  if (n.find(',') != std::string::npos) throw FeatureError("gövde adı virgül içeremez '" + n + "'");
  for (auto& b : c.bodies)
    if (b.name == n) throw FeatureError("gövde adı '" + n + "' zaten kullanılıyor");
  return n;
}

bool pointInside(const TopoDS_Shape& solid, const TopoDS_Shape& other) {
  TopExp_Explorer ex(other, TopAbs_VERTEX);
  if (!ex.More()) return false;
  BRepClass3d_SolidClassifier cl(solid, BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())), 1e-7);
  return cl.State() == TopAbs_IN;
}

// Two solids touch or overlap (distance ~0, or one inside the other).
bool solidsTouch(const TopoDS_Shape& a, const TopoDS_Shape& b) {
  Bnd_Box ba, bb;
  BRepBndLib::Add(a, ba);
  BRepBndLib::Add(b, bb);
  if (ba.IsVoid() || bb.IsVoid()) return false;
  ba.Enlarge(1e-4);
  if (ba.IsOut(bb)) return false;
  try {
    BRepExtrema_DistShapeShape d(a, b);
    if (d.IsDone() && d.Value() <= 1e-5) return true;
    if (d.IsDone()) return pointInside(a, b) || pointInside(b, a);
  } catch (...) {
  }
  return true;
}

// Bodies a feature works on: explicit `targets`, else all bodies. Used for through lengths / spans.
TopoDS_Shape scopeShape(const Ctx& c, const Feature& f) {
  std::string w;
  auto names = splitList(f.prop("targets"));
  if (names.empty()) return shapeOf(c.bodies);
  std::vector<BodyRec> bs;
  for (size_t i : resolveBodies(c, names, w)) bs.push_back(c.bodies[i]);
  return shapeOf(bs);
}

// Face name of a kept tool copy: "<fid>.tool.<rest>".
std::string keptName(const std::string& fid, const std::string& n) {
  const std::string pre = fid + ".";
  return fid + ".tool." + (n.rfind(pre, 0) == 0 ? n.substr(pre.size()) : n);
}

std::unique_ptr<BRepAlgoAPI_BooleanOperation> runBop(const std::string& op, const TopTools_ListOfShape& args,
                                                    const TopTools_ListOfShape& tls) {
  std::unique_ptr<BRepAlgoAPI_BooleanOperation> bop;
  if (op == "join") bop = std::make_unique<BRepAlgoAPI_Fuse>();
  else if (op == "cut") bop = std::make_unique<BRepAlgoAPI_Cut>();
  else if (op == "intersect") bop = std::make_unique<BRepAlgoAPI_Common>();
  else throw FeatureError("geçersiz işlem '" + op + "'");
  bop->SetArguments(args);
  bop->SetTools(tls);
  bop->SetRunParallel(Standard_False);
  bop->Build();
  if (!bop->IsDone() || bop->HasErrors()) throw FeatureError("Boolean işlemi (" + op + ") başarısız");
  return bop;
}

// Apply the tool (single solid or compound of solids) of feature f with `op` per the BODIES contract.
std::string applyOp(Ctx& c, const Feature& f, std::string op, const NamedShape& tool) {
  const std::string& fid = f.id;
  std::string warn;
  if (op != "new" && op != "join" && op != "cut" && op != "intersect") throw FeatureError("geçersiz işlem '" + op + "'");
  auto toolSolids = solidsOf(tool.shape);
  if (toolSolids.empty()) throw FeatureError("araç gövdesi boş");
  if (!c.hasBody()) {
    if (op == "cut" || op == "intersect") throw FeatureError("'" + op + "' işlemi için gövde yok");
    op = "new";
  }
  std::vector<Candidate> cands;
  auto addToolCands = [&](int prio) {
    for (NameMap::Iterator it(tool.faces); it.More(); it.Next()) cands.push_back({it.Key(), it.Value(), prio});
  };
  if (op == "new") {
    std::string name = newBodyName(c, f, fid);
    auto nb = c.bodies;
    for (auto& p : piecesOf(name, fid, tool.shape)) nb.push_back(p);
    addToolCands(1000);
    commit(c, std::move(nb), std::move(cands), fid);
    return warn;
  }
  const bool autoMode = splitList(f.prop("targets")).empty();
  std::vector<size_t> T;
  std::vector<TopoDS_Shape> freeSolids;  // AUTO join: tool solids touching no body -> new body
  if (!autoMode) T = resolveBodies(c, splitList(f.prop("targets")), warn);
  else {
    std::set<size_t> hit;
    std::vector<TopoDS_Shape> touching;
    for (auto& t : toolSolids) {
      bool any = false;
      for (size_t i = 0; i < c.bodies.size(); ++i)
        if (solidsTouch(t, c.bodies[i].solid)) {
          hit.insert(i);
          any = true;
        }
      (any ? touching : freeSolids).push_back(t);
    }
    T.assign(hit.begin(), hit.end());
    if (op == "join" && !T.empty()) toolSolids = touching;
    else freeSolids.clear();
  }
  std::set<size_t> inT(T.begin(), T.end());
  TopTools_ListOfShape tls;
  for (auto& t : toolSolids) tls.Append(t);

  if (op == "join") {
    if (T.empty()) {  // touches nothing: the tool becomes a new body
      std::string name = newBodyName(c, f, fid);
      auto nb = c.bodies;
      for (auto& p : piecesOf(name, fid, tool.shape)) nb.push_back(p);
      addToolCands(1000);
      commit(c, std::move(nb), std::move(cands), fid);
      return warn;
    }
    TopTools_ListOfShape args;
    for (size_t i : T) args.Append(c.bodies[i].solid);
    auto bop = runBop("join", args, tls);
    TopoDS_Shape r = bop->Shape();
    if (countSolids(r) == 0) throw FeatureError("işlem sonucu boş gövde (join)");
    TopTools_IndexedMapOfShape rf;
    TopExp::MapShapes(r, TopAbs_FACE, rf);
    for (size_t k = 0; k < T.size(); ++k) naming::collectCandidates(*bop, namedBody(c, T[k]), (int)k, rf, cands);
    naming::collectCandidates(*bop, tool, 1000, rf, cands);
    const BodyRec& oldest = c.bodies[T.front()];
    std::vector<BodyRec> nb;
    for (size_t i = 0; i < c.bodies.size(); ++i) {
      if (i == T.front())
        for (auto& p : piecesOf(oldest.name, oldest.createdBy, r)) nb.push_back(p);
      else if (!inT.count(i)) nb.push_back(c.bodies[i]);
    }
    if (!freeSolids.empty()) {  // the part of the tool that touches nothing becomes a new body
      BRep_Builder bb;
      TopoDS_Compound fc;
      bb.MakeCompound(fc);
      for (auto& t : freeSolids) bb.Add(fc, t);
      for (auto& p : piecesOf(newBodyName(c, f, f.id), f.id, fc)) nb.push_back(p);
      TopTools_IndexedMapOfShape ff;
      TopExp::MapShapes(fc, TopAbs_FACE, ff);
      for (NameMap::Iterator it(tool.faces); it.More(); it.Next())
        if (ff.Contains(it.Key())) cands.push_back({it.Key(), it.Value(), 1000});
    }
    commit(c, std::move(nb), std::move(cands), fid);
    return warn;
  }

  // cut / intersect: each target body separately
  if (T.empty() && op == "cut")
    warn += std::string(warn.empty() ? "" : "; ") + "araç hiçbir gövdeye değmiyor (değişiklik yok)";
  std::vector<BodyRec> nb;
  for (size_t i = 0; i < c.bodies.size(); ++i) {
    const BodyRec& b = c.bodies[i];
    if (!inT.count(i)) {
      if (op == "intersect" && autoMode) {
        warn += std::string(warn.empty() ? "" : "; ") + "gövde '" + b.name + "' kesişim dışında kaldı, kaldırıldı";
        continue;
      }
      nb.push_back(b);
      continue;
    }
    TopTools_ListOfShape args;
    args.Append(b.solid);
    auto bop = runBop(op, args, tls);
    TopoDS_Shape r = bop->Shape();
    auto ps = piecesOf(b.name, b.createdBy, r);
    if (ps.empty()) {
      warn += std::string(warn.empty() ? "" : "; ") + "gövde '" + b.name + "' tamamen kaldırıldı";
      continue;
    }
    TopTools_IndexedMapOfShape rf;
    TopExp::MapShapes(r, TopAbs_FACE, rf);
    naming::collectCandidates(*bop, namedBody(c, i), 0, rf, cands);
    naming::collectCandidates(*bop, tool, 1, rf, cands);
    for (auto& p : ps) nb.push_back(p);
  }
  // (the emptiness check comes after keepTool: a kept tool is a valid body even if every target vanished)
  if (num(c, f, "keepTool", 0.0) != 0) {
    // the tool solid is kept as a separate body (a copy: no topology shared with the cut bodies)
    std::string name = newBodyName(c, f, fid + ".tool");
    BRepBuilderAPI_Copy cp(tool.shape);
    for (auto& p : piecesOf(name, fid, cp.Shape())) nb.push_back(p);
    for (NameMap::Iterator it(tool.faces); it.More(); it.Next())
      addFaces(cp.ModifiedShape(it.Key()), keptName(fid, it.Value()), 1000, cands);
  }
  if (nb.empty()) throw FeatureError("işlem sonucu boş gövde (" + op + ")");
  commit(c, std::move(nb), std::move(cands), fid);
  return warn;
}

const sketch::Profile& profileOf(Ctx& c, const std::string& sid) {
  auto it = c.profiles.find(sid);
  if (it == c.profiles.end()) throw FeatureError("çizim '" + sid + "' bulunamadı veya geçersiz");
  return it->second;
}

TopoDS_Shape baseOf(const sketch::Profile& p, const TopLoc_Location& loc, const std::string& sid) {
  if (p.faces.empty())
    throw FeatureError("çizim '" + sid + "' kapalı profil içermiyor" + (p.message.empty() ? "" : (": " + p.message)));
  if (p.faces.size() == 1) return p.faces[0].Moved(loc);
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  for (auto& f : p.faces) bb.Add(comp, f.Moved(loc));
  return comp;
}

// Strip split suffixes (":1", ":d2") from a face or edge name: "edge(A:1|B):2" -> "edge(A|B)".
std::string baseName(const std::string& n) {
  std::string out;
  for (size_t i = 0; i < n.size();) {
    if (n[i] == ':') {
      size_t j = i + 1;
      if (j < n.size() && n[j] == 'd') ++j;
      size_t k = j;
      while (k < n.size() && std::isdigit((unsigned char)n[k])) ++k;
      if (k > j && (k == n.size() || n[k] == '|' || n[k] == ')' || n[k] == ':')) {
        i = k;
        continue;
      }
    }
    out += n[i++];
  }
  return out;
}

// Faces for a persistent name: exact match, else all split pieces "<name>:k" (warning added).
template <class M>
std::vector<typename M::mapped_type> resolveRef(const M& m, const std::string& name, std::string& warn) {
  std::vector<typename M::mapped_type> out;
  auto it = m.find(name);
  if (it != m.end()) {
    out.push_back(it->second);
    return out;
  }
  std::string b = baseName(name);
  std::vector<std::string> hits;
  for (auto& [n, x] : m)
    if (baseName(n) == b) {
      out.push_back(x);
      hits.push_back(n);
    }
  if (!out.empty()) {
    std::string h;
    for (auto& x : hits) h += (h.empty() ? "" : ", ") + x;
    warn += std::string(warn.empty() ? "" : "; ") + "'" + name + "' bölünmüş, kullanılan: " + h;
  }
  return out;
}

// ---------------------------------------------------------------- Sketch
std::string doSketch(Ctx& c, const Feature& f, FeatureInfo& info) {
  if (!f.sketch) throw FeatureError("çizim verisi yok");
  Sketch sk = *f.sketch;
  double off = evalExpr(c, sk.plane.offsetExpr.empty() ? "0" : sk.plane.offsetExpr, "offset");
  info.values["offset"] = off;
  std::string refWarn;
  gp_Ax3 fr;
  if (sk.plane.kind == SketchPlaneKind::OnFace) {
    if (!c.hasBody()) throw FeatureError("yüz bulunamadı '" + sk.plane.faceRef + "' (gövde yok)");
    auto by = namedAll(c).byName();
    auto hits = resolveRef(by, sk.plane.faceRef, refWarn);
    if (hits.empty()) throw FeatureError("yüz bulunamadı '" + sk.plane.faceRef + "'");
    if (!frameFromFace(hits.front(), off, fr)) throw FeatureError("yüz düzlemsel değil '" + sk.plane.faceRef + "'");
  } else if (sk.plane.kind == SketchPlaneKind::OnPlane) {
    const std::string& pr = sk.plane.planeRef;
    if (pr.empty()) throw FeatureError("düzlem referansı eksik (planeRef)");
    int pi = c.doc.indexOf(pr);
    if (pi < 0) throw FeatureError("düzlem bulunamadı '" + pr + "'");
    if (c.doc.features[pi].type != FeatureType::Plane) throw FeatureError("'" + pr + "' bir düzlem özelliği değil");
    if (pi >= c.featIndex) throw FeatureError("düzlem '" + pr + "' çizimden sonra tanımlı");
    auto it = c.datums.find(pr);
    if (it == c.datums.end()) throw FeatureError("düzlem '" + pr + "' kullanılamıyor (bastırılmış veya hatalı)");
    fr = it->second;
    fr.Translate(gp_Vec(fr.Direction()) * off);
  } else {
    fr = sketch::standardFrame(sk.plane.kind, off);
  }
  info.sketchFrame = fr;
  c.frames[f.id] = fr;
  sketch::SolveResult sr = sketch::solve(sk, c.vals);
  c.res.sketchResults[f.id] = sr;
  c.res.solvedSketches[f.id] = sk;
  std::string warn = refWarn;
  bool solvedOk = sr.ok() || (sr.converged && sr.status == sketch::SolveStatus::OverConstrained);
  if (!solvedOk) throw FeatureError("çizim çözülemedi" + (sr.message.empty() ? std::string() : (": " + sr.message)));
  if (!sr.ok()) warn = "fazla kısıtlı (gereksiz kısıtlar)" + (sr.message.empty() ? std::string() : (": " + sr.message));
  bool hasEnt = false;
  for (auto& e : sk.entities) hasEnt |= !e.construction;
  sketch::Profile p;
  if (hasEnt) {
    p = sketch::buildProfile(sk, fr);
    if (!p.ok) warn += std::string(warn.empty() ? "" : "; ") + "profil uyarısı: " + p.message;
  }
  c.profiles[f.id] = std::move(p);
  return warn;
}

TopoDS_Edge findEdge(const std::map<std::string, TopoDS_Edge>& em, const std::string& name, std::string& warn);

// SC-PULLUPTO: `regions=<k,...>` -> the profile restricted to those regions of pc::sketch::sketchRegions (the region
// list the designer shows); empty -> the classic profile. `storage` keeps the restricted profile alive.
const sketch::Profile& regionProfile(Ctx& c, const Feature& f, const std::string& sid, const gp_Ax3& fr, sketch::Profile& storage) {
  const sketch::Profile& prof = profileOf(c, sid);
  const std::string regs = trimStr(f.prop("regions"));
  if (regs.empty()) return prof;
  auto sit = c.res.solvedSketches.find(sid);
  const Feature* sf = c.doc.feature(sid);
  if (sit == c.res.solvedSketches.end() || !sf) throw FeatureError("çizim '" + sid + "' bulunamadı");
  double g = -1;
  if (!trimStr(sf->prop("gapTol")).empty()) g = evalExpr(c, sf->prop("gapTol"), "gapTol");
  const sketch::RegionResult R = sketch::sketchRegions(sit->second, fr, g);
  storage = sketch::Profile();
  storage.edges = R.profile.edges;
  storage.ok = true;
  for (const auto& k : splitList(regs)) {
    int idx = -1;
    try { idx = std::stoi(k); } catch (...) { throw FeatureError("geçersiz bölge '" + k + "'"); }
    if (idx < 0 || idx >= int(R.profile.faces.size()))
      throw FeatureError("bölge r" + k + " yok (" + sid + " çiziminde " + std::to_string(R.profile.faces.size()) + " bölge" +
                         (R.profile.message.empty() ? std::string() : "; " + R.profile.message) + ")");
    storage.faces.push_back(R.profile.faces[size_t(idx)]);
  }
  if (storage.faces.empty()) throw FeatureError("bölge listesi boş");
  return storage;
}

// SC-PULLUPTO: signed distance of an Up To target from the sketch plane (pc/UpTo.h) during regeneration.
double upToNow(Ctx& c, const std::string& ref, const gp_Ax3& fr, std::string& warn) {
  UpToSources src;
  src.sketch = [&](const std::string& sid, const Sketch** sk, gp_Ax3* out) {
    auto it = c.res.solvedSketches.find(sid);
    auto ft = c.frames.find(sid);
    if (it == c.res.solvedSketches.end() || ft == c.frames.end()) return false;
    const int si = c.doc.indexOf(sid);
    if (si < 0 || si >= c.featIndex) return false;
    *sk = &it->second;
    *out = ft->second;
    return true;
  };
  src.datum = [&](const std::string& id, gp_Ax3* out) {
    auto it = c.datums.find(id);
    if (it == c.datums.end()) return false;
    *out = it->second;
    return true;
  };
  src.face = [&](const std::string& n, TopoDS_Face* out) {
    if (!c.hasBody()) return false;
    auto by = namedAll(c).byName();
    std::string w;
    auto hits = resolveRef(by, n, w);
    if (hits.empty()) return false;
    *out = hits.front();
    return true;
  };
  src.edge = [&](const std::string& n, TopoDS_Edge* out) {
    if (!c.hasBody()) return false;
    auto em = naming::nameEdges(namedAll(c).shape, c.faces);
    std::string w;
    try { *out = findEdge(em, n, w); } catch (...) { return false; }
    return true;
  };
  double s = 0;
  std::string err, w;
  if (!upToSignedDistance(ref, fr, src, &s, &err, &w)) throw FeatureError(err);
  if (!w.empty()) warn += (warn.empty() ? "" : "; ") + w;
  return s;
}

// ---------------------------------------------------------------- Extrude
std::string doExtrude(Ctx& c, const Feature& f, FeatureInfo& info) {
  const std::string sid = f.prop("sketch");
  profileOf(c, sid);
  gp_Ax3 fr = c.frames[sid];
  sketch::Profile regionStore;   // SC-PULLUPTO
  const sketch::Profile& prof = regionProfile(c, f, sid, fr, regionStore);
  info.sketchFrame = fr;
  std::string op = f.prop("op", c.hasBody() ? "join" : "new");
  bool through = op == "cut" && num(c, f, "through", 0.0) != 0;
  std::string dir = f.prop("direction", "normal");
  double d = 0;
  std::string upWarn;   // SC-PULLUPTO: Up To (associative length)
  const std::string upTo = trimStr(f.prop("upTo"));
  if (!upTo.empty() && !through) {
    const double s = upToNow(c, upTo, fr, upWarn);
    const double off = num(c, f, "upToOffset", 0.0);
    if (std::abs(s) < 1e-9) throw FeatureError("Kadar hedefi sketch düzleminde (uzaklık 0): " + upTo);
    d = std::abs(s) + off;
    if (!(d > 1e-9)) throw FeatureError("Kadar: hedefe uzaklık " + fmt(std::abs(s)) + " + ofset " + fmt(off) + " pozitif olmalı");
    if (dir == "symmetric") d *= 2;
    else dir = s > 0 ? "normal" : "reverse";
    info.values["upTo"] = s;
    info.values["upToOffset"] = off;
  } else if (!through) {
    d = num(c, f, "distance");
    if (!(d > 0)) throw FeatureError("mesafe pozitif olmalı (" + fmt(d) + ")");
  } else {
    if (!c.hasBody()) throw FeatureError("kesme için gövde yok");
    d = throughLength(scopeShape(c, f), fr.Location());
  }
  double draft = num(c, f, "draft", 0.0);
  info.values["distance"] = through ? 0 : d;
  info.values["draft"] = draft;
  info.values["through"] = through ? 1 : 0;

  gp_Vec n(fr.Direction());
  gp_Vec vec = n * d;
  TopLoc_Location loc;
  if (dir == "reverse") vec.Reverse();
  else if (dir == "symmetric") {
    gp_Trsf t;
    t.SetTranslation(n * (-d / 2));
    loc = TopLoc_Location(t);
  } else if (dir != "normal") throw FeatureError("geçersiz yön '" + dir + "'");

  TopoDS_Shape base = baseOf(prof, loc, sid);
  BRepPrimAPI_MakePrism mk(base, vec, Standard_False, Standard_True);
  if (!mk.IsDone()) throw FeatureError("uzatma oluşturulamadı");
  std::vector<Candidate> cands;
  TopTools_IndexedMapOfShape edges;
  TopExp::MapShapes(base, TopAbs_EDGE, edges);
  for (int i = 1; i <= edges.Extent(); ++i) {
    const TopoDS_Edge& e = TopoDS::Edge(edges(i));
    int id = entityOf(e, prof, loc);
    addGenerated(mk, e, f.id + ".side.e" + (id >= 0 ? std::to_string(id) : std::string("x")), cands);
  }
  addFaces(mk.FirstShape(), f.id + ".start", 0, cands);
  addFaces(mk.LastShape(), f.id + ".end", 0, cands);
  NamedShape tool;
  tool.shape = mk.Shape();
  tool.faces = naming::finalize(tool.shape, cands, f.id, &c.log);

  std::string warn;
  if (std::abs(draft) > 1e-9) {
    try {
      BRepOffsetAPI_DraftAngle dr(tool.shape);
      gp_Pln neutral(fr.Location(), fr.Direction());
      gp_Dir pull(vec);
      bool okAdd = true;
      for (NameMap::Iterator it(tool.faces); it.More() && okAdd; it.Next()) {
        if (it.Value().rfind(f.id + ".side", 0) != 0) continue;
        dr.Add(TopoDS::Face(it.Key()), pull, draft * M_PI / 180.0, neutral);
        okAdd = dr.AddDone();
      }
      if (!okAdd) throw FeatureError("draft add");
      dr.Build();
      if (!dr.IsDone()) throw FeatureError("draft build");
      TopoDS_Shape ns = dr.Shape();
      std::vector<Candidate> dc;
      for (NameMap::Iterator it(tool.faces); it.More(); it.Next()) addFaces(dr.ModifiedShape(it.Key()), it.Value(), 0, dc);
      NameMap nm = naming::finalize(ns, dc, f.id, &c.log);
      tool.shape = ns;
      tool.faces = std::move(nm);
    } catch (...) {
      warn = "eğim açısı (" + fmt(draft) + "°) uygulanamadı, yok sayıldı";
    }
  }
  // SC-VIDEO2D: an explicit region list (multi-region Pull) whose prisms touch -- e.g. an L drawn as two overlapping
  // rectangles, Filled into 3 regions -- is ONE solid, as in SpaceClaim (op=new used to give one body per region).
  // Names follow the fuse history; disjoint regions stay separate. Documents without `regions=` are unchanged.
  if (!trimStr(f.prop("regions")).empty() && op != "cut" && op != "intersect") {
    const auto ts = solidsOf(tool.shape);
    bool touch = false;
    for (size_t i = 0; i < ts.size() && !touch; ++i)
      for (size_t j = i + 1; j < ts.size() && !touch; ++j) touch = solidsTouch(ts[i], ts[j]);
    if (touch) {
      try {
        TopTools_ListOfShape args, tls;
        args.Append(ts[0]);
        for (size_t k = 1; k < ts.size(); ++k) tls.Append(ts[k]);
        auto bop = runBop("join", args, tls);
        TopoDS_Shape r = bop->Shape();
        TopTools_IndexedMapOfShape rf;
        TopExp::MapShapes(r, TopAbs_FACE, rf);
        std::vector<Candidate> fc;
        naming::collectCandidates(*bop, tool, 0, rf, fc);
        tool.faces = naming::finalize(r, fc, f.id, &c.log);
        tool.shape = r;
      } catch (...) {   // keep the separate prisms
      }
    }
  }
  info.toolShape = tool.shape;

  if (op == "cut") {
    double depth = d;
    if (through) {
      depth = 0;
      try {
        BRepAlgoAPI_Common cm(scopeShape(c, f), tool.shape);
        if (cm.IsDone()) {
          Bnd_Box b;
          BRepBndLib::AddOptimal(cm.Shape(), b, Standard_False, Standard_False);
          if (!b.IsVoid()) {
            double x0, y0, z0, x1, y1, z1;
            b.Get(x0, y0, z0, x1, y1, z1);
            gp_Dir pd(vec);
            double lo = 1e300, hi = -1e300;
            for (int k = 0; k < 8; ++k) {
              gp_Pnt p((k & 1) ? x1 : x0, (k & 2) ? y1 : y0, (k & 4) ? z1 : z0);
              double s = gp_Vec(fr.Location(), p).Dot(gp_Vec(pd));
              lo = std::min(lo, s);
              hi = std::max(hi, s);
            }
            depth = hi - std::max(lo, 0.0);
          }
        }
      } catch (...) {
      }
    }
    info.values["depth"] = depth;
  }
  // Pull of a face (designer "Çek", prop pullFace=<face name>): when the operation consumes the pulled face (Add: it
  // becomes internal; Cut: it is cut away), the new end face is the pulled face moved -> it inherits its lineage so
  // named selections follow it (docs/named_selections/MODEL.md, docs/spaceclaim_ux/PULL_FACE_FIX.md).
  const std::string pullFace = trimStr(f.prop("pullFace"));
  std::set<std::string> pullLin;
  if (!pullFace.empty() && (op == "join" || op == "cut")) {
    auto it = c.lineage.find(pullFace);
    if (it != c.lineage.end()) pullLin = it->second;
    else pullLin.insert(pullFace);
  }
  std::string w2 = applyOp(c, f, op, tool);
  c.tools[f.id] = {tool, op};
  if (!pullLin.empty()) {
    bool survived = false;
    for (NameMap::Iterator it(c.faces); it.More() && !survived; it.Next())
      survived = it.Value() == pullFace || baseName(it.Value()) == pullFace;
    if (!survived) {
      const std::string endName = f.id + ".end";
      bool moved = false;
      for (auto& [n, lin] : c.lineage)
        if (lin.count(endName)) {
          lin.insert(pullLin.begin(), pullLin.end());
          moved = true;
        }
      if (moved)
        for (auto& n : pullLin) c.removedBy.erase(n);
    }
  }
  if (!w2.empty()) warn += (warn.empty() ? "" : "; ") + w2;
  if (!upWarn.empty()) warn += (warn.empty() ? "" : "; ") + upWarn;
  return warn;
}

// ---------------------------------------------------------------- Revolve
std::string doRevolve(Ctx& c, const Feature& f, FeatureInfo& info) {
  const std::string sid = f.prop("sketch");
  profileOf(c, sid);
  gp_Ax3 fr = c.frames[sid];
  sketch::Profile regionStore;   // SC-PULLUPTO: regions=
  const sketch::Profile& prof = regionProfile(c, f, sid, fr, regionStore);
  info.sketchFrame = fr;
  std::string op = f.prop("op", c.hasBody() ? "join" : "new");
  std::string axis = f.prop("axis", "V");
  gp_Ax1 ax;
  if (axis == "U" || axis == "u") ax = gp_Ax1(fr.Location(), fr.XDirection());
  else if (axis == "V" || axis == "v") ax = gp_Ax1(fr.Location(), fr.YDirection());
  else {
    int id = -1;
    try { id = std::stoi(axis); } catch (...) { throw FeatureError("geçersiz eksen '" + axis + "'"); }
    const Sketch& sk = c.res.solvedSketches[sid];
    const SkEntity* e = sk.entity(id);
    if (!e || e->type != SkEntityType::Line) throw FeatureError("eksen çizgisi bulunamadı (varlık " + axis + ")");
    const SkPoint* a = sk.point(e->points[0]);
    const SkPoint* b = sk.point(e->points[1]);
    if (!a || !b) throw FeatureError("eksen çizgisi noktaları eksik");
    auto to3 = [&](const SkPoint* p) {
      return gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * p->x + fr.YDirection().XYZ() * p->y);
    };
    gp_Pnt A = to3(a), B = to3(b);
    if (A.Distance(B) < 1e-9) throw FeatureError("eksen çizgisi sıfır uzunlukta");
    ax = gp_Ax1(A, gp_Dir(gp_Vec(A, B)));
  }
  double angle = num(c, f, "angle", 360.0);
  if (std::abs(angle) < 1e-9) throw FeatureError("açı sıfır olamaz");
  bool full = std::abs(angle) >= 360.0 - 1e-9;
  info.values["angle"] = angle;
  TopoDS_Shape base = baseOf(prof, TopLoc_Location(), sid);
  BRepPrimAPI_MakeRevol mk(base, ax, full ? 2 * M_PI : angle * M_PI / 180.0, Standard_False);
  if (!mk.IsDone()) throw FeatureError("döndürme oluşturulamadı");
  std::vector<Candidate> cands;
  TopTools_IndexedMapOfShape edges;
  TopExp::MapShapes(base, TopAbs_EDGE, edges);
  for (int i = 1; i <= edges.Extent(); ++i) {
    const TopoDS_Edge& e = TopoDS::Edge(edges(i));
    int id = entityOf(e, prof, TopLoc_Location());
    addRevolGenerated(mk, e, f.id + ".rev.e" + (id >= 0 ? std::to_string(id) : std::string("x")), cands);
  }
  if (!full) {
    addFaces(mk.FirstShape(), f.id + ".start", 0, cands);
    addFaces(mk.LastShape(), f.id + ".end", 0, cands);
  }
  NamedShape tool;
  tool.shape = mk.Shape();
  if (!BRepCheck_Analyzer(tool.shape).IsValid())
    throw FeatureError("döndürme gövdesi geçersiz (profil ekseni kesiyor olabilir)");
  tool.faces = naming::finalize(tool.shape, cands, f.id, &c.log);
  info.toolShape = tool.shape;
  std::string warn = applyOp(c, f, op, tool);
  c.tools[f.id] = {tool, op};
  return warn;
}

// ---------------------------------------------------------------- Hole
struct HoleSeg {
  double r, z;  // start vertex (r, z) of the segment; segment goes to next point
  const char* name;
};

// Material span along a line from P in direction A within [0, maxLen].
double materialSpan(const TopoDS_Shape& body, const gp_Pnt& P, const gp_Dir& A, double maxLen) {
  IntCurvesFace_ShapeIntersector inter;
  inter.Load(body, 1e-6);
  gp_Lin lin(P, A);
  inter.Perform(lin, -1e-7, maxLen + 1e-7);
  std::vector<double> w{0.0, maxLen};
  if (inter.IsDone())
    for (int i = 1; i <= inter.NbPnt(); ++i) {
      double t = inter.WParameter(i);
      if (t > 0 && t < maxLen) w.push_back(t);
    }
  std::sort(w.begin(), w.end());
  std::vector<std::unique_ptr<BRepClass3d_SolidClassifier>> cls;
  for (TopExp_Explorer ex(body, TopAbs_SOLID); ex.More(); ex.Next())
    cls.push_back(std::make_unique<BRepClass3d_SolidClassifier>(ex.Current()));
  double first = -1, last = -1;
  for (size_t i = 0; i + 1 < w.size(); ++i) {
    if (w[i + 1] - w[i] < 1e-7) continue;
    gp_Pnt m = P.Translated(gp_Vec(A) * (0.5 * (w[i] + w[i + 1])));
    bool in = false;
    for (auto& cl : cls) {
      cl->Perform(m, 1e-7);
      if (cl->State() == TopAbs_IN) {
        in = true;
        break;
      }
    }
    if (in) {
      if (first < 0) first = w[i];
      last = w[i + 1];
    }
  }
  return first < 0 ? 0.0 : last - first;
}

std::string doHole(Ctx& c, const Feature& f, FeatureInfo& info) {
  const std::string sid = f.prop("sketch");
  auto skIt = c.res.solvedSketches.find(sid);
  auto frIt = c.frames.find(sid);
  if (skIt == c.res.solvedSketches.end() || frIt == c.frames.end() || !c.profiles.count(sid))
    throw FeatureError("çizim '" + sid + "' bulunamadı veya geçersiz");
  const Sketch& sk = skIt->second;
  gp_Ax3 fr = frIt->second;
  info.sketchFrame = fr;

  // hole locations (u,v)
  std::set<int> used;
  for (auto& e : sk.entities)
    for (int p : e.points) used.insert(p);
  std::vector<std::pair<double, double>> locs;
  for (auto& p : sk.points)
    if (p.id != 0 && !used.count(p.id)) locs.emplace_back(p.x, p.y);
  for (auto& e : sk.entities)
    if (e.type == SkEntityType::Circle && !e.construction && !e.points.empty())
      if (const SkPoint* p = sk.point(e.points[0])) locs.emplace_back(p->x, p->y);
  std::sort(locs.begin(), locs.end(), [](auto& a, auto& b) {
    if (std::abs(a.first - b.first) > 1e-7) return a.first < b.first;
    return a.second < b.second - 1e-7;
  });
  std::vector<std::pair<double, double>> uniq;
  for (auto& l : locs)
    if (uniq.empty() || std::hypot(uniq.back().first - l.first, uniq.back().second - l.second) > 1e-6) uniq.push_back(l);
  locs = uniq;
  if (locs.empty()) throw FeatureError("delik konumu yok (çizimde serbest nokta veya daire yok)");
  if (!c.hasBody()) throw FeatureError("delik için gövde yok");
  const TopoDS_Shape scope = scopeShape(c, f);

  double dia = num(c, f, "diameter");
  if (!(dia > 0)) throw FeatureError("çap pozitif olmalı");
  double r = dia / 2;
  bool through = num(c, f, "through", 0.0) != 0;
  double depth = through ? 0 : num(c, f, "depth");
  if (!through && !(depth > 0)) throw FeatureError("derinlik pozitif olmalı");
  std::string kind = f.prop("kind", "simple");
  double tipAngle = num(c, f, "tipAngle", 118.0);
  info.values["diameter"] = dia;
  info.values["through"] = through ? 1 : 0;
  if (!through) info.values["tipAngle"] = tipAngle;
  double L = through ? throughLength(scope, fr.Location()) : depth;
  double tip = 0;
  if (!through && tipAngle > 1e-9) {
    if (tipAngle >= 180) throw FeatureError("uç açısı 180°'den küçük olmalı");
    tip = r / std::tan(tipAngle * M_PI / 360.0);
  }
  std::vector<HoleSeg> prof;  // closed polyline in (r,z); last segment returns along axis
  prof.push_back({0, 0, "top"});
  if (kind == "counterbore") {
    double cbD = num(c, f, "cbDiameter"), cbH = num(c, f, "cbDepth");
    if (!(cbD > dia)) throw FeatureError("havşa (cb) çapı delik çapından büyük olmalı");
    if (!(cbH > 0) || (!through && cbH >= depth)) throw FeatureError("cb derinliği geçersiz");
    info.values["cbDiameter"] = cbD;
    info.values["cbDepth"] = cbH;
    prof.push_back({cbD / 2, 0, "cb_wall"});
    prof.push_back({cbD / 2, cbH, "cb_floor"});
    prof.push_back({r, cbH, "wall"});
  } else if (kind == "countersink") {
    double csD = num(c, f, "csDiameter"), csA = num(c, f, "csAngle", 90.0);
    if (!(csD > dia)) throw FeatureError("havşa çapı delik çapından büyük olmalı");
    if (!(csA > 0 && csA < 180)) throw FeatureError("havşa açısı geçersiz");
    double hs = (csD / 2 - r) / std::tan(csA * M_PI / 360.0);
    if (!through && hs >= depth) throw FeatureError("havşa derinliği delik derinliğinden büyük");
    info.values["csDiameter"] = csD;
    info.values["csAngle"] = csA;
    info.values["csDepth"] = hs;
    prof.push_back({csD / 2, 0, "cs"});
    prof.push_back({r, hs, "wall"});
  } else if (kind == "simple") {
    prof.push_back({r, 0, "wall"});
  } else {
    throw FeatureError("geçersiz delik türü '" + kind + "'");
  }
  prof.push_back({r, L, "bottom"});
  prof.push_back({0, L + tip, "axis"});

  gp_Dir A = fr.Direction().Reversed();
  gp_XYZ U = fr.XDirection().XYZ();
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  std::vector<Candidate> cands;
  double maxMat = 0;
  for (size_t k = 0; k < locs.size(); ++k) {
    gp_XYZ P = fr.Location().XYZ() + fr.XDirection().XYZ() * locs[k].first + fr.YDirection().XYZ() * locs[k].second;
    double span = materialSpan(scope, gp_Pnt(P), A, through ? L : depth);
    maxMat = std::max(maxMat, span);
    std::vector<TopoDS_Vertex> vs;
    for (auto& s : prof) vs.push_back(BRepBuilderAPI_MakeVertex(gp_Pnt(P + U * s.r + A.XYZ() * s.z)));
    BRepBuilderAPI_MakeWire mw;
    std::vector<std::pair<TopoDS_Edge, std::string>> es;
    for (size_t i = 0; i < prof.size(); ++i) {
      TopoDS_Edge e = BRepBuilderAPI_MakeEdge(vs[i], vs[(i + 1) % vs.size()]);
      mw.Add(e);
      es.emplace_back(e, prof[i].name);
    }
    if (!mw.IsDone()) throw FeatureError("delik profili oluşturulamadı");
    BRepBuilderAPI_MakeFace mf(mw.Wire(), Standard_True);
    if (!mf.IsDone()) throw FeatureError("delik profili oluşturulamadı");
    TopoDS_Face face = mf.Face();
    BRepPrimAPI_MakeRevol rv(face, gp_Ax1(gp_Pnt(P), A), 2 * M_PI, Standard_False);
    if (!rv.IsDone()) throw FeatureError("delik gövdesi oluşturulamadı");
    std::string pre = f.id + ".h" + std::to_string(k) + ".";
    for (TopExp_Explorer ex(face, TopAbs_EDGE); ex.More(); ex.Next()) {
      for (auto& [e, nm] : es)
        if (e.IsSame(ex.Current()) && std::string(nm) != "axis") addRevolGenerated(rv, ex.Current(), pre + nm, cands);
    }
    bb.Add(comp, rv.Shape());
  }
  info.values["locations"] = (double)locs.size();
  info.values["materialDepth"] = maxMat;
  info.values["depth"] = through ? maxMat : depth;
  NamedShape tool;
  tool.shape = comp;
  tool.faces = naming::finalize(comp, cands, f.id, &c.log);
  info.toolShape = comp;
  std::string warn = applyOp(c, f, "cut", tool);
  c.tools[f.id] = {tool, "cut"};
  return warn;
}

// ---------------------------------------------------------------- Fillet / Chamfer
// Normal (outward, honoring face orientation) and pcurve data of edge e on face F at the edge mid-parameter.
bool edgeFaceFrame(const TopoDS_Edge& e, const TopoDS_Face& F, gp_Pnt2d& uv, gp_Vec2d& t2, gp_Vec& du, gp_Vec& dv,
                   gp_Vec& n) {
  Standard_Real v0, v1;
  Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(e, F, v0, v1);
  if (pc.IsNull()) return false;
  pc->D1(0.5 * (v0 + v1), uv, t2);
  BRepAdaptor_Surface s(F);
  gp_Pnt pp;
  s.D1(uv.X(), uv.Y(), pp, du, dv);
  n = du.Crossed(dv);
  if (n.Magnitude() < 1e-12 || t2.Magnitude() < 1e-12) return false;
  n.Normalize();
  if (F.Orientation() == TopAbs_REVERSED) n.Reverse();
  return true;
}

// Concave edge: the direction pointing from the edge into face 1 (tangent to face 1) points along
// the outward normal of face 2.
bool isConcave(const TopoDS_Edge& e, const TopTools_IndexedDataMapOfShapeListOfShape& anc) {
  int idx = anc.FindIndex(e);
  if (!idx) return false;
  std::vector<TopoDS_Face> fs;
  for (TopTools_ListIteratorOfListOfShape it(anc(idx)); it.More(); it.Next()) {
    bool dup = false;
    for (auto& f : fs) dup |= f.IsSame(it.Value());
    if (!dup) fs.push_back(TopoDS::Face(it.Value()));
  }
  if (fs.size() != 2) return false;
  gp_Pnt2d uv1, uv2;
  gp_Vec2d t1, t2;
  gp_Vec du1, dv1, n1, du2, dv2, n2;
  if (!edgeFaceFrame(e, fs[0], uv1, t1, du1, dv1, n1) || !edgeFaceFrame(e, fs[1], uv2, t2, du2, dv2, n2)) return false;
  gp_Vec2d perp(-t1.Y(), t1.X());
  perp.Normalize();
  gp_Vec w = du1 * perp.X() + dv1 * perp.Y();
  if (w.Magnitude() < 1e-12) return false;
  double delta = 0.01 / w.Magnitude();
  gp_Pnt2d probe(uv1.X() + perp.X() * delta, uv1.Y() + perp.Y() * delta);
  BRepClass_FaceClassifier fc(fs[0], probe, 1e-9);
  if (fc.State() != TopAbs_IN) w.Reverse();
  w.Normalize();
  return w.Dot(n2) > 1e-6;
}

// Index of the body owning a sub-shape (edge / face), -1 if none.
int ownerOf(const Ctx& c, const TopoDS_Shape& sub, TopAbs_ShapeEnum kind,
            std::vector<TopTools_IndexedMapOfShape>& cache) {
  if (cache.size() != c.bodies.size()) {
    cache.assign(c.bodies.size(), TopTools_IndexedMapOfShape());
    for (size_t i = 0; i < c.bodies.size(); ++i) TopExp::MapShapes(c.bodies[i].solid, kind, cache[i]);
  }
  for (size_t i = 0; i < cache.size(); ++i)
    if (cache[i].Contains(sub)) return (int)i;
  return -1;
}

std::string doBlend(Ctx& c, const Feature& f, FeatureInfo& info, bool fillet) {
  if (!c.hasBody()) throw FeatureError("gövde yok");
  double r = fillet ? num(c, f, "radius") : num(c, f, "distance");
  if (!(r > 0)) throw FeatureError(std::string(fillet ? "yarıçap" : "mesafe") + " pozitif olmalı");
  info.values[fillet ? "radius" : "distance"] = r;
  NamedShape all = namedAll(c);
  auto em = naming::nameEdges(all.shape, c.faces);
  NameMap edgeName;
  for (auto& [n, e] : em) edgeName.Bind(e, n);
  auto by = all.byName();
  TopTools_IndexedMapOfShape sel;
  std::string warn;
  for (const auto& ref : f.refs) {
    if (ref.rfind("face:", 0) == 0) {
      std::string fn = ref.substr(5);
      auto hits = resolveRef(by, fn, warn);
      if (hits.empty()) throw FeatureError("yüz bulunamadı '" + fn + "'");
      for (auto& F : hits)
        for (TopExp_Explorer ex(F, TopAbs_EDGE); ex.More(); ex.Next()) {
          const TopoDS_Edge& e = TopoDS::Edge(ex.Current());
          if (BRep_Tool::Degenerated(e) || BRep_Tool::IsClosed(e, F)) continue;
          sel.Add(e);
        }
    } else {
      auto hits = resolveRef(em, ref, warn);
      if (hits.empty()) throw FeatureError("kenar bulunamadı '" + ref + "'");
      for (auto& e : hits) sel.Add(e);
    }
  }
  if (sel.IsEmpty()) throw FeatureError("kenar seçilmedi");
  // concavity on the bodies before blending
  {
    TopTools_IndexedDataMapOfShapeListOfShape anc;
    TopExp::MapShapesAndAncestors(all.shape, TopAbs_EDGE, TopAbs_FACE, anc);
    int conc = 0;
    for (int i = 1; i <= sel.Extent(); ++i) conc += isConcave(TopoDS::Edge(sel(i)), anc) ? 1 : 0;
    info.values["edges"] = sel.Extent();
    info.values["concaveEdges"] = conc;
  }
  // group the selected edges by owning body (Fillet/Chamfer act on the owning bodies only)
  std::vector<TopTools_IndexedMapOfShape> cache;
  std::map<int, std::vector<TopoDS_Edge>> perBody;
  for (int i = 1; i <= sel.Extent(); ++i) {
    int b = ownerOf(c, sel(i), TopAbs_EDGE, cache);
    if (b < 0) throw FeatureError("kenar hiçbir gövdeye ait değil");
    perBody[b].push_back(TopoDS::Edge(sel(i)));
  }
  info.values["bodies"] = (double)perBody.size();
  const std::string tooBig = std::string(fillet ? "yarıçap " : "pah mesafesi ") + fmt(r) + " mm kenar için çok büyük";
  struct Run {
    int body;
    std::unique_ptr<BRepFilletAPI_LocalOperation> mk;
    TopoDS_Shape result;
  };
  std::vector<Run> runs;
  std::vector<std::tuple<std::string, TopoDS_Shape, size_t>> srt;  // (sort key, source edge, run)
  for (auto& [bi, edges] : perBody) {
    Run run;
    run.body = bi;
    TopTools_IndexedMapOfShape allE;
    const TopoDS_Shape& solid = c.bodies[bi].solid;
    try {
      if (fillet) {
        auto m = std::make_unique<BRepFilletAPI_MakeFillet>(solid);
        for (auto& e : edges) m->Add(r, e);
        m->Build();
        if (!m->IsDone()) throw FeatureError(tooBig);
        for (int i = 1; i <= m->NbContours(); ++i)
          for (int j = 1; j <= m->NbEdges(i); ++j) allE.Add(m->Edge(i, j));
        run.result = m->Shape();
        run.mk = std::move(m);
      } else {
        auto m = std::make_unique<BRepFilletAPI_MakeChamfer>(solid);
        for (auto& e : edges) m->Add(r, e);
        m->Build();
        if (!m->IsDone()) throw FeatureError(tooBig);
        for (int i = 1; i <= m->NbContours(); ++i)
          for (int j = 1; j <= m->NbEdges(i); ++j) allE.Add(m->Edge(i, j));
        run.result = m->Shape();
        run.mk = std::move(m);
      }
    } catch (const FeatureError&) {
      throw;
    } catch (...) {
      throw FeatureError(tooBig);
    }
    run.result = simplify(run.result);
    if (countSolids(run.result) == 0 || !BRepCheck_Analyzer(run.result).IsValid()) throw FeatureError(tooBig);
    for (int i = 1; i <= allE.Extent(); ++i) {
      const std::string* n = edgeName.Seek(allE(i));
      std::string key;
      if (n) key = *n;
      else {
        gp_Pnt m = edgeMid(TopoDS::Edge(allE(i)));
        std::ostringstream os;
        os.precision(9);
        os << "~" << std::fixed << m.X() << "," << m.Y() << "," << m.Z();
        key = os.str();
      }
      srt.emplace_back(key, allE(i), runs.size());
    }
    runs.push_back(std::move(run));
  }
  // k = index of the source edge sorted by persistent name, over all bodies of this feature
  std::stable_sort(srt.begin(), srt.end(), [](auto& a, auto& b) { return std::get<0>(a) < std::get<0>(b); });
  std::vector<Candidate> cands;
  const std::string pre = f.id + (fillet ? ".f" : ".c");
  for (size_t k = 0; k < srt.size(); ++k)
    addGenerated(*runs[std::get<2>(srt[k])].mk, std::get<1>(srt[k]), pre + std::to_string(k), cands);
  std::map<int, const Run*> byBody;
  for (auto& run : runs) {
    TopTools_IndexedMapOfShape rf;
    TopExp::MapShapes(run.result, TopAbs_FACE, rf);
    naming::collectCandidates(*run.mk, namedBody(c, run.body), 0, rf, cands);
    byBody[run.body] = &run;
  }
  std::vector<BodyRec> nb;
  for (size_t i = 0; i < c.bodies.size(); ++i) {
    auto it = byBody.find((int)i);
    if (it == byBody.end()) nb.push_back(c.bodies[i]);
    else
      for (auto& p : piecesOf(c.bodies[i].name, c.bodies[i].createdBy, it->second->result)) nb.push_back(p);
  }
  commit(c, std::move(nb), std::move(cands), f.id);
  return warn;
}

// ---------------------------------------------------------------- Shell
std::string doShell(Ctx& c, const Feature& f, FeatureInfo& info) {
  if (!c.hasBody()) throw FeatureError("gövde yok");
  double t = num(c, f, "thickness");
  if (!(t > 0)) throw FeatureError("et kalınlığı pozitif olmalı");
  std::string dir = f.prop("direction", "inward");
  if (dir != "inward" && dir != "outward") throw FeatureError("geçersiz yön '" + dir + "'");
  info.values["thickness"] = t;
  info.values["removedFaces"] = (double)f.refs.size();
  auto by = namedAll(c).byName();
  std::string warn;
  // faces to remove grouped by owning body; without refs: `targets` or every body (closed hollow)
  std::vector<TopTools_IndexedMapOfShape> cache;
  std::map<int, TopTools_ListOfShape> perBody;
  for (auto& n : f.refs) {
    auto hits = resolveRef(by, n, warn);
    if (hits.empty()) throw FeatureError("yüz bulunamadı '" + n + "'");
    for (auto& F : hits) {
      int b = ownerOf(c, F, TopAbs_FACE, cache);
      if (b < 0) throw FeatureError("yüz hiçbir gövdeye ait değil '" + n + "'");
      perBody[b].Append(F);
    }
  }
  if (f.refs.empty()) {
    auto names = splitList(f.prop("targets"));
    if (names.empty())
      for (size_t i = 0; i < c.bodies.size(); ++i) perBody[(int)i];
    else
      for (size_t i : resolveBodies(c, names, warn)) perBody[(int)i];
  }
  const std::string pre = f.id + (dir == "inward" ? ".inner." : ".outer.");
  auto em = naming::nameEdges(namedAll(c).shape, c.faces);
  std::vector<Candidate> cands;
  std::map<int, TopoDS_Shape> results;
  for (auto& [bi, rem] : perBody) {
    BRepOffsetAPI_MakeThickSolid mk;
    TopoDS_Shape result;
    try {
      mk.MakeThickSolidByJoin(c.bodies[bi].solid, rem, dir == "inward" ? -t : t, 1e-3, BRepOffset_Skin, Standard_False,
                              Standard_False, GeomAbs_Intersection);
      mk.Build();
      if (!mk.IsDone()) throw FeatureError("x");
      result = simplify(mk.Shape());
    } catch (...) {
      throw FeatureError("kabuk oluşturulamadı (kalınlık " + fmt(t) + " mm)");
    }
    if (countSolids(result) == 0) throw FeatureError("kabuk oluşturulamadı (kalınlık " + fmt(t) + " mm)");
    TopTools_IndexedMapOfShape rf;
    TopExp::MapShapes(result, TopAbs_FACE, rf);
    // OCCT thick-solid history: kept faces are unchanged, the offset (parallel) face of F is
    // Generated(F), a removed face F is Modified() into the rim face lying in its plane (keeps F's name).
    NamedShape nb = namedBody(c, bi);
    for (NameMap::Iterator it(nb.faces); it.More(); it.Next()) {
      const TopoDS_Shape& F = it.Key();
      if (rf.Contains(F)) cands.push_back({F, it.Value(), 0});
      for (TopTools_ListIteratorOfListOfShape li(mk.Modified(F)); li.More(); li.Next())
        if (rf.Contains(li.Value())) cands.push_back({li.Value(), it.Value(), 0});
      for (TopTools_ListIteratorOfListOfShape li(mk.Generated(F)); li.More(); li.Next())
        if (rf.Contains(li.Value())) cands.push_back({li.Value(), pre + it.Value(), 1});
    }
    // side faces generated from edges (e.g. open boundaries) -> "<fid>.rim.<edgeName>"
    for (auto& [en, e] : em) {
      const TopTools_ListOfShape& g = mk.Generated(e);
      for (TopTools_ListIteratorOfListOfShape li(g); li.More(); li.Next())
        if (li.Value().ShapeType() == TopAbs_FACE && rf.Contains(li.Value()))
          cands.push_back({li.Value(), f.id + ".rim." + en, 2});
    }
    results[bi] = result;
  }
  std::vector<BodyRec> nbs;
  for (size_t i = 0; i < c.bodies.size(); ++i) {
    auto it = results.find((int)i);
    if (it == results.end()) nbs.push_back(c.bodies[i]);
    else
      for (auto& p : piecesOf(c.bodies[i].name, c.bodies[i].createdBy, it->second)) nbs.push_back(p);
  }
  commit(c, std::move(nbs), std::move(cands), f.id);
  return warn;
}

// ---------------------------------------------------------------- Pattern
std::string doPattern(Ctx& c, const Feature& f, FeatureInfo& info) {
  std::string src = f.prop("source");
  auto it = c.tools.find(src);
  if (it == c.tools.end()) throw FeatureError("kaynak özellik bulunamadı '" + src + "'");
  const ToolRecord& rec = it->second;
  std::string kind = f.prop("kind", "linear");
  double cntD = num(c, f, "count");
  int count = (int)std::lround(cntD);
  if (count < 1) throw FeatureError("adet en az 1 olmalı");
  info.values["count"] = count;
  if (count == 1) {  // only the source instance: nothing to add (lets a count parameter go down to 1)
    if (kind != "linear" && kind != "circular") throw FeatureError("geçersiz desen türü '" + kind + "'");
    return "";
  }
  std::vector<gp_Trsf> trs;
  if (kind == "linear") {
    double sp = num(c, f, "spacing");
    gp_Vec d(num(c, f, "dirX", 1.0), num(c, f, "dirY", 0.0), num(c, f, "dirZ", 0.0));
    if (d.Magnitude() < 1e-12) throw FeatureError("doğrultu vektörü sıfır");
    d.Normalize();
    info.values["spacing"] = sp;
    for (int k = 1; k < count; ++k) {
      gp_Trsf t;
      t.SetTranslation(d * (sp * k));
      trs.push_back(t);
    }
  } else if (kind == "circular") {
    double ang = num(c, f, "angle", 360.0);
    std::string ax = f.prop("axis", "Z");
    gp_Dir dd = ax == "X" ? gp_Dir(1, 0, 0) : ax == "Y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
    if (ax != "X" && ax != "Y" && ax != "Z") throw FeatureError("geçersiz eksen '" + ax + "'");
    bool full = std::abs(ang) >= 360.0 - 1e-9;
    double step = full ? ang / count : ang / (count - 1);
    info.values["angle"] = ang;
    info.values["stepAngle"] = step;
    for (int k = 1; k < count; ++k) {
      gp_Trsf t;
      t.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), dd), step * k * M_PI / 180.0);
      trs.push_back(t);
    }
  } else {
    throw FeatureError("geçersiz desen türü '" + kind + "'");
  }
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  NamedShape tool;
  for (size_t k = 0; k < trs.size(); ++k) {
    TopLoc_Location loc(trs[k]);
    bb.Add(comp, rec.tool.shape.Moved(loc));
    std::string pre = f.id + ".i" + std::to_string(k + 1) + ".";
    for (NameMap::Iterator ni(rec.tool.faces); ni.More(); ni.Next()) tool.faces.Bind(ni.Key().Moved(loc), pre + ni.Value());
  }
  tool.shape = comp;
  info.toolShape = comp;
  // own `op` prop (v2: op=new creates bodies) else the source's op (new is treated as join, v1)
  std::string op = f.prop("op", rec.op == "new" ? "join" : rec.op);
  std::string warn = applyOp(c, f, op, tool);
  c.tools[f.id] = {tool, op};
  return warn;
}

// ---------------------------------------------------------------- Plane (datum)
// Edge by persistent name in the current result; a split edge falls back to its first piece (warning).
TopoDS_Edge findEdge(const std::map<std::string, TopoDS_Edge>& em, const std::string& name, std::string& warn) {
  auto it = em.find(name);
  if (it != em.end()) return it->second;
  const std::string b = baseName(name);
  for (auto& [n, e] : em)
    if (baseName(n) == b) {
      warn += std::string(warn.empty() ? "" : "; ") + "'" + name + "' bölünmüş, kullanılan: " + n;
      return e;
    }
  throw FeatureError("kenar bulunamadı '" + name + "'");
}

TopoDS_Face findFace(const std::map<std::string, TopoDS_Face>& by, const std::string& name, std::string& warn) {
  auto it = by.find(name);
  if (it != by.end()) return it->second;
  const std::string b = baseName(name);
  for (auto& [n, F] : by)
    if (baseName(n) == b) {
      warn += std::string(warn.empty() ? "" : "; ") + "'" + name + "' bölünmüş, kullanılan: " + n;
      return F;
    }
  throw FeatureError("yüz bulunamadı '" + name + "'");
}

// Curve parameter at normalised arc length t (0..1) in the curve parameter direction.
double paramAt(const BRepAdaptor_Curve& cv, double t) {
  const double a = cv.FirstParameter(), b = cv.LastParameter();
  if (t <= 0) return a;
  if (t >= 1) return b;
  const double L = GCPnts_AbscissaPoint::Length(cv, a, b);
  if (L < 1e-12) return a + t * (b - a);
  GCPnts_AbscissaPoint ap(cv, t * L, a);
  if (!ap.IsDone()) return a + t * (b - a);
  return ap.Parameter();
}

void pointTangent(const TopoDS_Edge& e, double t, gp_Pnt& P, gp_Dir& D) {
  BRepAdaptor_Curve cv(e);
  gp_Vec d1;
  cv.D1(paramAt(cv, t), P, d1);
  if (d1.Magnitude() < 1e-12) throw FeatureError("kenar teğeti tanımsız");
  D = gp_Dir(d1);
}

gp_Dir straightDir(const TopoDS_Edge& e, const std::string& name) {
  BRepAdaptor_Curve cv(e);
  if (cv.GetType() != GeomAbs_Line) throw FeatureError("kenar düz değil '" + name + "'");
  gp_Pnt P;
  gp_Dir D;
  pointTangent(e, 0.5, P, D);
  return D;
}

// OnFace u rule: world +X projected onto the plane, else world +Y.
gp_Dir ruleU(const gp_Dir& n) {
  gp_XYZ N = n.XYZ();
  auto proj = [&](const gp_XYZ& a) { return a - N * a.Dot(N); };
  gp_XYZ u = proj(gp_XYZ(1, 0, 0));
  if (u.Modulus() < 1e-6) u = proj(gp_XYZ(0, 1, 0));
  return gp_Dir(u);
}

gp_Dir outwardPlaneNormal(const TopoDS_Face& F, const std::string& name) {
  BRepAdaptor_Surface s(F, Standard_False);
  if (s.GetType() != GeomAbs_Plane) throw FeatureError("yüz düzlemsel değil '" + name + "'");
  gp_Dir n = s.Plane().Axis().Direction();
  if (F.Orientation() == TopAbs_REVERSED) n.Reverse();
  return n;
}

std::string doPlane(Ctx& c, const Feature& f, FeatureInfo& info) {
  const std::string kind = f.prop("kind", "edgeNormal");
  const double offset = num(c, f, "offset", 0.0);
  const double rot = num(c, f, "rotate", 0.0);
  const bool flip = num(c, f, "flip", 0.0) != 0;
  std::string warn;
  auto needRefs = [&](size_t n) {
    if (f.refs.size() < n || f.refs[0].empty() || (n > 1 && f.refs[1].empty()))
      throw FeatureError(n == 1 ? "referans eksik" : "iki kenar referansı gerekli");
    if (!c.hasBody()) throw FeatureError("referans için gövde yok");
  };
  NamedShape all = namedAll(c);
  gp_Pnt O;
  gp_Dir N(0, 0, 1), U(1, 0, 0);
  if (kind == "edgeNormal") {
    needRefs(1);
    const double t = num(c, f, "t", 0.0);
    if (t < -1e-12 || t > 1 + 1e-12) throw FeatureError("t 0 ile 1 arasında olmalı (" + fmt(t) + ")");
    info.values["t"] = t;
    auto em = naming::nameEdges(all.shape, c.faces);
    TopoDS_Edge e = findEdge(em, f.refs[0], warn);
    pointTangent(e, t, O, N);
    U = ruleU(N);
  } else if (kind == "edgeAngle") {
    needRefs(1);
    const double t = num(c, f, "t", 0.5);
    const double ang = num(c, f, "angle", 0.0);
    if (t < -1e-12 || t > 1 + 1e-12) throw FeatureError("t 0 ile 1 arasında olmalı (" + fmt(t) + ")");
    info.values["t"] = t;
    info.values["angle"] = ang;
    auto em = naming::nameEdges(all.shape, c.faces);
    TopoDS_Edge e = findEdge(em, f.refs[0], warn);
    gp_Dir d = straightDir(e, f.refs[0]);
    gp_Dir tan;
    pointTangent(e, t, O, tan);
    auto by = all.byName();
    gp_Dir n0;
    if (f.refs.size() > 1 && !f.refs[1].empty()) {
      n0 = outwardPlaneNormal(findFace(by, f.refs[1], warn), f.refs[1]);
    } else {
      // default reference: the planar face adjacent to the edge whose name sorts first
      TopTools_IndexedDataMapOfShapeListOfShape anc;
      TopExp::MapShapesAndAncestors(all.shape, TopAbs_EDGE, TopAbs_FACE, anc);
      int idx = anc.FindIndex(e);
      std::set<std::string> names;
      if (idx)
        for (TopTools_ListIteratorOfListOfShape li(anc(idx)); li.More(); li.Next())
          if (const std::string* nm = c.faces.Seek(li.Value()))
            if (BRepAdaptor_Surface(TopoDS::Face(li.Value()), Standard_False).GetType() == GeomAbs_Plane)
              names.insert(*nm);
      if (names.empty()) throw FeatureError("kenara bitişik düzlemsel yüz yok '" + f.refs[0] + "'");
      n0 = outwardPlaneNormal(by.at(*names.begin()), *names.begin());
    }
    gp_Vec np = gp_Vec(n0) - gp_Vec(d) * gp_Vec(n0).Dot(gp_Vec(d));
    if (np.Magnitude() < 1e-6) throw FeatureError("referans yüz normali kenara paralel");
    np.Rotate(gp_Ax1(gp_Pnt(0, 0, 0), d), ang * M_PI / 180.0);
    N = gp_Dir(np);
    U = d;
  } else if (kind == "twoEdges") {
    needRefs(2);
    auto em = naming::nameEdges(all.shape, c.faces);
    TopoDS_Edge e0 = findEdge(em, f.refs[0], warn), e1 = findEdge(em, f.refs[1], warn);
    gp_Dir d0 = straightDir(e0, f.refs[0]), d1 = straightDir(e1, f.refs[1]);
    gp_Pnt m0, m1;
    gp_Dir dd;
    pointTangent(e0, 0.5, m0, dd);
    pointTangent(e1, 0.5, m1, dd);
    gp_Vec w(m0, m1);
    gp_Vec n = gp_Vec(d0).Crossed(w);
    if (n.Magnitude() < 1e-6 * std::max(1.0, w.Magnitude())) n = gp_Vec(d0).Crossed(gp_Vec(d1));
    if (n.Magnitude() < 1e-9) throw FeatureError("kenarlar aynı doğru üzerinde, düzlem tanımsız");
    N = gp_Dir(n);
    // coplanarity: both ends of edge 1 must lie in the plane through m0
    BRepAdaptor_Curve c1(e1);
    for (double p : {c1.FirstParameter(), c1.LastParameter()}) {
      double dist = std::abs(gp_Vec(m0, c1.Value(p)).Dot(gp_Vec(N)));
      if (dist > 1e-6 * std::max(1.0, w.Magnitude())) throw FeatureError("kenarlar aynı düzlemde değil (aykırı kenarlar)");
    }
    O = m0;
    U = d0;
  } else if (kind == "faceOffset") {
    needRefs(1);
    auto by = all.byName();
    TopoDS_Face F = findFace(by, f.refs[0], warn);
    gp_Ax3 fr;
    if (!frameFromFace(F, 0, fr)) throw FeatureError("yüz düzlemsel değil '" + f.refs[0] + "'");
    O = fr.Location();
    N = fr.Direction();
    U = fr.XDirection();
  } else if (kind == "axisAngle") {
    // Plane CONTAINING a world axis (no references, works without a body): axis = X | Y | Z (default Z),
    // angle (deg) rotates the base plane about that axis (right-hand rule). Base frames (angle 0):
    //   Z: u=+X, v=+Z, n=-Y (= standard XZ)   X: u=+Y, v=+X, n=-Z   Y: u=+Z, v=+Y, n=-X
    const double ang = num(c, f, "angle", 0.0);
    info.values["angle"] = ang;
    const std::string ax = f.prop("axis", "Z");
    gp_Dir A, U0;
    if (ax == "Z") { A = gp_Dir(0, 0, 1); U0 = gp_Dir(1, 0, 0); }
    else if (ax == "X") { A = gp_Dir(1, 0, 0); U0 = gp_Dir(0, 1, 0); }
    else if (ax == "Y") { A = gp_Dir(0, 1, 0); U0 = gp_Dir(0, 0, 1); }
    else throw FeatureError("geçersiz eksen '" + ax + "'");
    O = gp_Pnt(0, 0, 0);
    U = U0.Rotated(gp_Ax1(O, A), ang * M_PI / 180.0);
    N = U.Crossed(A);  // v = N x U = A
  } else {
    throw FeatureError("geçersiz düzlem türü '" + kind + "'");
  }
  // common modifiers: rotate u/v about the normal, flip (normal and v reversed), offset along final normal
  if (std::abs(rot) > 1e-12) U = U.Rotated(gp_Ax1(O, N), rot * M_PI / 180.0);
  if (flip) N.Reverse();
  O.Translate(gp_Vec(N) * offset);
  gp_Ax3 fr(O, N, U);  // YDirection = N x U (flip: v := -v)
  info.values["offset"] = offset;
  info.values["rotate"] = rot;
  info.values["flip"] = flip ? 1 : 0;
  info.sketchFrame = fr;
  c.datums[f.id] = fr;
  return warn;
}

// ---------------------------------------------------------------- Combine (body-to-body boolean)
std::string doCombine(Ctx& c, const Feature& f, FeatureInfo& info) {
  if (!c.hasBody()) throw FeatureError("gövde yok");
  const std::string op = f.prop("op", "join");
  if (op != "join" && op != "cut" && op != "intersect") throw FeatureError("geçersiz işlem '" + op + "'");
  std::string warn;
  const std::string tname = trimStr(f.prop("target"));
  if (tname.empty()) throw FeatureError("hedef gövde belirtilmemiş");
  auto tIdx = resolveBodies(c, {tname}, warn);
  if (tIdx.size() != 1) throw FeatureError("hedef gövde '" + tname + "' bölünmüş, parçalardan birini seçin");
  const size_t ti = tIdx[0];
  auto toolNames = splitList(f.prop("tools"));
  if (toolNames.empty()) throw FeatureError("araç gövdesi belirtilmemiş");
  auto tl = resolveBodies(c, toolNames, warn);
  for (size_t i : tl)
    if (i == ti) throw FeatureError("hedef gövde '" + c.bodies[ti].name + "' araç olarak kullanılamaz");
  const bool keep = num(c, f, "keepTools", 0.0) != 0;
  info.values["tools"] = (double)tl.size();
  info.values["keepTools"] = keep ? 1 : 0;
  std::vector<NamedShape> toolNS;
  TopTools_ListOfShape args, tls;
  args.Append(c.bodies[ti].solid);
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  for (size_t i : tl) {
    NamedShape ns = namedBody(c, i);
    if (keep) {  // boolean with a copy; its faces inside the target are named "<fid>.<toolFace>"
      BRepBuilderAPI_Copy cp(ns.shape);
      NamedShape cn;
      cn.shape = cp.Shape();
      for (NameMap::Iterator it(ns.faces); it.More(); it.Next()) {
        TopoDS_Shape cf = cp.ModifiedShape(it.Key());
        if (!cf.IsNull()) cn.faces.Bind(cf, f.id + "." + it.Value());
      }
      ns = cn;
    }
    tls.Append(ns.shape);
    bb.Add(comp, ns.shape);
    toolNS.push_back(ns);
  }
  info.toolShape = comp;
  auto bop = runBop(op, args, tls);
  TopoDS_Shape r = bop->Shape();
  const BodyRec& tgt = c.bodies[ti];
  // largestOnly=1: keep only the largest resulting solid (drops enclosed pockets / slivers, e.g. dead volumes
  // of a fluid-volume extraction). The number of dropped pieces is reported in values["droppedPieces"].
  if (num(c, f, "largestOnly", 0.0) != 0) {
    auto sol = solidsOf(r);
    if (sol.size() > 1) {
      size_t best = 0;
      double bv = -1;
      for (size_t i = 0; i < sol.size(); ++i) {
        GProp_GProps g;
        BRepGProp::VolumeProperties(sol[i], g);
        if (std::abs(g.Mass()) > bv) { bv = std::abs(g.Mass()); best = i; }
      }
      info.values["droppedPieces"] = (double)sol.size() - 1;
      r = sol[best];
    } else {
      info.values["droppedPieces"] = 0;
    }
  }
  auto ps = piecesOf(tgt.name, tgt.createdBy, r);
  if (ps.empty()) throw FeatureError("işlem sonucu boş gövde (" + op + ")");
  TopTools_IndexedMapOfShape rf;
  TopExp::MapShapes(r, TopAbs_FACE, rf);
  std::vector<Candidate> cands;
  naming::collectCandidates(*bop, namedBody(c, ti), 0, rf, cands);
  for (size_t k = 0; k < toolNS.size(); ++k) naming::collectCandidates(*bop, toolNS[k], 1 + (int)k, rf, cands);
  std::set<size_t> consumed;
  if (!keep) consumed.insert(tl.begin(), tl.end());
  std::vector<BodyRec> nb;
  for (size_t i = 0; i < c.bodies.size(); ++i) {
    if (i == ti)
      for (auto& p : ps) nb.push_back(p);
    else if (!consumed.count(i))
      nb.push_back(c.bodies[i]);
  }
  commit(c, std::move(nb), std::move(cands), f.id);
  return warn;
}

// ---------------------------------------------------------------- Transform (SC-UX CR-3, SpaceClaim "Move")
std::string doTransform(Ctx& c, const Feature& f, FeatureInfo& info) {
  if (!c.hasBody()) throw FeatureError("gövde yok");
  std::string warn;
  std::vector<size_t> sel;
  auto names = splitList(f.prop("bodies"));
  if (names.empty())
    for (size_t i = 0; i < c.bodies.size(); ++i) sel.push_back(i);
  else
    sel = resolveBodies(c, names, warn);
  const double tx = num(c, f, "tx", 0.0), ty = num(c, f, "ty", 0.0), tz = num(c, f, "tz", 0.0);
  const double ang = num(c, f, "angle", 0.0);
  const std::string ax = f.prop("axis", "Z");
  if (ax != "X" && ax != "Y" && ax != "Z") throw FeatureError("geçersiz eksen '" + ax + "'");
  const gp_Dir dd = ax == "X" ? gp_Dir(1, 0, 0) : ax == "Y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1);
  const gp_Pnt O(num(c, f, "ox", 0.0), num(c, f, "oy", 0.0), num(c, f, "oz", 0.0));
  const bool copy = num(c, f, "copy", 0.0) != 0;
  info.values["tx"] = tx;
  info.values["ty"] = ty;
  info.values["tz"] = tz;
  info.values["angle"] = ang;
  info.values["copy"] = copy ? 1 : 0;
  info.values["bodies"] = (double)sel.size();
  gp_Trsf rot, tr;
  if (std::abs(ang) > 0) rot.SetRotation(gp_Ax1(O, dd), ang * M_PI / 180.0);
  tr.SetTranslation(gp_Vec(tx, ty, tz));
  const TopLoc_Location loc(tr * rot);  // rotation first, then translation
  std::vector<Candidate> cands;
  std::vector<BodyRec> nb = c.bodies;
  for (size_t i : sel) {
    NamedShape ns = namedBody(c, i);
    if (!copy) {
      nb[i].solid = c.bodies[i].solid.Moved(loc);
      for (NameMap::Iterator it(ns.faces); it.More(); it.Next()) cands.push_back({it.Key().Moved(loc), it.Value(), 0});
      continue;
    }
    BRepBuilderAPI_Copy cp(ns.shape);
    std::string bn = sel.size() == 1 && !trimStr(f.prop("bodyName")).empty() ? trimStr(f.prop("bodyName"))
                                                                             : c.bodies[i].name + "." + f.id;
    for (auto& b : nb)
      if (b.name == bn) throw FeatureError("gövde adı '" + bn + "' zaten kullanılıyor");
    nb.push_back({bn, f.id, cp.Shape().Moved(loc)});
    for (NameMap::Iterator it(ns.faces); it.More(); it.Next()) {
      TopoDS_Shape cf = cp.ModifiedShape(it.Key());
      if (!cf.IsNull()) cands.push_back({cf.Moved(loc), f.id + "." + it.Value(), 1000});
    }
  }
  commit(c, std::move(nb), std::move(cands), f.id);
  return warn;
}

void fixIfInvalid(Ctx& c) {
  if (!c.hasBody()) return;
  std::vector<Handle(ShapeBuild_ReShape)> ctxs;
  std::vector<BodyRec> nb = c.bodies;
  bool any = false;
  for (auto& b : nb) {
    if (BRepCheck_Analyzer(b.solid).IsValid()) continue;
    try {
      ShapeFix_Shape fix(b.solid);
      fix.Perform();
      auto sol = solidsOf(fix.Shape());
      if (sol.size() != 1) continue;
      b.solid = sol[0];
      if (!fix.Context().IsNull()) ctxs.push_back(fix.Context());
      any = true;
      c.log.push_back("ShapeFix applied to body " + b.name);
    } catch (...) {
    }
  }
  if (!any) return;
  std::vector<Candidate> cands;
  for (NameMap::Iterator it(c.faces); it.More(); it.Next()) {
    TopoDS_Shape v = it.Key();
    for (auto& cx : ctxs) v = cx->Value(v);
    addFaces(v, it.Value(), 0, cands);
  }
  c.faces.Clear();  // names now come from the remapped candidates only
  commit(c, std::move(nb), std::move(cands), "Fix");
}

} // namespace

// ================================================================ public API

const BodyInfo* ModelResult::body(const std::string& name) const {
  for (const auto& b : bodies)
    if (b.name == name) return &b;
  return nullptr;
}

std::string ModelResult::bodyOfFace(const std::string& faceName) const {
  for (const auto& b : bodies)
    if (std::binary_search(b.faces.begin(), b.faces.end(), faceName)) return b.name;
  return {};
}

std::string ModelResult::nameOf(const TopoDS_Face& f) const {
  for (auto& [n, x] : faces)
    if (x.IsSame(f)) return n;
  return {};
}

std::string ModelResult::nameOf(const TopoDS_Edge& e) const {
  for (auto& [n, x] : edges)
    if (x.IsSame(e)) return n;
  return {};
}

ModelResult regenerate(const Document& doc, const ParamValues& values, int stopAfter) {
  ModelResult res;
  res.params = values;
  Ctx c{doc, res.params, res};
  bool allOk = true;
  resetSimdUpperState();   // see pc/SimdState.h: GUI code may leave dirty AVX state that slows OCCT 2.5-3x
  const bool profile = std::getenv("PC_MODEL_PROFILE") != nullptr;   // per-feature timing on stderr
  const auto tAll = std::chrono::steady_clock::now();
  for (size_t i = 0; i < doc.features.size(); ++i) {
    if (stopAfter >= 0 && (int)i > stopAfter) break;
    const Feature& f = doc.features[i];
    c.featIndex = (int)i;
    FeatureStatus st;
    st.featureId = f.id;
    if (f.suppressed) {
      st.message = "bastırıldı (atlandı)";
      res.status.push_back(st);
      continue;
    }
    FeatureInfo info;
    info.featureId = f.id;
    info.type = f.type;
    std::string warn;
    const auto tFeat = std::chrono::steady_clock::now();
    try {
      switch (f.type) {
        case FeatureType::Sketch: warn = doSketch(c, f, info); break;
        case FeatureType::Extrude: warn = doExtrude(c, f, info); break;
        case FeatureType::Revolve: warn = doRevolve(c, f, info); break;
        case FeatureType::Hole: warn = doHole(c, f, info); break;
        case FeatureType::Fillet: warn = doBlend(c, f, info, true); break;
        case FeatureType::Chamfer: warn = doBlend(c, f, info, false); break;
        case FeatureType::Shell: warn = doShell(c, f, info); break;
        case FeatureType::Pattern: warn = doPattern(c, f, info); break;
        case FeatureType::Plane: warn = doPlane(c, f, info); break;
        case FeatureType::Combine: warn = doCombine(c, f, info); break;
        case FeatureType::Transform: warn = doTransform(c, f, info); break;
      }
      st.ok = true;
      if (!warn.empty()) st.message = f.id + ": " + warn;
    } catch (const FeatureError& e) {
      st.ok = false;
      st.message = f.id + ": " + e.what();
    } catch (const Standard_Failure& e) {
      st.ok = false;
      st.message = f.id + ": geometri hatası (" + std::string(e.GetMessageString() ? e.GetMessageString() : "OCCT") + ")";
    } catch (const std::exception& e) {
      st.ok = false;
      st.message = f.id + ": hata (" + std::string(e.what()) + ")";
    } catch (...) {
      st.ok = false;
      st.message = f.id + ": bilinmeyen hata";
    }
    allOk &= st.ok;
    if (profile)
      std::cerr << "[pc_profile] " << f.id << " "
                << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tFeat).count() << " ms\n";
    if (st.ok && c.hasBody() && f.type != FeatureType::Sketch && f.type != FeatureType::Plane) {
      const std::string pre = f.id + ".";
      for (naming::NameMap::Iterator it(c.faces); it.More(); it.Next())
        if (it.Value().compare(0, pre.size(), pre) == 0) info.createdFaces.push_back(it.Value());
      std::sort(info.createdFaces.begin(), info.createdFaces.end());
    }
    res.status.push_back(st);
    if (st.ok || f.type == FeatureType::Sketch) res.info.push_back(std::move(info));
  }
  try {
    fixIfInvalid(c);
    if (c.hasBody()) {
      NamedShape all = namedAll(c);
      res.shape = all.shape;
      res.faces = all.byName();
      res.edges = naming::nameEdges(all.shape, c.faces);
      for (const auto& b : c.bodies) {
        BodyInfo bi;
        bi.name = b.name;
        bi.createdBy = b.createdBy;
        bi.shape = b.solid;
        std::set<std::string> fs;
        for (TopExp_Explorer ex(b.solid, TopAbs_FACE); ex.More(); ex.Next())
          if (const std::string* n = c.faces.Seek(ex.Current())) fs.insert(*n);
        bi.faces.assign(fs.begin(), fs.end());
        res.bodies.push_back(std::move(bi));
      }
      for (auto& [n, f] : res.faces) {
        auto it = c.lineage.find(n);
        std::vector<std::string> v;
        if (it != c.lineage.end()) v.assign(it->second.begin(), it->second.end());
        if (!std::binary_search(v.begin(), v.end(), n)) v.insert(std::lower_bound(v.begin(), v.end(), n), n);
        res.faceLineage[n] = std::move(v);
      }
    }
    res.removedBy = c.removedBy;
    double size = 50;
    if (!res.shape.IsNull()) {
      Bnd_Box b;
      BRepBndLib::Add(res.shape, b);
      if (!b.IsVoid()) {
        double x0, y0, z0, x1, y1, z1;
        b.Get(x0, y0, z0, x1, y1, z1);
        size = std::max(10.0, 0.6 * std::max({x1 - x0, y1 - y0, z1 - z0}));
      }
    }
    for (auto& [id, fr] : c.datums) res.datumPlanes[id] = DatumPlane{id, fr, size};
  } catch (...) {
    allOk = false;
    res.status.push_back({"Model", false, "Model: sonuç şekli işlenemedi"});
  }
  res.ok = allOk;
  if (profile)
    std::cerr << "[pc_profile] TOTAL "
              << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tAll).count() << " ms\n";
  if (!c.log.empty() && std::getenv("PC_MODEL_LOG"))
    for (auto& l : c.log) std::cerr << "[pc_model] " << l << "\n";
  return res;
}

ModelResult regenerate(const Document& doc, const std::map<std::string, std::string>& overrides) {
  ParamValues v;
  try {
    v = doc.evaluateParams(overrides);
  } catch (const std::exception& e) {
    ModelResult r;
    r.ok = false;
    r.status.push_back({"Parametreler", false, std::string("Parametre hatası: ") + e.what()});
    return r;
  }
  return regenerate(doc, v, -1);
}

bool sketchFrame(const Document& doc, const std::string& sketchFeatureId, const ParamValues& values,
                 const ModelResult& partial, gp_Ax3& out, std::string* err) {
  const Feature* f = doc.feature(sketchFeatureId);
  auto fail = [&](const std::string& m) {
    if (err) *err = sketchFeatureId + ": " + m;
    return false;
  };
  if (!f || f->type != FeatureType::Sketch || !f->sketch) return fail("çizim özelliği bulunamadı");
  double off = 0;
  try {
    off = evalWith(f->sketch->plane.offsetExpr.empty() ? "0" : f->sketch->plane.offsetExpr, values);
  } catch (const std::exception& e) {
    return fail(std::string("ofset ifadesi hatalı: ") + e.what());
  }
  if (f->sketch->plane.kind == SketchPlaneKind::OnPlane) {
    const std::string& pr = f->sketch->plane.planeRef;
    if (pr.empty()) return fail("düzlem referansı eksik (planeRef)");
    int pi = doc.indexOf(pr);
    if (pi < 0) return fail("düzlem bulunamadı '" + pr + "'");
    if (doc.features[pi].type != FeatureType::Plane) return fail("'" + pr + "' bir düzlem özelliği değil");
    if (pi >= doc.indexOf(sketchFeatureId)) return fail("düzlem '" + pr + "' çizimden sonra tanımlı");
    auto it = partial.datumPlanes.find(pr);
    if (it == partial.datumPlanes.end()) return fail("düzlem '" + pr + "' kullanılamıyor (bastırılmış veya hatalı)");
    out = it->second.frame;
    out.Translate(gp_Vec(out.Direction()) * off);
    return true;
  }
  if (f->sketch->plane.kind != SketchPlaneKind::OnFace) {
    out = sketch::standardFrame(f->sketch->plane.kind, off);
    return true;
  }
  auto it = partial.faces.find(f->sketch->plane.faceRef);
  if (it == partial.faces.end()) return fail("yüz bulunamadı '" + f->sketch->plane.faceRef + "'");
  try {
    if (!frameFromFace(it->second, off, out)) return fail("yüz düzlemsel değil '" + f->sketch->plane.faceRef + "'");
  } catch (...) {
    return fail("yüz çerçevesi hesaplanamadı");
  }
  return true;
}

} // namespace pc::model
