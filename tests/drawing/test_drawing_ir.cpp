// WP-1: DrawingIR JSON (pcdrw/1) round trip, conventions, file I/O.
#include "wp1_helpers.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>

using namespace pc::drawing;
using nlohmann::json;

namespace {
DrawingIR fullIr() {
  const GroundTruth gt = wp1::samplePlateGt();
  DrawingIR ir = irFromGroundTruth(gt);
  ir.raw.prims[0].attrs["pdf.ocg"] = "Visible";
  ir.raw.prims[0].conf = 0.9f;
  ir.raw.prims[0].mergedFrom = {3, 4};
  ir.raw.texts[0].alternatives = {{"8O", 0.4f}};
  NativeDimension nd; nd.id = ir.newId(); nd.kind = NativeDimKind::Diameter; nd.defpts[0] = {1, 2}; nd.measured = 20;
  nd.textOverride = "<>"; nd.block = "*D3"; nd.blockPrimitiveIds = {1}; ir.raw.nativeDims.push_back(nd);
  RasterInfo ri; ri.widthPx = 100; ri.dpi = 300; ri.pxToSheet = {0.1, 0, 0, -0.1, 0, 10}; ir.norm.raster = ri;
  ir.norm.diagnostics.push_back({Severity::Warning, "pdf.font.symbolRepaired", "onarıldı", 1, {5}});
  ir.layout.border = {{5, 5}, {292, 205}};
  ir.layout.title.fields["scale"] = "1:2";
  ir.layout.title.scale.num = 1; ir.layout.title.scale.den = 2;
  ViewRegion v; v.id = ir.newId(); v.role = ViewRole::Section; v.label = "A-A"; v.primIds = {1, 2}; ir.layout.views.push_back(v);
  ir.annot.roles[1] = LineRole::Visible; ir.annot.roleConf[1] = 0.75f;
  Dimension d; d.id = ir.newId(); d.text.raw = "Ø16 +0/-0.5"; d.text.tolPlus = 0; d.text.tolMinus = -0.5; d.anchors = {Vec2{1, 1}, Vec2{2, 2}};
  ir.annot.dims.push_back(d);
  ir.views.axisOfView[v.id] = 7; ir.views.rotational[v.id] = true; ir.views.symmetryScore[v.id] = 0.97;
  ir.views.relations.push_back({v.id, v.id, Adjacency::RightOf, 0.01, 0.8f});
  SketchCandidate sc; sc.id = ir.newId(); sc.sketch = pc::Sketch::create(); sc.sketch.addLine(0, 0, 10, 0);
  ParamProposal pp; pp.param.name = "D1"; pp.param.expression = "16"; pp.tolMinus = -0.5; pp.nominalValue = 16; sc.params.push_back(pp);
  sc.entityToPrimitive[1] = 3; sc.dof = 0; ir.sketches.push_back(sc);
  SynthesisCandidate syn; syn.id = ir.newId(); syn.strategy = Strategy::Revolve; syn.score = 0.8f;
  syn.fragment.name = "frag"; pc::Parameter p; p.name = "D1"; p.expression = "16"; syn.fragment.params.add(p);
  ir.synthesis.push_back(syn);
  ir.edits.push_back({Stage::Annotate, edit_op::setRole, R"({"ids":[1,2],"role":"Hidden"})"});
  ir.stageVersion[Stage::Annotate] = "annot v1";
  ir.lastStage = static_cast<int>(Stage::Synthesize);
  ir.attrs["x"] = "y";
  return ir;
}
}  // namespace

TEST(IRJson, RoundTripByteIdentical) {
  const DrawingIR ir = fullIr();
  const json j = toJson(ir);
  const DrawingIR back = drawingIRFromJson(j);
  EXPECT_EQ(toJson(back).dump(1), j.dump(1));
  EXPECT_EQ(back.raw.prims[0].attrs.at("pdf.ocg"), "Visible");
  EXPECT_FLOAT_EQ(back.raw.prims[0].conf, 0.9f);
  EXPECT_EQ(back.raw.nativeDims.at(0).block, "*D3");
  ASSERT_TRUE(back.norm.raster.has_value());
  EXPECT_DOUBLE_EQ(back.norm.raster->dpi, 300);
  EXPECT_EQ(back.annot.roles.at(1), LineRole::Visible);
  EXPECT_TRUE(back.annot.dims[0].text.tolPlus.has_value());
  EXPECT_DOUBLE_EQ(*back.annot.dims[0].text.tolMinus, -0.5);
  EXPECT_TRUE(back.views.rotational.at(back.layout.views[0].id));
  EXPECT_EQ(back.sketches.at(0).sketch.entities.size(), 1u);
  EXPECT_EQ(back.sketches[0].params.at(0).param.name, "D1");
  ASSERT_NE(back.synthesis.at(0).fragment.params.find("D1"), nullptr);
  EXPECT_EQ(back.edits.at(0).op, "setRole");
  EXPECT_EQ(back.stageVersion.at(Stage::Annotate), "annot v1");
}

