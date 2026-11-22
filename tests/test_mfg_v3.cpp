// Tests for the v3 "analytic phase 2" of src/mfg (Agent C3): parameter-aware analytic checks for features
// sketched on datum planes (OnPlane), kept-tool bodies and Combine, run by validateChange after regeneration.
#include "pc/MfgChecks.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
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

const std::string kSample = std::string(PC_SAMPLES_DIR) + "/v2_multibody.pcad";

bool loadSample(Document& d) {
  std::string err;
  return loadDocument(d, kSample, &err);
}

const Issue* findIssue(const Report& r, const std::string& rule, const std::string& fid) {
  for (auto& i : r.issues)
    if (i.ruleId == rule && (fid.empty() || i.featureId == fid)) return &i;
  return nullptr;
}

double msSince(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

[[maybe_unused]] void dump(const Report& r) {
  for (auto& i : r.issues) {
    std::printf("  [%s] %s fid=%s body=%s params=", toString(i.severity), i.ruleId.c_str(), i.featureId.c_str(),
                i.body.c_str());
    for (auto& p : i.params) std::printf("%s,", p.c_str());
    if (i.suggestedValue) std::printf(" sugg=%.3f", *i.suggestedValue);
    std::printf(" faces=");
    for (auto& f : i.faces) std::printf("%s,", f.c_str());
    std::printf(" | %s\n", i.message.c_str());
  }
}

// relax the [min,max] range of a parameter so that out-of-range values reach the mfg rules
void widen(Document& d, const std::string& name, double mn) {
  Parameter* p = d.params.find(name);
  ASSERT_NE(p, nullptr) << name;
  p->minValue = mn;
}

}  // namespace

TEST(MfgV3, DefaultsNoErrorsAndFast) {
  Document d;
  ASSERT_TRUE(loadSample(d));
  validateChange(d, {}, d.mfg);  // warm-up (OCCT statics)
  auto t0 = std::chrono::steady_clock::now();
  auto vr = validateChange(d, {}, d.mfg);
  double ms = msSince(t0);
  ASSERT_TRUE(vr.regenerated);
  EXPECT_FALSE(vr.report.hasErrors());
  for (auto& i : vr.report.issues) EXPECT_NE(i.severity, Severity::Error) << i.message;
  EXPECT_LT(ms, 1500.0 * kPerfScale);
  // features on datum planes are analysed analytically now: no phantom corner errors, every faced issue has a body
  for (auto& i : vr.report.issues)
    if (!i.faces.empty()) EXPECT_FALSE(i.body.empty()) << i.ruleId;
}

TEST(MfgV3, RibOnDatumPlaneThinWallSuggestsTr) {
  Document d;
  ASSERT_TRUE(loadSample(d));
  widen(d, "tr", 0.1);
  auto t0 = std::chrono::steady_clock::now();
  auto vr = validateChange(d, {{"tr", "0.3"}}, d.mfg);
  double ms = msSince(t0);
  ASSERT_TRUE(vr.regenerated);
  bool sawAm = false, sawCnc = false;
  for (auto& i : vr.report.issues) {
    if (i.ruleId != "AM.MIN_WALL" && i.ruleId != "CNC.MIN_WALL") continue;
    if (i.featureId != "Extrude3") continue;
    ASSERT_FALSE(i.params.empty()) << i.message;
    EXPECT_EQ(i.params[0], "tr") << i.message;
    ASSERT_TRUE(i.suggestedValue.has_value()) << i.message;
    EXPECT_GE(*i.suggestedValue, i.limit - 1e-9) << i.message;
    EXPECT_NEAR(i.actual, 0.3, 1e-6);
    EXPECT_EQ(i.body, "Braket");
    EXPECT_NE(i.message.find("tr ≥"), std::string::npos) << i.message;
    (i.ruleId == "AM.MIN_WALL" ? sawAm : sawCnc) = true;
  }
  EXPECT_TRUE(sawAm);
  EXPECT_TRUE(sawCnc);
  // the geometric thin-wall findings on the rib fillets are merged into the analytic issue (one issue per rule)
  for (auto& i : vr.report.issues)
    if (i.ruleId == "AM.MIN_WALL" || i.ruleId == "CNC.MIN_WALL") EXPECT_EQ(i.featureId, "Extrude3") << i.message;
  EXPECT_LT(ms, 1500.0 * kPerfScale);

  // applying the suggestion clears the AM wall error of the rib
  const Issue* am = findIssue(vr.report, "AM.MIN_WALL", "Extrude3");
  ASSERT_TRUE(am);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", *am->suggestedValue);
  auto vr2 = validateChange(d, {{"tr", buf}}, d.mfg);
  EXPECT_FALSE(findIssue(vr2.report, "AM.MIN_WALL", "Extrude3"));
}

