// Tests for src/mfg (Agent C): DfAM / DfM analytic + geometric rules, suggestions, pipeline.
#include "pc/Expression.h"
#include "pc/MfgChecks.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <iostream>
// Performance budgets are tuned on Linux; the Windows (MSYS2 CLANG64) OCCT build measures ~1.5x slower.
#ifdef _WIN32
static constexpr double kPerfScale = 2.0;
#else
static constexpr double kPerfScale = 1.0;
#endif


using namespace pc;
using namespace pc::mfg;

namespace {

Document loadSample(const std::string& name) {
  Document d;
  std::string err;
  EXPECT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/" + name, &err)) << err;
  return d;
}

[[maybe_unused]] void dump(const Report& r) {
  for (auto& i : r.issues) {
    std::cout << "[" << toString(i.severity) << "] " << i.ruleId << " f=" << i.featureId << " p=";
    for (auto& p : i.params) std::cout << p << ",";
    if (i.suggestedValue) std::cout << " sugg=" << *i.suggestedValue;
    std::cout << " act=" << i.actual << " lim=" << i.limit << " faces=" << i.faces.size() << " :: " << i.message << "\n";
  }
}

const Issue* find(const Report& r, const std::string& rule, const std::string& fid = "*") {
  for (auto& i : r.issues)
    if (i.ruleId == rule && (fid == "*" || i.featureId == fid)) return &i;
  return nullptr;
}
int countRule(const Report& r, const std::string& rule) {
  int n = 0;
  for (auto& i : r.issues) n += i.ruleId == rule;
  return n;
}

Report analytic(const Document& d, const std::map<std::string, std::string>& ov = {}) {
  return checkAnalytic(d, d.evaluateParams(ov), d.mfg);
}

// ---------------------------------------------------------------- document builders
Parameter par(const std::string& n, const std::string& e, bool exposed = true, double mn = 0.01, double mx = 1000) {
  Parameter p;
  p.name = n;
  p.expression = e;
  p.exposed = exposed;
  p.minValue = mn;
  p.maxValue = mx;
  return p;
}

// rectangle with corner (x0,y0) and size w x h, all expressions; returns line ids bottom,right,top,left
Sketch rectSketch(SketchPlaneKind kind, const std::string& face, const std::string& x0, const std::string& y0,
                  const std::string& w, const std::string& h, double gx0, double gy0, double gw, double gh,
                  std::vector<int>* lines = nullptr) {
  Sketch sk = Sketch::create();
  sk.plane.kind = kind;
  sk.plane.faceRef = face;
  auto L = sk.addRectangle(gx0, gy0, gx0 + gw, gy0 + gh);
  int p1 = sk.entity(L[0])->points[0];
  sk.addConstraint(SkConstraintType::DistanceX, {0, p1}, x0);
  sk.addConstraint(SkConstraintType::DistanceY, {0, p1}, y0);
  sk.addConstraint(SkConstraintType::Length, {L[0]}, w);
  sk.addConstraint(SkConstraintType::Length, {L[1]}, h);
  if (lines) *lines = L;
  return sk;
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
std::string edgeOf(const std::string& a, const std::string& b) {
  return a < b ? "edge(" + a + "|" + b + ")" : "edge(" + b + "|" + a + ")";
}

// plate 60 x 40 x TP with a centred rectangular pocket PW x PH x PD (cut from the top), optional fillet RP
Document pocketDoc(bool fillet, const std::string& TP = "10", const std::string& PD = "6", const std::string& PW = "30",
                   const std::string& PH = "20", const std::string& RP = "2") {
  Document d;
  d.name = "Pocket";
  d.params.add(par("TP", TP));
  d.params.add(par("PD", PD));
  d.params.add(par("PW", PW));
  d.params.add(par("PH", PH));
  d.params.add(par("RP", RP));
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-30", "-20", "60", "40", -30, -20, 60, 40)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "TP"}, {"op", "new"}}));
  std::vector<int> L;
  d.features.push_back(sketchF("Sketch2", rectSketch(SketchPlaneKind::OnFace, "Extrude1.end", "-PW/2", "-PH/2", "PW", "PH",
                                                     -15, -10, 30, 20, &L)));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "PD"}, {"direction", "reverse"}, {"op", "cut"}}));
  if (fillet) {
    std::vector<std::string> refs;
    for (int i = 0; i < 4; ++i)
      refs.push_back(edgeOf("Extrude2.side.e" + std::to_string(L[i]), "Extrude2.side.e" + std::to_string(L[(i + 1) % 4])));
    d.features.push_back(feat("Fillet1", FeatureType::Fillet, {{"radius", "RP"}}, refs));
  }
  return d;
}

// block 40 x 40 x 40 (XY, centred) + horizontal hole from the +X side face
Document crossBoreDoc(const std::string& DX = "10") {
  Document d;
  d.name = "CrossBore";
  d.params.add(par("DX", DX));
  std::vector<int> L;
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-20", "-20", "40", "40", -20, -20, 40, 40, &L)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "40"}}));
  // L[1] = right line (x = +20): frame u = +Y, v = +Z, origin (20,0,0)
  Sketch s2 = Sketch::create();
  s2.plane.kind = SketchPlaneKind::OnFace;
  s2.plane.faceRef = "Extrude1.side.e" + std::to_string(L[1]);
  int p = s2.addPoint(0.0, 20.0);
  s2.addConstraint(SkConstraintType::DistanceX, {0, p}, "0");
  s2.addConstraint(SkConstraintType::DistanceY, {0, p}, "20");
  d.features.push_back(sketchF("Sketch2", s2));
  d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "DX"}, {"through", "1"}}));
  return d;
}

