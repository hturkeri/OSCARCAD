// R3 — slot / groove cut features on revolved bodies (see CutFeatures.h).
#include "synth/CutFeatures.h"
#include "interp/InterpUtil.h"
#include "pc/Model.h"
#include <BRepBndLib.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <Bnd_Box.hxx>
#include <algorithm>
#include <cmath>

namespace pc::drawing::synth {
using namespace interp;

namespace {

struct BodyExt { std::string name; double z0, z1, rmax; TopoDS_Shape shape; };

std::vector<BodyExt> bodyExtents(const model::ModelResult& mr) {
  std::vector<BodyExt> out;
  for (auto& b : mr.bodies) {
    Bnd_Box bb;
    BRepBndLib::Add(b.shape, bb);
    if (bb.IsVoid()) continue;
    double x0, y0, z0, x1, y1, z1;
    bb.Get(x0, y0, z0, x1, y1, z1);
    double gap = bb.GetGap();
    out.push_back({b.name, z0 + gap, z1 - gap, std::max({std::fabs(x0), std::fabs(x1), std::fabs(y0), std::fabs(y1)}) - gap, b.shape});
  }
  return out;
}

bool inside(const TopoDS_Shape& s, double x, double y, double z) {
  BRepClass3d_SolidClassifier c(s, gp_Pnt(x, y, z), 1e-6);
  return c.State() == TopAbs_IN;
}

struct Builder {
  Document& doc;
  CutReport& rep;
  void param(const std::string& name, const std::string& expr, ParamKind k, const std::string& desc) {
    if (Parameter* p = doc.params.find(name)) { p->expression = expr; p->description = desc; return; }
    Parameter p;
    p.name = name;
    p.expression = expr;
    p.kind = k;
    p.description = desc;
    p.group = "Kesit özellikleri (R3)";
    doc.params.add(p);
    rep.params.push_back(name);
  }
  double eval(const std::string& expr) {
    ParamValues v = doc.evaluateParams();
    return evalWith(expr, v);
  }
  // rectangle u in [u0, u0+lu], v in [v0, v0+lv] on datum `plane`, then symmetric extrude-cut + circular pattern
  void slotFeatures(const std::string& T, const std::string& plane, const std::string& u0, const std::string& lu,
                    const std::string& v0, const std::string& lv, const std::string& body) {
    Sketch sk = Sketch::create();
    sk.plane.kind = SketchPlaneKind::OnPlane;
    sk.plane.planeRef = plane;
    double a = eval(u0), b = eval(lu), c = eval(v0), d = eval(lv);
    auto L = sk.addRectangle(a, c, a + b, c + d);
    int bl = sk.entity(L[0])->points[0];
    sk.addConstraint(SkConstraintType::DistanceX, {0, bl}, u0);
    sk.addConstraint(SkConstraintType::DistanceY, {0, bl}, v0);
    sk.addConstraint(SkConstraintType::Length, {L[0]}, lu);
    sk.addConstraint(SkConstraintType::Length, {L[3]}, lv);
    Feature f;
    f.id = "SK_" + T;
    f.type = FeatureType::Sketch;
    f.sketch = sk;
    doc.features.push_back(f);
    Feature e;
    e.id = "CUT_" + T;
    e.type = FeatureType::Extrude;
    e.props = {{"sketch", "SK_" + T}, {"distance", "W_" + T}, {"direction", "symmetric"}, {"op", "cut"}, {"targets", body}};
    doc.features.push_back(e);
    Feature p;
    p.id = "PAT_" + T;
    p.type = FeatureType::Pattern;
    p.props = {{"kind", "circular"}, {"source", "CUT_" + T}, {"count", "N_" + T}, {"angle", "360"}, {"axis", "Z"}, {"targets", body}};
    doc.features.push_back(p);
    for (auto* id : {&f.id, &e.id, &p.id}) rep.features.push_back(*id);
  }
  void plane(const std::string& T, const std::string& angle, const std::string& offset) {
    Feature f;
    f.id = "PL_" + T;
    f.type = FeatureType::Plane;
    f.props = {{"kind", "axisAngle"}, {"axis", "Z"}, {"angle", angle}, {"offset", offset}, {"show", "0"}};
    doc.features.push_back(f);
    rep.features.push_back(f.id);
  }
};

std::string tagD(bool fromText) { return fromText ? "[D] " : "[Ö] "; }

}  // namespace

CutReport addTransverseCuts(Document& doc, const TransverseResult& tr, const CutOptions& opt) {
  CutReport rep;
  model::ModelResult mr0 = model::regenerate(doc);
  auto bodies = bodyExtents(mr0);
  if (bodies.empty()) { rep.warnings.push_back("R3: döndürülmüş gövde yok, kesit özellikleri eklenmedi"); return rep; }
  double zx0 = opt.zOfX0;
  if (!has(zx0)) { zx0 = 1e300; for (auto& b : bodies) zx0 = std::min(zx0, b.z0); }
  const int s = tr.flowSign >= 0 ? 1 : -1;
  Builder B{doc, rep};
  B.param("FLOW_R3", std::to_string(s), ParamKind::Scalar, "[V] akış yönü +1: +Z (sol->sağ), -1: -Z (" + tr.flowEvidence + ")");
  const size_t nFeat0 = doc.features.size();
  for (auto& V : tr.views) {
    // grooves first: a face slot of the same view ends inside the groove and refers to its parameters
    std::vector<const SlotFamily*> fams;
    for (auto& F : V.families) if (F.kind == "groove") fams.push_back(&F);
    for (auto& F : V.families) if (F.kind != "groove") fams.push_back(&F);
    const SlotFamily* groove = nullptr;
    for (auto* G : fams) if (G->kind == "groove" && has(G->height) && opt.grooves) { groove = G; break; }
    for (const SlotFamily* Fp : fams) {
      const SlotFamily& F = *Fp;
      if ((F.kind == "slot" && !opt.slots) || (F.kind == "groove" && !opt.grooves)) continue;
      const std::string& T = F.tag;
      const std::string vn = V.letter.empty() ? "?" : V.letter;
      // ---- target body
      const BodyExt* tb = nullptr;
      if (V.partNo > 0 && V.partNo <= (int)bodies.size()) tb = &bodies[V.partNo - 1];
      if (!tb && F.kind == "slot" && !F.faceRelative && has(F.x0)) {
        double z = zx0 + 0.5 * (F.x0 + F.x1), r = 0.5 * (F.rIn + F.rOut);
        for (auto& b : bodies)
          for (double a : {0.0, 0.7, 1.9, 3.3, 4.4})
            if (!tb && inside(b.shape, r * std::cos(a), r * std::sin(a), z)) tb = &b;
      }
      if (!tb) {   // body whose OD matches the view's OD (smallest such)
        for (auto& b : bodies)
          if (std::fabs(b.rmax - F.rOut) < 0.03 * F.rOut + 0.01 && (!tb || b.rmax * (b.z1 - b.z0) < tb->rmax * (tb->z1 - tb->z0))) tb = &b;
      }
      if (!tb) { rep.warnings.push_back("R3 " + vn + "/" + T + ": hedef gövde bulunamadı"); continue; }
      rep.targetOf[T] = tb->name;
      // ---- parameters
      const bool txtCount = F.count && F.count != F.countGeom ? true : !F.textIds.empty();
      B.param("N_" + T, std::to_string(F.count), ParamKind::Count,
              tagD(txtCount) + F.kind + " adedi, görünüş " + vn + " (çizim " + std::to_string(F.countGeom) + ")");
      B.param("W_" + T, fmt(F.width, 6), ParamKind::Length, tagD(F.width != F.widthGeom || !F.textIds.empty()) + "genişlik, görünüş " + vn);
      const double phi3 = s > 0 ? F.phaseDeg : 180.0 - F.phaseDeg;
      double ph = std::fmod(phi3, 360.0 / std::max(1, F.count));
      if (ph < 0) ph += 360.0 / std::max(1, F.count);
      B.param("PHI_" + T, fmt(ph, 4), ParamKind::Angle, "[Ö] faz açısı (Z etrafında), görünüş " + vn);
      B.param("RO_" + T, fmt(F.rOut, 6), ParamKind::Length, "[D] dış yarıçap (Ø" + fmt(2 * F.rOut, 4) + "), görünüş " + vn);
      const std::string body = tb->name;
      const std::string m = fmt(opt.margin, 4);
      if (F.kind == "groove") {
        B.param("H_" + T, fmt(F.height, 6), ParamKind::Length, "[D] oluk derinliği (radyal), görünüş " + vn);
        B.param("X0_" + T, fmt(tb->z0, 6), ParamKind::Length, "[T] oluk başlangıcı = gövde " + body + " başı (Z)");
        B.param("X1_" + T, fmt(tb->z1, 6), ParamKind::Length, "[T] oluk sonu = gövde " + body + " sonu (Z)");
        B.plane(T, "PHI_" + T, "0");
        B.slotFeatures(T, "PL_" + T, "RO_" + T + "-H_" + T, "H_" + T + "+" + m, "X0_" + T + "-" + m,
                       "X1_" + T + "-X0_" + T + "+2*" + m, body);
        continue;
      }
      // ---- tangential slot
      const int Lz = s * (F.senseView >= 0 ? 1 : -1);   // angular momentum about +Z
      if (has(F.tangentDia)) {
        B.param("DSC_" + T, fmt(F.tangentDia, 6), ParamKind::Length, "[D] " + F.tangentName + " çapı (dış duvar teğet)");
        B.param("OFS_" + T, "DSC_" + T + "/2-W_" + T + "/2", ParamKind::Length, "[T] eksen kaçıklığı: dış duvar Ø" + fmt(F.tangentDia, 4) + " teğet");
      } else {
        B.param("OFS_" + T, fmt(F.offset, 6), ParamKind::Length, tagD(F.offset != F.offsetGeom) + "yarık eksen kaçıklığı, görünüş " + vn);
      }
      B.param("SENSE_" + T, std::to_string(Lz), ParamKind::Scalar,
              std::string("[") + (F.senseText ? "D" : "Ö") + "] dönüş +1: +Z etrafında CCW; akıştan bakışta " +
                  (F.senseView > 0 ? "CCW" : "CW"));
      B.plane(T, "PHI_" + T, "-SENSE_" + T + "*OFS_" + T);
      std::string u0, lu, v0, lv;
      if (F.faceRelative) {
        const bool atMax = F.frontFace == (s > 0);   // front = downstream face: max Z when the flow goes to +Z
        const bool atMin = !atMax;
        const double zf = atMin ? tb->z0 : tb->z1;
        B.param("H_" + T, fmt(F.height, 6), ParamKind::Length, "[D] yarık derinliği (yüzden), görünüş " + vn);
        B.param("X0_" + T, fmt(zf, 6), ParamKind::Length,
                std::string("[T] ") + (F.frontFace ? "ön" : "arka") + " yüz Z (gövde " + body + ")");
        B.param("X1_" + T, std::string("X0_") + T + (atMin ? "+" : "-") + "H_" + T, ParamKind::Length, "[T] yarık tabanı Z");
        u0 = std::fabs(F.uIn) < 0.05 * F.rOut ? "0" : fmt(F.uIn, 4);
        if (groove && std::fabs(groove->rOut - F.rOut) < 0.03 * F.rOut) {
          // the slot runs from the tangent point into the feed groove (VIEW B): its centreline ends at the groove's
          // mid-depth radius, so both end corners lie inside the groove (not through the OD beside it)
          const std::string Tg = groove->tag;
          lu = "sqrt((RO_" + T + "-H_" + Tg + "/2)^2-OFS_" + T + "^2)" + (u0 == "0" ? "" : "-" + u0);
          B.param("LS_" + T, lu, ParamKind::Length, "[T] yarık boyu: teğet noktasından oluk " + Tg + " orta derinliğine");
          lu = "LS_" + T;
        } else {
          lu = "RO_" + T + "+" + m + (u0 == "0" ? "" : "-" + u0);
        }
        v0 = atMin ? "X0_" + T + "-" + m : "X1_" + T;
        lv = "H_" + T + "+" + m;
      } else {
        if (!has(F.x0)) { rep.warnings.push_back("R3 " + vn + "/" + T + ": eksenel konum yok, atlandı"); continue; }
        B.param("RI_" + T, fmt(F.rIn, 6), ParamKind::Length, "[D] iç yarıçap (Ø" + fmt(2 * F.rIn, 4) + "), görünüş " + vn);
        B.param("X0_" + T, fmt(F.x0 + (zx0), 6), ParamKind::Length, "[D] yarık başlangıcı x (" + F.axialEvidence + ")");
        B.param("X1_" + T, fmt(F.x1 + (zx0), 6), ParamKind::Length, "[D] yarık sonu x");
        B.param("H_" + T, "X1_" + T + "-X0_" + T, ParamKind::Length, "[T] yarık yüksekliği (eksenel)");
        // start inside the bore: the far wall leaves the bore at sqrt(RI^2-(OFS+W/2)^2)
        u0 = "sqrt(RI_" + T + "^2-(OFS_" + T + "+W_" + T + "/2)^2)-0.1*W_" + T;
        lu = "RO_" + T + "+" + m + "-(" + u0 + ")";
        v0 = "X0_" + T;
        lv = "H_" + T;
      }
      B.slotFeatures(T, "PL_" + T, u0, lu, v0, lv, body);
    }
  }
  if (doc.features.size() == nFeat0) return rep;
  model::ModelResult mr = model::regenerate(doc);
  rep.ok = true;
  for (auto& st : mr.status)
    if (!st.ok && std::find(rep.features.begin(), rep.features.end(), st.featureId) != rep.features.end()) {
      rep.ok = false;
      rep.warnings.push_back("R3 " + st.featureId + ": " + st.message);
    }
  return rep;
}

int applyTransverseCuts(const DrawingIR& ir, Document& doc, std::vector<std::string>& warnings, Attrs& attrs) {
  TransverseResult tr = analyzeTransverse(ir.norm);
  if (tr.views.empty()) return 0;
  CutReport rep = addTransverseCuts(doc, tr);
  for (auto& w : rep.warnings) warnings.push_back(w);
  for (auto& V : tr.views)
    for (auto& F : V.families)
      for (auto& w : F.warnings) warnings.push_back("R3 " + V.letter + "/" + F.tag + ": " + w);
  std::string list;
  for (auto& f : rep.features) list += (list.empty() ? "" : ",") + f;
  attrs["r3.cuts"] = list;
  attrs["r3.cuts.ok"] = rep.ok ? "1" : "0";
  return (int)rep.features.size();
}

}  // namespace pc::drawing::synth
