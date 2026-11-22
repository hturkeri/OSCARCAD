// Pull on faces (docs/spaceclaim_ux/PULL_FACE_FIX.md): PullMapping decisions + the regenerated model.
// Regression for the swirl-cup video: pulling the planar END face of a 360° Revolve must extend the body (or cut /
// make a new body), never change the revolve angle. Links pc_designer (PullMapping is designer logic, Qt strings).
#include "PullMapping.h"
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gtest/gtest.h>
#include <cmath>

using namespace pc;
using namespace pc::model;
using namespace pcd;

namespace {

Document loadSwc() {
  Document d;
  std::string err;
  EXPECT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/ref_swc_rr_001.pcad", &err)) << err;
  return d;
}

ModelResult regen(const Document& d) {
  ModelResult r = regenerate(d);
  for (const auto& s : r.status) EXPECT_TRUE(s.ok) << s.message;
  return r;
}

double volume(const TopoDS_Shape& s) {
  GProp_GProps g;
  BRepGProp::VolumeProperties(s, g);
  return g.Mass();
}
double area(const TopoDS_Face& f) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(f, g);
  return g.Mass();
}

pull::Evaluator evaluator(const Document& d) {
  return [&d](const QString& s, double* v) {
    try {
      *v = evalWith(s.toStdString(), d.evaluateParams());
      return true;
    } catch (...) {
      return false;
    }
  };
}

// the largest planar Cup_Don profile face perpendicular to the revolve axis (Z) = the end face of the video
std::string endFace(const ModelResult& r) {
  std::string best;
  double ba = 0;
  for (const auto& [n, f] : r.faces) {
    if (n.rfind("Cup_Don.rev.", 0) != 0) continue;
    BRepAdaptor_Surface s(f, Standard_False);
    if (s.GetType() != GeomAbs_Plane || std::fabs(std::fabs(s.Plane().Axis().Direction().Z()) - 1) > 1e-9) continue;
    const double a = area(f);
    if (a > ba) { ba = a; best = n; }
  }
  return best;
}

pull::Target analyzeFace(const Document& d, const ModelResult& r, const std::string& face, const pull::Options& o = {}) {
  return pull::analyze(d, r, {SelItem{SelItem::Kind::Face, QString::fromStdString(face)}}, o);
}

struct Pulled {
  Document doc;
  ModelResult res;
  pull::Result pr;
};
Pulled pullFace(const Document& d, const ModelResult& r, const std::string& face, const QString& value, const pull::Options& o) {
  Pulled p;
  const pull::Target t = analyzeFace(d, r, face, o);
  EXPECT_EQ(t.kind, pull::Kind::FaceExtrude) << t.refusal.toStdString();
  p.pr = pull::apply(d, r, t, value, false, o, evaluator(d));
  EXPECT_TRUE(p.pr.ok) << p.pr.refusal.toStdString();
  p.doc = pull::withNewFeatures(d, p.pr, -1);
  p.res = regen(p.doc);
  return p;
}

} // namespace

TEST(PullFace, SwirlCupEndFaceMapsToFaceExtrudeNotAngle) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const std::string ef = endFace(r);
  ASSERT_FALSE(ef.empty());
  const pull::Target t = analyzeFace(d, r, ef);
  EXPECT_EQ(t.kind, pull::Kind::FaceExtrude);
  EXPECT_NE(t.kind, pull::Kind::RevolveAngle);
  EXPECT_TRUE(t.valid()) << t.refusal.toStdString();
  EXPECT_EQ(t.fieldLabel, "Mesafe");
  EXPECT_EQ(t.unit, "mm");
  EXPECT_EQ(t.body, "SwirlCup");
  // the face sketch holds the ring (2 circles) on the face
  Feature sk;
  QString err;
  ASSERT_TRUE(pull::faceSketch(d, r, QString::fromStdString(ef), &sk, &err)) << err.toStdString();
  ASSERT_TRUE(sk.sketch);
  EXPECT_EQ(sk.sketch->plane.kind, SketchPlaneKind::OnFace);
  EXPECT_EQ(sk.sketch->plane.faceRef, ef);
  EXPECT_GE(sk.sketch->entities.size(), 2u);
}