// base 20 x 20 x 10 with a 60 x 20 x 5 slab on top (cantilevers of 20 mm on both sides)
Document overhangDoc() {
  Document d;
  d.name = "Overhang";
  d.params.add(par("OL", "60"));
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-10", "-10", "20", "20", -10, -10, 20, 20)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "10"}}));
  d.features.push_back(sketchF("Sketch2", rectSketch(SketchPlaneKind::OnFace, "Extrude1.end", "-OL/2", "-10", "OL", "20",
                                                     -30, -10, 60, 20)));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "5"}, {"op", "join"}}));
  return d;
}

// box 40 x 40 x 30 shelled with thickness ST (top face removed unless closed)
Document shellDoc(const std::string& ST, bool closed) {
  Document d;
  d.name = "Shell";
  d.params.add(par("ST", ST));
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-20", "-20", "40", "40", -20, -20, 40, 40)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "30"}}));
  std::vector<std::string> refs;
  if (!closed) refs.push_back("Extrude1.end");
  d.features.push_back(feat("Shell1", FeatureType::Shell, {{"thickness", "ST"}}, refs));
  return d;
}

// block 40 x 40 x 20 with an internal cylindrical void (sketch at z = 10, symmetric cut 6 mm)
Document voidDoc() {
  Document d;
  d.name = "Void";
  d.params.add(par("VD", "10"));
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-20", "-20", "40", "40", -20, -20, 40, 40)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "20"}}));
  Sketch s2 = Sketch::create();
  s2.plane.kind = SketchPlaneKind::XY;
  s2.plane.offsetExpr = "10";
  int c = s2.addCircle(0.0, 0.0, 5.0);
  s2.addConstraint(SkConstraintType::Coincident, {0, s2.entity(c)->points[0]});
  s2.addConstraint(SkConstraintType::Diameter, {c}, "VD");
  d.features.push_back(sketchF("Sketch2", s2));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "6"}, {"direction", "symmetric"}, {"op", "cut"}}));
  return d;
}

// block 40 x 40 x 30 with a blind rectangular pocket cut horizontally from the -Y side face (undercut for 3-axis)
Document sidePocketDoc() {
  Document d;
  d.name = "SidePocket";
  d.params.add(par("SD", "15"));
  std::vector<int> L;
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-20", "-20", "40", "40", -20, -20, 40, 40, &L)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "30"}}));
  // XZ plane at y = 0 has normal -Y; offset 20 moves it to y = -20 (the bottom side face)
  d.features.push_back(sketchF("Sketch2", rectSketch(SketchPlaneKind::XZ, "", "-10", "10", "20", "10", -10, 10, 20, 10)));
  d.features.back().sketch->plane.offsetExpr = "20";
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "SD"}, {"direction", "reverse"}, {"op", "cut"}}));
  return d;
}

}  // namespace

// ================================================================ basic API
TEST(MfgApi, ToStringAndReport) {
  EXPECT_STREQ(toString(Severity::Error), "Hata");
  EXPECT_STREQ(toString(Domain::Additive), "Katmanlı İmalat");
  Report r;
  Issue a;
  a.severity = Severity::Warning;
  r.issues.push_back(a);
  EXPECT_FALSE(r.hasErrors());
  EXPECT_TRUE(r.hasWarnings());
  a.severity = Severity::Error;
  r.issues.push_back(a);
  EXPECT_TRUE(r.hasErrors());
  EXPECT_EQ(r.count(Severity::Error), 1);
}

TEST(MfgApi, Presets) {
  auto l = amPreset(AmProcess::LPBF_Metal);
  EXPECT_NEAR(l.minWallThickness, 0.4, 1e-12);
  EXPECT_NEAR(l.maxSelfSupportingHoleDia, 8, 1e-12);
  EXPECT_NEAR(l.minOverhangAngle, 45, 1e-12);
  EXPECT_GE(l.minEscapeHoleDia, 2);
  auto f = amPreset(AmProcess::FDM_Polymer);
  EXPECT_GE(f.minWallThickness, 0.8);
  EXPECT_LE(f.minWallThickness, 1.2);
  EXPECT_GE(f.minHoleDiameter, 2);
  auto sls = amPreset(AmProcess::SLS_Polymer);
  EXPECT_EQ(sls.minOverhangAngle, 0);
  EXPECT_GE(sls.minEscapeHoleDia, 3.5);
  EXPECT_NEAR(sls.minWallThickness, 0.7, 1e-12);
  EXPECT_GT(amPreset(AmProcess::DED_Metal).minWallThickness, l.minWallThickness);
  EXPECT_GT(amPreset(AmProcess::EBM_Metal).minWallThickness, l.minWallThickness);
  EXPECT_FALSE(amPreset(AmProcess::None).enabled);
  auto m3 = convPreset(ConvProcess::CNC_Milling3Axis);
  EXPECT_NEAR(m3.maxToolLengthToDia, 4, 1e-12);
  EXPECT_NEAR(m3.maxHoleDepthToDia, 10, 1e-12);
  EXPECT_NEAR(m3.minWallThickness, 0.8, 1e-12);
  EXPECT_GT(convPreset(ConvProcess::CNC_Milling5Axis).maxToolLengthToDia, m3.maxToolLengthToDia);
  EXPECT_EQ(convPreset(ConvProcess::CNC_Turning).process, ConvProcess::CNC_Turning);
  EXPECT_FALSE(convPreset(ConvProcess::None).enabled);
}

