// WP-2: dimension text grammar (SPEC §2.10) — parseDimText / rankDimTextHypotheses / formatDimText.
#include "pc/drawing/Confidence.h"
#include "pc/drawing/DimGrammar.h"
#include <gtest/gtest.h>

using namespace pc::drawing;

namespace {

DimText P(const std::string& s, bool ocr = false, bool circle = false, bool angular = false) {
  DimTextContext c;
  c.fromOcr = ocr;
  c.nearCircleOrCylinder = circle;
  c.angular = angular;
  return parseDimText(s, c);
}

}  // namespace

TEST(DimGrammar, PlainLinear) {
  auto d = P("80");
  EXPECT_TRUE(d.valid);
  EXPECT_EQ(d.kind, DimKind::Linear);
  EXPECT_DOUBLE_EQ(d.nominal, 80);
  EXPECT_EQ(formatDimText(d), "80");
  EXPECT_FLOAT_EQ(d.conf, 1.0f);
  d = P("12,5");   // Turkish decimal comma
  EXPECT_TRUE(d.valid);
  EXPECT_DOUBLE_EQ(d.nominal, 12.5);
  EXPECT_EQ(formatDimText(d), "12.5");
}

TEST(DimGrammar, DiameterRadiusSpherical) {
  auto d = P("Ø100");
  EXPECT_EQ(d.kind, DimKind::Diameter);
  EXPECT_DOUBLE_EQ(d.nominal, 100);
  for (const char* s : {"⌀2", "%%c2", "∅2", "ø2"}) {
    d = P(s);
    EXPECT_EQ(d.kind, DimKind::Diameter) << s;
    EXPECT_DOUBLE_EQ(d.nominal, 2) << s;
  }
  d = P("R5");
  EXPECT_EQ(d.kind, DimKind::Radius);
  EXPECT_EQ(formatDimText(d), "R5");
  d = P("SR12.5");
  EXPECT_TRUE(d.spherical);
  EXPECT_EQ(d.kind, DimKind::Radius);
  EXPECT_EQ(formatDimText(d), "SR12.5");
  d = P("SØ20");
  EXPECT_TRUE(d.spherical);
  EXPECT_EQ(d.kind, DimKind::Diameter);
  d = P("□14");
  EXPECT_TRUE(d.squareSym);
  EXPECT_DOUBLE_EQ(d.nominal, 14);
}

TEST(DimGrammar, Tolerances) {
  auto d = P("12±0.1");
  EXPECT_TRUE(d.valid);
  ASSERT_TRUE(d.tolPlus && d.tolMinus);
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0.1);
  EXPECT_DOUBLE_EQ(*d.tolMinus, -0.1);
  EXPECT_EQ(formatDimText(d), "12±0.1");
  d = P("12 +/- 0.1");
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0.1);
  d = P("%%p");
  EXPECT_FALSE(d.valid);
  d = P("Ø40 +0.05/-0");
  EXPECT_EQ(d.kind, DimKind::Diameter);
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0.05);
  EXPECT_DOUBLE_EQ(*d.tolMinus, 0);
  EXPECT_EQ(formatDimText(d), "Ø40 +0.05/-0");
  d = P("Ø16 +0/-0,5");
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0);
  EXPECT_DOUBLE_EQ(*d.tolMinus, -0.5);
  EXPECT_DOUBLE_EQ(d.tolMid(), 15.75);
  EXPECT_EQ(formatDimText(d), "Ø16 +0/-0.5");
  d = P("7.25 -0.1 -0.3");
  EXPECT_DOUBLE_EQ(*d.tolPlus, -0.1);
  EXPECT_DOUBLE_EQ(*d.tolMinus, -0.3);
  d = P("16 0/-0.25");
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0);
  EXPECT_DOUBLE_EQ(*d.tolMinus, -0.25);
  d = P("14−±0.2");   // garbage minus in front of ± -> not a clean dimension
  EXPECT_LT(d.conf, 1.0f);
}

TEST(DimGrammar, Fits) {
  auto d = P("Ø20 H7");
  EXPECT_EQ(d.fitClass, "H7");
  EXPECT_EQ(formatDimText(d), "Ø20 H7");
  d = P("Ø20H7/g6");
  EXPECT_EQ(d.fitClass, "H7/g6");
  d = P("Ø25 js9");
  EXPECT_EQ(d.fitClass, "js9");
  d = P("Ø20 H7 (+0.021/0)");
  EXPECT_EQ(d.fitClass, "H7");
  ASSERT_TRUE(d.tolPlus);
  EXPECT_DOUBLE_EQ(*d.tolPlus, 0.021);
}

