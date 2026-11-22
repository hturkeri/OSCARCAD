#include <filesystem>
// apppkg v2: multi-body / datum-plane documents — JSON round trip + lintApp body warnings.
#include "pc/AppLint.h"
#include "pc/AppPackage.h"
#include "pc/MfgChecks.h"
#include "pc/Model.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <nlohmann/json.hpp>

using namespace pc;
using namespace pc::app;

namespace {
Feature feat(const std::string& id, FeatureType t, std::map<std::string, std::string> props, std::vector<std::string> refs = {}) {
  Feature f;
  f.id = id;
  f.type = t;
  f.props = std::move(props);
  f.refs = std::move(refs);
  return f;
}
Feature rectSketch(const std::string& id, double x0, double y0, double x1, double y1, SketchPlaneKind k = SketchPlaneKind::XY,
                   const std::string& planeRef = "") {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  Sketch s = Sketch::create();
  s.plane.kind = k;
  s.plane.planeRef = planeRef;
  s.addRectangle(x0, y0, x1, y1);
  f.sketch = s;
  return f;
}
AppDefinition baseApp(const std::string& name) {
  AppDefinition a;
  a.appName = name;
  a.document.params.add({"T", "10"});
  auto& p = a.document.params.all().back();
  p.minValue = 2; p.maxValue = 40; p.exposed = true; p.label = "Kalınlık";
  a.document.mfg.am.enabled = false;
  a.document.mfg.conv.enabled = false;
  return a;
}
// Base plate (body "Taban") + second body at x offset `dx` (overlapping when dx < 40).
AppDefinition twoBodies(double dx) {
  AppDefinition a = baseApp("Cok Govde");
  auto& f = a.document.features;
  f.push_back(rectSketch("Sketch1", -20, -20, 20, 20));
  f.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "T"}, {"bodyName", "Taban"}}));
  f.push_back(rectSketch("Sketch2", dx - 10, -10, dx + 10, 10));
  f.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "T"}, {"op", "new"}, {"bodyName", "Pim"}}));
  return a;
}
// Plane + OnPlane sketch + Combine.
AppDefinition planeCombine() {
  AppDefinition a = baseApp("Duzlem Combine");
  auto& f = a.document.features;
  f.push_back(rectSketch("Sketch1", -20, -20, 20, 20));
  f.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "T"}, {"bodyName", "Taban"}}));
  f.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "faceOffset"}, {"offset", "0"}}, {"Extrude1.end"}));
  f.push_back(rectSketch("Sketch2", -5, -5, 5, 5, SketchPlaneKind::OnPlane, "Plane1"));
  f.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "8"}, {"op", "new"}, {"bodyName", "Kapak"}}));
  f.push_back(feat("Combine1", FeatureType::Combine, {{"op", "join"}, {"target", "Taban"}, {"tools", "Kapak"}}));
  return a;
}
bool hasWarn(const std::vector<std::string>& v, const char* needle = "") {
  for (auto& s : v) if (!lintIsError(s) && s.find(needle) != std::string::npos) return true;
  return false;
}
bool hasRule(const pc::mfg::Report& r, const char* rule) {
  return std::any_of(r.issues.begin(), r.issues.end(), [&](auto& i) { return i.ruleId.find(rule) != std::string::npos; });
}
} // namespace

TEST(AppPkgV2, RoundTripPlaneCombineOnPlane) {
  AppDefinition a = planeCombine();
  nlohmann::json j = toJson(a);
  AppDefinition b = appFromJson(j);
  EXPECT_EQ(toJson(b), j);
  ASSERT_EQ(b.document.features.size(), a.document.features.size());
  const Feature* pl = b.document.feature("Plane1");
  ASSERT_TRUE(pl);
  EXPECT_EQ(pl->type, FeatureType::Plane);
  EXPECT_EQ(pl->prop("kind"), "faceOffset");
  ASSERT_EQ(pl->refs.size(), 1u);
  EXPECT_EQ(pl->refs[0], "Extrude1.end");
  const Feature* sk = b.document.feature("Sketch2");
  ASSERT_TRUE(sk && sk->sketch);
  EXPECT_EQ(sk->sketch->plane.kind, SketchPlaneKind::OnPlane);
  EXPECT_EQ(sk->sketch->plane.planeRef, "Plane1");
  const Feature* cb = b.document.feature("Combine1");
  ASSERT_TRUE(cb);
  EXPECT_EQ(cb->type, FeatureType::Combine);
  EXPECT_EQ(cb->prop("target"), "Taban");
  EXPECT_EQ(cb->prop("tools"), "Kapak");
  EXPECT_EQ(b.document.feature("Extrude2")->prop("bodyName"), "Kapak");
}

TEST(AppPkgV2, RoundTripThroughPackageAndModelMatches) {
  AppDefinition a = planeCombine();
  std::string p = (std::filesystem::temp_directory_path().u8string() + "/test_apppkg_v2.pcapp"), err;
  ASSERT_TRUE(savePackage(a, p, &err)) << err;
  auto b = loadPackage(p, &err);
  ASSERT_TRUE(b.has_value()) << err;
  std::remove(p.c_str());
  auto m1 = pc::model::regenerate(a.document), m2 = pc::model::regenerate(b->document);
  EXPECT_EQ(m1.ok, m2.ok);
  EXPECT_EQ(m1.bodies.size(), m2.bodies.size());
  EXPECT_EQ(m1.datumPlanes.size(), m2.datumPlanes.size());
  if (m1.ok) {
    EXPECT_EQ(m1.bodies.size(), 1u);                 // Kapak consumed by the Combine
    EXPECT_EQ(m1.datumPlanes.count("Plane1"), 1u);
    EXPECT_NEAR(pc::model::massProperties(m1.shape).volume, pc::model::massProperties(m2.shape).volume, 1e-6);
  }
}

TEST(AppPkgV2, LintInterferingBodiesWarns) {
  AppDefinition a = twoBodies(5);   // second body overlaps the plate
  a.document.mfg.conv.enabled = true;
  auto vr = pc::mfg::validateChange(a.document, {}, a.document.mfg);
  auto l = lintApp(a);
  EXPECT_TRUE(lintHasErrors(l));   // interference is also an mfg Error on the defaults
  for (auto& st : vr.model.status) if (!st.ok) ADD_FAILURE() << st.featureId << ": " << st.message;
  ASSERT_EQ(vr.model.bodies.size(), 2u);
  EXPECT_TRUE(hasRule(vr.report, "BODY_INTERFERENCE"));
  EXPECT_TRUE(hasWarn(l, "gövde")) << "interference not linted";
}

TEST(AppPkgV2, LintSplitBodiesWarns) {
  AppDefinition a = twoBodies(60);  // disjoint second body
  auto vr = pc::mfg::validateChange(a.document, {}, a.document.mfg);
  auto l = lintApp(a);
  EXPECT_FALSE(lintHasErrors(l));
  if (hasRule(vr.report, "MULTI_BODY")) EXPECT_TRUE(hasWarn(l, "gövde")) << "multi-body not linted";
}

TEST(AppPkgV2, LintSingleBodyNoBodyWarning) {
  AppDefinition a = planeCombine();   // combined into a single body
  auto m = pc::model::regenerate(a.document);
  ASSERT_TRUE(m.ok);
  ASSERT_EQ(m.bodies.size(), 1u);
  auto l = lintApp(a);
  EXPECT_FALSE(hasWarn(l, "gövde"));
  EXPECT_FALSE(lintHasErrors(l));
}