TEST(MfgApi, Drills) {
  const auto& v = standardDrillSizes();
  auto has = [&](double x) { return std::any_of(v.begin(), v.end(), [&](double y) { return std::fabs(x - y) < 1e-9; }); };
  for (double x : {0.5, 1.1, 2.9, 3.0, 3.4, 4.5, 5.5, 6.6, 9.0, 11.0, 13.5, 17.5, 19.5, 20.0})
    EXPECT_TRUE(has(x)) << x;
  EXPECT_FALSE(has(6.3));
  EXPECT_TRUE(std::is_sorted(v.begin(), v.end()));
  EXPECT_NEAR(nearestStandardDrill(6.3), 6.5, 1e-9);
  EXPECT_NEAR(nearestStandardDrill(6.6), 6.6, 1e-9);
  EXPECT_NEAR(nearestStandardDrill(2.04), 2.0, 1e-9);
  EXPECT_NEAR(nearestStandardDrill(13.4), 13.5, 1e-9);
  EXPECT_NEAR(nearestStandardDrill(1.27), 1.3, 1e-9);
}

TEST(MfgApi, SolveParamFor) {
  ParamValues v{{"T", 0.25}, {"W", 80}};
  auto r = solveParamFor("T/2", "T", v, 0.4);
  ASSERT_TRUE(r);
  EXPECT_NEAR(*r, 0.8, 1e-9);
  r = solveParamFor("W/2 - 10 + T", "W", v, 25);
  ASSERT_TRUE(r);
  EXPECT_NEAR(*r, 69.5, 1e-9);
  r = solveParamFor("100/T", "T", v, 10);  // decreasing
  ASSERT_TRUE(r);
  EXPECT_NEAR(*r, 10, 1e-9);
  r = solveParamFor("T*T", "T", v, 2500);  // far from the current value
  ASSERT_TRUE(r);
  EXPECT_NEAR(*r, 50, 1e-6);
  EXPECT_FALSE(solveParamFor("W/2", "T", v, 3));          // T does not influence
  EXPECT_FALSE(solveParamFor("(T-5)^2", "T", v, -1));     // unreachable
  EXPECT_FALSE(solveParamFor("1 + 0*T", "T", v, 3));      // constant
}

// ================================================================ checkParameters
TEST(MfgParams, RangeCountEval) {
  Document d = loadSample("bracket.pcad");
  auto vals = d.evaluateParams({{"W", "250"}});
  Report r = checkParameters(d, vals);
  const Issue* i = find(r, "PARAM.RANGE");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Error);
  EXPECT_EQ(i->params.at(0), "W");
  ASSERT_TRUE(i->suggestedValue);
  EXPECT_NEAR(*i->suggestedValue, 200, 1e-12);
  EXPECT_TRUE(checkParameters(d, d.evaluateParams()).issues.empty());

  Parameter n = par("N", "2.5");
  n.kind = ParamKind::Count;
  n.minValue = 1;
  n.maxValue = 10;
  d.params.add(n);
  r = checkParameters(d, d.evaluateParams());
  i = find(r, "PARAM.COUNT");
  ASSERT_TRUE(i);
  ASSERT_TRUE(i->suggestedValue);
  EXPECT_NEAR(std::fabs(*i->suggestedValue - 2.5), 0.5, 1e-12);

  // missing value -> evaluation error
  auto v2 = d.evaluateParams();
  v2.erase("Lbore");
  r = checkParameters(d, v2);
  EXPECT_TRUE(find(r, "PARAM.EVAL"));

  // feature property referencing an unknown parameter
  Document d2 = loadSample("bracket.pcad");
  d2.feature("Fillet1")->props["radius"] = "Rx";
  r = checkParameters(d2, d2.evaluateParams());
  i = find(r, "PARAM.FEATURE_EXPR", "Fillet1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->params.at(0), "Rx");
}

TEST(MfgPipeline, ParameterErrorsStopBeforeRegenerate) {
  Document d = loadSample("bracket.pcad");
  auto vr = validateChange(d, {{"T", "foo+1"}}, d.mfg);
  EXPECT_FALSE(vr.regenerated);
  EXPECT_TRUE(find(vr.report, "PARAM.EVAL"));
  EXPECT_FALSE(vr.acceptable());
  vr = validateChange(d, {{"W", "250"}}, d.mfg);
  EXPECT_FALSE(vr.regenerated);
  EXPECT_TRUE(find(vr.report, "PARAM.RANGE"));
  EXPECT_NEAR(vr.values.at("W"), 250, 1e-12);
}

// ================================================================ bracket sample (analytic)
TEST(MfgBracket, DefaultHasNoErrorsOrWarnings) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d);
  dump(r);
  EXPECT_FALSE(r.hasErrors());
  EXPECT_FALSE(r.hasWarnings());
  auto vr = validateChange(d, {}, d.mfg);
  dump(vr.report);
  EXPECT_TRUE(vr.regenerated);
  EXPECT_TRUE(vr.model.ok);
  EXPECT_FALSE(vr.report.hasErrors());
  EXPECT_TRUE(vr.acceptable());
}

TEST(MfgBracket, ThinPlate) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"T", "0.2"}});
  const Issue* a = find(r, "AM.MIN_WALL", "Extrude1");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->severity, Severity::Error);
  EXPECT_EQ(a->domain, Domain::Additive);
  EXPECT_EQ(a->params.at(0), "T");
  ASSERT_TRUE(a->suggestedValue);
  EXPECT_GE(*a->suggestedValue, 0.4 - 1e-9);
  EXPECT_LE(*a->suggestedValue, 0.45);
  EXPECT_NEAR(a->actual, 0.2, 1e-6);
  EXPECT_NE(a->message.find("0.20 mm < LPBF min. et kalınlığı 0.40 mm (Ti-6Al-4V)"), std::string::npos) << a->message;
  EXPECT_NE(a->message.find("T ≥ 0.40 mm önerilir"), std::string::npos) << a->message;
  const Issue* c = find(r, "CNC.MIN_WALL", "Extrude1");
  ASSERT_TRUE(c);
  EXPECT_NEAR(*c->suggestedValue, 0.8, 1e-9);
  // passing case: T = 0.5 ok for AM, still too thin for CNC (0.8)
  r = analytic(d, {{"T", "0.5"}});
  EXPECT_FALSE(find(r, "AM.MIN_WALL"));
  EXPECT_TRUE(find(r, "CNC.MIN_WALL"));
}

