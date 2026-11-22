// R2 — axisymmetric SECTION views (hatch families -> parts, centreline -> half profiles, Ø/axial/angle dims ->
// constraints, one revolved body per part). Fixtures: tests/drawing/fixtures/r2/*.pcdrw.json (real PDF geometry +
// hand-placed dimension TextRuns, see R2_SECTION_AXISYM.md). Regenerate them with
//   pc_drawing_tests --gtest_also_run_disabled_tests --gtest_filter=R2Tool.*
#include "pc/drawing/Drawing.h"
#include "pc/Model.h"
#include "../../src/drawing/interp/SectionAxisym.h"
#include "../../src/drawing/synth/RevolveMulti.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <set>

using namespace pc::drawing;

namespace {

std::string r2Dir() { return std::string(PC_DRAWING_TEST_DIR) + "/fixtures/r2/"; }
std::string r2Fixture(const std::string& n) { return r2Dir() + n + ".pcdrw.json"; }

// Build a fixture: S1 ingest of the real PDF + S2 normalize (arrowheads >= 0.5 mm: the A3 drawing is printed on a
// 172 mm page, arrows are 0.9 mm), glyph outlines replaced by the hand-made TextRuns of <name>.texts.json.
void makeFixture(const std::string& name) {
  nlohmann::json spec = nlohmann::json::parse(std::ifstream(r2Dir() + name + ".texts.json"));
  const std::string pdf = std::string(PC_DRAWING_TEST_DIR) + "/" + spec.at("pdf").get<std::string>();
  DrawingIR ir;
  int id = 1;
  IngestOptions io;
  ASSERT_TRUE(ingestPdfVector(pdf, io, ir.raw, id).ok());
  NormalizeOptions no;
  no.arrowMinMm = 0.5;
  ASSERT_TRUE(normalize(ir.raw, no, ir.norm, id).ok());
  // drop glyph outlines (filled, unstroked small curves) — they are what r1 turns into TextRuns
  std::vector<Primitive> keep;
  for (auto& p : ir.norm.prims) {
    if ((p.kind == PrimKind::Bezier || p.kind == PrimKind::Polyline) && p.style.filled && !p.style.stroked) {
      Box2 b{{1e300, 1e300}, {-1e300, -1e300}};
      for (auto& q : p.pts) { b.lo.x = std::min(b.lo.x, q.x); b.lo.y = std::min(b.lo.y, q.y); b.hi.x = std::max(b.hi.x, q.x); b.hi.y = std::max(b.hi.y, q.y); }
      if (b.hi.x - b.lo.x < 8 && b.hi.y - b.lo.y < 8) continue;
    }
    keep.push_back(p);
  }
  ir.norm.prims = keep;
  // raw: keep only the lines consumed by S2 hatch regions (the section analysis re-reads them); saves ~4 MB
  std::set<int> hatchSrc;
  for (auto& p : ir.norm.prims) if (p.kind == PrimKind::HatchRegion) hatchSrc.insert(p.mergedFrom.begin(), p.mergedFrom.end());
  std::vector<Primitive> rawKeep;
  for (auto& p : ir.raw.prims) if (hatchSrc.count(p.id)) rawKeep.push_back(p);
  ir.raw.prims = rawKeep;
  ir.raw.texts.clear();
  for (auto& t : spec.at("texts")) {
    TextRun r;
    r.id = id++;
    r.utf8 = t.at("t").get<std::string>();
    auto b = t.at("box");
    double x0 = b[0], y0 = b[1], x1 = b[2], y1 = b[3];
    int rot = t.value("rot", 0);
    r.rotDeg = rot;
    r.font = "r2-fixture";
    if (rot == 90) { r.origin = {x1, y0}; r.height = x1 - x0; r.quad = {Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}, Vec2{x0, y0}}; }
    else if (rot == -90 || rot == 270) { r.rotDeg = -90; r.origin = {x0, y1}; r.height = x1 - x0; r.quad = {Vec2{x0, y1}, Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}}; }
    else { r.origin = {x0, y0}; r.height = y1 - y0; r.quad = {Vec2{x0, y0}, Vec2{x1, y0}, Vec2{x1, y1}, Vec2{x0, y1}}; }
    r.attrs["r2.handPlaced"] = "1";
    ir.norm.texts.push_back(r);
  }
  ir.nextId = id;
  ir.attrs["r2.fixture"] = "ingest+normalize(arrowMinMm=0.5) of " + spec.at("pdf").get<std::string>() + ", texts from " + name + ".texts.json";
  ASSERT_TRUE(saveIR(ir, r2Fixture(name)));
}

