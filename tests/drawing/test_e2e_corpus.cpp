// End-to-end on the committed WP-1 corpus (integration, WP-3 owned): real ingest (PDF / DXF / raster) -> normalize ->
// interp -> sketch -> 3D, compared with gt.json (strategy, volume, bbox from the gt views, dims, DOF, parameters).
// Writes a metrics table to PC_DRAWING_TMP_DIR/e2e_metrics.md and prints it.
// Env: PC_E2E_ONLY=<family>[,..]  PC_E2E_INPUTS=sheet.pdf,..  (developer filters)
#include "pc/drawing/Drawing.h"
#include "pc/drawing/GroundTruth.h"
#include "pc/Model.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace pc::drawing;
namespace fs = std::filesystem;

namespace {

const char* kFamilies[] = {"plate", "lbracket", "flange", "shaft", "bushing", "injector"};
const char* kInputs[] = {"sheet.pdf", "sheet.dxf", "sheet_exploded.dxf", "sheet_pscript.pdf", "clean.png", "scan_a.jpg"};

bool isRaster(const std::string& in) { return in.find(".png") != std::string::npos || in.find(".jpg") != std::string::npos; }

bool wanted(const char* env, const std::string& v) {
  const char* e = std::getenv(env);
  if (!e || !*e) return true;
  std::stringstream ss(e);
  std::string it;
  while (std::getline(ss, it, ',')) if (it == v) return true;
  return false;
}

struct E2E {
  std::string family, input;
  bool ran = false, ok = false;
  std::string strategy = "-", wantStrategy;
  bool strategyOk = false;
  double volErrPct = NAN, bboxErrPct = NAN;
  int dims = 0, gtDims = 0, sketches = 0, maxDof = -1, params = 0, dimParams = 0;
  double secs = 0;
  std::string note, features;
};

// Part-mm extents of the gt views (visible geometry inside each view box / scale): the set of model extents.
std::vector<double> gtViewExtents(const GroundTruth& gt) {
  std::vector<double> out;
  const double f = gt.scale.factor();
  for (auto& v : gt.views) {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    auto add = [&](Vec2 p) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); };
    for (auto& p : gt.sheet.prims) {
      auto r = gt.roles.find(p.id);
      if (r == gt.roles.end() || r->second != LineRole::Visible) continue;
      std::vector<Vec2> pts;
      if (p.kind == PrimKind::Line) pts = {p.p0, p.p1};
      else if (p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) {
        double a0 = p.kind == PrimKind::Circle ? 0 : p.a0, a1 = p.kind == PrimKind::Circle ? 360 : p.a1;
        if (a1 < a0) a1 += 360;
        for (int k = 0; k <= 64; ++k) {
          double a = (a0 + (a1 - a0) * k / 64.0) * 3.14159265358979323846 / 180.0;
          pts.push_back({p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)});
        }
      } else continue;
      Vec2 c{0, 0};
      for (auto& q : pts) { c.x += q.x / pts.size(); c.y += q.y / pts.size(); }
      if (c.x < v.box.lo.x || c.x > v.box.hi.x || c.y < v.box.lo.y || c.y > v.box.hi.y) continue;
      for (auto& q : pts) add(q);
    }
    if (x1 > x0) { out.push_back((x1 - x0) * f); out.push_back((y1 - y0) * f); }
  }
  return out;
}