TEST(MfgBracket, NonStandardDrill) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"Dh", "6.3"}});
  const Issue* i = find(r, "CNC.STD_DRILL", "Hole1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Info);
  EXPECT_EQ(i->params.at(0), "Dh");
  ASSERT_TRUE(i->suggestedValue);
  EXPECT_NEAR(*i->suggestedValue, 6.5, 1e-9);
  EXPECT_FALSE(r.hasErrors());
  EXPECT_FALSE(find(analytic(d), "CNC.STD_DRILL"));  // 6.6 = M6 clearance
}

TEST(MfgBracket, ConvexCornerFilletIsNotAToolRadiusProblem) {
  // The plate's corner fillets are CONVEX outer edges: any end mill contours them, so R=1 must not
  // raise CNC.FILLET_TOOL_RADIUS (the internal-corner rule is tested on the pocket model below).
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"R", "1"}});
  EXPECT_FALSE(find(r, "CNC.FILLET_TOOL_RADIUS"));
  EXPECT_FALSE(r.hasErrors());
  // below the AM min feature size it is flagged
  r = analytic(d, {{"R", "0.2"}});
  const Issue* i = find(r, "AM.MIN_FEATURE", "Fillet1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->params.at(0), "R");
  EXPECT_NEAR(*i->suggestedValue, 0.3, 1e-9);
}

TEST(MfgBracket, DrillDepthToDiameter) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"Dbore", "1.5"}});
  const Issue* i = find(r, "CNC.HOLE_LD", "Hole2");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Error);
  EXPECT_EQ(i->params.at(0), "Dbore");
  ASSERT_TRUE(i->suggestedValue);
  double L = d.evaluateParams().at("Lbore");
  EXPECT_GE(*i->suggestedValue, L / 10 - 1e-9);
  EXPECT_LE(*i->suggestedValue, L / 10 + 0.06);
  EXPECT_NEAR(i->actual, L / 1.5, 1e-6);
  EXPECT_FALSE(find(analytic(d, {{"Dbore", "3"}}), "CNC.HOLE_LD"));
  // AM: a 1.5 mm blind hole cannot be de-powdered
  EXPECT_TRUE(find(r, "AM.BLIND_HOLE", "Hole2"));
}

TEST(MfgBracket, DerivedDriverChainsToExposedParameter) {
  // Dbore hidden: the only exposed way to fix L/D is through Lbore = Hb + T - 4
  Document d = loadSample("bracket.pcad");
  d.params.find("Dbore")->exposed = false;
  Report r = analytic(d, {{"Dbore", "1.5"}});
  const Issue* i = find(r, "CNC.HOLE_LD", "Hole2");
  ASSERT_TRUE(i);
  ASSERT_FALSE(i->params.empty());
  EXPECT_TRUE(d.params.find(i->params[0])->exposed) << i->params[0];
  ASSERT_TRUE(i->suggestedValue);
  auto vals = d.evaluateParams({{"Dbore", "1.5"}, {i->params[0], formatNumber(*i->suggestedValue)}});
  EXPECT_LE(vals.at("Lbore") / 1.5, 10 + 1e-9);
  EXPECT_GE(vals.at("Lbore") / 1.5, 9.5);
  EXPECT_NE(std::find(i->params.begin(), i->params.end(), "Lbore"), i->params.end());
  EXPECT_NE(i->message.find(i->params[0] + " ≤ "), std::string::npos) << i->message;
}

TEST(MfgBracket, BossWall) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"Db", "11"}, {"Dbore", "10"}});
  const Issue* c = find(r, "CNC.MIN_WALL", "Hole2");
  ASSERT_TRUE(c);
  EXPECT_NEAR(c->actual, 0.5, 1e-6);
  EXPECT_EQ(c->params.at(0), "Dbore");
  EXPECT_NE(std::find(c->params.begin(), c->params.end(), "Db"), c->params.end());
  ASSERT_TRUE(c->suggestedValue);
  EXPECT_NEAR(*c->suggestedValue, 9.4, 1e-9);
  EXPECT_NE(c->message.find("Dbore ≤ 9.40 mm"), std::string::npos) << c->message;
  EXPECT_FALSE(find(r, "AM.MIN_WALL"));  // 0.5 >= LPBF 0.4
  // the 20 mm high, 0.5 mm wall is slender for both processes
  EXPECT_TRUE(find(r, "AM.ASPECT_RATIO", "Hole2"));
  EXPECT_TRUE(find(r, "CNC.WALL_RATIO", "Hole2"));
  // AM fails below 0.4
  r = analytic(d, {{"Db", "10.6"}, {"Dbore", "10"}});
  EXPECT_TRUE(find(r, "AM.MIN_WALL", "Hole2"));
}

TEST(MfgBracket, HoleToEdgeLigament) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"E", "3.5"}});  // ligament = E - Dh/2 = 0.2
  const Issue* a = find(r, "AM.MIN_WALL", "Hole1");
  ASSERT_TRUE(a);
  EXPECT_NEAR(a->actual, 0.2, 1e-6);
  EXPECT_EQ(a->params.at(0), "Dh");
  EXPECT_NE(std::find(a->params.begin(), a->params.end(), "E"), a->params.end());
  EXPECT_NEAR(*a->suggestedValue, 6.2, 1e-9);
  EXPECT_FALSE(find(analytic(d, {{"E", "4"}}), "AM.MIN_WALL"));  // 0.7 mm ligament ok for LPBF
}

