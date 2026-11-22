// Tests for the v2 multi-body awareness of src/mfg (Agent C2).
#include "pc/Expression.h"
#include "pc/MfgChecks.h"

#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
// Performance budgets are tuned on Linux; the Windows (MSYS2 CLANG64) OCCT build measures ~1.5x slower.
#ifdef _WIN32
static constexpr double kPerfScale = 2.0;
#else
static constexpr double kPerfScale = 1.0;
#endif


using namespace pc;
using namespace pc::mfg;

namespace {

Parameter par(const std::string& n, const std::string& e) {
  Parameter p;
  p.name = n;
  p.expression = e;
  p.exposed = true;
  p.minValue = 0.01;
  p.maxValue = 1000;
  return p;
}
Feature sketchF(const std::string& id, Sketch sk) {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  f.sketch = std::move(sk);
  return f;
}
Feature feat(const std::string& id, FeatureType t, std::map<std::string, std::string> props, std::vector<std::string> refs = {}) {
  Feature f;
  f.id = id;
  f.type = t;
  f.props = std::move(props);
  f.refs = std::move(refs);
  return f;
}
Sketch rectSketch(double x0, double y0, double w, double h, const std::string& W, const std::string& H) {
  Sketch sk = Sketch::create();
  sk.plane.kind = SketchPlaneKind::XY;
  auto L = sk.addRectangle(x0, y0, x0 + w, y0 + h);
  int p1 = sk.entity(L[0])->points[0];
  sk.addConstraint(SkConstraintType::DistanceX, {0, p1}, std::to_string(x0));
  sk.addConstraint(SkConstraintType::DistanceY, {0, p1}, std::to_string(y0));
  sk.addConstraint(SkConstraintType::Length, {L[0]}, W);
  sk.addConstraint(SkConstraintType::Length, {L[1]}, H);
  return sk;
}

// Extrude1 = 20x20xT1 body, Extrude2 (+extra props) = 20x20xT2 box placed at x = 30
Document twoBoxDoc(std::map<std::string, std::string> p2, const std::string& T2 = "10") {
  Document d;
  d.name = "TwoBox";
  d.params.add(par("T1", "10"));
  d.params.add(par("T2", T2));
  d.features.push_back(sketchF("Sketch1", rectSketch(0, 0, 20, 20, "20", "20")));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "T1"}, {"op", "new"}}));
  d.features.push_back(sketchF("Sketch2", rectSketch(30, 0, 20, 20, "20", "20")));
  std::map<std::string, std::string> pr{{"sketch", "Sketch2"}, {"distance", "T2"}};
  for (auto& kv : p2) pr[kv.first] = kv.second;
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, pr));
  return d;
}

struct BoxSpec {
  std::string name, createdBy;
  double x, y, z, dx, dy, dz;
};

model::ModelResult makeResult(const std::vector<BoxSpec>& boxes, bool fillBodies = true) {
  model::ModelResult mr;
  mr.ok = true;
  BRep_Builder bb;
  TopoDS_Compound comp;
  bb.MakeCompound(comp);
  for (auto& b : boxes) {
    TopoDS_Shape so = BRepPrimAPI_MakeBox(gp_Pnt(b.x, b.y, b.z), b.dx, b.dy, b.dz).Solid();
    bb.Add(comp, so);
    if (fillBodies) {
      model::BodyInfo bi;
      bi.name = b.name;
      bi.createdBy = b.createdBy;
      bi.shape = so;
      mr.bodies.push_back(bi);
    }
  }
  mr.shape = comp;
  return mr;
}

const Issue* find(const Report& r, const std::string& rule) {
  for (auto& i : r.issues)
    if (i.ruleId == rule) return &i;
  return nullptr;
}
int countRule(const Report& r, const std::string& rule) {
  int n = 0;
  for (auto& i : r.issues) n += i.ruleId == rule;
  return n;
}

}  // namespace