DrawingIR runFixture(const std::string& name, StageResult* out = nullptr) {
  DrawingIR ir;
  PipelineOptions o;
  StageResult r = runPipeline(r2Fixture(name), ir, Stage::Ingest, Stage::Synthesize, o);
  if (out) *out = r;
  return ir;
}

}  // namespace

TEST(R2Tool, DISABLED_MakeFixtures) {
  makeFixture("atomizer_ATM-PS-001");
  makeFixture("swirlcup_SWC-RR-001");
}

namespace {
void dump(const DrawingIR& ir, const StageResult& r) {
  std::cout << "pipeline: " << toString(r.status) << " " << r.message << "\n";
  for (auto& d : ir.diagnostics) if (d.severity != Severity::Info) std::cout << "  diag " << d.stage << " " << d.code << ": " << d.message << "\n";
  for (auto& [k, v] : ir.attrs) if (k.rfind("r2.", 0) == 0) std::cout << "  " << k << " = " << v << "\n";
  for (auto& c : ir.sketches) {
    std::cout << " sketch " << c.id << " " << c.purpose << " dof=" << c.dof << " dev=" << c.maxGeomDeviation << " ents=" << c.sketch.entities.size() << "\n";
    for (auto& [k, v] : c.attrs) if (k.rfind("r2.constraintText", 0)) std::cout << "    " << k << " = " << v << "\n";
    for (auto& p : c.params) std::cout << "    P " << p.param.name << " = " << p.param.expression << "  " << toString(p.tag) << "\n";
  }
  for (auto& s : ir.synthesis) {
    std::cout << " synth " << toString(s.strategy) << " score=" << s.score << " vol=" << s.volume << " reproj=" << s.reprojectionError << " : " << s.explanation << "\n";
    for (auto& w : s.warnings) std::cout << "    W " << w << "\n";
    for (auto& [k, v] : s.attrs) if (k.rfind("r2.", 0) == 0) std::cout << "    " << k << " = " << v << "\n";
  }
}
}  // namespace

TEST(R2Tool, DISABLED_Dump) {
  for (const char* n : {"atomizer_ATM-PS-001", "swirlcup_SWC-RR-001"}) {
    StageResult r;
    DrawingIR ir = runFixture(n, &r);
    std::cout << "==== " << n << "\n";
    dump(ir, r);
  }
}

// ------------------------------------------------------------------------------------------------ assertions
namespace {
const ParamProposal* findParam(const DrawingIR& ir, const std::string& n) {
  for (auto& c : ir.sketches) if (c.purpose == r2::kPurpose) for (auto& p : c.params) if (p.param.name == n) return &p;
  return nullptr;
}
std::vector<const SketchCandidate*> partSketches(const DrawingIR& ir) {
  std::vector<const SketchCandidate*> v;
  for (auto& c : ir.sketches) if (c.purpose == r2::kPurpose) v.push_back(&c);
  return v;
}
void expectDims(const DrawingIR& ir, std::initializer_list<std::pair<const char*, double>> ps) {
  for (auto& [n, v] : ps) {
    const ParamProposal* p = findParam(ir, n);
    ASSERT_NE(p, nullptr) << n;
    EXPECT_EQ(p->tag, ParamTag::FromDimension) << n;
    EXPECT_NEAR(p->nominalValue, v, 1e-9) << n;
  }
}
}  // namespace

