// Real-drawing acceptance (R4 reference). Ground truth = hand-built parametric models
// samples/ref_swc_rr_001.pcad / ref_atm_ps_001.pcad (samples/tools/gen_real_refs.py), recorded in
// tests/drawing/real/ground_truth/{swc_rr_001,atm_ps_001}.json (+ .dims.json = every dimension text of the sheet).
//
//  RealRef.*        always run: the reference models regenerate and still match their recorded ground truth.
//                   PC_R4_WRITE_GT=1 rewrites the ground-truth json + STEP files from the current regeneration.
//  RealAcceptance.* full pipeline (PDF -> ingest -> ... -> synthesize -> merge -> regenerate) vs ground truth:
//                   body count, volume within 3 %, bbox (sorted extents) within 2 %, slot counts, key parameters.
//                   Always run (enabled at integration, 16.11.2026). Only the TOP-RANKED candidate counts: it is the one
//                   the wizard / pc_drawing_cli select.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/drawing/Drawing.h"
#include <BRepCheck_Analyzer.hxx>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>

using namespace pc;
using namespace pc::drawing;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path kReal = fs::path(PC_DRAWING_TEST_DIR) / "real";
const fs::path kGt = kReal / "ground_truth";

struct Ref {
  const char* key;        // ground-truth file stem
  const char* pcad;       // samples/<pcad>
  const char* pdf;        // tests/drawing/real/<pdf>
  std::vector<std::string> bodies;
  std::vector<int> slotCounts;                 // circular Pattern counts expected (sorted)
  std::vector<double> keyValues;               // key dimension values that must appear among the parameters
};
const Ref kRefs[] = {
    {"swc_rr_001", "ref_swc_rr_001.pcad", "radrad_swirler_SWC-RR-001.pdf", {"SwirlCup"}, {8, 10},
     {50, 10, 27, 17, 19, 43, 47, 32, 22, 34, 24, 14, 30, 3.5}},
    {"atm_ps_001", "ref_atm_ps_001.pcad", "simplex_atomizer_ATM-PS-001.pdf", {"NozzleTip", "SwirlerInsert"}, {3, 3},
     {0.8, 3.2, 5, 8, 2, 0.5, 0.6, 4.2, 5.8, 10, 1.35}},
};

struct RefMetrics {
  bool ok = false;
  int bodies = 0;
  double volume = 0, xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
  std::vector<double> bodyVolumes;
  std::vector<std::string> bodyNames, featureTypes;
  std::vector<int> patternCounts;
  std::map<std::string, double> params;
  std::string log;
};

RefMetrics measure(const Document& doc) {
  RefMetrics m;
  try {
    auto vals = doc.evaluateParams();
    m.params = vals;
    auto r = pc::model::regenerate(doc, vals);
    m.ok = r.ok;
    for (auto& st : r.status) if (!st.ok) m.log += st.featureId + ": " + st.message + "\n";
    if (r.shape.IsNull()) return m;
    auto mp = pc::model::massProperties(r.shape);
    m.volume = mp.volume; m.xmin = mp.xmin; m.ymin = mp.ymin; m.zmin = mp.zmin; m.xmax = mp.xmax; m.ymax = mp.ymax; m.zmax = mp.zmax;
    m.bodies = (int)r.bodies.size();
    for (auto& b : r.bodies) {
      m.bodyNames.push_back(b.name);
      m.bodyVolumes.push_back(pc::model::massProperties(b.shape).volume);
    }
    for (auto& f : doc.features) {
      if (f.suppressed) continue;
      m.featureTypes.push_back(std::string(toString(f.type)) + ":" + f.id);
      if (f.type == FeatureType::Pattern && f.prop("kind") == "circular")
        m.patternCounts.push_back((int)std::lround(evalWith(f.prop("count", "1"), vals)));
    }
    std::sort(m.patternCounts.begin(), m.patternCounts.end());
  } catch (const std::exception& e) { m.log += std::string("exception: ") + e.what(); }
  return m;
}

std::vector<double> sortedExtents(const RefMetrics& m) {
  std::vector<double> e{m.xmax - m.xmin, m.ymax - m.ymin, m.zmax - m.zmin};
  std::sort(e.begin(), e.end());
  return e;
}

json readJson(const fs::path& p) {
  std::ifstream f(p);
  return f ? json::parse(f, nullptr, false) : json();
}

Document loadRef(const Ref& r) {
  Document d;
  std::string err;
  EXPECT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/" + r.pcad, &err)) << err;
  return d;
}

