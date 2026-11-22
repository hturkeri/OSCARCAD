// S7 — 3D synthesis: ranked strategies (revolve / section revolve, two-view extrude (+ holes, circular patterns),
// single-view extrude, sketch-only fallback), regeneration with pc::model and verification by HLR re-projection
// of the solid against the drawing's visible lines.
#include "interp/InterpUtil.h"
#include "synth/RevolveMulti.h"   // R2 hook
#include "synth/CutFeatures.h"   // R3 hook
#include "pc/Model.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace pc::drawing {
using namespace interp;

namespace {

gp_Dir axisDir(char c) { return c == 'X' ? gp_Dir(1, 0, 0) : c == 'Y' ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1); }

using Polyline = std::vector<Vec2>;

std::vector<Polyline> projectEdges(const TopoDS_Shape& shape, const gp_Dir& ex, const gp_Dir& ey, double step) {
  std::vector<Polyline> out;
  gp_Dir n = ex.Crossed(ey);
  Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
  algo->Add(shape);
  HLRAlgo_Projector P(gp_Ax2(gp_Pnt(0, 0, 0), n, ex));
  algo->Projector(P);
  algo->Update();
  algo->Hide();
  HLRBRep_HLRToShape h(algo);
  for (TopoDS_Shape comp : {h.VCompound(), h.OutLineVCompound()}) {
    if (comp.IsNull()) continue;
    for (TopExp_Explorer e(comp, TopAbs_EDGE); e.More(); e.Next()) {
      BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
      double a = c.FirstParameter(), b = c.LastParameter();
      gp_Pnt p0 = c.Value(a), p1 = c.Value(b);
      double approxLen = p0.Distance(p1) + 1e-9;
      if (c.GetType() != GeomAbs_Line) {
        approxLen = 0;
        gp_Pnt prev = p0;
        for (int i = 1; i <= 16; ++i) { gp_Pnt q = c.Value(a + (b - a) * i / 16); approxLen += prev.Distance(q); prev = q; }
      }
      int ns = c.GetType() == GeomAbs_Line ? 1 : std::clamp((int)std::ceil(approxLen / step), 4, 400);
      Polyline pl;
      for (int i = 0; i <= ns; ++i) { gp_Pnt q = c.Value(a + (b - a) * i / ns); pl.push_back({q.X(), q.Y()}); }
      out.push_back(pl);
    }
  }
  return out;
}

double nearest(const std::vector<Polyline>& pls, Vec2 q) {
  double best = 1e300;
  for (auto& pl : pls)
    for (size_t i = 1; i < pl.size(); ++i) best = std::min(best, distPointSeg(q, pl[i - 1], pl[i]));
  return best;
}

struct Reproj { double err = -1; std::vector<int> mismatch; std::vector<Polyline> projected; };