TEST(R2Section, AtomizerHatchFamiliesAxisAndScale) {
  DrawingIR ir = runFixture("atomizer_ATM-PS-001");
  r2::AxisymSection S = r2::analyzeAxisymSection(ir);
  ASSERT_TRUE(S.found) << S.reason;
  EXPECT_TRUE(S.horizontal);
  EXPECT_NEAR(S.axisPoint.y, 80.605, 0.01);
  ASSERT_EQ(S.parts.size(), 2u);                        // nozzle tip (45°) + swirler insert (135°)
  EXPECT_EQ(S.parts[0].name, "NOZZLE_TIP");
  EXPECT_EQ(S.parts[1].name, "SWIRLER_INSERT");
  EXPECT_NEAR(S.parts[0].family.angleDeg, 45, 1);
  EXPECT_NEAR(S.parts[1].family.angleDeg, 135, 1);
  EXPECT_GT(S.parts[0].family.lines, 30);
  EXPECT_GT(S.parts[1].family.lines, 10);
  // hatch lines never enter the profile
  std::set<int> prof(S.profilePrimIds.begin(), S.profilePrimIds.end());
  for (int h : S.hatchLineIds) EXPECT_FALSE(prof.count(h)) << h;
  EXPECT_GE(S.hatchLineIds.size(), 60u);
  // scale from Ø dims (page printed at 41 % of A3): title "10:1" is reported as inconsistent
  EXPECT_NEAR(S.scale, 8.0 / 32.403, 0.002);
  EXPECT_NEAR(S.titleScale, 0.1, 1e-9);
  // the section cuts a feed groove in the upper half of the insert: lower (solid) half is used
  EXPECT_GT(S.parts[0].mirrorIoU, 0.99);
  EXPECT_LT(S.parts[1].mirrorIoU, 0.9);
  EXPECT_EQ(S.parts[1].side, 1);
}

TEST(R2Section, AtomizerTwoBodies) {
  StageResult r;
  DrawingIR ir = runFixture("atomizer_ATM-PS-001", &r);
  ASSERT_TRUE(r.ok()) << r.message;
  auto ps = partSketches(ir);
  ASSERT_EQ(ps.size(), 2u);
  for (auto* c : ps) { EXPECT_EQ(c->dof, 0); EXPECT_LT(c->maxGeomDeviation, 0.05); EXPECT_EQ(c->attrs.at("interp.converged"), "1"); }
  expectDims(ir, {{"D_8", 8}, {"D_5", 5}, {"D_3p2", 3.2}, {"D_2", 2}, {"D_0p8", 0.8}, {"C_0p5", 0.5}, {"L_4p2", 4.2},
                  {"L_5p8", 5.8}, {"L_2", 2}, {"L_0p6", 0.6}, {"L_0p4", 0.4}, {"A_90", 90}});
  ASSERT_FALSE(ir.synthesis.empty());
  const SynthesisCandidate& s = ir.synthesis.front();
  EXPECT_EQ(s.strategy, Strategy::SectionRevolve);
  EXPECT_EQ(s.attrs.at("r2.bodies"), "2");
  ASSERT_TRUE(s.regenerated);
  // hand estimate (R2_SECTION_AXISYM.md): tip 364.28 mm³, insert pi*2.5²*5.8 = 113.88 mm³
  EXPECT_NEAR(std::stod(s.attrs.at("r2.body.0.volume")), 364.28, 0.01 * 364.28);
  EXPECT_NEAR(std::stod(s.attrs.at("r2.body.1.volume")), 113.88, 0.01 * 113.88);
  EXPECT_LT(s.reprojectionError, 0.1);
  // regenerate the fragment: two separate solids (BODIES contract), named after the leader labels
  auto mr = pc::model::regenerate(s.fragment);
  ASSERT_TRUE(mr.ok);
  ASSERT_EQ(mr.bodies.size(), 2u);
  EXPECT_EQ(mr.bodies[0].name, "NOZZLE_TIP");
  EXPECT_EQ(mr.bodies[1].name, "SWIRLER_INSERT");
  auto mp = pc::model::massProperties(mr.bodies[0].shape);
  EXPECT_NEAR(mp.xmax - mp.xmin, 8.0, 0.02);
  EXPECT_NEAR(mp.zmax - mp.zmin, 10.0, 0.02);
  // parameter drives the model: Ø8 -> Ø9
  auto vals = s.fragment.evaluateParams({{"D_8", "9"}});
  auto mr2 = pc::model::regenerate(s.fragment, vals);
  ASSERT_TRUE(mr2.ok);
  auto mp2 = pc::model::massProperties(mr2.bodies[0].shape);
  EXPECT_NEAR(mp2.xmax - mp2.xmin, 9.0, 0.02);
}