TEST(PullFace, SwirlCupAddExtendsBody) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const std::string ef = endFace(r);
  const double A = area(r.faces.at(ef));
  const double V0 = volume(r.shape);
  pull::Options o;   // Otomatik
  Pulled p = pullFace(d, r, ef, "5", o);
  EXPECT_TRUE(p.pr.hasPreFeature);
  EXPECT_EQ(p.pr.feature.prop("op"), "join");
  EXPECT_EQ(p.pr.feature.prop("targets"), "SwirlCup");
  EXPECT_EQ(p.pr.feature.prop("pullFace"), ef);
  EXPECT_EQ(p.res.bodies.size(), 1u);
  EXPECT_NEAR(volume(p.res.shape), V0 + 5 * A, 1e-4 * V0);
  EXPECT_EQ(p.doc.feature("Cup_Don")->prop("angle"), "360");
  EXPECT_EQ(p.doc.features.size(), d.features.size() + 2);
  // the moved face: the new end face, carrying the pulled face's name in its lineage
  const std::string endName = p.pr.resultFace.toStdString();
  ASSERT_TRUE(p.res.faces.count(endName)) << endName;
  EXPECT_FALSE(p.res.faces.count(ef)) << "the pulled face became internal";
  const auto& lin = p.res.faceLineage.at(endName);
  EXPECT_TRUE(std::find(lin.begin(), lin.end(), ef) != lin.end());
  EXPECT_FALSE(p.res.removedBy.count(ef));
  // pulling the new end face again edits the new extrude's distance (no feature explosion)
  const pull::Target t2 = analyzeFace(p.doc, p.res, endName);
  EXPECT_EQ(t2.kind, pull::Kind::ExtrudeEnd);
  EXPECT_EQ(t2.featureId, p.pr.featureId);
  // Ekle (Add) explicitly: same result
  o.merge = "add";
  Pulled p2 = pullFace(d, r, ef, "5", o);
  EXPECT_EQ(p2.res.bodies.size(), 1u);
  EXPECT_NEAR(volume(p2.res.shape), V0 + 5 * A, 1e-4 * V0);
}

TEST(PullFace, SwirlCupNoMergeMakesNewBody) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const std::string ef = endFace(r);
  const double A = area(r.faces.at(ef));
  const double V0 = volume(r.shape);
  pull::Options o;
  o.merge = "nomerge";
  Pulled p = pullFace(d, r, ef, "5", o);
  EXPECT_EQ(p.pr.feature.prop("op"), "new");
  EXPECT_EQ(p.res.bodies.size(), 2u);
  EXPECT_NEAR(volume(p.res.shape), V0 + 5 * A, 1e-4 * V0);
  EXPECT_TRUE(p.res.faces.count(ef)) << "the original face is untouched";
  EXPECT_EQ(p.doc.feature("Cup_Don")->prop("angle"), "360");
}

TEST(PullFace, SwirlCupNegativeCuts) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const std::string ef = endFace(r);
  const double A = area(r.faces.at(ef));
  const double V0 = volume(r.shape);
  Pulled p = pullFace(d, r, ef, "-2", pull::Options{});
  EXPECT_EQ(p.pr.feature.prop("op"), "cut");
  EXPECT_EQ(p.res.bodies.size(), 1u);
  EXPECT_NEAR(volume(p.res.shape), V0 - 2 * A, 1e-4 * V0);
  EXPECT_EQ(p.doc.feature("Cup_Don")->prop("angle"), "360");
  // Kes (Cut) with a positive value also cuts into the material
  pull::Options o;
  o.merge = "cut";
  Pulled p2 = pullFace(d, r, ef, "2", o);
  EXPECT_NEAR(volume(p2.res.shape), V0 - 2 * A, 1e-4 * V0);
}