TEST(MfgBracket, Envelope) {
  Document d = loadSample("bracket.pcad");
  Report r = analytic(d, {{"W", "400"}});
  const Issue* a = find(r, "AM.BUILD_ENVELOPE");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->severity, Severity::Error);
  EXPECT_EQ(a->params.at(0), "W");
  EXPECT_NEAR(*a->suggestedValue, 250, 1e-9);
  const Issue* c = find(r, "CNC.STOCK_ENVELOPE");
  ASSERT_TRUE(c);
  EXPECT_NEAR(*c->suggestedValue, 300, 1e-9);
  // tall boss exceeds the 150 mm machine Z, not the 300 mm build height
  r = analytic(d, {{"Hb", "160"}});
  EXPECT_FALSE(find(r, "AM.BUILD_ENVELOPE"));
  c = find(r, "CNC.STOCK_ENVELOPE");
  ASSERT_TRUE(c);
  EXPECT_EQ(c->params.at(0), "Hb");
}

TEST(MfgBracket, BlindHoleTipBreakout) {
  Document d = loadSample("bracket.pcad");
  d.params.setExpression("Lbore", "Hb + T - 2");  // the 3 mm drill tip pierces the 2 mm floor
  Report r = analytic(d);
  const Issue* i = find(r, "HOLE.TIP_BREAKOUT", "Hole2");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Warning);
  ASSERT_TRUE(i->suggestedValue);
  // deterministic, correct direction: the suggested value must fix the problem
  auto vals = d.evaluateParams({{i->params[0], formatNumber(*i->suggestedValue)}});
  Report r2 = checkAnalytic(d, vals, d.mfg);
  EXPECT_FALSE(find(r2, "HOLE.TIP_BREAKOUT")) << i->message;
  EXPECT_FALSE(r2.hasErrors());
  // floor thinner than the CNC wall: 0.5 mm remaining under the tip
  d.params.setExpression("Lbore", "Hb + T - 3.5");
  r = analytic(d);
  i = find(r, "CNC.MIN_WALL", "Hole2");
  ASSERT_TRUE(i);
  EXPECT_NEAR(i->actual, 0.5 - (5 / std::tan(59 * M_PI / 180) - 3), 1e-6);
}

TEST(MfgBracket, ProcessSwitching) {
  Document d = loadSample("bracket.pcad");
  MfgSettings s = d.mfg;
  s.conv.enabled = false;
  Report r = checkAnalytic(d, d.evaluateParams({{"T", "0.5"}}), s);
  EXPECT_FALSE(find(r, "CNC.MIN_WALL"));
  s.am = amPreset(AmProcess::FDM_Polymer);  // FDM min wall 0.8
  r = checkAnalytic(d, d.evaluateParams({{"T", "0.5"}}), s);
  const Issue* i = find(r, "AM.MIN_WALL", "Extrude1");
  ASSERT_TRUE(i);
  EXPECT_NE(i->message.find("FDM"), std::string::npos);
}

TEST(MfgBracket, AnalyticTiming) {
  Document d = loadSample("bracket.pcad");
  auto v = d.evaluateParams();
  checkAnalytic(d, v, d.mfg);
  auto t0 = std::chrono::steady_clock::now();
  const int N = 10;
  for (int i = 0; i < N; ++i) checkAnalytic(d, v, d.mfg);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / N;
  std::cout << "checkAnalytic(bracket) = " << ms << " ms\n";
  EXPECT_LT(ms, 20 * kPerfScale);
  auto v2 = d.evaluateParams({{"E", "3.5"}, {"Db", "11"}});  // several issues with suggestions
  t0 = std::chrono::steady_clock::now();
  checkAnalytic(d, v2, d.mfg);
  ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "checkAnalytic(bracket, 8 issues) = " << ms << " ms\n";
  EXPECT_LT(ms, 60 * kPerfScale);
}

// ================================================================ bracket sample (geometric + pipeline)
TEST(MfgGeometric, BracketPipelineMergesAndTimes) {
  Document d = loadSample("bracket.pcad");
  auto vals = d.evaluateParams();
  auto mr = model::regenerate(d, vals);
  ASSERT_TRUE(mr.ok);
  auto t0 = std::chrono::steady_clock::now();
  Report g = checkGeometric(d, mr, d.mfg);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::cout << "checkGeometric(bracket) = " << ms << " ms\n";
  EXPECT_LT(ms, 500 * kPerfScale);
  EXPECT_FALSE(g.hasErrors());

  auto vr = validateChange(d, {{"Db", "11"}}, d.mfg);
  ASSERT_TRUE(vr.regenerated);
  const Issue* c = find(vr.report, "CNC.MIN_WALL", "Hole2");
  ASSERT_TRUE(c);
  EXPECT_EQ(countRule(vr.report, "CNC.MIN_WALL"), 1);  // analytic + geometric merged
  EXPECT_EQ(c->params.at(0), "Dbore");                // analytic driver kept
  ASSERT_TRUE(c->suggestedValue);
  EXPECT_FALSE(c->faces.empty());
  bool hasBossFace = false;
  for (auto& f : c->faces) hasBossFace |= f.rfind("Extrude2.side", 0) == 0 || f.rfind("Hole2.h0", 0) == 0;
  EXPECT_TRUE(hasBossFace);
  EXPECT_FALSE(vr.acceptable());

  vr = validateChange(d, {{"T", "0.3"}}, d.mfg);
  c = find(vr.report, "AM.MIN_WALL", "Extrude1");
  ASSERT_TRUE(c);
  EXPECT_EQ(c->params.at(0), "T");
  EXPECT_FALSE(c->faces.empty());  // from the B-Rep ray casting
}