TEST(DimGrammar, CountAndHoles) {
  auto d = P("4x Ø6.6");
  EXPECT_TRUE(d.valid);
  EXPECT_EQ(d.count, 4);
  EXPECT_EQ(d.kind, DimKind::Diameter);
  EXPECT_DOUBLE_EQ(d.nominal, 6.6);
  EXPECT_EQ(formatDimText(d), "4x Ø6.6");
  d = P("4X Ø6.6 THRU");
  EXPECT_TRUE(d.through);
  d = P("6 adet Ø5 derinlik 10");
  EXPECT_EQ(d.count, 6);
  ASSERT_TRUE(d.depth);
  EXPECT_DOUBLE_EQ(*d.depth, 10);
  d = P("4 DELİK Ø6");
  EXPECT_EQ(d.count, 4);
  d = P("Ø6 ↧12");
  ASSERT_TRUE(d.depth);
  EXPECT_DOUBLE_EQ(*d.depth, 12);
  d = P("8x");
  EXPECT_EQ(d.kind, DimKind::Count);
  EXPECT_EQ(d.count, 8);
}

TEST(DimGrammar, Threads) {
  auto d = P("M8x1.25");
  EXPECT_EQ(d.kind, DimKind::Thread);
  EXPECT_DOUBLE_EQ(d.threadMajor, 8);
  EXPECT_DOUBLE_EQ(d.threadPitch, 1.25);
  EXPECT_EQ(formatDimText(d), "M8x1.25");
  d = P("M10x1.5-6H");
  EXPECT_EQ(d.threadSpec, "M10x1.5-6H");
  d = P("M12");
  EXPECT_DOUBLE_EQ(d.threadPitch, 1.75);   // ISO coarse
  d = P("2x M6 ↧10");
  EXPECT_EQ(d.count, 2);
  EXPECT_EQ(d.kind, DimKind::Thread);
  ASSERT_TRUE(d.depth);
  d = P("3/8\"-40UNS");
  EXPECT_EQ(d.kind, DimKind::Thread);
  EXPECT_NEAR(d.threadMajor, 9.525, 1e-9);
  EXPECT_NEAR(d.threadPitch, 25.4 / 40, 1e-9);
  EXPECT_EQ(formatDimText(d), "3/8\"-40UNS");
  d = P("9/16\"-24UNEF-2A");
  EXPECT_EQ(d.threadSpec, "9/16\"-24UNEF-2A");
  d = P("1/4-20 UNC");
  EXPECT_EQ(d.kind, DimKind::Thread);
  d = P("G1/4");
  EXPECT_EQ(d.kind, DimKind::Thread);
  EXPECT_NEAR(d.threadMajor, 13.157, 1e-9);
}

TEST(DimGrammar, AnglesChamfers) {
  auto d = P("30°");
  EXPECT_EQ(d.kind, DimKind::Angle);
  EXPECT_DOUBLE_EQ(d.nominal, 30);
  EXPECT_EQ(formatDimText(d), "30°");
  d = P("%%d");
  EXPECT_FALSE(d.valid);
  d = P("12°30'");
  EXPECT_DOUBLE_EQ(d.nominal, 12.5);
  d = P("45°±1°");
  EXPECT_DOUBLE_EQ(*d.tolPlus, 1);
  d = P("2x45°");
  EXPECT_EQ(d.kind, DimKind::Chamfer);
  EXPECT_DOUBLE_EQ(d.nominal, 2);
  EXPECT_DOUBLE_EQ(d.chamferAngle, 45);
  EXPECT_EQ(formatDimText(d), "2x45°");
  d = P("1.5 × 30º");
  EXPECT_EQ(d.kind, DimKind::Chamfer);
  EXPECT_DOUBLE_EQ(d.chamferAngle, 30);
  d = P("C2");
  EXPECT_EQ(d.kind, DimKind::Chamfer);
  EXPECT_DOUBLE_EQ(d.chamferAngle, 45);
}

TEST(DimGrammar, SuffixesReference) {
  auto d = P("20.5 max.");
  EXPECT_TRUE(d.isMax);
  EXPECT_DOUBLE_EQ(d.nominal, 20.5);
  EXPECT_EQ(formatDimText(d), "20.5 max");
  d = P("Ø max.11.8");
  EXPECT_TRUE(d.isMax);
  EXPECT_EQ(d.kind, DimKind::Diameter);
  EXPECT_DOUBLE_EQ(d.nominal, 11.8);
  d = P("(12)");
  EXPECT_TRUE(d.reference);
  EXPECT_EQ(formatDimText(d), "(12)");
  d = P("12 REF");
  EXPECT_TRUE(d.reference);
  d = P("R3 TYP");
  EXPECT_TRUE(d.valid);
  EXPECT_EQ(d.remainder, "TYP");
  EXPECT_EQ(formatDimText(d), "R3 TYP");
  d = P("5 min");
  EXPECT_TRUE(d.isMin);
}

