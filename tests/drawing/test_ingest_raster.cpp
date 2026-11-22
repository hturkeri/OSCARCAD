// WP-2: raster ingest (S1c) — loadRasterFile, ingestRaster on synthetic fixtures (clean / noisy / skewed) with
// metrics, contract checks, and (when present) the committed / generated corpus.
#include "wp2_fixtures.h"
#include "pc/drawing/Confidence.h"
#include "pc/drawing/GroundTruth.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

using namespace pc::drawing;
namespace fs = std::filesystem;

namespace {

IngestOptions rasterOpts() {
  IngestOptions o;
  o.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  if (const char* d = std::getenv("PC_WP2_DEBUG_DIR")) o.debugDir = d;   // writes binarised + overlay PNGs
  return o;
}

bool haveOcr() { return buildFeatures().tesseract && std::string(PC_DRAWING_TESSDATA_DIR).size() > 0; }

struct RunResult { wp2::Metrics m; PrimitiveSheet sheet; StageResult r; };

RunResult runFixture(const std::string& name, const std::string& variant) {
  RunResult rr;
  RasterImage img;
  std::string err;
  EXPECT_TRUE(wp2::makeVariant(name, variant, img, &err)) << err;
  int nextId = 1;
  rr.r = ingestRaster(img, rasterOpts(), rr.sheet, nextId);
  EXPECT_TRUE(rr.r.ok()) << rr.r.message;
  wp2::Truth t;
  EXPECT_TRUE(wp2::loadTruth(name, t, &err)) << err;
  rr.m = wp2::evaluate(rr.sheet, t);
  return rr;
}

std::string pct(const wp2::PR& p) {
  char b[96];
  std::snprintf(b, sizeof b, "R %.2f P %.2f (%d/%d/%d)", p.recall(), p.precision(), p.tp, p.nTruth, p.nDet);
  return b;
}

}  // namespace

TEST(RasterIngest, LoadRasterFileReadsDpi) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  RasterImage img;
  std::string err;
  ASSERT_TRUE(loadRasterFile(wp2::fixtureDir() + "/wp2_flange.png", img, &err)) << err;
  EXPECT_EQ(img.width, 3508);
  EXPECT_EQ(img.height, 2480);
  EXPECT_EQ(img.channels, 1);
  EXPECT_NEAR(img.dpi, 300, 0.5);   // PNG pHYs
  EXPECT_FALSE(loadRasterFile(wp2::fixtureDir() + "/nope.png", img, &err));
  EXPECT_FALSE(err.empty());
}

TEST(RasterIngest, RejectsEmptyAndBadImages) {
  RasterImage img;
  PrimitiveSheet s;
  int id = 1;
  EXPECT_FALSE(ingestRaster(img, rasterOpts(), s, id).ok());
  img.width = 10; img.height = 10; img.channels = 2; img.data.assign(200, 255);
  EXPECT_FALSE(ingestRaster(img, rasterOpts(), s, id).ok());
}

TEST(RasterIngest, BlankPageAndCancel) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  RasterImage img;
  img.width = 1240; img.height = 1754; img.channels = 1; img.dpi = 150;   // A4 portrait @150
  img.data.assign(static_cast<std::size_t>(img.width) * img.height, 255);
  PrimitiveSheet s;
  int id = 5;
  auto r = ingestRaster(img, rasterOpts(), s, id);
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_TRUE(s.prims.empty());
  EXPECT_TRUE(s.texts.empty());
  ASSERT_TRUE(s.raster.has_value());
  EXPECT_NEAR(s.sheet.hi.x, 210, 0.5);
  EXPECT_NEAR(s.sheet.hi.y, 297, 0.5);
  EXPECT_EQ(id, 5);
  ProgressSink ps;
  ps.cancelled = [] { return true; };
  EXPECT_EQ(ingestRaster(img, rasterOpts(), s, id, &ps).status, Status::Cancelled);
  // DPI estimation from the sheet size when metadata is missing
  img.dpi = 0;
  r = ingestRaster(img, rasterOpts(), s, id);
  ASSERT_TRUE(r.ok());
  EXPECT_TRUE(s.raster->dpiEstimated);
  EXPECT_NEAR(s.raster->dpi, 150, 1e-9);
}