// ================================================================ pocket model (CNC internal corners)
TEST(MfgPocket, InternalFilletBelowToolRadius) {
  Document d = pocketDoc(true, "10", "6", "30", "20", "1");
  Report r = analytic(d);
  const Issue* i = find(r, "CNC.FILLET_TOOL_RADIUS", "Fillet1");
  ASSERT_TRUE(i) << "pocket fillet";
  EXPECT_EQ(i->severity, Severity::Error);
  EXPECT_EQ(i->params.at(0), "RP");
  ASSERT_TRUE(i->suggestedValue);
  EXPECT_NEAR(*i->suggestedValue, 1.5, 1e-9);
  EXPECT_EQ(i->edges.size(), 4u);
  EXPECT_FALSE(find(r, "CNC.SHARP_INTERNAL_CORNER"));
  // passing
  EXPECT_FALSE(find(analytic(pocketDoc(true, "10", "6", "30", "20", "2")), "CNC.FILLET_TOOL_RADIUS"));
  // geometric confirms on the B-Rep (concave cylinder R1 parallel to the spindle)
  auto vr = validateChange(d, {}, d.mfg);
  ASSERT_TRUE(vr.model.ok);
  i = find(vr.report, "CNC.FILLET_TOOL_RADIUS", "Fillet1");
  ASSERT_TRUE(i);
  EXPECT_FALSE(i->faces.empty());
  EXPECT_EQ(i->params.at(0), "RP");
}

TEST(MfgPocket, SharpInternalCorners) {
  Document d = pocketDoc(false);
  Report r = analytic(d);
  const Issue* i = find(r, "CNC.SHARP_INTERNAL_CORNER", "Extrude2");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Error);
  EXPECT_EQ(i->edges.size(), 4u);
  EXPECT_FALSE(find(r, "AM.MIN_WALL"));  // AM does not care about sharp internal corners
  auto mr = model::regenerate(d, d.evaluateParams());
  ASSERT_TRUE(mr.ok);
  Report g = checkGeometric(d, mr, d.mfg);
  i = find(g, "CNC.SHARP_INTERNAL_CORNER");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->edges.size(), 4u);
  // filleted pocket has no sharp corners geometrically either
  auto mr2 = model::regenerate(pocketDoc(true), pocketDoc(true).evaluateParams());
  EXPECT_FALSE(find(checkGeometric(pocketDoc(true), mr2, d.mfg), "CNC.SHARP_INTERNAL_CORNER"));
}

TEST(MfgPocket, FloorSlotAndDepth) {
  // floor 0.5 mm: CNC error (0.8), AM ok (0.4)
  Document d = pocketDoc(true, "10", "9.5");
  Report r = analytic(d);
  const Issue* i = find(r, "CNC.MIN_WALL", "Extrude2");
  ASSERT_TRUE(i);
  EXPECT_NEAR(i->actual, 0.5, 1e-6);
  EXPECT_EQ(i->params.at(0), "PD");  // shallower pocket (the pocket's own distance comes first)
  EXPECT_NEAR(*i->suggestedValue, 9.2, 1e-9);
  EXPECT_NE(std::find(i->params.begin(), i->params.end(), "TP"), i->params.end());
  EXPECT_FALSE(find(r, "AM.MIN_WALL"));
  // narrow slot (2 mm < Ø3 tool)
  r = analytic(pocketDoc(true, "10", "6", "30", "2", "0.8"));
  i = find(r, "CNC.SLOT_WIDTH");
  ASSERT_TRUE(i);
  EXPECT_NEAR(i->actual, 2, 1e-6);
  EXPECT_EQ(i->params.at(0), "PH");
  EXPECT_NEAR(*i->suggestedValue, 3, 1e-9);
  // deep pocket: tool limited by the R1.5 corners -> Ø3 x 4 = 12 mm
  r = analytic(pocketDoc(true, "40", "30", "30", "20", "1.5"));
  i = find(r, "CNC.POCKET_DEPTH", "Extrude2");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Warning);
  EXPECT_NEAR(i->limit, 12, 1e-9);
  EXPECT_EQ(i->params.at(0), "PD");
  EXPECT_NEAR(*i->suggestedValue, 12, 1e-9);
  EXPECT_FALSE(find(analytic(pocketDoc(true, "40", "10", "30", "20", "1.5")), "CNC.POCKET_DEPTH"));
}