TEST(DimGrammar, NotesAreNotDimensions) {
  for (const char* s : {"ÖLÇEK 1:2", "MALZEME: Ti-6Al-4V", "A-A", "1:2", "", "GENEL TOLERANS ISO 2768-m"}) {
    auto d = P(s);
    EXPECT_FALSE(d.valid) << s;
    EXPECT_EQ(formatDimText(d), d.raw);
  }
  EXPECT_EQ(P("A-A").kind, DimKind::Note);
}

TEST(DimGrammar, OcrConfusions) {
  // spike readings
  auto d = P("O20 H7", true, true);
  EXPECT_EQ(formatDimText(d), "Ø20 H7");
  EXPECT_FALSE(d.corrections.empty());
  EXPECT_LE(d.conf, kConfCorrectionPenalty + 1e-6f);
  d = P("4x 06.6", true);
  EXPECT_EQ(formatDimText(d), "4x Ø6.6");
  d = P("020 H7", true, true);
  EXPECT_EQ(formatDimText(d), "Ø20 H7");
  d = P("@100", true);
  EXPECT_EQ(formatDimText(d), "Ø100");
  d = P("12+0.1", true);   // ± misread as +
  EXPECT_EQ(formatDimText(d), "12±0.1");
  d = P("12+0.1", false);  // vector: keep as unilateral
  EXPECT_FALSE(d.tolMinus.has_value());
  d = P("M8x1.2S", true);
  EXPECT_EQ(formatDimText(d), "M8x1.25");
  d = P("MBx1.25", true);
  EXPECT_EQ(formatDimText(d), "M8x1.25");
  d = P("l2.5", true);
  EXPECT_DOUBLE_EQ(d.nominal, 12.5);
  d = P("30o", true);
  EXPECT_EQ(formatDimText(d), "30°");
  d = P("2x450", true);
  EXPECT_EQ(formatDimText(d), "2x45°");
  d = P("450", true, false, true);
  EXPECT_EQ(formatDimText(d), "45°");
  d = P("0.5", true);   // a real leading zero stays
  EXPECT_DOUBLE_EQ(d.nominal, 0.5);
  EXPECT_EQ(d.kind, DimKind::Linear);
  d = P("240 +0.05/-0", true);
  EXPECT_EQ(formatDimText(d), "240 +0.05/-0");
  // vector text: no confusion corrections
  d = P("O20", false);
  EXPECT_FALSE(d.valid);
}

TEST(DimGrammar, RankHypotheses) {
  DimTextContext c;
  c.fromOcr = true;
  c.nearCircleOrCylinder = true;
  std::vector<TextHypothesis> h{{"O2O H7", 0.80f}, {"Ø20 H7", 0.75f}, {"020 HT", 0.6f}};
  DimText best;
  EXPECT_EQ(rankDimTextHypotheses(h, c, &best), 1);
  EXPECT_EQ(formatDimText(best), "Ø20 H7");
  std::vector<TextHypothesis> h2{{"MALZEME", 0.9f}, {"12", 0.3f}};
  EXPECT_EQ(rankDimTextHypotheses(h2, c, &best), 1);
  EXPECT_EQ(rankDimTextHypotheses({}, c, &best), -1);
  std::vector<TextHypothesis> h3{{"4x 06.6", 0.85f}};
  EXPECT_EQ(rankDimTextHypotheses(h3, c, &best), 0);
  EXPECT_EQ(formatDimText(best), "4x Ø6.6");
}

TEST(DimGrammar, NeverThrowsOnGarbage) {
  for (const char* s : {"\xff\xfe", "((((", "+-+-+-", "ØØØ", "4x", "x", "M", "9/0\"-1UNC", "...", "±"}) {
    EXPECT_NO_THROW((void)P(s, true, true, true)) << s;
    EXPECT_NO_THROW((void)formatDimText(P(s, true)));
  }
}

TEST(DimGrammar, OcrFitAndHoleContext) {
  auto d = P("Ø30 ho", true, true);
  EXPECT_TRUE(d.valid);
  EXPECT_EQ(d.fitClass, "h6");
  EXPECT_EQ(formatDimText(d), "Ø30 h6");
  EXPECT_FALSE(d.corrections.empty());
  EXPECT_EQ(formatDimText(P("Ø30 ho")), "Ø30 ho");          // vector text: never corrected
  d = P("4x 10.2", true, true);                               // hole callout next to a circle
  EXPECT_EQ(d.kind, DimKind::Diameter);
  EXPECT_EQ(formatDimText(d), "4x Ø10.2");
  EXPECT_NE(P("4x 10.2", true, false).kind, DimKind::Diameter);   // no circle context -> unchanged
  EXPECT_LT(P("Ø30 ho", true).conf, P("Ø30 h6", true).conf);
}
