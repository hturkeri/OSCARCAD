// WP-1: vector PDF ingest + sniff tests.
#include "wp1_helpers.h"
#include "wp1_pdfmini.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <fstream>

using namespace pc::drawing;

namespace {
constexpr double kPt = 25.4 / 72.0;

std::string enjektorPdf() {
  for (const char* p : {PC_DRAWING_TEST_DIR "/real/ef502211-09-DB01-4A-E.pdf", "/mnt/user-data/uploads/ef502211-09-DB01-4A-E.pdf"})
    if (std::filesystem::exists(p)) return p;
  return {};
}

std::string miniPdf() {
  const std::string path = wp1::tmpDir("pdf") + "/mini.pdf";
  // 0.5 mm-ish solid line, dashed line, 4-Bezier circle r=20pt at (300,300), text "Ø20" (WinAnsi D8)
  const double k = 0.5523 * 20;
  std::ostringstream c;
  c << "1.4 w 0 0 0 RG 100 100 m 400 100 l S\n"
    << "[6 3] 0 d 0.7 w 100 150 m 400 150 l S [] 0 d\n"
    << "1.4 w 320 300 m 320 " << 300 + k << " " << 300 + k << " 320 300 320 c "
    << 300 - k << " 320 280 " << 300 + k << " 280 300 c 280 " << 300 - k << " " << 300 - k << " 280 300 280 c "
    << 300 + k << " 280 320 " << 300 - k << " 320 300 c S\n"
    << "BT /F1 10 Tf 1 0 0 1 200 200 Tm (\xD8" "20) Tj ET\n";
  wp1::writeMiniPdf(path, c.str());
  return path;
}
}  // namespace

#if PC_DRAWING_HAVE_QPDF
TEST(PdfIngest, MiniPageGeometryAndText) {
  PrimitiveSheet raw; int id = 1; IngestOptions o;
  StageResult r = ingestPdfVector(miniPdf(), o, raw, id);
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_NEAR(raw.sheet.hi.x, 842 * kPt, 0.01);
  EXPECT_NEAR(raw.sheet.hi.y, 595 * kPt, 0.01);
  bool solid = false, dashed = false;
  for (auto& p : raw.prims)
    if (p.kind == PrimKind::Line && std::abs(p.p0.y - 100 * kPt) < 0.01) {
      solid = true; EXPECT_NEAR(p.style.width, 1.4 * kPt, 1e-3); EXPECT_NEAR(std::abs(p.p1.x - p.p0.x), 300 * kPt, 0.01);
    } else if (p.kind == PrimKind::Line && std::abs(p.p0.y - 150 * kPt) < 0.01) {
      dashed = true; ASSERT_EQ(p.style.dash.size(), 2u); EXPECT_NEAR(p.style.dash[0], 6 * kPt, 1e-3);
    }
  EXPECT_TRUE(solid); EXPECT_TRUE(dashed);
  ASSERT_FALSE(raw.texts.empty());
  std::string all; for (auto& t : raw.texts) all += t.utf8;
  EXPECT_EQ(all, "Ø20");
  EXPECT_NEAR(raw.texts.front().origin.x, 200 * kPt, 0.05);

  PrimitiveSheet n; NormalizeOptions no;
  ASSERT_TRUE(normalize(raw, no, n, id).ok());
  auto circ = std::find_if(n.prims.begin(), n.prims.end(), [](const Primitive& p) { return p.kind == PrimKind::Circle; });
  ASSERT_NE(circ, n.prims.end()) << "4-Bezier circle not recovered";
  EXPECT_NEAR(circ->radius, 20 * kPt, 0.02);
  EXPECT_NEAR(circ->center.x, 300 * kPt, 0.02);
  ASSERT_EQ(n.texts.size(), 1u);
  EXPECT_EQ(n.texts[0].utf8, "Ø20");
}

TEST(PdfIngest, ErrorsAndPageRange) {
  PrimitiveSheet raw; int id = 1; IngestOptions o;
  EXPECT_FALSE(ingestPdfVector("/nonexistent/x.pdf", o, raw, id).ok());
  o.page = 5;
  EXPECT_FALSE(ingestPdfVector(miniPdf(), o, raw, id).ok());
}

TEST(PdfIngest, EnjektorDimensionStringsExact) {
  const std::string f = enjektorPdf();
  if (f.empty()) GTEST_SKIP() << "enjektör PDF yok";
  PrimitiveSheet raw, n; int id = 1; IngestOptions o; NormalizeOptions no;
  ASSERT_TRUE(ingestPdfVector(f, o, raw, id).ok());
  ASSERT_TRUE(normalize(raw, no, n, id).ok());
  std::vector<std::string> texts;
  for (auto& t : n.texts) texts.push_back(t.utf8);
  for (const char* want : {"16", "-0.25", "7.5±0.5", "14±0.2", "Ø16", "-0.5", "20.5 max.", "Ø11.8 max.", "R8", "-0.2",
                           "7.25", "-0.1", "45°", "60°", "80°"})
    EXPECT_NE(std::find(texts.begin(), texts.end(), want), texts.end()) << want;
  int arcs = 0, arrows = 0;
  for (auto& p : n.prims) { arcs += p.kind == PrimKind::Arc || p.kind == PrimKind::Circle; arrows += p.kind == PrimKind::Arrowhead; }
  EXPECT_GT(arcs, 100);
  EXPECT_GT(arrows, 10);
}
#endif

TEST(Sniff, Kinds) {
  const std::string d = wp1::tmpDir("sniff");
#if PC_DRAWING_HAVE_QPDF
  {
    SniffResult s = sniff(miniPdf());
    ASSERT_TRUE(s.result.ok()) << s.message;
    EXPECT_EQ(s.kind, SourceKind::VectorPdf);
    EXPECT_EQ(s.pageCount, 1);
    ASSERT_EQ(s.pages.size(), 1u);
    EXPECT_GT(s.pages[0].pathCount, 0);
    EXPECT_GT(s.vectorScore, 0.0);
  }
#endif
  {
    SniffResult s = sniff(PC_DRAWING_TEST_DIR "/fixtures/wp1/dxf/ezdxf_r2010.dxf");
    ASSERT_TRUE(s.result.ok()) << s.message;
    EXPECT_EQ(s.kind, SourceKind::Dxf);
    ASSERT_FALSE(s.dxfLayouts.empty());
    EXPECT_EQ(s.dxfLayouts[0], "");
  }
  {
    std::ofstream(d + "/x.bin", std::ios::binary) << "hello world, not a drawing";
    EXPECT_FALSE(sniff(d + "/x.bin").result.ok());
    EXPECT_FALSE(sniff(d + "/missing.pdf").result.ok());
  }
  {
    // 1x1 PNG with pHYs 11811 px/m (300 dpi)
    const unsigned char png[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0, 1, 0, 0, 0, 1,
                                 8, 0, 0, 0, 0, 0x3A, 0x7E, 0x9B, 0x55, 0, 0, 0, 9, 'p', 'H', 'Y', 's', 0, 0, 0x2E, 0x23, 0, 0,
                                 0x2E, 0x23, 1, 0x78, 0xA5, 0x3F, 0x76};
    std::ofstream(d + "/p.png", std::ios::binary).write(reinterpret_cast<const char*>(png), sizeof png);
    SniffResult s = sniff(d + "/p.png");
    ASSERT_TRUE(s.result.ok()) << s.message;
    EXPECT_EQ(s.kind, SourceKind::RasterImage);
    EXPECT_NEAR(s.dpi, 300, 1);
  }
}
