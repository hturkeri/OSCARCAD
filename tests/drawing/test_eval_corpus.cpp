// WP-1: synthetic corpus round trip — generated sheet.pdf / sheet_pscript.pdf / sheet.dxf / sheet_exploded.dxf are
// ingested + normalized and evaluated against gt.json (SPEC §6.2 S1/S2 metrics). Phase-1 gates below.
#include "wp1_helpers.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <iomanip>
#include <iostream>

using namespace pc::drawing;
namespace fs = std::filesystem;

namespace {
std::vector<std::string> gtFiles(const std::string& root) {
  std::vector<std::string> v;
  if (!fs::exists(root)) return v;
  for (auto& e : fs::recursive_directory_iterator(root)) if (e.path().filename() == "gt.json") v.push_back(e.path().string());
  std::sort(v.begin(), v.end());
  return v;
}
struct Gate { const char* file; double geomRecall, primRecall, dimTextExact, textExact; };

void runCorpus(const std::string& root, bool requireAll) {
  const auto files = gtFiles(root);
  if (files.empty()) GTEST_SKIP() << "derlem yok: " << root;
  const Gate gates[] = {
#if PC_DRAWING_HAVE_QPDF
      {"sheet.pdf", 0.99, 0.98, 1.0, 1.0},
      {"sheet_pscript.pdf", 0.99, 0.75, 1.0, 1.0},   // dashed circles + short centre lines stay pieces (phase 2)
      {"sheet_outline.pdf", 0.99, 0.0, 0.0, 0.0},   // text as outlines: geometry only (text recognition = phase 2)
#endif
      {"sheet.dxf", 0.99, 0.98, 1.0, 1.0},
      {"sheet_exploded.dxf", 0.99, 0.98, 1.0, 1.0},
  };
  std::cout << std::left << std::setw(16) << "case" << std::setw(20) << "file" << " geomR  primR  primP  arcR   txtEx  dimTxt endRms\n";
  for (auto& gf : files) {
    GroundTruth gt; std::string err;
    ASSERT_TRUE(loadGroundTruth(gf, gt, &err)) << err;
    for (auto& g : gates) {
      const std::string f = (fs::path(gf).parent_path() / g.file).string();
      if (!fs::exists(f)) { if (requireAll) ADD_FAILURE() << "eksik " << f; continue; }
      DrawingIR ir; int id = 1; IngestOptions io;
      const bool dxf = std::string(g.file).find(".dxf") != std::string::npos;
      StageResult r = dxf ? ingestDxf(f, io, ir.raw, id) : ingestPdfVector(f, io, ir.raw, id);
      ASSERT_TRUE(r.ok()) << f << ": " << r.message;
      ASSERT_TRUE(normalize(ir.raw, NormalizeOptions{}, ir.norm, id).ok()) << f;
      Metrics m = evaluate(ir, gt);
      auto V = [&](const char* k) { return m.values.count(k) ? m.values[k] : -1.0; };
      std::cout << std::setw(16) << gt.caseName << std::setw(20) << g.file << std::fixed << std::setprecision(3) << " " << V("geom.recall") << "  "
                << V("prim.recall") << "  " << V("prim.precision") << "  " << V("arc.recall") << "  " << V("text.exact") << "  "
                << V("dim.textExact") << "  " << V("geom.endpointRms") << "\n";
      EXPECT_GE(V("geom.recall"), g.geomRecall) << gt.caseName << " " << g.file;
      EXPECT_GE(V("prim.recall"), g.primRecall) << gt.caseName << " " << g.file;
      EXPECT_GE(V("dim.textExact"), g.dimTextExact) << gt.caseName << " " << g.file;
      EXPECT_GE(V("text.exact"), g.textExact) << gt.caseName << " " << g.file;
      if (::testing::Test::HasFailure() && std::getenv("PC_WP1_NOTES"))
        for (auto& n : m.notes) std::cout << "    " << n << "\n";
    }
    // gt -> perfect S2 is a fixed point of the metrics
    Metrics self = evaluate(irFromGroundTruth(gt), gt);
    EXPECT_DOUBLE_EQ(self.values["geom.recall"], 1.0) << gt.caseName;
    EXPECT_DOUBLE_EQ(self.values["text.exact"], 1.0) << gt.caseName;
  }
}
}  // namespace

TEST(EvalCorpus, CommittedCoreSet) { runCorpus(PC_DRAWING_CORPUS_DIR, true); }
TEST(EvalCorpus, GeneratedCorpus) { runCorpus(PC_DRAWING_CORPUS_GEN_DIR, true); }
