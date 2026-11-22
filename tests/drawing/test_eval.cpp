// WP-1: ground truth I/O, irFromGroundTruth, evaluate().
#include "wp1_helpers.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace pc::drawing;

namespace {
GroundTruth roundTrip(const GroundTruth& gt, const std::string& name) {
  const std::string dir = wp1::tmpDir(name);
  const std::string path = dir + "/gt.json";
  std::string err;
  EXPECT_TRUE(saveGroundTruth(gt, path, &err)) << err;
  GroundTruth back;
  EXPECT_TRUE(loadGroundTruth(path, back, &err)) << err;
  return back;
}
}  // namespace

TEST(EvalGroundTruth, SaveLoadRoundTrip) {
  const GroundTruth gt = wp1::samplePlateGt();
  const GroundTruth b = roundTrip(gt, "gt_roundtrip");
  EXPECT_EQ(b.family, "plate");
  EXPECT_EQ(b.caseName, "plate_unit");
  ASSERT_EQ(b.sheet.prims.size(), gt.sheet.prims.size());
  EXPECT_EQ(b.sheet.prims[6].style.dash, gt.sheet.prims[6].style.dash);
  ASSERT_EQ(b.sheet.texts.size(), 3u);
  EXPECT_EQ(b.sheet.texts[1].utf8, "Ø20");
  EXPECT_EQ(b.roles.at(7), LineRole::Center);
  ASSERT_EQ(b.dims.size(), 2u);
  EXPECT_EQ(b.dims[1].kind, DimKind::Diameter);
  EXPECT_EQ(b.dims[1].textId, 9);
  EXPECT_EQ(b.dims[0].geomPrimIds, (std::vector<int>{4, 2}));
  EXPECT_FALSE(b.dims[0].tolPlus.has_value());
  EXPECT_DOUBLE_EQ(b.scale.factor(), 2.0);
  EXPECT_EQ(b.views.at(0).role, ViewRole::Front);
  EXPECT_EQ(b.params.size(), 2u);
  EXPECT_EQ(b.expectedStrategy, Strategy::ExtrudeTwoView);
  EXPECT_DOUBLE_EQ(b.volume, 1234.5);
  EXPECT_FALSE(b.dir.empty());
  // byte-identical re-serialisation
  EXPECT_EQ(toJson(b).dump(), toJson(gt).dump());
}

TEST(EvalGroundTruth, LoadErrors) {
  GroundTruth gt; std::string err;
  EXPECT_FALSE(loadGroundTruth(std::string(PC_DRAWING_TMP_DIR) + "/nope/gt.json", gt, &err));
  EXPECT_FALSE(err.empty());
}

TEST(EvalGroundTruth, IrFromGroundTruthIsPerfectS2) {
  const GroundTruth gt = wp1::samplePlateGt();
  DrawingIR ir = irFromGroundTruth(gt);
  EXPECT_EQ(ir.raw.prims.size(), gt.sheet.prims.size());
  EXPECT_EQ(ir.norm.texts.size(), gt.sheet.texts.size());
  EXPECT_EQ(ir.nextId, 11);
  EXPECT_EQ(ir.lastStage, static_cast<int>(Stage::Normalize));
  EXPECT_EQ(ir.newId(), 11);
  const Metrics m = evaluate(ir, gt);
  EXPECT_DOUBLE_EQ(m.values.at("prim.recall"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("prim.precision"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("geom.recall"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("text.exact"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("dim.textExact"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("arc.recall"), 1.0);
  EXPECT_NEAR(m.values.at("arc.radiusErrMax"), 0.0, 1e-12);
}

TEST(EvalMetrics, DetectsMissingAndWrongElements) {
  const GroundTruth gt = wp1::samplePlateGt();
  DrawingIR ir = irFromGroundTruth(gt);
  ir.norm.prims.erase(ir.norm.prims.begin());             // drop one visible edge
  ir.norm.prims[3].radius += 0.2;                           // circle radius wrong by 0.2 > tol
  ir.norm.prims.push_back(wp1::line(100, {0, 0}, {10, 10}));  // spurious
  ir.norm.texts[1].utf8 = "020";                            // OCR-like error
  const Metrics m = evaluate(ir, gt);
  EXPECT_NEAR(m.values.at("prim.recall"), 5.0 / 7.0, 1e-9);
  EXPECT_NEAR(m.values.at("prim.precision"), 5.0 / 7.0, 1e-9);
  EXPECT_NEAR(m.values.at("text.exact"), 2.0 / 3.0, 1e-9);
  EXPECT_NEAR(m.values.at("dim.textExact"), 0.5, 1e-9);
  EXPECT_NEAR(m.values.at("dim.textCer"), 1.0 / 5.0, 1e-9);   // "80" + "Ø20": 1 edit / 5 chars
  EXPECT_NEAR(m.values.at("arc.recall"), 0.5, 1e-9);
  EXPECT_FALSE(m.notes.empty());
}

TEST(EvalMetrics, SplitLinesStillCovered) {
  const GroundTruth gt = wp1::samplePlateGt();
  DrawingIR ir = irFromGroundTruth(gt);
  // split edge 1 in two halves: one-to-one recall drops, coverage stays 1
  ir.norm.prims[0] = wp1::line(1, {50, 50}, {90, 50});
  ir.norm.prims.push_back(wp1::line(50, {90, 50}, {130, 50}));
  const Metrics m = evaluate(ir, gt);
  EXPECT_LT(m.values.at("prim.recall"), 1.0);
  EXPECT_NEAR(m.values.at("prim.coverage"), 1.0, 1e-9);
}

TEST(EvalMetrics, DimensionAndLayoutMetrics) {
  const GroundTruth gt = wp1::samplePlateGt();
  DrawingIR ir = irFromGroundTruth(gt);
  Dimension d; d.id = ir.newId(); d.textId = 8; d.text.nominal = 80; d.geomPrimIds = {4, 2}; ir.annot.dims.push_back(d);
  Dimension d2; d2.id = ir.newId(); d2.textId = 9; d2.text.nominal = 20; d2.geomPrimIds = {6}; ir.annot.dims.push_back(d2);
  ViewRegion v; v.id = ir.newId(); v.role = ViewRole::Front; v.box = {{46, 41}, {139, 99}}; ir.layout.views.push_back(v);
  ir.layout.title.scale.num = 1; ir.layout.title.scale.den = 2; ir.layout.title.scale.conf = 0.9f;
  ir.lastStage = static_cast<int>(Stage::Annotate);
  const Metrics m = evaluate(ir, gt);
  EXPECT_DOUBLE_EQ(m.values.at("dim.valueAcc"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("dim.assocAcc"), 0.5);    // Ø20 bound to the arc instead of the circle
  EXPECT_DOUBLE_EQ(m.values.at("view.roleAcc"), 1.0);
  EXPECT_DOUBLE_EQ(m.values.at("scale.exact"), 1.0);
}