TEST(IRJson, Conventions) {
  const json j = toJson(fullIr());
  EXPECT_EQ(j["schema"], "pcdrw/1");
  EXPECT_EQ(j["irVersion"], "1.0");
  EXPECT_TRUE(j["raw"]["prims"][0]["p0"].is_array());                 // Vec2 = [x,y]
  EXPECT_TRUE(j["raw"]["sheet"]["lo"].is_array());                    // Box2
  EXPECT_EQ(j["raw"]["prims"][0]["kind"], "Line");                    // enum names
  EXPECT_EQ(j["raw"]["prims"][0]["conf"].dump(), "0.9");              // float written shortest
  EXPECT_TRUE(j["annot"]["roles"][0].is_array());                     // map<int,T> = [[k,v]]
  EXPECT_TRUE(j["stageVersion"].contains("Annotate"));                // keyed by Stage name
  EXPECT_TRUE(j["edits"][0]["payload"].is_object());                  // payload embedded parsed
  EXPECT_TRUE(j["raw"]["raster"].is_null());                          // optional absent -> null
  EXPECT_TRUE(j["annot"]["dims"][0]["text"]["tolPlus"].is_number());
  EXPECT_TRUE(j["annot"]["dims"][0]["text"]["depth"].is_null());
  EXPECT_TRUE(j["raw"]["prims"][0].contains("clipped"));              // defaults written
}

TEST(IRJson, ForwardCompatibleAndErrors) {
  json j = toJson(fullIr());
  j["futureKey"] = 42;
  j["raw"]["prims"][0]["futureKey"] = "x";
  j["norm"]["prims"][0].erase("style");
  EXPECT_NO_THROW((void)drawingIRFromJson(j));
  const DrawingIR b = drawingIRFromJson(j);
  EXPECT_TRUE(b.norm.prims[0].style.stroked);   // default restored
  json bad = j; bad["schema"] = "other/1";
  EXPECT_THROW((void)drawingIRFromJson(bad), std::runtime_error);
  json v2 = j; v2["irVersion"] = "2.0";
  EXPECT_THROW((void)drawingIRFromJson(v2), std::runtime_error);
  EXPECT_THROW((void)primitiveSheetFromJson(json::array()), std::runtime_error);
  json minimal = {{"schema", "pcdrw/1"}};
  const DrawingIR m = drawingIRFromJson(minimal);
  EXPECT_EQ(m.nextId, 1);
  EXPECT_EQ(m.chosenSynthesis, -1);
}

TEST(IRJson, SaveLoadFile) {
  const DrawingIR ir = fullIr();
  const std::string path = wp1::tmpDir("ir") + "/x.pcdrw.json";
  std::string err;
  ASSERT_TRUE(saveIR(ir, path, &err)) << err;
  DrawingIR back;
  ASSERT_TRUE(loadIR(back, path, &err)) << err;
  EXPECT_EQ(toJson(back).dump(), toJson(ir).dump());
  std::ifstream f(path); std::string first; std::getline(f, first); std::getline(f, first);
  EXPECT_EQ(first.rfind(" \"", 0), 0u);          // indent 1
  EXPECT_FALSE(loadIR(back, path + ".missing", &err));
  std::ofstream(path + ".bad") << "{not json";
  EXPECT_FALSE(loadIR(back, path + ".bad", &err));
}

TEST(IRJson, PrimitiveSheetStandalone) {
  const DrawingIR ir = fullIr();
  const json j = toJson(ir.norm);
  EXPECT_EQ(j.dump(), toJson(ir)["norm"].dump());
  EXPECT_EQ(toJson(primitiveSheetFromJson(j)).dump(), j.dump());
}
