// P0 bootstrap test (OWNER P0/WP-3; keep passing). Checks that the frozen headers compile together, that the
// contract helpers behave, and that every stage links (stub, fallback or real implementation).
#include "pc/drawing/Drawing.h"
#include "pc/drawing/GroundTruth.h"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace pc::drawing;

TEST(Bootstrap, Version) {
  EXPECT_STREQ(kDrawingIRVersion, "DrawingIR 1.0");
  DrawingIR ir;
  EXPECT_EQ(ir.schema, "pcdrw/1");
  EXPECT_EQ(ir.irVersion, "1.0");
  EXPECT_EQ(ir.newId(), 1);
  EXPECT_EQ(ir.nextId, 2);
  EXPECT_NE(libraryVersion().find("DrawingIR 1.0"), std::string::npos);
}

TEST(Bootstrap, FeatureFlagsMatchRuntime) {
  const auto& f = buildFeatures();
  EXPECT_EQ(f.qpdf, PC_DRAWING_HAVE_QPDF != 0);
  EXPECT_EQ(f.opencv, PC_DRAWING_HAVE_OPENCV != 0);
  EXPECT_EQ(f.tesseract, PC_DRAWING_HAVE_TESSERACT != 0);
  EXPECT_EQ(f.qtpdf, PC_DRAWING_HAVE_QTPDF != 0);
  std::cout << "[features] " << f.summary() << "\n";
}

template <class E> static void roundTripAll() {
  for (std::size_t i = 0; i < EnumNames<E>::count; ++i) {
    const E e = static_cast<E>(static_cast<int>(i) + EnumNames<E>::first);
    E back{};
    ASSERT_TRUE(fromString(toString(e), back)) << toString(e);
    EXPECT_EQ(back, e);
  }
}

TEST(Bootstrap, EnumNames) {
  roundTripAll<SourceKind>(); roundTripAll<PrimKind>(); roundTripAll<ProjectionMethod>(); roundTripAll<ViewRole>();
  roundTripAll<LineRole>(); roundTripAll<DimKind>(); roundTripAll<NativeDimKind>(); roundTripAll<Adjacency>();
  roundTripAll<ParamTag>(); roundTripAll<Strategy>(); roundTripAll<Stage>(); roundTripAll<Severity>();
  roundTripAll<Status>();
  EXPECT_STREQ(toString(Stage::Ingest), "Ingest");
  EXPECT_STREQ(toString(Stage::Synthesize), "Synthesize");
  EXPECT_STREQ(toString(LineRole::Phantom), "Phantom");
  EXPECT_STREQ(toString(static_cast<Stage>(0)), "?");
  LineRole r = LineRole::Visible;
  EXPECT_FALSE(fromString("visible", r));   // case-sensitive
  EXPECT_EQ(r, LineRole::Visible);
  EXPECT_STREQ(labelTr(ViewRole::Section), "Kesit");
}

TEST(Bootstrap, AffineAndScale) {
  const Affine2 m{2, 0, 0, 2, 10, 5};
  const Vec2 p = transformPoint(m, {1, 1});
  EXPECT_DOUBLE_EQ(p.x, 12); EXPECT_DOUBLE_EQ(p.y, 7);
  Affine2 inv{};
  ASSERT_TRUE(invert(m, inv));
  const Vec2 q = transformPoint(inv, p);
  EXPECT_NEAR(q.x, 1, 1e-12); EXPECT_NEAR(q.y, 1, 1e-12);
  const Affine2 c = compose(m, inv);
  for (int i = 0; i < 6; ++i) EXPECT_NEAR(c[i], kIdentityAffine[i], 1e-12);
  Scale s; s.num = 1; s.den = 2;   // 1:2 -> part = 2 x sheet
  EXPECT_DOUBLE_EQ(s.factor(), 2);
  s.num = 3; s.den = 1;            // 3:1 (enjektör)
  EXPECT_DOUBLE_EQ(s.factor(), 1.0 / 3);
}

TEST(Bootstrap, ConfidenceBands) {
  EXPECT_EQ(confBand(0.95f), ConfBand::High);
  EXPECT_EQ(confBand(0.7f), ConfBand::Medium);
  EXPECT_EQ(confBand(0.3f), ConfBand::Low);
  EXPECT_FALSE(usableInStrict(0.5f));
}

TEST(Bootstrap, DimTextTolMid) {
  DimText d; d.nominal = 16; d.tolPlus = 0; d.tolMinus = -0.5;
  EXPECT_DOUBLE_EQ(d.tolMid(), 15.75);
}