json toGt(const Ref& r, const RefMetrics& m, const Document& doc) {
  json j;
  j["name"] = r.key; j["pcad"] = std::string("samples/") + r.pcad; j["pdf"] = std::string("tests/drawing/real/") + r.pdf;
  j["units"] = "mm"; j["frame"] = "model Z = drawing axis x (x=0 at the forward / exit face)";
  j["body_count"] = m.bodies; j["body_names"] = m.bodyNames; j["body_volumes"] = m.bodyVolumes;
  j["volume"] = m.volume;
  j["bbox"] = {{"min", {m.xmin, m.ymin, m.zmin}}, {"max", {m.xmax, m.ymax, m.zmax}}, {"sorted_extents", sortedExtents(m)}};
  j["slot_pattern_counts"] = m.patternCounts;
  j["features"] = m.featureTypes;
  j["key_values"] = r.keyValues;
  json pj = json::object();
  for (auto& p : doc.params.all()) pj[p.name] = {{"expr", p.expression}, {"value", m.params.at(p.name)}};
  j["parameters"] = pj;
  j["tolerances"] = {{"volume_rel", 0.03}, {"bbox_rel", 0.02}};
  return j;
}

// Independent check (numpy point-in-solid sampling of the drawing's analytic definition, /tmp-script in R4_REFERENCE.md).
const std::map<std::string, double> kIndependentVolume = {{"swc_rr_001", 26273.8}, {"atm_ps_001", 364.37 + 107.62}};

}  // namespace

// ---------------------------------------------------------------------------------------------- reference models
TEST(RealRef, ModelsRegenerateAndMatchGroundTruth) {
  const bool write = std::getenv("PC_R4_WRITE_GT") != nullptr;
  for (auto& r : kRefs) {
    SCOPED_TRACE(r.key);
    Document doc = loadRef(r);
    RefMetrics m = measure(doc);
    ASSERT_TRUE(m.ok) << m.log;
    EXPECT_EQ(m.bodies, (int)r.bodies.size());
    EXPECT_EQ(m.bodyNames, r.bodies);
    EXPECT_EQ(m.patternCounts, [&] { auto v = r.slotCounts; std::sort(v.begin(), v.end()); return v; }());
    // independent numeric volume (grid sampling of the drawing definition) within 1 %
    EXPECT_NEAR(m.volume, kIndependentVolume.at(r.key), 0.01 * kIndependentVolume.at(r.key));
    if (write) {
      fs::create_directories(kGt);
      std::ofstream(kGt / (std::string(r.key) + ".json")) << toGt(r, m, doc).dump(1) << "\n";
      auto rr = pc::model::regenerate(doc);
      std::string err;
      pc::model::exportSTEP(rr.shape, (kGt / (std::string(r.key) + ".step")).string(), &err);
      continue;
    }
    json gt = readJson(kGt / (std::string(r.key) + ".json"));
    ASSERT_FALSE(gt.is_discarded() || gt.is_null()) << "ground truth json missing (run with PC_R4_WRITE_GT=1)";
    EXPECT_EQ(gt["body_count"].get<int>(), m.bodies);
    EXPECT_NEAR(gt["volume"].get<double>(), m.volume, 1e-6 * m.volume);
    auto e = sortedExtents(m);
    for (int i = 0; i < 3; ++i) EXPECT_NEAR(gt["bbox"]["sorted_extents"][i].get<double>(), e[i], 1e-6);
    EXPECT_EQ(gt["features"].size(), m.featureTypes.size());
    // dims json exists and every key value of the sheet is a model parameter value
    json dims = readJson(kGt / (std::string(r.key) + ".dims.json"));
    ASSERT_TRUE(dims.contains("dims"));
    EXPECT_GT(dims["dims"].size(), 15u);
    for (double kv : r.keyValues) {
      bool found = false;
      for (auto& [n, v] : m.params) found |= std::abs(v - kv) < 1e-9;
      EXPECT_TRUE(found) << "no parameter with value " << kv;
    }
  }
}