TEST(RasterIngest, CleanFixtureContract) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  auto rr = runFixture("wp2_flange", "clean");
  const auto& s = rr.sheet;
  ASSERT_TRUE(s.raster.has_value());
  EXPECT_EQ(s.source, SourceKind::RasterImage);
  EXPECT_NEAR(s.raster->dpi, 300, 0.5);
  EXPECT_NEAR(s.sheet.hi.x, 297, 0.5);   // A4 landscape
  EXPECT_NEAR(s.sheet.hi.y, 210, 0.5);
  EXPECT_NEAR(s.raster->deskewDeg, 0, 0.05);
  std::set<int> ids;
  for (const auto& p : s.prims) {
    EXPECT_TRUE(ids.insert(p.id).second);
    EXPECT_GE(p.conf, 0.f);
    EXPECT_LE(p.conf, 0.9f);   // raster never claims exact vector evidence
    EXPECT_TRUE(contains(s.sheet, p.kind == PrimKind::Circle || p.kind == PrimKind::Arc ? p.center : p.p0));
    if (p.kind == PrimKind::Arc) { EXPECT_GT(p.a1, p.a0); EXPECT_LE(p.a1 - p.a0, 360); }
  }
  for (const auto& t : s.texts) EXPECT_TRUE(ids.insert(t.id).second);
  // dashed / centre lines carry a dash pattern
  int dashed = 0, center = 0;
  for (const auto& p : s.prims)
    if (p.kind == PrimKind::Line && !p.style.dash.empty()) {
      ++dashed;
      if (p.style.dash.size() == 4) ++center;
    }
  EXPECT_GE(dashed, 4);
  EXPECT_GE(center, 2);
  // border / title block found
  EXPECT_TRUE(s.attrs.count("raster.borderBox"));
  EXPECT_TRUE(s.attrs.count("raster.titleBlockBox"));
  EXPECT_GE(rr.m.lines.recall(), 0.85);
  EXPECT_GE(rr.m.circles.recall(), 0.99);
  EXPECT_GE(rr.m.arrows.recall(), 0.7);
}

TEST(RasterIngest, MetricsCleanNoisySkewed) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  std::string report = "| variant | lines R/P | circles R/P | arcs R/P | arrows R/P | text boxes R | dim raw exact | dim exact (grammar) | notes exact | deskew° |\n|---|---|---|---|---|---|---|---|---|---|\n";
  std::map<std::string, wp2::Metrics> agg;
  for (const std::string variant : {"clean", "noisy", "skewed"}) {
    wp2::Metrics acc;
    for (const auto& name : wp2::fixtureNames()) {
      auto rr = runFixture(name, variant);
      wp2::accumulate(acc, rr.m);
      acc.deskewDeg = rr.m.deskewDeg;
      std::printf("[wp2] %-10s %-7s lines %s | circ %s | arcs %s | arrows %s | text %s | dims raw %d grammar %d /%d | notes %d/%d | skew %.3f\n",
                  name.c_str(), variant.c_str(), pct(rr.m.lines).c_str(), pct(rr.m.circles).c_str(), pct(rr.m.arcs).c_str(),
                  pct(rr.m.arrows).c_str(), pct(rr.m.textBoxes).c_str(), rr.m.dimRawExact, rr.m.dimGrammarExact,
                  rr.m.dimTotal, rr.m.noteExact, rr.m.noteTotal, rr.m.deskewDeg);
    }
    for (const auto& n : acc.notes) std::printf("[wp2]   %s: %s\n", variant.c_str(), n.c_str());
    char row[512];
    std::snprintf(row, sizeof row, "| %s | %.2f / %.2f | %.2f / %.2f | %.2f / %.2f | %.2f / %.2f | %.2f | %d/%d (%.0f%%) | %d/%d (%.0f%%) | %d/%d | %.3f |\n",
                  variant.c_str(), acc.lines.recall(), acc.lines.precision(), acc.circles.recall(), acc.circles.precision(),
                  acc.arcs.recall(), acc.arcs.precision(), acc.arrows.recall(), acc.arrows.precision(), acc.textBoxes.recall(),
                  acc.dimRawExact, acc.dimTotal, 100.0 * acc.dimRawExact / std::max(1, acc.dimTotal), acc.dimGrammarExact,
                  acc.dimTotal, 100.0 * acc.dimGrammarExact / std::max(1, acc.dimTotal), acc.noteExact, acc.noteTotal, acc.deskewDeg);
    report += row;
    agg[variant] = acc;
  }
  std::printf("%s", report.c_str());
  std::ofstream(std::string(PC_DRAWING_TMP_DIR) + "/wp2_metrics.md") << report;
  // phase-1 gates on our synthetic set (SPEC §7.2: clean PNG geom.recall >= 0.9, dim.textExact >= 0.75)
  EXPECT_GE(agg["clean"].lines.recall(), 0.9);
  EXPECT_GE(agg["clean"].circles.recall(), 0.95);
  EXPECT_GE(agg["clean"].lines.precision(), 0.8);
  EXPECT_GE(agg["noisy"].lines.recall(), 0.8);
  EXPECT_GE(agg["skewed"].lines.recall(), 0.8);
  EXPECT_NEAR(agg["skewed"].deskewDeg, -2.0, 0.1);
  if (haveOcr()) {
    EXPECT_GE(agg["clean"].dimGrammarExact, 0.75 * agg["clean"].dimTotal);
    EXPECT_GE(agg["clean"].dimGrammarExact, agg["clean"].dimRawExact);
    EXPECT_GE(agg["noisy"].dimGrammarExact, 0.5 * agg["noisy"].dimTotal);
    EXPECT_GE(agg["skewed"].dimGrammarExact, 0.5 * agg["skewed"].dimTotal);
  }
}