Reproj reproject(const TopoDS_Shape& shape0, const DrawingIR& ir, const ViewRegion& v, const std::string& map) {
  Reproj R;
  if (map.size() < 3) return R;
  gp_Dir ex = axisDir(map[0]), ey = axisDir(map[2]);
  gp_Dir n = ex.Crossed(ey);
  TopoDS_Shape shape = shape0;
  try {
    if (v.role == ViewRole::Section) {   // remove the half towards the viewer (cutting plane through the origin)
      Bnd_Box bb;
      BRepBndLib::Add(shape, bb);
      double x0, y0, z0, x1, y1, z1;
      bb.Get(x0, y0, z0, x1, y1, z1);
      double lo[3] = {x0 - 1, y0 - 1, z0 - 1}, hi[3] = {x1 + 1, y1 + 1, z1 + 1};
      double nc[3] = {n.X(), n.Y(), n.Z()};
      for (int k = 0; k < 3; ++k) {
        if (nc[k] > 0.5) lo[k] = 0;
        if (nc[k] < -0.5) hi[k] = 0;
      }
      TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(lo[0], lo[1], lo[2]), gp_Pnt(hi[0], hi[1], hi[2])).Shape();
      BRepAlgoAPI_Cut cut(shape, box);
      if (cut.IsDone()) shape = cut.Shape();
    }
    Bnd_Box bb;
    BRepBndLib::Add(shape, bb);
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    double size = std::max({x1 - x0, y1 - y0, z1 - z0, 1.0});
    double step = size / 150;
    // both viewing directions (the drawing may show the view from either side; 2D flips are tried below)
    std::vector<std::vector<Polyline>> projs;
    for (int sgn : {1, -1}) {
      gp_Dir exs = sgn > 0 ? ex : ex.Reversed();
      TopoDS_Shape sh = shape;
      if (sgn < 0 && v.role == ViewRole::Section) continue;   // section: cut side fixes the direction
      projs.push_back(projectEdges(sh, exs, ey, step));
    }
    // drawing samples (view coords)
    std::vector<std::pair<int, Vec2>> dr;
    double f = viewScaleFactor(v);
    for (auto* p : viewPrims(ir, v, {LineRole::Visible}))
      for (auto& q : samplePrim(*p, step / std::max(f, 1e-9))) dr.push_back({p->id, transformPoint(v.sheetToView, q)});
    if (dr.empty()) return R;
    Box2 db = emptyBox();
    for (auto& [id, q] : dr) expand(db, q);
    double best = 1e300;
    for (size_t pi = 0; pi < projs.size(); ++pi)
    for (int variant = 0; variant < 8; ++variant) {
      if (projs[pi].empty()) continue;
      bool fx = variant & 1, fy = variant & 2, align = variant & 4;
      std::vector<Polyline> P = projs[pi];
      Box2 pb = emptyBox();
      for (auto& pl : P) for (auto& q : pl) { if (fx) q.x = -q.x; if (fy) q.y = -q.y; expand(pb, q); }
      Vec2 off = align ? db.lo - pb.lo : Vec2{0, 0};
      for (auto& pl : P) for (auto& q : pl) q = q + off;
      double s1 = 0;
      std::map<int, std::pair<double, int>> perPrim;
      for (auto& [id, q] : dr) { double d = nearest(P, q); s1 += d; perPrim[id].first += d; perPrim[id].second++; }
      s1 /= dr.size();
      if (s1 > best) continue;   // cheap reject before the reverse direction
      double s2 = 0;
      int n2 = 0;
      std::vector<Polyline> D;
      for (auto* p : viewPrims(ir, v, {LineRole::Visible})) {
        Polyline pl;
        for (auto& q : samplePrim(*p, step / std::max(f, 1e-9))) pl.push_back(transformPoint(v.sheetToView, q));
        D.push_back(pl);
      }
      for (auto& pl : P) for (auto& q : pl) { s2 += nearest(D, q); ++n2; }
      s2 = n2 ? s2 / n2 : 0;
      double e = 0.5 * (s1 + s2);
      if (e < best) {
        best = e;
        R.err = e;
        R.projected = P;
        R.mismatch.clear();
        double tolMis = std::max(0.2, 0.01 * size);
        for (auto& [id, ss] : perPrim) if (ss.first / ss.second > tolMis) R.mismatch.push_back(id);
      }
    }
  } catch (const Standard_Failure& e) {
    R.err = -1;
  }
  return R;
}

std::string encodePreview(const std::vector<Polyline>& pls) {
  std::ostringstream os;
  size_t budget = 3000;
  for (auto& pl : pls) {
    if (budget < pl.size()) break;
    budget -= pl.size();
    for (size_t i = 0; i < pl.size(); ++i) os << (i ? ";" : "") << fmt(pl[i].x, 3) << "," << fmt(pl[i].y, 3);
    os << "|";
  }
  return os.str();
}

Feature sketchFeature(const std::string& id, const Sketch& sk) {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  f.sketch = sk;
  return f;
}

void addParams(Document& doc, const SketchCandidate& c) {
  for (auto& p : c.params)
    if (!doc.params.find(p.param.name)) doc.params.add(p.param);
}

}  // namespace

