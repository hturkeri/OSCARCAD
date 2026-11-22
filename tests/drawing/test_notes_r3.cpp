// R3 — notes / caption parser: structured hints from the swirl-cup and atomizer drawings, OCR-noise robustness.
#include "../../src/drawing/interp/Notes.h"
#include "pc/drawing/Drawing.h"
#include <gtest/gtest.h>

using namespace pc::drawing;
using namespace pc::drawing::interp;

namespace {

NoteHints parse1(const std::string& s) {
  NoteHints h;
  parseNoteText(s, 1, 0.95f, h);
  return h;
}

std::string fixture(const char* name) { return std::string(PC_DRAWING_TEST_DIR) + "/fixtures/r3/" + name; }

}  // namespace

TEST(R3Notes, SlotCallout) {
  auto h = parse1("8x TANGENTIAL SLOTS 4 x 5, EQ. SP. 45° - CCW SWIRL");
  ASSERT_EQ(h.features.size(), 1u);
  auto& f = h.features[0];
  EXPECT_EQ(f.kind, "slot");
  EXPECT_EQ(f.count, 8);
  EXPECT_TRUE(f.tangential);
  EXPECT_DOUBLE_EQ(f.width, 4);
  EXPECT_DOUBLE_EQ(f.height, 5);
  EXPECT_DOUBLE_EQ(f.pitchDeg, 45);
  EXPECT_EQ(f.sense, 1);
  auto c = parse1("10x TANGENTIAL SLOTS 4 x 6, EQ. SP. 36° - CW SWIRL").features.at(0);
  EXPECT_EQ(c.count, 10);
  EXPECT_DOUBLE_EQ(c.height, 6);
  EXPECT_DOUBLE_EQ(c.pitchDeg, 36);
  EXPECT_EQ(c.sense, -1);
}

TEST(R3Notes, ModellingAxialRangesScaleFlow) {
  auto h = parse1("6. MODELLING: (a) REVOLVE HALF PROFILE 360°; (b) CUT 8 PRIMARY SLOTS x = 3..8; (c) CUT 10 SECONDARY SLOTS x = 10..16.");
  ASSERT_EQ(h.modellingSteps.size(), 3u);
  EXPECT_EQ(h.modellingSteps[0], "REVOLVE HALF PROFILE 360~");
  ASSERT_EQ(h.features.size(), 2u);
  auto* p = findHint(h, "slot", "primary");
  auto* s = findHint(h, "slot", "secondary");
  ASSERT_TRUE(p && s);
  EXPECT_TRUE(p->cut);
  EXPECT_EQ(p->count, 8);
  EXPECT_DOUBLE_EQ(p->x0, 3);
  EXPECT_DOUBLE_EQ(p->x1, 8);
  EXPECT_EQ(s->count, 10);
  EXPECT_DOUBLE_EQ(s->x0, 10);
  EXPECT_DOUBLE_EQ(s->x1, 16);
  auto n1 = parse1("1. ALL DIMENSIONS IN mm. SHEET A3, SCALE 2:1. x = 0 AT FORWARD FACE, FLOW LEFT TO RIGHT.");
  EXPECT_DOUBLE_EQ(n1.scaleNum, 2);
  EXPECT_DOUBLE_EQ(n1.scaleDen, 1);
  EXPECT_EQ(n1.flowSign, 1);
  EXPECT_TRUE(n1.features.empty());
}

TEST(R3Notes, OffsetsDiametersTangencyCaptions) {
  auto h = parse1("4. SLOT CENTRELINE OFFSET FROM AXIS: PRIMARY 8, SECONDARY 11. FLOW AREAS: PRIMARY 160 mm2, SECONDARY 240 mm2");
  ASSERT_TRUE(findHint(h, "slot", "primary") && findHint(h, "slot", "secondary"));
  EXPECT_DOUBLE_EQ(findHint(h, "slot", "primary")->offset, 8);
  EXPECT_DOUBLE_EQ(findHint(h, "slot", "secondary")->offset, 11);
  auto d = parse1("Ø14 THROAT");
  EXPECT_DOUBLE_EQ(d.namedDiameters.at("THROAT"), 14);
  auto t = parse1("5. SLOT OUTER WALL TANGENT TO SWIRL CHAMBER WALL.");
  EXPECT_EQ(t.tangentOuterWallTo, "SWIRL CHAMBER");
  EXPECT_TRUE(t.features.empty());
  auto sc = parse1("dashed = swirl chamber Ø3.2 (part 1)");
  EXPECT_DOUBLE_EQ(sc.namedDiameters.at("SWIRL CHAMBER"), 3.2);
  auto g = parse1("3. 3 SLOTS 0.5 x 0.5, Ap = 0.75 mm2; GROOVES 0.6 x 0.5.");
  ASSERT_EQ(g.features.size(), 2u);
  auto* sl = findHint(g, "slot", "");
  auto* gr = findHint(g, "groove", "");
  ASSERT_TRUE(sl && gr);
  EXPECT_EQ(sl->count, 3);
  EXPECT_DOUBLE_EQ(sl->width, 0.5);
  EXPECT_DOUBLE_EQ(sl->height, 0.5);
  EXPECT_EQ(gr->count, 0);
  EXPECT_DOUBLE_EQ(gr->width, 0.6);
  EXPECT_DOUBLE_EQ(gr->height, 0.5);
  auto c = parse1("VIEW B  - SWIRLER (2) FRONT FACE");
  ASSERT_EQ(c.captions.size(), 1u);
  EXPECT_EQ(c.captions[0].letter, "B");
  EXPECT_FALSE(c.captions[0].section);
  EXPECT_EQ(c.captions[0].partNo, 2);
  EXPECT_TRUE(c.captions[0].frontFace);
  auto s = parse1("SECTION C-C  SECONDARY SWIRLER");
  ASSERT_EQ(s.captions.size(), 1u);
  EXPECT_EQ(s.captions[0].letter, "C");
  EXPECT_TRUE(s.captions[0].section);
  EXPECT_EQ(s.captions[0].role, "secondary");
}