TEST(RasterIngest, OcrUnavailableStillEmitsTextRegions) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  RasterImage img;
  ASSERT_TRUE(wp2::makeVariant("wp2_shaft", "clean", img));
  IngestOptions o;
  o.tessdataDir = "/definitely/not/here";
  o.ocrLanguages = {"xxx_nolang"};
  PrimitiveSheet s;
  int id = 1;
  // force "no tessdata" by asking for a language that cannot exist next to an empty dir: the engine falls back to
  // system eng data if present, so only check the contract that texts are emitted either way
  auto r = ingestRaster(img, o, s, id);
  ASSERT_TRUE(r.ok());
  EXPECT_GE(s.texts.size(), 8u);
  for (const auto& t : s.texts) {
    EXPECT_EQ(t.font, "ocr");
    EXPECT_GT(t.height, 1.0);
  }
}

// Corpus (WP-1): committed core set tests/drawing/corpus/<family>/<case>/{clean.png,gt.json} and the generated
// corpus in PC_DRAWING_CORPUS_GEN_DIR. Skipped until WP-1's files arrive at merge.
TEST(RasterIngest, CorpusCleanPng) {
  if (!buildFeatures().opencv) GTEST_SKIP() << "OpenCV yok";
  std::vector<fs::path> cases;
  for (const std::string root : {std::string(PC_DRAWING_CORPUS_DIR), std::string(PC_DRAWING_CORPUS_GEN_DIR)}) {
    std::error_code ec;
    if (!fs::is_directory(root, ec)) continue;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec))
      if (it->path().filename() == "clean.png" && fs::exists(it->path().parent_path() / "gt.json")) cases.push_back(it->path());
  }
  if (cases.empty()) GTEST_SKIP() << "korpus yok (WP-1 birleştirmede gelir)";
  int nVis = 0, hitVis = 0, nDim = 0, hitDim = 0, used = 0;
  for (const auto& png : cases) {
    GroundTruth gt;
    std::string err;
    if (!loadGroundTruth((png.parent_path() / "gt.json").string(), gt, &err)) continue;
    RasterImage img;
    if (!loadRasterFile(png.string(), img, &err)) continue;
    PrimitiveSheet s;
    int id = 1;
    auto r = ingestRaster(img, rasterOpts(), s, id);
    ASSERT_TRUE(r.ok()) << png << ": " << r.message;
    ++used;
    // the PNG covers the same sheet as gt.sheet (sheet mm, y up)
    const double tol = 0.5;
    for (const auto& gp : gt.sheet.prims) {
      if (gp.kind != PrimKind::Line) continue;
      auto role = gt.roles.find(gp.id);
      if (role == gt.roles.end() || role->second != LineRole::Visible) continue;
      ++nVis;
      for (const auto& p : s.prims) {
        if (p.kind != PrimKind::Line) continue;
        const double e = std::min(std::max(distance(gp.p0, p.p0), distance(gp.p1, p.p1)), std::max(distance(gp.p0, p.p1), distance(gp.p1, p.p0)));
        if (e <= tol) { ++hitVis; break; }
      }
    }
    for (const auto& gd : gt.dims) {
      ++nDim;
      const std::string want = formatDimText(parseDimText(gd.text));
      for (const auto& t : s.texts)
        if (wp2::normalizeWs(t.utf8) == wp2::normalizeWs(want)) { ++hitDim; break; }
    }
  }
  if (!used) GTEST_SKIP() << "gt.json okunamadı (loadGroundTruth henüz uygulanmadı?)";
  std::printf("[wp2] corpus: %d cases, visible line recall %d/%d, dim text exact %d/%d\n", used, hitVis, nVis, hitDim, nDim);
  if (nVis) EXPECT_GE(static_cast<double>(hitVis) / nVis, 0.8);
  if (nDim && haveOcr()) EXPECT_GE(static_cast<double>(hitDim) / nDim, 0.5);
}