TEST(MfgV2, OverlappingBodiesAreAnError) {
  Document d = twoBoxDoc({{"op", "new"}, {"bodyName", "Pin"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Pin", "Extrude2", 10, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(r, "MODEL.BODY_INTERFERENCE");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Error);
  EXPECT_EQ(i->featureId, "Extrude2");
  EXPECT_EQ(i->body, "Pin");
  EXPECT_NEAR(i->actual, 10 * 20 * 10, 1e-3);
  EXPECT_NE(i->message.find("Extrude2"), std::string::npos);
  EXPECT_FALSE(i->params.empty());
  EXPECT_EQ(countRule(r, "AM.BODY_GAP"), 0);  // overlap is reported, not a gap
}

TEST(MfgV2, NearBodiesWarnAboutGap) {
  Document d = twoBoxDoc({{"op", "new"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude2", "Extrude2", 20.1, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(r, "AM.BODY_GAP");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Warning);
  EXPECT_NEAR(i->actual, 0.1, 1e-6);
  EXPECT_NEAR(i->limit, d.mfg.am.minGap, 1e-12);
  EXPECT_EQ(countRule(r, "MODEL.BODY_INTERFERENCE"), 0);
  // 1 mm apart: fine
  auto mr2 = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude2", "Extrude2", 21, 0, 0, 20, 20, 10}});
  EXPECT_EQ(countRule(checkGeometric(d, mr2, d.mfg), "AM.BODY_GAP"), 0);
  // FDM: gap rule is for powder/resin processes only
  MfgSettings s = d.mfg;
  s.am = amPreset(AmProcess::FDM_Polymer);
  EXPECT_EQ(countRule(checkGeometric(d, mr, s), "AM.BODY_GAP"), 0);
}

TEST(MfgV2, IntentionalBodiesAreInfoAndSeparateParts) {
  Document d = twoBoxDoc({{"op", "new"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude2", "Extrude2", 30, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* mb = find(r, "MODEL.MULTI_BODY");
  ASSERT_TRUE(mb);
  EXPECT_EQ(mb->severity, Severity::Info);
  EXPECT_NE(mb->message.find("2 gövde"), std::string::npos);
  const Issue* sp = find(r, "CNC.SEPARATE_PARTS");
  ASSERT_TRUE(sp);
  EXPECT_EQ(sp->severity, Severity::Info);
  EXPECT_FALSE(r.hasWarnings());
  EXPECT_FALSE(r.hasErrors());
  // conventional off -> no SEPARATE_PARTS
  MfgSettings s = d.mfg;
  s.conv.enabled = false;
  EXPECT_EQ(countRule(checkGeometric(d, mr, s), "CNC.SEPARATE_PARTS"), 0);
}

TEST(MfgV2, KeepToolBodyIsIntentional) {
  Document d = twoBoxDoc({{"op", "cut"}, {"keepTool", "1"}, {"targets", "Extrude1"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude2.tool", "Extrude2", 30, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* mb = find(r, "MODEL.MULTI_BODY");
  ASSERT_TRUE(mb);
  EXPECT_EQ(mb->severity, Severity::Info);
}

TEST(MfgV2, SplitBodyStaysWarning) {
  Document d = twoBoxDoc({{"op", "cut"}});
  auto mr = makeResult({{"Extrude1:1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude1:2", "Extrude1", 40, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* mb = find(r, "MODEL.MULTI_BODY");
  ASSERT_TRUE(mb);
  EXPECT_EQ(mb->severity, Severity::Warning);
  EXPECT_NE(mb->message.find("Extrude1:2"), std::string::npos);
}

TEST(MfgV2, UnintendedAutoBodyIsWarning) {
  // a join that touches nothing becomes a body named by its feature id, without op=new -> unintended
  Document d = twoBoxDoc({{"op", "join"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Extrude2", "Extrude2", 30, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* mb = find(r, "MODEL.MULTI_BODY");
  ASSERT_TRUE(mb);
  EXPECT_EQ(mb->severity, Severity::Warning);
}

TEST(MfgV2, SingleBodyKeepsOldBehaviour) {
  Document d = twoBoxDoc({{"op", "join"}});
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}});
  Report r = checkGeometric(d, mr, d.mfg);
  EXPECT_EQ(countRule(r, "MODEL.MULTI_BODY"), 0);
  EXPECT_EQ(countRule(r, "CNC.SEPARATE_PARTS"), 0);
  EXPECT_EQ(r.issues.size(), 0u);
}

TEST(MfgV2, WallThicknessIsPerBodyAndNamed) {
  Document d = twoBoxDoc({{"op", "new"}, {"bodyName", "Foil"}}, "0.2");
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 20, 20, 10}, {"Foil", "Extrude2", 30, 0, 0, 20, 20, 0.2}});
  Report r = checkGeometric(d, mr, d.mfg);
  int walls = 0;
  for (auto& i : r.issues)
    if (i.ruleId == "AM.MIN_WALL" || i.ruleId == "CNC.MIN_WALL") {
      ++walls;
      EXPECT_EQ(i.body, "Foil");
      EXPECT_NE(i.message.find("Foil"), std::string::npos);
      EXPECT_NEAR(i.actual, 0.2, 1e-3);
    }
  EXPECT_GE(walls, 1);
}

TEST(MfgV2, EnvelopePerBodyAndWholeBuild) {
  Document d = twoBoxDoc({{"op", "new"}});
  MfgSettings s = d.mfg;
  s.conv.enabled = false;
  s.am.buildX = 250;
  s.am.buildY = 250;
  // each 200 x 200 body fits, but side by side they span 410 mm
  auto mr = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 200, 200, 10}, {"Extrude2", "Extrude2", 210, 0, 0, 200, 200, 10}});
  Report r = checkGeometric(d, mr, s);
  const Issue* e = find(r, "AM.BUILD_ENVELOPE");
  ASSERT_TRUE(e);
  EXPECT_EQ(e->body, "");  // whole build
  EXPECT_NE(e->message.find("Tüm yapı"), std::string::npos);
  EXPECT_EQ(countRule(r, "AM.BUILD_ENVELOPE"), 1);
  // one body too big on its own
  auto mr2 = makeResult({{"Extrude1", "Extrude1", 0, 0, 0, 300, 20, 10}, {"Extrude2", "Extrude2", 0, 100, 0, 20, 20, 10}});
  Report r2 = checkGeometric(d, mr2, s);
  bool perBody = false;
  for (auto& i : r2.issues)
    if (i.ruleId == "AM.BUILD_ENVELOPE" && i.body == "Extrude1") perBody = true;
  EXPECT_TRUE(perBody);
}

TEST(MfgV2, FallbackWithoutBodiesUsesSolids) {
  Document d = twoBoxDoc({{"op", "new"}});
  auto mr = makeResult({{"", "", 0, 0, 0, 20, 20, 10}, {"", "", 10, 0, 0, 20, 20, 10}}, /*fillBodies=*/false);
  ASSERT_TRUE(mr.bodies.empty());
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(r, "MODEL.BODY_INTERFERENCE");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->body, "Body2");
  const Issue* mb = find(r, "MODEL.MULTI_BODY");
  ASSERT_TRUE(mb);
  EXPECT_EQ(mb->severity, Severity::Info);  // the document has an explicit op=new
}

TEST(MfgV2, AnalyticIgnoresPlaneAndCombineAndTargets) {
  Document d = twoBoxDoc({{"op", "new"}, {"bodyName", "B"}}, "0.2");
  d.features.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "faceOffset"}, {"offset", "5"}}, {"Extrude1.end"}));
  d.features.push_back(feat("Combine1", FeatureType::Combine, {{"op", "join"}, {"target", "Extrude1"}, {"tools", "B"}, {"keepTools", "1"}}));
  d.features.push_back(sketchF("Sketch3", rectSketch(2, 2, 5, 5, "5", "5")));
  d.features.push_back(feat("Extrude3", FeatureType::Extrude, {{"sketch", "Sketch3"}, {"distance", "3"}, {"op", "cut"}, {"targets", "Extrude1"}}));
  auto t0 = std::chrono::steady_clock::now();
  Report r;
  for (int k = 0; k < 10; ++k) r = checkAnalytic(d, d.evaluateParams({}), d.mfg);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 10;
  EXPECT_LT(ms, 20.0 * kPerfScale);
  for (auto& i : r.issues) EXPECT_NE(i.featureId, "Plane1");
  // the thin body B is attributed and named
  bool named = false;
  for (auto& i : r.issues)
    if (i.featureId == "Extrude2" && i.body == "B" && i.message.find("Gövde B") == 0) named = true;
  EXPECT_TRUE(named);
}

TEST(MfgV2, AnalyticPlaneOnlyFeatureHasNoMaterial) {
  Document d = twoBoxDoc({{"op", "join"}});
  Report base = checkAnalytic(d, d.evaluateParams({}), d.mfg);
  d.features.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "faceOffset"}}, {"Extrude1.end"}));
  Report with = checkAnalytic(d, d.evaluateParams({}), d.mfg);
  EXPECT_EQ(base.issues.size(), with.issues.size());
}

TEST(MfgV2, BracketSampleStillClean) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  ValidationResult vr = validateChange(d, {}, d.mfg);
  EXPECT_FALSE(vr.report.hasErrors());
  EXPECT_FALSE(vr.report.hasWarnings());
}

TEST(MfgV2, MultiBodySample) {
  std::string path = std::string(PC_SAMPLES_DIR) + "/v2_multibody.pcad";
  if (!std::ifstream(path).good()) GTEST_SKIP() << "samples/v2_multibody.pcad not available yet";
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, path, &err)) << err;
  ValidationResult vr = validateChange(d, {}, d.mfg);
  ASSERT_TRUE(vr.regenerated);
  for (auto& i : vr.report.issues) EXPECT_NE(i.ruleId, "MODEL.FEATURE_FAILED") << i.message;
  if (vr.model.bodies.size() > 1) {
    const Issue* mb = find(vr.report, "MODEL.MULTI_BODY");
    ASSERT_TRUE(mb);
    for (auto& i : vr.report.issues)
      if (!i.faces.empty()) EXPECT_FALSE(i.body.empty()) << i.ruleId;
  }
}
