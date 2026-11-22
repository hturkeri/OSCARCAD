// Independent review of v2 (datum planes, multi-body, Combine): regression tests for defects found in review
// and edge cases of the BODIES contract (pc/Feature.h) that were not covered by test_model_v2.cpp.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/MfgChecks.h"
#include "pc/Model.h"

#include <BRepAdaptor_Curve.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace pc;
using namespace pc::model;

namespace {

std::string dump(const ModelResult& r) {
  std::string s;
  for (auto& st : r.status) s += st.featureId + (st.ok ? " ok " : " FAIL ") + st.message + "\n";
  for (auto& b : r.bodies) s += "  body " + b.name + " (" + b.createdBy + ")\n";
  return s;
}
const FeatureStatus* statusOf(const ModelResult& r, const std::string& id) {
  for (auto& s : r.status)
    if (s.featureId == id) return &s;
  return nullptr;
}
std::vector<std::string> names(const ModelResult& r) {
  std::vector<std::string> v;
  for (auto& b : r.bodies) v.push_back(b.name);
  return v;
}
double bodyVol(const ModelResult& r, const std::string& n) {
  const BodyInfo* b = r.body(n);
  EXPECT_NE(b, nullptr) << n << "\n" << dump(r);
  return b ? massProperties(b->shape).volume : -1;
}

Feature feat(const std::string& id, FeatureType t, std::map<std::string, std::string> props,
             std::vector<std::string> refs = {}) {
  Feature f;
  f.id = id;
  f.type = t;
  f.props = std::move(props);
  f.refs = std::move(refs);
  return f;
}

Feature rectSketch(const std::string& id, SketchPlaneKind k, const std::string& ref, double x0, double y0, double x1,
                   double y1, const std::string& off = "0") {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  Sketch s = Sketch::create();
  s.plane.kind = k;
  if (k == SketchPlaneKind::OnFace) s.plane.faceRef = ref;
  if (k == SketchPlaneKind::OnPlane) s.plane.planeRef = ref;
  s.plane.offsetExpr = off;
  auto L = s.addRectangle(x0, y0, x1, y1);
  int a = s.entity(L[0])->points[0];
  int c = s.entity(L[2])->points[0];
  s.addConstraint(SkConstraintType::DistanceX, {0, a}, formatNumber(x0));
  s.addConstraint(SkConstraintType::DistanceY, {0, a}, formatNumber(y0));
  s.addConstraint(SkConstraintType::DistanceX, {0, c}, formatNumber(x1));
  s.addConstraint(SkConstraintType::DistanceY, {0, c}, formatNumber(y1));
  f.sketch = s;
  return f;
}

// 80 x 50 x 10 plate centred at the origin (Sketch1/Extrude1).
Document plate() {
  Document d;
  d.features.push_back(rectSketch("Sketch1", SketchPlaneKind::XY, "", -40, -25, 40, 25));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "10"}}));
  return d;
}

void addBox(Document& d, const std::string& sid, const std::string& eid, double x0, double y0, double x1, double y1,
            double z0, double h, std::map<std::string, std::string> props) {
  d.features.push_back(rectSketch(sid, SketchPlaneKind::XY, "", x0, y0, x1, y1, formatNumber(z0)));
  props["sketch"] = sid;
  props["distance"] = formatNumber(h);
  d.features.push_back(feat(eid, FeatureType::Extrude, props));
}

}  // namespace

