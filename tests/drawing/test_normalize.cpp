// WP-1: S2 normalization tests (synthetic PScript-like input).
#include "wp1_helpers.h"
#include <gtest/gtest.h>
#include <cmath>

using namespace pc::drawing;
using wp1::line;

namespace {
int count(const PrimitiveSheet& s, PrimKind k) {
  int n = 0; for (auto& p : s.prims) n += p.kind == k; return n;
}
PrimitiveSheet base() { PrimitiveSheet s; s.sheet = {{0, 0}, {297, 210}}; s.unitToMm = kPtToMm; return s; }
}  // namespace

TEST(Normalize, SplitDashesBecomeOneStyledLine) {
  PrimitiveSheet raw = base();
  int id = 1;
  for (int i = 0; i < 10; ++i) raw.prims.push_back(line(id++, {20.0 + 4 * i, 50}, {23.0 + 4 * i, 50}, 0.25));
  raw.prims.push_back(line(id++, {20, 80}, {120, 80}));   // unrelated solid line
  PrimitiveSheet n; NormalizeOptions o;
  ASSERT_TRUE(normalize(raw, o, n, id).ok());
  ASSERT_EQ(count(n, PrimKind::Line), 2);
  const Primitive* dashed = nullptr;
  for (auto& p : n.prims) if (!p.style.dash.empty()) dashed = &p;
  ASSERT_NE(dashed, nullptr);
  EXPECT_NEAR(std::abs(dashed->p1.x - dashed->p0.x), 39, 0.01);
  EXPECT_EQ(dashed->mergedFrom.size(), 10u);
  EXPECT_NEAR(dashed->style.dash[0], 3, 0.05);
  // untouched element keeps its id
  bool kept = false; for (auto& p : n.prims) kept |= p.id == 11;
  EXPECT_TRUE(kept);
}

TEST(Normalize, SegmentChainBecomesArcAndCircle) {
  PrimitiveSheet raw = base();
  int id = 1;
  const double pi = 3.14159265358979;
  for (int i = 0; i < 18; ++i) {   // 90-degree arc R10 at (100,100) as 18 chords
    const double a0 = i * pi / 36, a1 = (i + 1) * pi / 36;
    raw.prims.push_back(line(id++, {100 + 10 * std::cos(a0), 100 + 10 * std::sin(a0)}, {100 + 10 * std::cos(a1), 100 + 10 * std::sin(a1)}));
  }
  for (int i = 0; i < 48; ++i) {   // full circle R6 at (200,100)
    const double a0 = i * pi / 24, a1 = (i + 1) * pi / 24;
    raw.prims.push_back(line(id++, {200 + 6 * std::cos(a0), 100 + 6 * std::sin(a0)}, {200 + 6 * std::cos(a1), 100 + 6 * std::sin(a1)}));
  }
  PrimitiveSheet n; NormalizeOptions o; o.arcFitTol = 0.1;
  ASSERT_TRUE(normalize(raw, o, n, id).ok());
  ASSERT_EQ(count(n, PrimKind::Arc), 1);
  ASSERT_EQ(count(n, PrimKind::Circle), 1);
  for (auto& p : n.prims) {
    if (p.kind == PrimKind::Arc) { EXPECT_NEAR(p.radius, 10, 0.05); EXPECT_NEAR(p.center.x, 100, 0.05); }
    if (p.kind == PrimKind::Circle) { EXPECT_NEAR(p.radius, 6, 0.05); EXPECT_NEAR(p.center.x, 200, 0.05); }
  }
}

TEST(Normalize, GlyphRunsMergeAndDuplicatesDrop) {
  PrimitiveSheet raw = base();
  int id = 1;
  const char* glyphs[] = {"Ø", "1", "6"};
  for (int i = 0; i < 3; ++i) raw.texts.push_back(wp1::text(id++, glyphs[i], {50.0 + 2.1 * i, 60}, 3.5));
  raw.texts.push_back(wp1::text(id++, "R8", {150, 60}, 3.5));
  raw.prims.push_back(line(id++, {10, 10}, {60, 10}));
  raw.prims.push_back(line(id++, {10, 10}, {60, 10}));   // exact duplicate
  PrimitiveSheet n; NormalizeOptions o;
  ASSERT_TRUE(normalize(raw, o, n, id).ok());
  ASSERT_EQ(n.texts.size(), 2u);
  bool d16 = false, r8 = false;
  for (auto& t : n.texts) { d16 |= t.utf8 == "Ø16"; r8 |= t.utf8 == "R8"; }
  EXPECT_TRUE(d16); EXPECT_TRUE(r8);
  EXPECT_EQ(count(n, PrimKind::Line), 1);
  EXPECT_GT(id, 7);   // new ids allocated from the shared counter
}