E2E runCase(const std::string& family, const std::string& input) {
  E2E m;
  m.family = family;
  m.input = input;
  const fs::path dir = fs::path(PC_DRAWING_CORPUS_DIR) / family / (family + "_00");
  const fs::path file = dir / input;
  GroundTruth gt;
  std::string err;
  if (!fs::exists(file) || !loadGroundTruth((dir / "gt.json").string(), gt, &err)) { m.note = "yok"; return m; }
  m.ran = true;
  m.wantStrategy = toString(gt.expectedStrategy);
  m.gtDims = (int)gt.dims.size();
  PipelineOptions opts;
  opts.ingest.tessdataDir = PC_DRAWING_TESSDATA_DIR;
  DrawingIR ir;
  auto t0 = std::chrono::steady_clock::now();
  StageResult r = runPipeline(file.string(), ir, Stage::Ingest, Stage::Synthesize, opts);
  m.secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  m.ok = r.ok();
  if (!m.ok) m.note = r.message;
  m.dims = (int)ir.annot.dims.size();
  m.sketches = (int)ir.sketches.size();
  for (auto& s : ir.sketches) m.maxDof = std::max(m.maxDof, s.dof);
  if (ir.synthesis.empty()) return m;
  const SynthesisCandidate& best = ir.synthesis.front();
  m.strategy = toString(best.strategy);
  // gt "MultiFeature" (base solid + holes / threads / flats): phase 1 reports it as its base strategy (Revolve /
  // ExtrudeTwoView) with Hole / Pattern features; the feature list is printed so missing details stay visible.
  const Strategy base = family == "lbracket" ? Strategy::ExtrudeTwoView : Strategy::Revolve;
  m.strategyOk = best.strategy == gt.expectedStrategy || (gt.expectedStrategy == Strategy::MultiFeature && best.strategy == base);
  for (auto& f : best.fragment.features)
    if (f.type != pc::FeatureType::Sketch) m.features += (m.features.empty() ? "" : "+") + std::string(pc::toString(f.type));
  m.params = (int)best.fragment.params.all().size();
  for (auto& p : best.fragment.params.all()) m.dimParams += p.exposed ? 1 : 0;
  if (best.strategy == Strategy::SketchOnly) return m;
  pc::Document doc;
  if (mergeIntoDocument(best, doc, &err).empty()) { m.note = "merge: " + err; return m; }
  auto mr = pc::model::regenerate(doc);
  if (!mr.ok) { m.note = "regenerate"; return m; }
  auto mp = pc::model::massProperties(mr.shape);
  if (gt.volume > 0) m.volErrPct = 100.0 * (mp.volume - gt.volume) / gt.volume;
  std::vector<double> ext{mp.xmax - mp.xmin, mp.ymax - mp.ymin, mp.zmax - mp.zmin};
  auto gte = gtViewExtents(gt);
  if (!gte.empty()) {
    double worst = 0;
    for (double e : ext) {
      double bestRel = 1e9;
      for (double g : gte) bestRel = std::min(bestRel, std::abs(e - g) / std::max(g, 1e-6));
      worst = std::max(worst, bestRel);
    }
    m.bboxErrPct = 100.0 * worst;
  }
  return m;
}

std::string fmtPct(double v) {
  if (std::isnan(v)) return "-";
  char b[32];
  std::snprintf(b, sizeof b, "%+.2f%%", v);
  return b;
}

std::vector<E2E>& results() {
  static std::vector<E2E> R;
  static bool done = false;
  if (!done) {
    done = true;
    for (const char* f : kFamilies)
      for (const char* in : kInputs)
        if (wanted("PC_E2E_ONLY", f) && wanted("PC_E2E_INPUTS", in)) R.push_back(runCase(f, in));
    std::ostringstream md;
    md << "| family | input | strategy (gt) | ok | vol err | bbox err | dims (gt) | sketches | max DOF | params (dim) | features | s |\n"
       << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    for (auto& m : R) {
      if (!m.ran) continue;
      char sec[16];
      std::snprintf(sec, sizeof sec, "%.1f", m.secs);
      md << "| " << m.family << " | " << m.input << " | " << m.strategy << " (" << m.wantStrategy << ") | "
         << (m.strategyOk ? "yes" : "NO") << " | " << fmtPct(m.volErrPct) << " | " << fmtPct(m.bboxErrPct) << " | " << m.dims
         << " (" << m.gtDims << ") | " << m.sketches << " | " << m.maxDof << " | " << m.params << " (" << m.dimParams
         << ") | " << (m.features.empty() ? "-" : m.features) << " | " << sec << " |" << (m.note.empty() ? "" : " " + m.note) << "\n";
    }
    std::printf("%s", md.str().c_str());
    std::ofstream(fs::path(PC_DRAWING_TMP_DIR) / "e2e_metrics.md") << md.str();
  }
  return R;
}

}  // namespace