TEST(PullFace, NamedSelectionFollowsThePulledFace) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const std::string ef = endFace(r);
  std::string err;
  ASSERT_TRUE(ns::create(d, "Giris", {ns::face(ef)}, r, -1, &err)) << err;
  Pulled p = pullFace(d, r, ef, "5", pull::Options{});
  const NsResolution res = resolve(p.doc, p.doc.namedSelections.front(), p.res);
  EXPECT_TRUE(isResolved(res.status));
  ASSERT_EQ(res.faces.size(), 1u);
  EXPECT_EQ(res.faces.front(), p.pr.resultFace.toStdString());
  EXPECT_TRUE(res.status == NsStatus::Ok || res.status == NsStatus::Merged) << int(res.status);
  // cut: the name moves to the new bottom face
  Pulled c = pullFace(d, r, ef, "-2", pull::Options{});
  const NsResolution rc = resolve(c.doc, c.doc.namedSelections.front(), c.res);
  ASSERT_EQ(rc.faces.size(), 1u);
  EXPECT_EQ(rc.faces.front(), c.pr.resultFace.toStdString());
  // new body: the original face keeps the name
  pull::Options o;
  o.merge = "nomerge";
  Pulled n = pullFace(d, r, ef, "5", o);
  const NsResolution rn = resolve(n.doc, n.doc.namedSelections.front(), n.res);
  ASSERT_EQ(rn.faces.size(), 1u);
  EXPECT_EQ(rn.faces.front(), ef);
}

TEST(PullFace, PartialRevolveCapsStillChangeTheAngle) {
  Document d = loadSwc();
  d.feature("Cup_Don")->props["angle"] = "90";
  ModelResult r = regen(d);
  ASSERT_TRUE(r.faces.count("Cup_Don.end"));
  pull::Target t = analyzeFace(d, r, "Cup_Don.end");
  EXPECT_EQ(t.kind, pull::Kind::RevolveAngle);
  EXPECT_EQ(t.fieldLabel, "Açı");
  // its profile faces are offset, not rotated
  t = analyzeFace(d, r, endFace(r));
  EXPECT_EQ(t.kind, pull::Kind::FaceExtrude);
}

TEST(PullFace, CylindricalRevolveFaceOffsetsTheRadius) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const double V0 = volume(r.shape);
  // outer wall: line e59 (25,0)-(25,19), DistanceX = D_outer/2
  ASSERT_TRUE(r.faces.count("Cup_Don.rev.e59"));
  pull::Target t = analyzeFace(d, r, "Cup_Don.rev.e59");
  ASSERT_EQ(t.kind, pull::Kind::RevolveRadius) << t.refusal.toStdString();
  EXPECT_EQ(t.fieldLabel, "Mesafe");
  EXPECT_EQ(t.sign, 1);
  EXPECT_NEAR(t.current, 25, 1e-9);
  pull::Result pr = pull::apply(d, r, t, "1", false, {}, evaluator(d));
  ASSERT_TRUE(pr.ok) << pr.refusal.toStdString();
  ASSERT_TRUE(pr.paramName.isEmpty());
  const Feature* sk = pr.doc.feature("Ck_Profile");
  int hits = 0;
  for (const auto& c : sk->sketch->constraints)
    if (c.valueExpr == "D_outer/2 + 1") ++hits;
  EXPECT_EQ(hits, 2);
  EXPECT_EQ(pr.doc.feature("Cup_Don")->prop("angle"), "360");
  ModelResult r2 = regen(pr.doc);
  EXPECT_GT(volume(r2.shape), V0);
  BRepAdaptor_Surface s(r2.faces.at("Cup_Don.rev.e59"), Standard_False);
  ASSERT_EQ(s.GetType(), GeomAbs_Cylinder);
  EXPECT_NEAR(s.Cylinder().Radius(), 26, 1e-6);
  // a second pull folds the offset
  pull::Target t2 = analyzeFace(pr.doc, r2, "Cup_Don.rev.e59");
  pull::Result pr2 = pull::apply(pr.doc, r2, t2, "1", false, {}, evaluator(pr.doc));
  ASSERT_TRUE(pr2.ok);
  hits = 0;
  for (const auto& c : pr2.doc.feature("Ck_Profile")->sketch->constraints)
    if (c.valueExpr == "D_outer/2 + 2") ++hits;
  EXPECT_EQ(hits, 2);
  // the bore (line e76 (5,3)-(5,0)): its outward normal points to the axis -> pulling +1 shrinks the radius
  pull::Target tb = analyzeFace(d, r, "Cup_Don.rev.e76");
  ASSERT_EQ(tb.kind, pull::Kind::RevolveRadius) << tb.refusal.toStdString();
  EXPECT_EQ(tb.sign, -1);
  pull::Result pb = pull::apply(d, r, tb, "1", false, {}, evaluator(d));
  ASSERT_TRUE(pb.ok) << pb.refusal.toStdString();
  ModelResult rb = regen(pb.doc);
  BRepAdaptor_Surface sb(rb.faces.at("Cup_Don.rev.e76"), Standard_False);
  EXPECT_NEAR(sb.Cylinder().Radius(), 4, 1e-6);
}