StageResult synthesize(DrawingIR& ir, const InterpretOptions& opt, ProgressSink* prog) {
  StageResult res = StageResult::success();
  const Stage st = Stage::Synthesize;
  ir.synthesis.clear();
  if (ir.sketches.empty()) {
    res.warn("synth.noSketch", "Sketch adayı yok; 3B sentezi yapılamadı", {}, (int)st);
    res.message = "sketch yok";
    return res;
  }
  std::map<int, std::vector<const SketchCandidate*>> holesOf;
  for (auto& c : ir.sketches)
    if (c.purpose == "hole-pattern") holesOf[attrInt(c.attrs, "interp.forSketch", -1)].push_back(&c);
  int idx = 0;
  for (auto& c : ir.sketches) {
    if (c.purpose != "revolve-half-profile" && c.purpose != "extrude-profile") continue;
    if (isCancelled(prog)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    report(prog, 0.1 + 0.7 * idx++ / std::max<size_t>(1, ir.sketches.size()), "3B aday üretiliyor");
    SynthesisCandidate s;
    s.id = ir.newId();
    std::string strat = c.attrs.count("interp.strategy") ? c.attrs.at("interp.strategy") : "SketchOnly";
    fromString(strat, s.strategy);
    s.sketchIds.push_back(c.id);
    Document& doc = s.fragment;
    doc.name = "TeknikResim";
    addParams(doc, c);
    const bool revolve = c.purpose == "revolve-half-profile";
    std::string body = revolve ? "Revolve1" : "Extrude1";
    doc.features.push_back(sketchFeature("Sketch1", c.sketch));
    Feature main;
    main.id = body;
    if (revolve) {
      main.type = FeatureType::Revolve;
      main.props = {{"sketch", "Sketch1"}, {"axis", "V"}, {"angle", "360"}, {"op", "new"}};
    } else {
      main.type = FeatureType::Extrude;
      main.props = {{"sketch", "Sketch1"}, {"distance", c.attrs.count("interp.depthParam") ? c.attrs.at("interp.depthParam") : "10"},
                    {"direction", "normal"}, {"op", "new"}};
    }
    doc.features.push_back(main);
    int k = 1;
    for (auto* h : holesOf[c.id]) {
      s.sketchIds.push_back(h->id);
      addParams(doc, *h);
      std::string skId = "Sketch" + std::to_string(++k), hid = "Hole" + std::to_string(k - 1);
      doc.features.push_back(sketchFeature(skId, h->sketch));
      Feature hf;
      hf.id = hid;
      hf.type = FeatureType::Hole;
      hf.props = {{"sketch", skId}, {"diameter", h->attrs.at("hole.diameterParam")}, {"targets", body}};
      if (h->attrs.count("hole.through") && h->attrs.at("hole.through") == "1") hf.props["through"] = "1";
      else hf.props["depth"] = h->attrs.count("hole.depthExpr") ? h->attrs.at("hole.depthExpr") : "1";
      doc.features.push_back(hf);
      if (h->attrs.count("pattern.kind") && h->attrs.at("pattern.kind") == "circular") {
        Feature pf;
        pf.id = "Pattern" + std::to_string(k - 1);
        pf.type = FeatureType::Pattern;
        pf.props = {{"kind", "circular"}, {"source", hid}, {"count", h->attrs.at("pattern.countParam")},
                    {"axis", h->attrs.count("pattern.axis") ? h->attrs.at("pattern.axis") : "Z"}, {"angle", "360"}, {"targets", body}};
        doc.features.push_back(pf);
      }
    }
    // R3 hook: non-axisymmetric slots/grooves from transverse views (synth/CutFeatures.h)
    if (revolve) synth::applyTransverseCuts(ir, doc, s.warnings, s.attrs);
    // ---- regenerate
    model::ModelResult mr;
    try {
      mr = model::regenerate(doc);
    } catch (const std::exception& e) {
      s.warnings.push_back(std::string("yeniden üretim hatası: ") + e.what());
    }
    s.regenerated = mr.ok && !mr.shape.IsNull();
    for (auto& fs : mr.status) if (!fs.ok) s.warnings.push_back(fs.featureId + ": " + fs.message);
    if (s.regenerated) {
      auto mp = model::massProperties(mr.shape);
      s.volume = mp.volume;
      s.attrs["synth.bbox"] = fmt(mp.xmax - mp.xmin, 4) + "," + fmt(mp.ymax - mp.ymin, 4) + "," + fmt(mp.zmax - mp.zmin, 4);
    }
    // ---- verification by re-projection
    double errSum = 0;
    int errN = 0;
    std::set<int> views;
    for (int sid : s.sketchIds)
      if (auto* sc = [&]() -> const SketchCandidate* { for (auto& x : ir.sketches) if (x.id == sid) return &x; return nullptr; }())
        for (auto& [k2, val] : sc->attrs)
          if (k2.rfind("interp.viewMap.", 0) == 0) views.insert(std::stoi(k2.substr(15)));
    if (s.regenerated && opt.verifyByReprojection) {
      for (int vid : views) {
        const ViewRegion* v = findView(ir.layout, vid);
        if (!v) continue;
        std::string map = c.attrs.count("interp.viewMap." + std::to_string(vid)) ? c.attrs.at("interp.viewMap." + std::to_string(vid)) : "";
        if (map.empty()) continue;
        Reproj R = reproject(mr.shape, ir, *v, map);
        if (R.err < 0) continue;
        errSum += R.err;
        ++errN;
        s.attrs["synth.reproj." + std::to_string(vid)] = fmt(R.err, 4);
        s.attrs["synth.preview." + std::to_string(vid)] = encodePreview(R.projected);
        if (!R.mismatch.empty()) {
          std::string ids;
          for (int m : R.mismatch) ids += (ids.empty() ? "" : ",") + std::to_string(m);
          s.attrs["synth.mismatch." + std::to_string(vid)] = ids;
          std::string vn = v->label.empty() ? labelTr(v->role) : v->label;
          s.warnings.push_back("Görünüş " + vn + ": " + std::to_string(R.mismatch.size()) + " çizgi modelin izdüşümüyle örtüşmüyor");
          res.warn("synth.reproj.mismatch", "Görünüş " + vn + ": yeniden izdüşümde örtüşmeyen çizgiler", R.mismatch, (int)st);
        }
      }
      if (errN) s.reprojectionError = errSum / errN;
    }
    // ---- score
    int vid = c.viewId;
    if (revolve) s.conditionScore = ir.views.symmetryScore.count(vid) ? ir.views.symmetryScore.at(vid) : 0.5;
    else {
      s.conditionScore = 0.5;
      int adj = attrInt(c.attrs, "interp.adjView", -1);
      for (auto& r : ir.views.relations) if (r.a == vid && r.b == adj) s.conditionScore = std::max<double>(0.5, r.conf);
      if (c.attrs.count("interp.depthAssumed")) s.conditionScore = 0.3;
    }
    int dof0 = attrInt(c.attrs, "interp.dof0", 0), dofD = attrInt(c.attrs, "interp.dofAfterDims", 0);
    s.dofCoverage = dof0 > 0 ? std::clamp(double(dof0 - dofD) / dof0, 0.0, 1.0) : 1.0;
    std::set<int> used;
    for (int sid : s.sketchIds)
      for (auto& x : ir.sketches)
        if (x.id == sid) {
          for (auto& [cc, d] : x.constraintToDimension) used.insert(d);
          for (auto& p : x.params) if (p.dimensionId >= 0) used.insert(p.dimensionId);
        }
    for (auto& d : ir.annot.dims)
      if (views.count(d.viewId) && d.driving && d.text.valid && !used.count(d.id)) ++s.unresolved;
    double rp = s.reprojectionError < 0 ? 0.5 : std::exp(-s.reprojectionError / 0.5);
    s.score = float(std::clamp(0.35 * s.conditionScore + 0.25 * s.dofCoverage + 0.3 * rp + (s.regenerated ? 0.1 : 0.0) -
                               0.03 * s.unresolved, 0.0, 1.0));
    if (!s.regenerated) s.score *= 0.3f;
    std::ostringstream ex;
    const ViewRegion* v = findView(ir.layout, vid);
    std::string vn = v ? (v->label.empty() ? labelTr(v->role) : v->label) : "?";
    if (revolve)
      ex << "Görünüş " << vn << (s.strategy == Strategy::SectionRevolve ? " (kesit)" : "") << ": eksen + ayna simetrisi %"
         << fmt(100 * s.conditionScore, 0) << " -> yarım profil döndürme";
    else
      ex << "Görünüş " << vn << " profili çekme, derinlik "
         << (c.attrs.count("interp.adjView") ? "komşu görünüşten" : "nottan/varsayım");
    if (!holesOf[c.id].empty()) ex << " + " << holesOf[c.id].size() << " delik grubu";
    ex << "; ölçü kapsamı %" << fmt(100 * s.dofCoverage, 0);
    if (s.reprojectionError >= 0) ex << ", yeniden izdüşüm hatası " << fmt(s.reprojectionError, 3) << " mm";
    if (s.unresolved) ex << ", " << s.unresolved << " ölçü kullanılamadı";
    s.explanation = ex.str();
    ir.synthesis.push_back(std::move(s));
  }
  // ---- (e) sketch only
  {
    SynthesisCandidate s;
    s.id = ir.newId();
    s.strategy = Strategy::SketchOnly;
    s.fragment.name = "TeknikResim";
    int k = 0;
    std::string suggest;
    for (auto& c : ir.sketches) {
      addParams(s.fragment, c);
      s.fragment.features.push_back(sketchFeature("Sketch" + std::to_string(++k), c.sketch));
      s.sketchIds.push_back(c.id);
      if (c.purpose == "revolve-half-profile") suggest = "Döndür (eksen V)";
      else if (c.purpose == "extrude-profile" && suggest.empty()) suggest = "Çek";
    }
    s.regenerated = true;
    s.conditionScore = 1;
    s.score = 0.15f;
    s.explanation = "Yalnız sketch: " + std::to_string(k) + " sketch düzlemlere yerleştirilir" +
                    (suggest.empty() ? std::string() : ", önerilen işlem: " + suggest);
    ir.synthesis.push_back(std::move(s));
  }
  // R2 hook (docs/drawing_import/R2_SECTION_AXISYM.md): part profiles of an axisymmetric section -> one body per part
  r2::addRevolveMultiCandidate(ir, opt, res);
  std::stable_sort(ir.synthesis.begin(), ir.synthesis.end(), [](auto& a, auto& b) { return a.score > b.score; });
  const auto& best = ir.synthesis.front();
  res.message = std::string(labelTr(best.strategy)) + " (puan " + fmt(best.score, 2) + ")";
  report(prog, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