// ================================================================ AM orientation rules
TEST(MfgAm, HorizontalHole) {
  Document d = crossBoreDoc("10");
  Report r = analytic(d);
  const Issue* i = find(r, "AM.HORIZONTAL_HOLE", "Hole1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Warning);
  EXPECT_EQ(i->params.at(0), "DX");
  EXPECT_NEAR(*i->suggestedValue, 8, 1e-9);
  const Issue* a = find(r, "CNC.ACCESS", "Hole1");
  ASSERT_TRUE(a);  // 3-axis: horizontal hole needs another setup
  EXPECT_EQ(a->severity, Severity::Warning);
  EXPECT_FALSE(find(analytic(crossBoreDoc("6")), "AM.HORIZONTAL_HOLE"));
  // SLS: powder bed supports everything
  MfgSettings s = d.mfg;
  s.am = amPreset(AmProcess::SLS_Polymer);
  EXPECT_FALSE(find(checkAnalytic(d, d.evaluateParams(), s), "AM.HORIZONTAL_HOLE"));
  // geometric
  auto mr = model::regenerate(d, d.evaluateParams());
  ASSERT_TRUE(mr.ok);
  Report g = checkGeometric(d, mr, d.mfg);
  i = find(g, "AM.HORIZONTAL_HOLE", "Hole1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->params.at(0), "DX");
}

TEST(MfgAm, Overhang) {
  Document d = overhangDoc();
  Report r = analytic(d);
  const Issue* i = find(r, "AM.OVERHANG", "Extrude2");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->severity, Severity::Warning);
  EXPECT_NEAR(i->actual, 0, 1e-6);  // horizontal down-skin
  EXPECT_NEAR(i->limit, 45, 1e-9);
  EXPECT_FALSE(i->faces.empty());
  MfgSettings s = d.mfg;
  s.am = amPreset(AmProcess::SLS_Polymer);
  EXPECT_FALSE(find(checkAnalytic(d, d.evaluateParams(), s), "AM.OVERHANG"));
  auto mr = model::regenerate(d, d.evaluateParams());
  ASSERT_TRUE(mr.ok);
  Report g = checkGeometric(d, mr, d.mfg);
  i = find(g, "AM.OVERHANG");
  ASSERT_TRUE(i);
  EXPECT_NEAR(std::stod(i->message.substr(i->message.find("≈") + 4)), 800, 40);  // 2 x 20 x 20 mm²
  // no overhang when the slab fits on the base
  EXPECT_FALSE(find(analytic(d, {{"OL", "20"}}), "AM.OVERHANG"));
}

TEST(MfgAm, ShellThicknessAndClosedShell) {
  Document d = shellDoc("0.25", false);
  Report r = analytic(d);
  const Issue* i = find(r, "AM.MIN_WALL", "Shell1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->params.at(0), "ST");
  EXPECT_NEAR(*i->suggestedValue, 0.4, 1e-9);
  EXPECT_NE(i->message.find("Kabuk et kalınlığı 0.25 mm < LPBF min. et kalınlığı 0.40 mm (Ti-6Al-4V). ST ≥ 0.40 mm önerilir."),
            std::string::npos)
      << i->message;
  EXPECT_FALSE(find(analytic(shellDoc("1", false)), "AM.MIN_WALL"));
  Document c = shellDoc("2", true);
  r = analytic(c);
  EXPECT_TRUE(find(r, "AM.TRAPPED_POWDER", "Shell1"));
  EXPECT_TRUE(find(r, "CNC.ENCLOSED_VOID", "Shell1"));
  MfgSettings s = c.mfg;
  s.am = amPreset(AmProcess::FDM_Polymer);  // closed cavities are fine for FDM
  EXPECT_FALSE(find(checkAnalytic(c, c.evaluateParams(), s), "AM.TRAPPED_POWDER"));
}

TEST(MfgAm, EnclosedCutVoid) {
  Document d = voidDoc();
  Report r = analytic(d);
  EXPECT_TRUE(find(r, "AM.TRAPPED_POWDER", "Extrude2"));
  EXPECT_TRUE(find(r, "CNC.ENCLOSED_VOID", "Extrude2"));
  auto mr = model::regenerate(d, d.evaluateParams());
  ASSERT_TRUE(mr.ok);
  Report g = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(g, "AM.TRAPPED_POWDER");
  ASSERT_TRUE(i);
  EXPECT_FALSE(i->faces.empty());
  EXPECT_TRUE(find(g, "CNC.ENCLOSED_VOID"));
}

TEST(MfgCnc, SidePocketAccessAndUndercut) {
  Document d = sidePocketDoc();
  Report r = analytic(d);
  EXPECT_TRUE(find(r, "CNC.ACCESS", "Extrude2"));
  auto mr = model::regenerate(d, d.evaluateParams());
  ASSERT_TRUE(mr.ok);
  Report g = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(g, "CNC.UNDERCUT", "Extrude2");
  ASSERT_TRUE(i);
  EXPECT_FALSE(i->faces.empty());
  // 5-axis: no undercut warning
  MfgSettings s = d.mfg;
  s.conv = convPreset(ConvProcess::CNC_Milling5Axis);
  EXPECT_FALSE(find(checkGeometric(d, mr, s), "CNC.UNDERCUT"));
  // the bracket is fully accessible from +-Z
  Document b = loadSample("bracket.pcad");
  auto mb = model::regenerate(b, b.evaluateParams());
  EXPECT_FALSE(find(checkGeometric(b, mb, b.mfg), "CNC.UNDERCUT"));
}

TEST(MfgCnc, Turning) {
  Document d = loadSample("bracket.pcad");
  MfgSettings s = d.mfg;
  s.conv = convPreset(ConvProcess::CNC_Turning);
  Report r = checkAnalytic(d, d.evaluateParams(), s);
  EXPECT_TRUE(find(r, "CNC.TURN_AXISYMMETRY", "Extrude1"));   // rectangular plate
  EXPECT_FALSE(find(r, "CNC.TURN_AXISYMMETRY", "Extrude2"));  // round boss on the axis
  EXPECT_FALSE(find(r, "CNC.SHARP_INTERNAL_CORNER"));
}

// ================================================================ geometric rules on hand-made shapes
TEST(MfgGeometric, SyntheticThinBoxAndVoid) {
  Document d;
  d.name = "Synthetic";
  model::ModelResult mr;
  mr.ok = true;
  mr.shape = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 30, 20, 0.25).Shape();
  int k = 0;
  for (TopExp_Explorer ex(mr.shape, TopAbs_FACE); ex.More(); ex.Next()) mr.faces["Ext.f" + std::to_string(k++)] = TopoDS::Face(ex.Current());
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(r, "AM.MIN_WALL", "Ext");
  ASSERT_TRUE(i);
  EXPECT_NEAR(i->actual, 0.25, 1e-6);
  EXPECT_EQ(i->faces.size(), 2u);
  EXPECT_TRUE(find(r, "CNC.MIN_WALL", "Ext"));

  // hollow cube: two shells
  TopoDS_Shape outer = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 20, 20, 20).Shape();
  TopoDS_Shape inner = BRepPrimAPI_MakeBox(gp_Pnt(5, 5, 5), 10, 10, 10).Shape();
  model::ModelResult mv;
  mv.ok = true;
  mv.shape = BRepAlgoAPI_Cut(outer, inner).Shape();
  k = 0;
  for (TopExp_Explorer ex(mv.shape, TopAbs_FACE); ex.More(); ex.Next()) mv.faces["Box.f" + std::to_string(k++)] = TopoDS::Face(ex.Current());
  r = checkGeometric(d, mv, d.mfg);
  i = find(r, "AM.TRAPPED_POWDER");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->faces.size(), 6u);
  EXPECT_TRUE(find(r, "CNC.ENCLOSED_VOID"));
  EXPECT_FALSE(find(r, "AM.MIN_WALL"));  // 5 mm walls

  // L-shape: one sharp concave vertical edge; plain box: none
  model::ModelResult ml;
  ml.ok = true;
  ml.shape = BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 20, 20, 10).Shape(),
                             BRepPrimAPI_MakeBox(gp_Pnt(10, 10, -1), 20, 20, 12).Shape())
                 .Shape();
  k = 0;
  for (TopExp_Explorer ex(ml.shape, TopAbs_FACE); ex.More(); ex.Next()) ml.faces["L.f" + std::to_string(k++)] = TopoDS::Face(ex.Current());
  r = checkGeometric(d, ml, d.mfg);
  i = find(r, "CNC.SHARP_INTERNAL_CORNER");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->faces.size(), 2u);
  model::ModelResult mbx;
  mbx.ok = true;
  mbx.shape = outer;
  EXPECT_FALSE(find(checkGeometric(d, mbx, d.mfg), "CNC.SHARP_INTERNAL_CORNER"));

  // too big for the machine
  model::ModelResult big;
  big.ok = true;
  big.shape = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 280, 50, 200).Shape();
  r = checkGeometric(d, big, d.mfg);
  EXPECT_TRUE(find(r, "AM.BUILD_ENVELOPE"));    // X 280 > 250
  EXPECT_TRUE(find(r, "CNC.STOCK_ENVELOPE"));   // Z 200 > 150
}