TEST(Bootstrap, IRUtilAndClear) {
  DrawingIR ir;
  Primitive p; p.id = ir.newId(); ir.raw.prims.push_back(p); ir.norm.prims.push_back(p);
  ViewRegion v; v.id = ir.newId(); ir.layout.views.push_back(v);
  Dimension d; d.id = ir.newId(); ir.annot.dims.push_back(d);
  ir.annot.roles[p.id] = LineRole::Visible;
  ir.stageVersion[Stage::Layout] = "x"; ir.stageVersion[Stage::Normalize] = "y";
  ir.lastStage = static_cast<int>(Stage::Annotate);
  EXPECT_NE(findPrim(ir.norm, p.id), nullptr);
  EXPECT_EQ(findPrim(ir.norm, 999), nullptr);
  EXPECT_NE(findView(ir.layout, v.id), nullptr);
  EXPECT_NE(findDimension(ir.annot, d.id), nullptr);
  EXPECT_EQ(roleOf(ir, p.id), LineRole::Visible);
  clearFromStage(ir, Stage::Layout);
  EXPECT_EQ(ir.layout.views.size(), 0u);
  EXPECT_EQ(ir.annot.dims.size(), 0u);
  EXPECT_EQ(ir.norm.prims.size(), 1u);
  EXPECT_EQ(ir.stageVersion.count(Stage::Layout), 0u);
  EXPECT_EQ(ir.stageVersion.count(Stage::Normalize), 1u);
  EXPECT_EQ(ir.lastStage, static_cast<int>(Stage::Normalize));
  EXPECT_EQ(ir.nextId, 4);   // ids never reused
}

TEST(Bootstrap, StageResultHelpers) {
  auto r = StageResult::notImplemented("x");
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status, Status::NotImplemented);
  ASSERT_EQ(r.diagnostics.size(), 1u);
  EXPECT_EQ(r.diagnostics[0].code, "stub.notImplemented");
  auto u = StageResult::unavailable("ingestRaster", "opencv");
  EXPECT_EQ(u.status, Status::Unavailable);
  EXPECT_EQ(u.diagnostics[0].code, "dep.missing.opencv");
  EXPECT_TRUE(StageResult::success().ok());
  NormalizeOptions o = NormalizeOptions::forRaster(300);
  EXPECT_NEAR(o.snapTol, 1.5 * 25.4 / 300, 1e-12);
  EXPECT_DOUBLE_EQ(o.arcFitTol, 0.25);
}

// Every public entry point links and returns a well-formed result (stub, dependency fallback or real code).
TEST(Bootstrap, AllStagesLink) {
  const std::string none = std::string(PC_DRAWING_TMP_DIR) + "/does-not-exist.pdf";
  DrawingIR ir;
  PrimitiveSheet sheet;
  IngestOptions io;
  int nextId = 1;
  (void)sniff(none);
  EXPECT_FALSE(ingestPdfVector(none, io, sheet, nextId).ok());
  EXPECT_FALSE(ingestDxf(none, io, sheet, nextId).ok());
  RasterImage img;
  std::string err;
  EXPECT_FALSE(loadRasterFile(none, img, &err));
  EXPECT_FALSE(extractPdfPageImage(none, 0, img, &err));
  EXPECT_FALSE(ingestRaster(img, io, sheet, nextId).ok());   // empty image is never Ok
  (void)normalize(ir.raw, NormalizeOptions{}, ir.norm, ir.nextId);
  (void)parseDimText("Ø16 +0/-0.5");
  EXPECT_EQ(rankDimTextHypotheses({}, {}), -1);
  (void)formatDimText(DimText{});
  InterpretOptions o;
  (void)analyzeLayout(ir, o); (void)annotate(ir, o); (void)buildViews(ir, o);
  (void)reconstructSketches(ir, o); (void)synthesize(ir, o);
  PipelineOptions po;
  EXPECT_FALSE(runPipeline(none, ir, Stage::Ingest, Stage::Synthesize, po).ok());
  (void)applyUserEdits(ir, Stage::Layout);
  pc::Document doc;
  (void)mergeIntoDocument(SynthesisCandidate{}, doc, &err);
  (void)toJson(ir);
  EXPECT_FALSE(loadIR(ir, none, &err));
  GroundTruth gt;
  (void)evaluate(ir, gt);
  (void)irFromGroundTruth(gt);
}
