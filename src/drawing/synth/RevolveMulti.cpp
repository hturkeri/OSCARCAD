// R2 — S7 multi-body revolve of an axisymmetric section (see RevolveMulti.h).
#include "synth/RevolveMulti.h"
#include "interp/InterpUtil.h"
#include "interp/SectionAxisym.h"
#include "synth/CutFeatures.h"   // R3 cuts on the R2 bodies (integration)
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
#include <cstdlib>
#include <fstream>

namespace pc::drawing::r2 {
using namespace interp;

namespace {

using Poly = std::vector<Vec2>;

// visible HLR edges of the half of `shape` behind the XZ plane, projected on (X, Z)
std::vector<Poly> sectionProjection(const TopoDS_Shape& shape0, double step) {
  std::vector<Poly> out;
  Bnd_Box bb;
  BRepBndLib::Add(shape0, bb);
  double x0, y0, z0, x1, y1, z1;
  bb.Get(x0, y0, z0, x1, y1, z1);
  TopoDS_Shape shape = shape0;
  BRepAlgoAPI_Cut cut(shape0, BRepPrimAPI_MakeBox(gp_Pnt(x0 - 1, y0 - 1, z0 - 1), gp_Pnt(x1 + 1, 0, z1 + 1)).Shape());
  if (cut.IsDone()) shape = cut.Shape();
  gp_Dir ex(1, 0, 0), ey(0, 0, 1);
  Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
  algo->Add(shape);
  algo->Projector(HLRAlgo_Projector(gp_Ax2(gp_Pnt(0, 0, 0), ex.Crossed(ey), ex)));
  algo->Update();
  algo->Hide();
  HLRBRep_HLRToShape h(algo);
  for (TopoDS_Shape comp : {h.VCompound(), h.OutLineVCompound()}) {
    if (comp.IsNull()) continue;
    for (TopExp_Explorer e(comp, TopAbs_EDGE); e.More(); e.Next()) {
      BRepAdaptor_Curve c(TopoDS::Edge(e.Current()));
      double a = c.FirstParameter(), b = c.LastParameter();
      int ns = c.GetType() == GeomAbs_Line ? 1 : 32;
      Poly pl;
      for (int i = 0; i <= ns; ++i) { gp_Pnt q = c.Value(a + (b - a) * i / ns); pl.push_back({q.X(), q.Y()}); }
      // densify for the distance test
      Poly d;
      for (size_t i = 0; i + 1 < pl.size(); ++i) {
        int k = std::max(1, (int)std::ceil(distance(pl[i], pl[i + 1]) / step));
        for (int j = 0; j < k; ++j) d.push_back(pl[i] + (pl[i + 1] - pl[i]) * (double(j) / k));
      }
      d.push_back(pl.back());
      out.push_back(d);
    }
  }
  return out;
}

double nearest(const std::vector<Poly>& P, Vec2 q) {
  double b = 1e300;
  for (auto& pl : P) for (size_t i = 1; i < pl.size(); ++i) b = std::min(b, distPointSeg(q, pl[i - 1], pl[i]));
  return b;
}

std::string sanitize(const std::string& s) {
  std::string o;
  for (char c : s) o += std::isalnum((unsigned char)c) || c == '_' ? c : '_';
  return o.empty() ? "Part" : o;
}

}  // namespace

int addRevolveMultiCandidate(DrawingIR& ir, const InterpretOptions& opt, StageResult& res) {
  std::vector<const SketchCandidate*> parts;
  for (auto& c : ir.sketches) if (c.purpose == kPurpose) parts.push_back(&c);
  if (parts.empty()) return -1;
  std::sort(parts.begin(), parts.end(), [](auto* a, auto* b) { return attrInt(a->attrs, "r2.part", 0) < attrInt(b->attrs, "r2.part", 0); });
  SynthesisCandidate s;
  s.id = ir.newId();
  s.strategy = Strategy::SectionRevolve;
  Document& doc = s.fragment;
  doc.name = "TeknikResim";
  std::set<std::string> bodyNames;
  int k = 0;
  for (auto* c : parts) {
    ++k;
    for (auto& p : c->params) if (!doc.params.find(p.param.name)) doc.params.add(p.param);
    Feature sk;
    sk.id = "Sketch" + std::to_string(k);
    sk.type = FeatureType::Sketch;
    sk.sketch = c->sketch;
    doc.features.push_back(sk);
    std::string bn = sanitize(c->attrs.count("r2.partName") ? c->attrs.at("r2.partName") : "Part" + std::to_string(k));
    while (bodyNames.count(bn)) bn += "_" + std::to_string(k);
    bodyNames.insert(bn);
    Feature rv;
    rv.id = "Revolve" + std::to_string(k);
    rv.type = FeatureType::Revolve;
    rv.props = {{"sketch", sk.id}, {"axis", "V"}, {"angle", "360"}, {"op", "new"}, {"bodyName", bn}};
    doc.features.push_back(rv);
    s.sketchIds.push_back(c->id);
  }
  model::ModelResult mr;
  try {
    mr = model::regenerate(doc);
  } catch (const std::exception& e) {
    s.warnings.push_back(std::string("yeniden üretim hatası: ") + e.what());
  }
  s.regenerated = mr.ok && !mr.shape.IsNull();
  for (auto& fs : mr.status) if (!fs.ok) s.warnings.push_back(fs.featureId + ": " + fs.message);
  s.attrs["r2.bodies"] = std::to_string(mr.bodies.size());
  double volErrMax = 0;
  if (s.regenerated) {
    s.volume = model::massProperties(mr.shape).volume;
    for (size_t i = 0; i < mr.bodies.size() && i < parts.size(); ++i) {
      double v = model::massProperties(mr.bodies[i].shape).volume;
      double pv = attrNum(parts[i]->attrs, "r2.pappusVolume", 0);
      s.attrs["r2.body." + std::to_string(i) + ".name"] = mr.bodies[i].name;
      s.attrs["r2.body." + std::to_string(i) + ".volume"] = fmt(v, 6);
      s.attrs["r2.body." + std::to_string(i) + ".pappusVolume"] = fmt(pv, 6);
      if (pv > 0) volErrMax = std::max(volErrMax, std::abs(v - pv) / pv);
    }
    s.attrs["r2.volumeRelErrMax"] = fmt(volErrMax, 5);
    if (mr.bodies.size() != parts.size())
      s.warnings.push_back("Gövde sayısı (" + std::to_string(mr.bodies.size()) + ") parça sayısından (" + std::to_string(parts.size()) + ") farklı");
    if (volErrMax > 0.02) s.warnings.push_back("Hacim, çizimdeki yarım profilin Pappus hacminden %" + fmt(100 * volErrMax, 1) + " sapıyor");
  }
  // ---- R3 (integration): slots / grooves of the transverse views are cut into THESE bodies. The Pappus check above
  // and the HLR re-projection below verify the revolve profile, so they use the uncut revolve (mr); volume / bbox of
  // the candidate are those of the final (cut) model.
  TopoDS_Shape verifyShape = mr.shape;   // re-projection: the final (cut) model when the cuts regenerate
  if (s.regenerated) {
    const size_t nf0 = doc.features.size();
    if (synth::applyTransverseCuts(ir, doc, s.warnings, s.attrs) > 0 && doc.features.size() > nf0) {
      model::ModelResult mc;
      try {
        mc = model::regenerate(doc);
      } catch (const std::exception& e) {
        s.warnings.push_back(std::string("R3 kesimleriyle yeniden üretim hatası: ") + e.what());
      }
      bool cutsOk = mc.ok && !mc.shape.IsNull() && mc.bodies.size() == mr.bodies.size();
      for (auto& fs : mc.status) if (!fs.ok) { s.warnings.push_back(fs.featureId + ": " + fs.message); cutsOk = false; }
      if (cutsOk) {
        auto mp = model::massProperties(mc.shape);
        s.volume = mp.volume;
        verifyShape = mc.shape;
        s.attrs["synth.bbox"] = fmt(mp.xmax - mp.xmin, 4) + "," + fmt(mp.ymax - mp.ymin, 4) + "," + fmt(mp.zmax - mp.zmin, 4);
        for (size_t i = 0; i < mc.bodies.size(); ++i)
          s.attrs["r2.body." + std::to_string(i) + ".volumeCut"] = fmt(model::massProperties(mc.bodies[i].shape).volume, 6);
      } else {
        s.warnings.push_back("R3 kesimleri yeniden üretilemedi; kesimsiz döndürme kullanılıyor");
        doc.features.resize(nf0);
        s.attrs["r3.cuts.ok"] = "0";
      }
    }
  }
  // ---- HLR re-projection vs the drawing's visible section edges (part mm, signed radius)
  AxisymSection S = analyzeAxisymSection(ir);
  if (s.regenerated && opt.verifyByReprojection && S.found) {
    try {
      Bnd_Box bb;
      BRepBndLib::Add(verifyShape, bb);
      double x0, y0, z0, x1, y1, z1;
      bb.Get(x0, y0, z0, x1, y1, z1);
      double step = std::max({x1 - x0, z1 - z0, 1.0}) / 200;
      // drawing -> model against the final (cut) model: the section lines of slot walls crossing the cutting plane
      // must be explained; model -> drawing against the uncut revolve: slot edges BEHIND the cutting plane are not
      // drawn in a section, the revolve's own edges must all be on the sheet.
      std::vector<Poly> P = sectionProjection(verifyShape, step);
      std::vector<Poly> Pu = verifyShape.IsSame(mr.shape) ? P : sectionProjection(mr.shape, step);
      std::vector<Poly> D;
      for (int id : S.profilePrimIds) {
        const Primitive* p = findPrim(ir.norm, id);
        if (!p) continue;
        Poly pl;
        for (auto& q : samplePrim(*p, step / S.scale)) pl.push_back(S.toPartSigned(q));
        if (!pl.empty()) D.push_back(pl);
      }
      if (const char* dp = std::getenv("R2_DUMP_REPROJ")) {   // developer overlay: "M x,y;x,y" / "D x,y;..." lines
        std::ofstream f(dp);
        for (auto& pl : Pu) { f << "M"; for (auto& q : pl) f << " " << q.x << "," << q.y; f << "\n"; }
        for (auto& pl : D) { f << "D"; for (auto& q : pl) f << " " << q.x << "," << q.y; f << "\n"; }
      }
      double s1 = 0, s2 = 0;
      int n1 = 0, n2 = 0, mis = 0;
      for (auto& pl : D) for (auto& q : pl) { double d = nearest(P, q); s1 += d; ++n1; if (d > 0.05 * (x1 - x0)) ++mis; }
      // model -> drawing: background edges of the section often coincide with thin extension lines; compare the
      // model edges against every solid (non-hatch, non-dashed) line of the section box
      std::vector<Poly> D2 = D;
      std::set<int> skip(S.hatchLineIds.begin(), S.hatchLineIds.end());
      skip.insert(S.profilePrimIds.begin(), S.profilePrimIds.end());
      for (auto& p : ir.norm.prims) {
        if (p.kind != PrimKind::Line || skip.count(p.id) || !p.style.dash.empty() || !p.style.stroked) continue;
        Box2 b = primBox(p);
        if (b.hi.x < S.box.lo.x - 1 || b.lo.x > S.box.hi.x + 1 || b.hi.y < S.box.lo.y - 1 || b.lo.y > S.box.hi.y + 1) continue;
        Poly pl;
        for (auto& q : samplePrim(p, step / S.scale)) pl.push_back(S.toPartSigned(q));
        if (pl.size() >= 2) D2.push_back(pl);
      }
      for (auto& pl : Pu) for (auto& q : pl) { s2 += nearest(D2, q); ++n2; }
      if (n1 && n2) {
        s.reprojectionError = 0.5 * (s1 / n1 + s2 / n2);
        s.attrs["r2.reproj.drawingToModel"] = fmt(s1 / n1, 4);
        s.attrs["r2.reproj.modelToDrawing"] = fmt(s2 / n2, 4);
        s.attrs["r2.reproj.farSamples"] = fmt(double(mis) / n1, 4);
      }
    } catch (const Standard_Failure&) {
      s.warnings.push_back("HLR yeniden izdüşümü başarısız");
    }
  }
  // ---- score
  double dof0 = 0, dofEnd = 0;
  for (auto* c : parts) { dof0 += attrInt(c->attrs, "interp.dof0", 0); dofEnd += std::max(0, c->dof); }
  int dimsBound = attrInt(ir.attrs, "r2.section.dimsBound", 0), dimsUnbound = attrInt(ir.attrs, "r2.section.dimsUnbound", 0);
  s.conditionScore = attrNum(ir.attrs, "r2.section.mirrorIoU", 0.5);
  s.dofCoverage = dimsBound + dimsUnbound > 0 ? double(dimsBound) / (dimsBound + dimsUnbound) : 0;
  s.unresolved = dimsUnbound;
  double rp = s.reprojectionError < 0 ? 0.5 : std::exp(-s.reprojectionError / 0.25);
  s.score = float(std::clamp(0.3 + 0.25 * s.conditionScore + 0.2 * s.dofCoverage + 0.25 * rp, 0.0, 1.0));
  if (!s.regenerated || mr.bodies.size() != parts.size()) s.score *= 0.3f;
  std::ostringstream ex;
  ex << "Kesit (eksenel simetrik): " << parts.size() << " parça, tarama ailesine göre ayrıldı -> her biri ayrı gövde (döndürme 360°)"
     << "; ölçek " << ir.attrs["r2.section.scaleEvidence"] << "; " << dimsBound << " ölçü bağlandı";
  if (dimsUnbound) ex << ", " << dimsUnbound << " ölçü bağlanamadı";
  if (s.reprojectionError >= 0) ex << ", yeniden izdüşüm hatası " << fmt(s.reprojectionError, 3) << " mm";
  if (s.attrs.count("r3.cuts") && s.attrs["r3.cuts.ok"] == "1") ex << " + kanal/oluk kesimleri (R3)";
  if (s.volume > 0) ex << ", hacim " << fmt(s.volume, 1) << " mm³";
  s.explanation = ex.str();
  // Multi-part section: demote single-profile revolve candidates of the same view (they merge all parts into one
  // body). Single part: the generic S6/S7 revolve stays first if it already re-projects well (< 0.2 mm).
  if (s.regenerated && mr.bodies.size() == parts.size()) {
    int vid = parts.front()->viewId;
    auto sameView = [&](const SynthesisCandidate& o) {
      if (o.strategy != Strategy::Revolve && o.strategy != Strategy::SectionRevolve) return false;
      bool same = vid < 0;
      for (int sid : o.sketchIds) for (auto& c : ir.sketches) if (c.id == sid && c.viewId == vid) same = true;
      return same;
    };
    const SynthesisCandidate* good = nullptr;
    for (auto& o : ir.synthesis)
      if (sameView(o) && o.regenerated && o.reprojectionError >= 0 && o.reprojectionError < 0.2 && (!good || o.score > good->score)) good = &o;
    if (parts.size() == 1 && good) {
      s.score = std::min(s.score, good->score - 0.01f);
      s.warnings.push_back("Genel döndürme adayı aynı profili zaten iyi açıklıyor; R2 adayı ikinci sırada");
    } else {
      for (auto& o : ir.synthesis)
        if (sameView(o)) { o.score *= 0.5f; o.warnings.push_back("Çok parçalı kesit adayı (R2) bu görünüşü daha iyi açıklıyor"); }
    }
  }
  int id = s.id;
  res.diagnostics.push_back({Severity::Info, "r2.synth", s.explanation, (int)Stage::Synthesize, {}});
  ir.synthesis.push_back(std::move(s));
  return id;
}

}  // namespace pc::drawing::r2