TEST(R2Section, SwirlCupOneBodyThinFlare) {
  StageResult r;
  DrawingIR ir = runFixture("swirlcup_SWC-RR-001", &r);
  ASSERT_TRUE(r.ok()) << r.message;
  r2::AxisymSection S = r2::analyzeAxisymSection(ir);
  ASSERT_TRUE(S.found) << S.reason;
  ASSERT_EQ(S.parts.size(), 1u);                        // sections B-B / C-C (transverse, arcs) are not chosen
  EXPECT_GT(S.mirrorIoU, 0.95);
  EXPECT_NEAR(S.scale, 50.0 / 38.779, 0.005);
  auto ps = partSketches(ir);
  ASSERT_EQ(ps.size(), 1u);
  EXPECT_EQ(ps[0]->dof, 0);
  expectDims(ir, {{"D_50", 50}, {"D_47", 47}, {"D_43", 43}, {"D_27", 27}, {"D_19", 19}, {"D_17", 17}, {"D_10", 10},
                  {"D_22", 22}, {"D_32", 32}, {"L_3", 3}, {"L_5", 5}, {"L_2", 2}, {"L_6", 6}, {"L_11", 11}, {"L_8", 8},
                  {"L_3p5", 3.5}, {"A_90", 90}});
  EXPECT_EQ(ir.attrs.at("r2.section.dimsUnbound"), "0");
  const SynthesisCandidate& s = ir.synthesis.front();
  EXPECT_EQ(s.strategy, Strategy::SectionRevolve);
  EXPECT_EQ(s.attrs.at("r2.bodies"), "1");
  // hand estimate (Pappus of the nominal half profile, R2_SECTION_AXISYM.md): 30075.5 mm³
  EXPECT_NEAR(s.volume, 30075.5, 0.01 * 30075.5);
  EXPECT_LT(s.reprojectionError, 0.1);
  auto mr = pc::model::regenerate(s.fragment);
  ASSERT_TRUE(mr.ok);
  ASSERT_EQ(mr.bodies.size(), 1u);
  auto mp = pc::model::massProperties(mr.shape);
  EXPECT_NEAR(mp.xmax - mp.xmin, 50.0, 0.02);
  EXPECT_NEAR(mp.zmax - mp.zmin, 30.0, 0.02);
}

TEST(R2Section, NoSectionNoCandidates) {
  // generic WP-3 fixture without hatching: the hook stays silent
  DrawingIR ir;
  runPipeline(std::string(PC_DRAWING_TEST_DIR) + "/fixtures/wp3/plate4.pcdrw.json", ir, Stage::Ingest, Stage::Synthesize, PipelineOptions{});
  EXPECT_TRUE(partSketches(ir).empty());
  for (auto& s : ir.synthesis) EXPECT_EQ(s.attrs.count("r2.bodies"), 0u);
}