// ---------------------------------------------------------------------------------------------- keepTool
// Review defect: a cut whose tool swallows its (only) target threw "işlem sonucu boş gövde" even with
// keepTool=1, although the kept tool is a valid body (SpaceClaim: subtract + keep tool leaves the tool).
TEST(ReviewV2Bodies, CutKeepToolConsumingOnlyBodyKeepsTool) {
  Document d = plate();
  addBox(d, "Sketch2", "Extrude2", -50, -30, 50, 30, -5, 20, {{"op", "cut"}, {"keepTool", "1"}, {"bodyName", "Kalip"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_EQ(names(r), (std::vector<std::string>{"Kalip"}));
  EXPECT_NEAR(bodyVol(r, "Kalip"), 100.0 * 60 * 20, 1e-6);
  EXPECT_NE(statusOf(r, "Extrude2")->message.find("tamamen kaldırıldı"), std::string::npos) << dump(r);
  // without keepTool the part would be empty: still an error
  d.features.back().props.erase("keepTool");
  ModelResult r2 = regenerate(d);
  EXPECT_FALSE(r2.ok);
  EXPECT_NE(statusOf(r2, "Extrude2")->message.find("boş gövde"), std::string::npos) << dump(r2);
}

TEST(ReviewV2Bodies, CutConsumingOneOfTwoBodiesWarnsAndKeepsOther) {
  Document d = plate();
  addBox(d, "Sketch2", "Extrude2", 100, 0, 110, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kup"}});
  addBox(d, "Sketch3", "Extrude3", 95, -5, 115, 15, -5, 20, {{"op", "cut"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_EQ(names(r), (std::vector<std::string>{"Extrude1"}));
  EXPECT_NE(statusOf(r, "Extrude3")->message.find("'Kup' tamamen kaldırıldı"), std::string::npos) << dump(r);
  // a later feature that still targets the consumed body reports it clearly
  addBox(d, "Sketch4", "Extrude4", 0, 0, 5, 5, 10, 5, {{"op", "join"}, {"targets", "Kup"}});
  ModelResult r2 = regenerate(d);
  EXPECT_FALSE(r2.ok);
  EXPECT_NE(statusOf(r2, "Extrude4")->message.find("gövde bulunamadı 'Kup'"), std::string::npos) << dump(r2);
}

TEST(ReviewV2Bodies, IntersectKeepToolWithNoOverlapKeepsTool) {
  Document d = plate();
  addBox(d, "Sketch2", "Extrude2", 100, 0, 110, 10, 0, 10, {{"op", "intersect"}, {"targets", "Extrude1"}, {"keepTool", "1"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_EQ(names(r), (std::vector<std::string>{"Extrude2.tool"}));
}

// --------------------------------------------------------------------------------------------- Combine
TEST(ReviewV2Combine, JoinNonTouchingAndMissingTarget) {
  Document d = plate();
  addBox(d, "Sketch2", "Extrude2", 100, 0, 110, 10, 0, 10, {{"op", "new"}, {"bodyName", "Uzak"}});
  d.features.push_back(feat("Combine1", FeatureType::Combine, {{"op", "join"}, {"target", "Extrude1"}, {"tools", "Uzak"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  // disjoint join: pieces of the target, tool consumed, total volume preserved
  double v = 0;
  for (auto& b : r.bodies) v += massProperties(b.shape).volume;
  EXPECT_NEAR(v, 80.0 * 50 * 10 + 1000, 1e-6);
  EXPECT_EQ(r.body("Uzak"), nullptr);
  // renamed tool body (bodyName changed, Combine not updated) -> clear error, earlier bodies intact
  d.features[3].props["bodyName"] = "Baska";
  ModelResult r2 = regenerate(d);
  EXPECT_FALSE(r2.ok);
  EXPECT_NE(statusOf(r2, "Combine1")->message.find("gövde bulunamadı 'Uzak'"), std::string::npos) << dump(r2);
  EXPECT_EQ(names(r2), (std::vector<std::string>{"Extrude1", "Baska"}));
}

// ------------------------------------------------------------------------------------------ datum planes
// edgeNormal on an arc: normal = tangent of the underlying curve, origin on the arc, orthonormal frame.
TEST(ReviewV2Plane, FrameIsOrthonormalForAllKindsWithModifiers) {
  Document base = plate();
  const std::string e5 = "edge(Extrude1.end|Extrude1.side.e5)", e7 = "edge(Extrude1.end|Extrude1.side.e7)";
  const std::vector<std::pair<std::map<std::string, std::string>, std::vector<std::string>>> cases = {
      {{{"kind", "edgeNormal"}, {"t", "0.3"}, {"rotate", "33"}, {"flip", "1"}, {"offset", "4"}}, {e5}},
      {{{"kind", "edgeAngle"}, {"angle", "-20"}, {"rotate", "10"}, {"offset", "-2"}}, {e5}},
      {{{"kind", "twoEdges"}, {"flip", "1"}, {"offset", "1.5"}}, {e5, e7}},
      {{{"kind", "faceOffset"}, {"offset", "7"}, {"rotate", "90"}}, {"Extrude1.end"}}};
  for (auto& [props, refs] : cases) {
    Document d = base;
    d.features.push_back(feat("Plane1", FeatureType::Plane, props, refs));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << dump(r);
    const gp_Ax3& f = r.datumPlanes.at("Plane1").frame;
    EXPECT_NEAR(f.XDirection().Dot(f.Direction()), 0, 1e-12);
    EXPECT_NEAR(f.YDirection().Dot(f.Direction()), 0, 1e-12);
    EXPECT_TRUE(f.Direct()) << props.at("kind");  // v = n x u (right-handed) also after flip
    // offset is applied along the FINAL normal: the un-offset origin lies offset behind
    Document d0 = d;
    d0.features.back().props["offset"] = "0";
    ModelResult r0 = regenerate(d0);
    ASSERT_TRUE(r0.ok);
    const gp_Ax3& f0 = r0.datumPlanes.at("Plane1").frame;
    const double off = std::stod(props.at("offset"));
    gp_Vec dv(f0.Location(), f.Location());
    EXPECT_NEAR(dv.Dot(gp_Vec(f.Direction())), off, 1e-9) << props.at("kind");
    EXPECT_NEAR(dv.Magnitude(), std::abs(off), 1e-9) << props.at("kind");
  }
  // faceOffset on the top face with offset 7: z = 17, normal +Z
  Document d = base;
  d.features.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "faceOffset"}, {"offset", "7"}}, {"Extrude1.end"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(r.datumPlanes.at("Plane1").frame.Location().Z(), 17, 1e-9);
  EXPECT_NEAR(r.datumPlanes.at("Plane1").frame.Direction().Z(), 1, 1e-12);
}

// A suppressed / deleted / reordered plane makes its sketch fail with a clear message; un-suppress restores.
TEST(ReviewV2Plane, SuppressedPlaneBreaksDependentSketchCleanly) {
  Document d = plate();
  d.features.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "edgeNormal"}, {"t", "0.5"}},
                            {"edge(Extrude1.end|Extrude1.side.e5)"}));
  d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnPlane, "Plane1", -5, 0, 5, 10));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "2"}, {"direction", "symmetric"}}));
  ModelResult ok = regenerate(d);
  ASSERT_TRUE(ok.ok) << dump(ok);
  d.features[2].suppressed = true;
  ModelResult r = regenerate(d);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(statusOf(r, "Sketch2")->message.find("kullanılamıyor"), std::string::npos) << dump(r);
  EXPECT_EQ(r.datumPlanes.count("Plane1"), 0u);
  // public sketchFrame agrees
  gp_Ax3 fr;
  std::string err;
  EXPECT_FALSE(sketchFrame(d, "Sketch2", d.evaluateParams(), r, fr, &err));
  EXPECT_NE(err.find("kullanılamıyor"), std::string::npos) << err;
  d.features[2].suppressed = false;
  EXPECT_TRUE(sketchFrame(d, "Sketch2", d.evaluateParams(), ok, fr, &err)) << err;
}

// --------------------------------------------------------------------------------------------- sample
TEST(ReviewV2Sample, ParameterSweepKeepsBodiesPlanesAndNoErrors) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/v2_multibody.pcad", &err)) << err;
  const std::vector<std::map<std::string, std::string>> sweeps = {
      {}, {{"W", "120"}}, {{"D", "70"}}, {{"T", "12"}}, {{"Atab", "0"}}, {{"Atab", "60"}}, {{"W", "40"}, {"D", "30"}},
      {{"Ltab", "40"}, {"Wtab", "30"}}, {{"Hb", "40"}}};
  ModelResult r0 = regenerate(d);
  ASSERT_TRUE(r0.ok) << dump(r0);
  const auto names0 = names(r0);
  for (auto& ov : sweeps) {
    ModelResult r = regenerate(d, ov);
    std::string tag;
    for (auto& [k, v] : ov) tag += k + "=" + v + " ";
    ASSERT_TRUE(r.ok) << tag << "\n" << dump(r);
    EXPECT_EQ(names(r), names0) << tag;
    EXPECT_EQ(r.datumPlanes.size(), r0.datumPlanes.size()) << tag;
    // Plane1 (edgeNormal at t=0.5 of the +X... top edge) stays in the middle of that edge: x = 0
    EXPECT_NEAR(r.datumPlanes.at("Plane1").frame.Location().X(), 0, 1e-6) << tag;
  }
  // W moves the edge used by Plane2 (right side, x = W/2)
  ModelResult rw = regenerate(d, {{"W", "120"}});
  EXPECT_NEAR(rw.datumPlanes.at("Plane2").frame.Location().X() - r0.datumPlanes.at("Plane2").frame.Location().X(), 20, 1e-6);
}

// ----------------------------------------------------------------------------------- mfg multi-body info
namespace {
const mfg::Issue* findRule(const mfg::Report& rep, const std::string& id) {
  for (auto& i : rep.issues)
    if (i.ruleId == id) return &i;
  return nullptr;
}
}  // namespace

// Review defect: a Pattern/Extrude with op=new whose tool has several disjoint solids creates bodies named
// "<name>:k" by design (BODIES contract); MODEL.MULTI_BODY must not call those an unintended split.
TEST(ReviewV2Mfg, IntentionalMultiPieceNewBodiesAreInfo) {
  Document e = plate();
  addBox(e, "Sketch2", "Extrude2", 50, 0, 60, 10, 0, 5, {{"op", "new"}, {"bodyName", "Pim"}});
  e.features.push_back(feat("Pattern1", FeatureType::Pattern,
                            {{"source", "Extrude2"}, {"kind", "linear"}, {"count", "3"}, {"spacing", "20"}, {"op", "new"}}));
  ModelResult r = regenerate(e);
  ASSERT_TRUE(r.ok) << dump(r);
  ASSERT_EQ(names(r), (std::vector<std::string>{"Extrude1", "Pim", "Pattern1:1", "Pattern1:2"}));
  MfgSettings s;
  s.am = mfg::amPreset(AmProcess::LPBF_Metal);
  s.am.enabled = true;
  s.conv.enabled = false;
  mfg::Report rep = mfg::checkGeometric(e, r, s);
  const mfg::Issue* mb = findRule(rep, "MODEL.MULTI_BODY");
  ASSERT_NE(mb, nullptr);
  EXPECT_EQ(mb->severity, mfg::Severity::Info) << mb->message;

  // a real split of an intentional body by a later cut is still a warning
  Document d = plate();
  addBox(d, "Sketch2", "Extrude2", 50, -10, 90, 10, 0, 5, {{"op", "new"}, {"bodyName", "Kol"}});
  addBox(d, "Sketch3", "Extrude3", 65, -20, 70, 20, -1, 10, {{"op", "cut"}, {"targets", "Kol"}});
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << dump(r2);
  ASSERT_EQ(names(r2), (std::vector<std::string>{"Extrude1", "Kol:1", "Kol:2"}));
  mfg::Report rep2 = mfg::checkGeometric(d, r2, s);
  const mfg::Issue* mb2 = findRule(rep2, "MODEL.MULTI_BODY");
  ASSERT_NE(mb2, nullptr);
  EXPECT_EQ(mb2->severity, mfg::Severity::Warning) << mb2->message;
}
