// R1: glyph-outline text (vector PDF text drawn as filled paths) — detection, recognition, ground-truth metrics.
#include "pc/drawing/Drawing.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#if PC_DRAWING_HAVE_QPDF
#include "../../src/drawing/glyphtext/GlyphText.h"
#endif
#include "wp1_pdfmini.h"

using namespace pc::drawing;

#if PC_DRAWING_HAVE_QPDF
namespace {

std::string norm(const std::string& s) {   // compare modulo whitespace runs
  std::string o;
  bool sp = false;
  for (char c : s) {
    if (c == ' ' || c == '\t' || c == '\n') { sp = !o.empty(); continue; }
    if (sp) o.push_back(' ');
    sp = false;
    o.push_back(c);
  }
  return o;
}
std::string squeeze(const std::string& s) { std::string o; for (char c : s) if (c != ' ') o.push_back(c); return o; }

struct GtItem { std::string text, kind; bool partial = false; };
std::vector<GtItem> loadGt(const std::string& path) {
  std::vector<GtItem> v;
  std::ifstream f(path);
  if (!f) return v;
  nlohmann::json j; f >> j;
  for (auto& it : j.at("items")) v.push_back({it.at("text").get<std::string>(), it.value("kind", "note"), it.value("partial", false)});
  return v;
}

struct Score { int dims = 0, dimsExact = 0, all = 0, allExact = 0, allFound = 0; std::vector<std::string> missed; };

// exact = some run equals the item (whitespace-normalised; "partial" items: contained in a run).
// found = squeezed item contained in some squeezed run (recall of the characters, spacing ignored).
Score score(const std::vector<GtItem>& gt, const std::vector<TextRun>& runs) {
  Score s;
  std::vector<std::string> rn, rs;
  for (auto& r : runs) { rn.push_back(norm(r.utf8)); rs.push_back(squeeze(r.utf8)); }
  std::vector<char> used(runs.size(), 0);
  for (auto& g : gt) {
    const std::string n = norm(g.text), q = squeeze(g.text);
    bool exact = false, found = false;
    for (std::size_t i = 0; i < runs.size() && !exact; ++i) {
      if (used[i] && !g.partial) continue;
      if (g.partial ? rn[i].find(n) != std::string::npos : rn[i] == n) { exact = true; if (!g.partial) used[i] = 1; }
    }
    for (std::size_t i = 0; i < runs.size() && !found; ++i) found = rs[i].find(q) != std::string::npos;
    found |= exact;
    ++s.all; s.allExact += exact; s.allFound += found;
    if (g.kind == "dim") { ++s.dims; s.dimsExact += exact; }
    if (!exact) s.missed.push_back(g.kind + ": " + g.text);
  }
  return s;
}

struct RealCase { const char* pdf; const char* gt; };

void runReal(const RealCase& c, double minDimExact, const std::vector<std::string>& mustRead, double scaleNum, double scaleDen) {
  const std::string pdf = std::string(PC_DRAWING_TEST_DIR) + "/real/" + c.pdf;
  const std::string gtp = std::string(PC_DRAWING_TEST_DIR) + "/real/" + c.gt;
  if (!std::filesystem::exists(pdf) || !std::filesystem::exists(gtp)) GTEST_SKIP() << "missing " << pdf;
  if (!buildFeatures().tesseract) GTEST_SKIP() << "no Tesseract";
  IngestOptions io; io.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  PrimitiveSheet raw; int id = 1;
  const StageResult r = ingestPdfVector(pdf, io, raw, id);
  ASSERT_TRUE(r.ok()) << r.message;
  int outlineRuns = 0;
  for (auto& t : raw.texts) outlineRuns += t.asOutlines;
  ASSERT_GT(outlineRuns, 10);
  // no glyph contour left in the geometry: small filled curved/complex shapes
  int leftovers = 0;
  for (auto& p : raw.prims)
    if (p.style.filled && !p.style.stroked && p.kind == PrimKind::Bezier && p.pts.size() > 12) {
      Box2 b = emptyBox(); for (auto& q : p.pts) expand(b, q);
      if (std::max(width(b), height(b)) < 4) ++leftovers;
    }
  EXPECT_LE(leftovers, 2);
  const auto gt = loadGt(gtp);
  ASSERT_FALSE(gt.empty());
  const Score s = score(gt, raw.texts);
  const double dimRate = s.dims ? static_cast<double>(s.dimsExact) / s.dims : 1;
  std::printf("[glyphtext] %s: runs=%d dims exact %d/%d (%.1f%%), all exact %d/%d (%.1f%%), recall(squeezed) %d/%d (%.1f%%)\n",
              c.pdf, outlineRuns, s.dimsExact, s.dims, 100 * dimRate, s.allExact, s.all, 100.0 * s.allExact / s.all,
              s.allFound, s.all, 100.0 * s.allFound / s.all);
  for (auto& m : s.missed) std::printf("[glyphtext]   not exact: %s\n", m.c_str());
  if (std::getenv("PC_GLYPHTEXT_DUMP"))
    for (auto& t : raw.texts)
      std::printf("[glyphtext]   run rot=%6.1f h=%.2f conf=%.2f %-18s '%s'\n", t.rotDeg, t.height, t.conf,
                  t.attrs.count("glyphtext.method") ? t.attrs.at("glyphtext.method").c_str() : "", t.utf8.c_str());
  EXPECT_GE(dimRate, minDimExact);
  for (auto& m : mustRead) {
    bool ok = false;
    for (auto& t : raw.texts) ok |= norm(t.utf8).find(m) != std::string::npos;
    EXPECT_TRUE(ok) << "not read: " << m;
  }
  // S0..S3 through the public pipeline: the title-block scale now comes from the outline text
  DrawingIR ir;
  PipelineOptions po; po.ingest.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  const StageResult pr = runPipeline(pdf, ir, Stage::Ingest, Stage::Layout, po);
  ASSERT_TRUE(pr.ok()) << pr.message;
  EXPECT_DOUBLE_EQ(ir.layout.title.scale.num, scaleNum) << ir.layout.title.scale.evidence;
  EXPECT_DOUBLE_EQ(ir.layout.title.scale.den, scaleDen) << ir.layout.title.scale.evidence;
}

}  // namespace