// Vector inputs (real text PDF, DXF with and without DIMENSION entities): the five non-injector families give the
// gt strategy and the gt volume within 2 %.
TEST(E2ECorpus, VectorStrategyAndVolume) {
  if (!buildFeatures().qpdf) GTEST_SKIP() << "qpdf yok (PDF girdileri atlanır)";
  for (auto& m : results()) {
    if (!m.ran || isRaster(m.input) || m.family == "injector") continue;
    if (m.input == "sheet_pscript.pdf") continue;   // flattened arcs / split dashes: measured only (phase 2)
    SCOPED_TRACE(m.family + "/" + m.input);
    EXPECT_TRUE(m.ok) << m.note;
    EXPECT_TRUE(m.strategyOk) << m.strategy << " != " << m.wantStrategy;
    ASSERT_FALSE(std::isnan(m.volErrPct));
    EXPECT_LT(std::abs(m.volErrPct), 2.0);
    EXPECT_GT(m.dimParams, 0);
  }
}

// Injector (thread, hex A/F, cone = phase-2 details): base revolve within 5 % on every vector input.
TEST(E2ECorpus, VectorInjectorBaseRevolve) {
  if (!buildFeatures().qpdf) GTEST_SKIP() << "qpdf yok";
  for (auto& m : results()) {
    if (!m.ran || isRaster(m.input) || m.family != "injector") continue;
    SCOPED_TRACE(m.input);
    EXPECT_TRUE(m.strategyOk) << m.strategy;
    ASSERT_FALSE(std::isnan(m.volErrPct));
    EXPECT_LT(std::abs(m.volErrPct), 5.0);
  }
}

// Raster clean scan: at least a sketch is produced (OCR + vectoriser are phase-1 quality); the plate is exact enough.
TEST(E2ECorpus, RasterCleanGivesSketch) {
  if (!buildFeatures().opencv || !buildFeatures().tesseract) GTEST_SKIP() << "OpenCV/Tesseract yok";
  for (auto& m : results()) {
    if (!m.ran || m.input != "clean.png") continue;
    SCOPED_TRACE(m.family);
    EXPECT_GE(m.sketches, 1);
    EXPECT_NE(m.strategy, "-");
    if (m.family == "plate") {
      EXPECT_TRUE(m.strategyOk) << m.strategy;
      EXPECT_LT(std::abs(m.volErrPct), 3.0);
    }
  }
}

// Real enjektör assembly drawing (tests/drawing/real/ or /mnt/user-data/uploads/; skipped when absent): CAD-export
// dimension lines shortened by the arrow length are assembled, and the 3:1 scale is found from them.
TEST(E2ECorpus, RealEnjektorPdfDimensions) {
  if (!buildFeatures().qpdf) GTEST_SKIP() << "qpdf yok";
  std::string f;
  for (const char* c : {PC_DRAWING_TEST_DIR "/real/ef502211-09-DB01-4A-E.pdf", "/mnt/user-data/uploads/ef502211-09-DB01-4A-E.pdf"})
    if (fs::exists(c)) { f = c; break; }
  if (f.empty()) GTEST_SKIP() << "enjektör PDF yok";
  DrawingIR ir;
  PipelineOptions opts;
  ASSERT_TRUE(runPipeline(f, ir, Stage::Ingest, Stage::Synthesize, opts).ok());
  int bound = 0;
  for (auto& d : ir.annot.dims) bound += d.kind == DimKind::Linear && std::abs(d.residual) < 0.05;
  EXPECT_GE(bound, 5);
  EXPECT_NEAR(ir.layout.scaleFromDims.num / ir.layout.scaleFromDims.den, 3.0, 0.01);
  EXPECT_FALSE(ir.sketches.empty());
}