TEST(PullFace, ConicalRevolveFaceIsRefusedNotRotated) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  // line e62 (16,22.5)-(23.5,30) is slanted -> cone
  ASSERT_TRUE(r.faces.count("Cup_Don.rev.e62"));
  const pull::Target t = analyzeFace(d, r, "Cup_Don.rev.e62");
  EXPECT_EQ(t.kind, pull::Kind::Unsupported);
  EXPECT_FALSE(t.valid());
  EXPECT_TRUE(t.refusal.contains("açısı değiştirilmez")) << t.refusal.toStdString();
}

TEST(PullFace, PlanarFacesOfCutsAndPatternsAreExtruded) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  int tried = 0;
  for (const auto& [n, f] : r.faces) {
    if (n.rfind("Slot_Pri", 0) != 0 || n.find(".side.") == std::string::npos) continue;   // .start/.end keep ExtrudeEnd/Start
    BRepAdaptor_Surface s(f, Standard_False);
    if (s.GetType() != GeomAbs_Plane) continue;
    const pull::Target t = analyzeFace(d, r, n);
    EXPECT_EQ(t.kind, pull::Kind::FaceExtrude) << n;
    if (tried++ == 0) {   // one of them through the whole chain (Add 0.5 into the slot)
      pull::Result pr = pull::apply(d, r, t, "0.5", false, {}, evaluator(d));
      ASSERT_TRUE(pr.ok) << pr.refusal.toStdString();
      const ModelResult r2 = regen(pull::withNewFeatures(d, pr, -1));
      EXPECT_EQ(r2.bodies.size(), 1u);
      EXPECT_GT(volume(r2.shape), volume(r.shape));
    }
  }
  EXPECT_GT(tried, 0);
  // bracket: a plain extrude side face is extruded too; existing mappings stay
  Document br;
  std::string err;
  ASSERT_TRUE(loadDocument(br, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  const ModelResult rb = regen(br);
  EXPECT_EQ(analyzeFace(br, rb, "Extrude1.side.e5").kind, pull::Kind::FaceExtrude);
  EXPECT_EQ(analyzeFace(br, rb, "Extrude1.end").kind, pull::Kind::ExtrudeEnd);
  EXPECT_EQ(analyzeFace(br, rb, "Fillet1.f0").kind, pull::Kind::FilletRadius);
  EXPECT_EQ(analyzeFace(br, rb, "Hole2.h0.bottom").kind, pull::Kind::HoleDepth);
}

TEST(PullFace, RollbackInsertion) {
  Document d = loadSwc();
  ModelResult r = regen(d);
  const pull::Target t = analyzeFace(d, r, endFace(r));
  const pull::Result pr = pull::apply(d, r, t, "5", false, {}, evaluator(d));
  ASSERT_TRUE(pr.ok);
  const Document c = pull::withNewFeatures(d, pr, 1);
  ASSERT_EQ(c.features.size(), d.features.size() + 2);
  EXPECT_EQ(c.features[2].id, pr.preFeature.id);
  EXPECT_EQ(c.features[3].id, pr.feature.id);
}