TEST(MfgV3, TabThicknessSuggestsTtab) {
  Document d;
  ASSERT_TRUE(loadSample(d));
  widen(d, "Ttab", 0.1);
  auto vr = validateChange(d, {{"Ttab", "0.5"}}, d.mfg);
  ASSERT_TRUE(vr.regenerated);
  const Issue* tab = nullptr;
  for (auto& i : vr.report.issues)
    if (i.ruleId == "CNC.MIN_WALL" && i.body == "Tab") tab = &i;
  ASSERT_TRUE(tab);
  EXPECT_EQ(tab->featureId, "Extrude4");
  ASSERT_FALSE(tab->params.empty());
  EXPECT_EQ(tab->params[0], "Ttab");
  ASSERT_TRUE(tab->suggestedValue.has_value());
  EXPECT_GE(*tab->suggestedValue, d.mfg.conv.minWallThickness - 1e-9);
  EXPECT_LE(*tab->suggestedValue, d.mfg.conv.minWallThickness + 0.1);
  // the kept plug (body "Tapa", keepTool of Extrude5) is as thick as the tab: reported on Extrude5, body Tapa
  const Issue* plug = nullptr;
  for (auto& i : vr.report.issues)
    if (i.ruleId == "CNC.MIN_WALL" && i.body == "Tapa") plug = &i;
  ASSERT_TRUE(plug);
  EXPECT_EQ(plug->featureId, "Extrude5");
  EXPECT_EQ(plug->message.find(".tool "), std::string::npos) << plug->message;
  ASSERT_FALSE(plug->params.empty());
  EXPECT_EQ(plug->params[0], "Ttab");
}

TEST(MfgV3, PlugHoleNonStandardDrill) {
  Document d;
  ASSERT_TRUE(loadSample(d));
  auto vr = validateChange(d, {{"Dp", "7.3"}}, d.mfg);
  ASSERT_TRUE(vr.regenerated);
  const Issue* sd = findIssue(vr.report, "CNC.STD_DRILL", "Extrude5");
  ASSERT_TRUE(sd);
  EXPECT_EQ(sd->severity, Severity::Info);
  EXPECT_EQ(sd->body, "Tab");
  ASSERT_FALSE(sd->params.empty());
  EXPECT_EQ(sd->params[0], "Dp");
  ASSERT_TRUE(sd->suggestedValue.has_value());
  EXPECT_NEAR(*sd->suggestedValue, nearestStandardDrill(7.3), 1e-6);
  EXPECT_FALSE(vr.report.hasErrors());
  // standard size: no info
  auto vr2 = validateChange(d, {{"Dp", "7.5"}}, d.mfg);
  EXPECT_FALSE(findIssue(vr2.report, "CNC.STD_DRILL", "Extrude5"));
  // deep plug hole: L/D on the tab (L = Ttab) with a stricter drill limit
  MfgSettings s = d.mfg;
  s.conv.maxHoleDepthToDia = 0.4;  // Ttab/Dp = 4/8 = 0.5
  auto vr3 = validateChange(d, {}, s);
  const Issue* ld = findIssue(vr3.report, "CNC.HOLE_LD", "Extrude5");
  ASSERT_TRUE(ld);
  EXPECT_NEAR(ld->actual, 0.5, 0.02);
  ASSERT_FALSE(ld->params.empty());
  ASSERT_TRUE(ld->suggestedValue.has_value());
}

TEST(MfgV3, CheckAnalyticAloneSkipsDatumFeatures) {
  Document d;
  ASSERT_TRUE(loadSample(d));
  widen(d, "tr", 0.1);
  std::vector<ParamError> errs;
  auto v = d.params.evaluate(&errs, {{"tr", "0.3"}});
  ASSERT_TRUE(v);
  auto t0 = std::chrono::steady_clock::now();
  Report r = checkAnalytic(d, *v, d.mfg);
  double ms = msSince(t0);
  EXPECT_FALSE(findIssue(r, "AM.MIN_WALL", "Extrude3"));  // no model -> datum-plane rib not placed
  EXPECT_LT(ms, 300.0 * kPerfScale);
}