TEST(MfgGeometric, ModelStatusErrors) {
  Document d = loadSample("bracket.pcad");
  model::ModelResult mr;
  mr.status.push_back({"Fillet1", false, "Fillet1: yarıçap 30 mm kenar için çok büyük"});
  Report r = checkGeometric(d, mr, d.mfg);
  const Issue* i = find(r, "MODEL.FEATURE_FAILED", "Fillet1");
  ASSERT_TRUE(i);
  EXPECT_EQ(i->domain, Domain::Model);
  EXPECT_EQ(i->params.at(0), "R");
}

TEST(MfgAm, GapBetweenPinsAndSmallRoundPocket) {
  Document d;
  d.name = "Pins";
  d.params.add(par("G", "0.2"));
  d.params.add(par("DP", "2"));
  d.features.push_back(sketchF("Sketch1", rectSketch(SketchPlaneKind::XY, "", "-20", "-10", "40", "20", -20, -10, 40, 20)));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "5"}}));
  Sketch s2 = Sketch::create();
  s2.plane.kind = SketchPlaneKind::OnFace;
  s2.plane.faceRef = "Extrude1.end";
  for (int sg : {-1, 1}) {
    int c = s2.addCircle(sg * 2.1, 0.0, 2.0);
    int p = s2.entity(c)->points[0];
    s2.addConstraint(SkConstraintType::DistanceX, {0, p}, sg < 0 ? "-(2 + G/2)" : "2 + G/2");
    s2.addConstraint(SkConstraintType::DistanceY, {0, p}, "0");
    s2.addConstraint(SkConstraintType::Diameter, {c}, "4");
  }
  d.features.push_back(sketchF("Sketch2", s2));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "6"}, {"op", "join"}}));
  Sketch s3 = Sketch::create();
  s3.plane.kind = SketchPlaneKind::OnFace;
  s3.plane.faceRef = "Extrude1.end";
  int c = s3.addCircle(14.0, 0.0, 1.0);
  int p = s3.entity(c)->points[0];
  s3.addConstraint(SkConstraintType::DistanceX, {0, p}, "14");
  s3.addConstraint(SkConstraintType::DistanceY, {0, p}, "0");
  s3.addConstraint(SkConstraintType::Diameter, {c}, "DP");
  d.features.push_back(sketchF("Sketch3", s3));
  d.features.push_back(feat("Extrude3", FeatureType::Extrude,
                            {{"sketch", "Sketch3"}, {"distance", "2"}, {"direction", "reverse"}, {"op", "cut"}}));
  Report r = analytic(d);
  const Issue* g = find(r, "AM.MIN_GAP", "Extrude2");
  ASSERT_TRUE(g);
  EXPECT_NEAR(g->actual, 0.2, 1e-6);
  EXPECT_EQ(g->params.at(0), "G");
  EXPECT_NEAR(*g->suggestedValue, 0.3, 1e-9);
  const Issue* s = find(r, "CNC.SLOT_WIDTH", "Extrude2");
  ASSERT_TRUE(s);
  EXPECT_NEAR(*s->suggestedValue, 3, 1e-9);
  const Issue* k = find(r, "CNC.CORNER_RADIUS", "Extrude3");  // Ø2 milled round pocket < Ø3 tool
  ASSERT_TRUE(k);
  EXPECT_EQ(k->params.at(0), "DP");
  EXPECT_NEAR(*k->suggestedValue, 3, 1e-9);
  Report ok = analytic(d, {{"G", "4"}, {"DP", "4"}});
  EXPECT_FALSE(find(ok, "AM.MIN_GAP"));
  EXPECT_FALSE(find(ok, "CNC.SLOT_WIDTH"));
  EXPECT_FALSE(find(ok, "CNC.CORNER_RADIUS"));
}