TEST(RealRef, KnownGeometry) {
  {  // swirl cup: Ø50 x 30, axis = Z, slots cut (volume well below the slot-free revolve)
    RefMetrics m = measure(loadRef(kRefs[0]));
    EXPECT_NEAR(m.xmax - m.xmin, 50, 1e-6); EXPECT_NEAR(m.ymax - m.ymin, 50, 1e-6);
    EXPECT_NEAR(m.zmin, 0, 1e-6); EXPECT_NEAR(m.zmax, 30, 1e-6);
  }
  {  // atomizer: Ø8 x 10, two separate solids (tip + insert)
    RefMetrics m = measure(loadRef(kRefs[1]));
    EXPECT_NEAR(m.xmax - m.xmin, 8, 1e-6); EXPECT_NEAR(m.zmin, 0, 1e-6); EXPECT_NEAR(m.zmax, 10, 1e-6);
    ASSERT_EQ(m.bodyVolumes.size(), 2u);
    // insert Ø5 x 5.8 = 113.9 mm3 minus 3 slots (0.5x0.5x~1.9) and 3 grooves (0.6x0.5x5.8 ~ 1.7 each)
    EXPECT_GT(m.bodyVolumes[1], 100); EXPECT_LT(m.bodyVolumes[1], 113.9);
  }
}

// ---------------------------------------------------------------------------------------------- pipeline acceptance
namespace {
void acceptance(const Ref& r) {
  const fs::path pdf = kReal / r.pdf;
  ASSERT_TRUE(fs::exists(pdf)) << pdf;
  json gt = readJson(kGt / (std::string(r.key) + ".json"));
  ASSERT_FALSE(gt.is_null() || gt.is_discarded());

  PipelineOptions opts;
  opts.ingest.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  DrawingIR ir;
  StageResult sr = runPipeline(pdf.string(), ir, Stage::Ingest, Stage::Synthesize, opts);
  ASSERT_TRUE(sr.ok()) << sr.message;
  ASSERT_FALSE(ir.synthesis.empty()) << "pipeline produced no synthesis candidate";

  // the top-ranked candidate (what the wizard / CLI select) must match the ground truth
  std::string firstReport;
  for (auto& cand : std::vector<SynthesisCandidate>{ir.synthesis.front()}) {
    Document doc;
    std::string err;
    if (mergeIntoDocument(cand, doc, &err).empty()) { if (firstReport.empty()) firstReport = "merge: " + err; continue; }
    RefMetrics m = measure(doc);
    std::string rep;
    auto fail = [&](const std::string& s) { rep += "  - " + s + "\n"; };
    if (!m.ok) fail("regenerate failed: " + m.log);
    if (m.bodies != gt["body_count"].get<int>()) fail("body count " + std::to_string(m.bodies) + " != " + std::to_string(gt["body_count"].get<int>()));
    const double gv = gt["volume"].get<double>();
    if (std::abs(m.volume - gv) > 0.03 * gv) fail("volume " + std::to_string(m.volume) + " vs " + std::to_string(gv) + " (>3%)");
    auto e = sortedExtents(m);
    for (int i = 0; i < 3; ++i) {
      double g = gt["bbox"]["sorted_extents"][i].get<double>();
      if (std::abs(e[i] - g) > 0.02 * g) fail("bbox extent[" + std::to_string(i) + "] " + std::to_string(e[i]) + " vs " + std::to_string(g) + " (>2%)");
    }
    // slot counts: every expected circular-pattern count must be present (multiset inclusion)
    auto have = m.patternCounts;
    for (int c : gt["slot_pattern_counts"].get<std::vector<int>>()) {
      auto it = std::find(have.begin(), have.end(), c);
      if (it == have.end()) fail("no circular pattern with count " + std::to_string(c)); else have.erase(it);
    }
    // key parameters: every key dimension value of the sheet is present among the parameters (names may differ)
    for (double kv : gt["key_values"].get<std::vector<double>>()) {
      bool found = false;
      for (auto& [n, v] : m.params) found |= std::abs(v - kv) < 1e-6 * std::max(1.0, std::abs(kv));
      if (!found) fail("no parameter with value " + std::to_string(kv));
    }
    if (rep.empty()) { SUCCEED() << "candidate " << cand.id << " matches ground truth"; return; }
    if (firstReport.empty()) firstReport = "candidate " + std::to_string(cand.id) + " (" + toString(cand.strategy) + "):\n" + rep;
  }
  ADD_FAILURE() << "no synthesis candidate matches the ground truth of " << r.key << "; best:\n" << firstReport;
}
}  // namespace

TEST(RealAcceptance, SwirlCupSWC_RR_001) {
  acceptance(kRefs[0]);
}
TEST(RealAcceptance, SimplexAtomizerATM_PS_001) {
  acceptance(kRefs[1]);
}