TEST(GlyphText, RasterizeRingBothRules) {
  // 10x10 square with a 4x4 hole: opposite winding -> ring under both rules; same winding -> ring only in even-odd
  std::vector<Vec2> outer{{0, 0}, {10, 0}, {10, 10}, {0, 10}}, hole{{3, 3}, {3, 7}, {7, 7}, {7, 3}}, same{{3, 3}, {7, 3}, {7, 7}, {3, 7}};
  const auto nz = glyphtext::rasterize({&outer, &same}, Box2{{0, 0}, {10, 10}}, 2.0, 1, false);
  EXPECT_EQ(nz.px[11u * nz.w + 11], 0);   // nonzero fills the same-winding hole
  const auto eo = glyphtext::rasterize({&outer, &same}, Box2{{0, 0}, {10, 10}}, 2.0, 1);
  EXPECT_EQ(eo.px[11u * eo.w + 11], 255);
  const auto img = glyphtext::rasterize({&outer, &hole}, Box2{{0, 0}, {10, 10}}, 2.0, 1, false);
  ASSERT_EQ(img.w, 22);
  ASSERT_EQ(img.h, 22);
  auto at = [&](int x, int y) { return img.px[static_cast<std::size_t>(y) * img.w + x]; };
  EXPECT_EQ(at(0, 0), 255);     // padding
  EXPECT_EQ(at(2, 2), 0);       // ink
  EXPECT_EQ(at(11, 11), 255);   // hole
  EXPECT_EQ(at(19, 19), 0);
}

TEST(GlyphText, NormalTextPdfUntouched) {
  // real text + filled arrowheads + a small filled curved dot: nothing may become glyph text
  const std::string path = std::string(PC_DRAWING_TMP_DIR) + "/glyphtext_normal.pdf";
  std::ostringstream c;
  c << "1 w 100 100 m 400 100 l S\n";
  for (int i = 0; i < 6; ++i) c << 100 + 40 * i << " 100 m " << 110 + 40 * i << " 103 l " << 110 + 40 * i << " 97 l h f\n";
  c << "200 300 m 200 302 202 304 204 304 c 206 304 208 302 208 300 c 208 298 206 296 204 296 c 202 296 200 298 200 300 c f\n";
  c << "BT /F1 10 Tf 1 0 0 1 200 200 Tm (SECTION A-A) Tj ET\n";
  wp1::writeMiniPdf(path, c.str());
  IngestOptions io; io.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  PrimitiveSheet raw; int id = 1;
  ASSERT_TRUE(ingestPdfVector(path, io, raw, id).ok());
  for (auto& t : raw.texts) EXPECT_FALSE(t.asOutlines);
  int filled = 0;
  for (auto& p : raw.prims) filled += p.style.filled;
  EXPECT_EQ(filled, 7);
}

TEST(GlyphText, RealRadialSwirler) {
  runReal({"radrad_swirler_SWC-RR-001.pdf", "radrad_swirler_SWC-RR-001.gt_text.json"}, 0.95,
          {"SECTION A-A", "SECTION B-B PRIMARY SWIRLER", "SECTION C-C SECONDARY SWIRLER", "2:1", "SWC-RR-001"}, 2, 1);
}

TEST(GlyphText, RealSimplexAtomizer) {
  runReal({"simplex_atomizer_ATM-PS-001.pdf", "simplex_atomizer_ATM-PS-001.gt_text.json"}, 0.95,
          {"SECTION A-A", "10:1", "ATM-PS-001", "\xC3\x98" "0.8 ORIFICE", "0.5 x 45\xC2\xB0"}, 10, 1);
}
#endif
