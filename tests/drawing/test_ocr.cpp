// WP-2: OCR wrapper (private API src/drawing/ocr/Ocr.h) — tessdata lookup, eng/tur engines, whitelist, n-best.
// Images are rendered at test time with OpenCV (Hershey font); skipped without Tesseract / OpenCV / tessdata.
#include "../../src/drawing/ocr/Ocr.h"
#include "pc/drawing/DimGrammar.h"
#include "pc/drawing/Features.h"
#include <gtest/gtest.h>
#if PC_DRAWING_HAVE_OPENCV
#include <opencv2/imgproc.hpp>
#endif

using namespace pc::drawing;

namespace {
bool haveTess() { return ocr::available() && !std::string(PC_DRAWING_TESSDATA_DIR).empty(); }
}  // namespace

TEST(Ocr, TessdataLookup) {
  if (std::string(PC_DRAWING_TESSDATA_DIR).empty()) GTEST_SKIP() << "tessdata yok";
  EXPECT_FALSE(ocr::resolveTessdataDir(PC_DRAWING_TESSDATA_DIR, "eng").empty());
  EXPECT_TRUE(ocr::resolveTessdataDir("/nonexistent", "xx_nolang").empty());
  EXPECT_EQ(ocr::available(), buildFeatures().tesseract && buildFeatures().opencv);
}

TEST(Ocr, EngineErrorsAreReported) {
  ocr::Engine e("/nonexistent", "xx_nolang");
  EXPECT_FALSE(e.ok());
  EXPECT_FALSE(e.error().empty());
  const auto r = e.recognize({}, 7, "", 300);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.error.empty());
}

#if PC_DRAWING_HAVE_OPENCV
namespace {
cv::Mat renderLine(const std::string& text, double scale = 2.0) {
  int base = 0;
  const cv::Size sz = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, scale, 4, &base);
  cv::Mat m(sz.height + base + 40, sz.width + 40, CV_8U, cv::Scalar(255));
  cv::putText(m, text, cv::Point(20, sz.height + 20), cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0), 4, cv::LINE_AA);
  return m;
}
ocr::GrayView view(const cv::Mat& m) { return {m.cols, m.rows, m.step, m.data}; }
}  // namespace

TEST(Ocr, ReadsDimensionLineWithWhitelist) {
  if (!haveTess()) GTEST_SKIP() << "tesseract / tessdata yok";
  ocr::Engine eng(PC_DRAWING_TESSDATA_DIR, "eng");
  ASSERT_TRUE(eng.ok()) << eng.error();
  EXPECT_EQ(eng.languages(), "eng");
  const cv::Mat img = renderLine("12.5");
  const auto r = eng.recognize(view(img), 7, "0123456789.", 300);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.text, "12.5");
  EXPECT_GT(r.conf, 0.5f);
  EXPECT_LE(r.conf, 1.0f);
  for (const auto& a : r.alternatives) {
    EXPECT_NE(a.utf8, r.text);
    EXPECT_LE(a.conf, r.conf + 1e-6f);
  }
  const auto r2 = eng.recognize(view(renderLine("80 H7")), 7, "", 300);
  ASSERT_TRUE(r2.ok);
  DimTextContext ctx;
  ctx.fromOcr = true;
  std::vector<TextHypothesis> hyps{{r2.text, r2.conf}};
  hyps.insert(hyps.end(), r2.alternatives.begin(), r2.alternatives.end());
  DimText d;
  ASSERT_GE(rankDimTextHypotheses(hyps, ctx, &d), 0);
  EXPECT_TRUE(d.valid);
  EXPECT_DOUBLE_EQ(d.nominal, 80);
  EXPECT_EQ(d.fitClass, "H7");
}

TEST(Ocr, RotatedLineReadAfterRotation) {
  if (!haveTess()) GTEST_SKIP() << "tesseract / tessdata yok";
  ocr::Engine eng(PC_DRAWING_TESSDATA_DIR, "eng");
  ASSERT_TRUE(eng.ok());
  cv::Mat img = renderLine("250"), rot, back;
  cv::rotate(img, rot, cv::ROTATE_90_COUNTERCLOCKWISE);   // vertical dimension text (reads bottom -> top)
  cv::rotate(rot, back, cv::ROTATE_90_CLOCKWISE);         // what raster ingest tries for rot = 90
  const auto r = eng.recognize(view(back), 7, "0123456789", 300);
  EXPECT_EQ(r.text, "250");
}
#endif