TEST(R3Notes, OcrNoise) {
  // l/1, O/0, S/5 confusions, degree read as 'o', spaced range dots, decimal comma, Ø read as 0
  auto h = parse1("8x TANGENTlAL SL0TS 4 X 5, EQ SP 45o - CCW SWlRL");
  ASSERT_EQ(h.features.size(), 1u);
  EXPECT_EQ(h.features[0].count, 8);
  EXPECT_DOUBLE_EQ(h.features[0].width, 4);
  EXPECT_DOUBLE_EQ(h.features[0].pitchDeg, 45);
  EXPECT_EQ(h.features[0].sense, 1);
  EXPECT_LT(h.features[0].conf, 0.95f);   // repairs lower the confidence
  EXPECT_FALSE(h.features[0].corrections.empty());
  auto r = parse1("(c) CUT lO SECONDARY SLOTS x = 1O. .16");
  ASSERT_EQ(r.features.size(), 1u);
  EXPECT_EQ(r.features[0].count, 10);
  EXPECT_DOUBLE_EQ(r.features[0].x0, 10);
  EXPECT_DOUBLE_EQ(r.features[0].x1, 16);
  auto g = parse1("GROOVES 0,6 x 0,5");
  ASSERT_EQ(g.features.size(), 1u);
  EXPECT_DOUBLE_EQ(g.features[0].width, 0.6);
  auto d = parse1("014 THROAT");
  ASSERT_TRUE(d.namedDiameters.count("THROAT"));
  EXPECT_DOUBLE_EQ(d.namedDiameters.at("THROAT"), 14);
  auto sc = parse1("SCALE 2 : 1");
  EXPECT_DOUBLE_EQ(sc.scaleNum, 2);
  auto p = parse1("10x TANGENTIAL SLOTS 4 x 6, EQ. SP. 360 - CW SWIRL");   // ° read as 0
  EXPECT_DOUBLE_EQ(p.features.at(0).pitchDeg, 36);
}

TEST(R3Notes, SheetMergeSwirlCup) {
  DrawingIR ir;
  ASSERT_TRUE(loadIR(ir, fixture("swirl_cup_SWC-RR-001.pcdrw.json")));
  NoteHints h = parseNotes(ir.norm);
  EXPECT_EQ(h.flowSign, 1);
  EXPECT_TRUE(h.viewedFromDownstream);
  EXPECT_DOUBLE_EQ(h.scaleNum, 2);
  EXPECT_EQ(h.modellingSteps.size(), 3u);
  auto* p = findHint(h, "slot", "primary");
  auto* s = findHint(h, "slot", "secondary");
  ASSERT_TRUE(p && s);
  // caption sub-line (role from "SECTION B-B PRIMARY SWIRLER") + note 2 + note 4 + note 6 merged
  EXPECT_EQ(p->viewLetter, "B");
  EXPECT_EQ(p->count, 8);
  EXPECT_DOUBLE_EQ(p->width, 4);
  EXPECT_DOUBLE_EQ(p->height, 5);
  EXPECT_DOUBLE_EQ(p->offset, 8);
  EXPECT_DOUBLE_EQ(p->x0, 3);
  EXPECT_EQ(p->sense, 1);
  EXPECT_EQ(s->viewLetter, "C");
  EXPECT_EQ(s->count, 10);
  EXPECT_DOUBLE_EQ(s->offset, 11);
  EXPECT_DOUBLE_EQ(s->x1, 16);
  EXPECT_EQ(s->sense, -1);
  EXPECT_DOUBLE_EQ(h.namedDiameters.at("THROAT"), 14);
  EXPECT_DOUBLE_EQ(h.namedDiameters.at("HUB"), 32);
}

TEST(R3Notes, SheetMergeAtomizer) {
  DrawingIR ir;
  ASSERT_TRUE(loadIR(ir, fixture("atomizer_ATM-PS-001.pcdrw.json")));
  NoteHints h = parseNotes(ir.norm);
  EXPECT_EQ(h.flowSign, 0);
  EXPECT_DOUBLE_EQ(h.scaleNum, 10);
  auto* s = findHint(h, "slot", "");
  auto* g = findHint(h, "groove", "");
  ASSERT_TRUE(s && g);
  EXPECT_EQ(s->count, 3);
  EXPECT_DOUBLE_EQ(s->pitchDeg, 120);
  EXPECT_DOUBLE_EQ(s->width, 0.5);
  EXPECT_EQ(s->viewLetter, "B");
  EXPECT_DOUBLE_EQ(g->width, 0.6);
  EXPECT_EQ(h.tangentOuterWallTo, "SWIRL CHAMBER");
  EXPECT_DOUBLE_EQ(h.namedDiameters.at("SWIRL CHAMBER"), 3.2);
}
