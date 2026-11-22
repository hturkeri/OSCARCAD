// R3 — transverse-view analysis (SECTION B-B / C-C of the swirl cup, VIEW B of the simplex atomizer) and the
// slot / groove cut synthesis on revolved test bodies (volumes vs hand computation, handedness by point tests).
#include "../../src/drawing/synth/CutFeatures.h"
#include "pc/Model.h"
#include "pc/drawing/Drawing.h"
#include <BRepClass3d_SolidClassifier.hxx>
#include <gtest/gtest.h>
#include <cmath>

using namespace pc;
using namespace pc::drawing;
using namespace pc::drawing::interp;

namespace {

constexpr double kPI = 3.14159265358979323846;

std::string fixture(const char* name) { return std::string(PC_DRAWING_TEST_DIR) + "/fixtures/r3/" + name; }

TransverseResult load(const char* name) {
  DrawingIR ir;
  EXPECT_TRUE(loadIR(ir, fixture(name)));
  return analyzeTransverse(ir.norm);
}

const TransverseView* viewOf(const TransverseResult& t, const std::string& letter) {
  for (auto& v : t.views) if (v.letter == letter) return &v;
  return nullptr;
}
const SlotFamily* famOf(const TransverseView& v, const std::string& kind) {
  for (auto& f : v.families) if (f.kind == kind) return &f;
  return nullptr;
}

// ---- hand computation: area of the strip |n - e| <= W/2 between radii Ri and Ro (beyond the foot, u > 0)
double F(double R, double n) { return 0.5 * (n * std::sqrt(R * R - n * n) + R * R * std::asin(n / R)); }
double stripArea(double e, double W, double Ri, double Ro) {
  double a = e - W / 2, b = e + W / 2;
  return F(Ro, b) - F(Ro, a) - (Ri > 0 ? F(Ri, b) - F(Ri, a) : 0.0);
}

// revolve body from a closed (r, z) polygon (XZ sketch, axis V = Z)
void addRevolve(Document& d, const std::string& id, const std::vector<std::pair<double, double>>& rz) {
  Sketch sk = Sketch::create();
  sk.plane.kind = SketchPlaneKind::XZ;
  std::vector<int> pts;
  for (auto& [r, z] : rz) {
    pts.push_back(sk.addPoint(r, z));
    sk.addConstraint(SkConstraintType::Fixed, {pts.back()});
  }
  for (size_t i = 0; i < pts.size(); ++i) sk.addLine(pts[i], pts[(i + 1) % pts.size()]);
  Feature s;
  s.id = "SK_" + id;
  s.type = FeatureType::Sketch;
  s.sketch = sk;
  d.features.push_back(s);
  Feature r;
  r.id = id;
  r.type = FeatureType::Revolve;
  r.props = {{"sketch", s.id}, {"axis", "V"}, {"angle", "360"}, {"op", "new"}, {"bodyName", id}};
  d.features.push_back(r);
}

// distance of angle a (deg) to the nearest multiple of p
double angMod(double a, double p) { double r = std::fmod(std::fmod(a, p) + p, p); return std::min(r, p - r); }

// point (x, y) inside slot instance k of a family: phase phi (rad), signed centreline offset o along (-sin, cos)
bool inFamily(double x, double y, double phi, int N, double o, double W, double uIn) {
  for (int k = 0; k < N; ++k) {
    double a = phi + 2 * kPI * k / N;
    double u = x * std::cos(a) + y * std::sin(a), n = -x * std::sin(a) + y * std::cos(a);
    if (u >= uIn - 0.05 && std::fabs(n - o) <= W / 2 + 0.05) return true;   // (boundary counts as inside)
  }
  return false;
}

double vol(const model::ModelResult& mr, const std::string& body) {
  auto* b = mr.body(body);
  return b ? model::massProperties(b->shape).volume : -1;
}
bool solidAt(const model::ModelResult& mr, const std::string& body, double x, double y, double z) {
  BRepClass3d_SolidClassifier c(mr.body(body)->shape, gp_Pnt(x, y, z), 1e-7);
  return c.State() == TopAbs_IN;
}

Document swirlCupBodies() {
  Document d;
  // swirler: OD 50, x 0..22; bore Ø10 (0..3), primary chamber Ø24 (3..8), separator Ø22 (8..10), Ø34 (10..16), hub Ø32
  addRevolve(d, "SwirlCup", {{5, 0}, {25, 0}, {25, 22}, {16, 22}, {16, 16}, {17, 16}, {17, 10}, {11, 10}, {11, 8},
                             {12, 8}, {12, 3}, {5, 3}});
  addRevolve(d, "Venturi", {{7, 10}, {10.5, 10}, {10.5, 22}, {7, 22}});
  return d;
}
Document atomizerBodies() {
  Document d;
  addRevolve(d, "Tip", {{0.4, 0}, {4, 0}, {4, 4.2}, {1.6, 4.2}, {1.6, 3.6}, {0.4, 3.0}});
  addRevolve(d, "Insert", {{0, 4.2}, {2.5, 4.2}, {2.5, 10}, {0, 10}});
  return d;
}

}  // namespace

// ------------------------------------------------------------------------------------------- analysis
TEST(R3Transverse, SwirlCupSectionsBandC) {
  auto t = load("swirl_cup_SWC-RR-001.pcdrw.json");
  ASSERT_EQ(t.views.size(), 2u);
  EXPECT_EQ(t.flowSign, 1);
  auto* B = viewOf(t, "B");
  auto* C = viewOf(t, "C");
  ASSERT_TRUE(B && C);
  EXPECT_NEAR(B->factor, 25 / 19.389, 0.002);   // calibrated from Ø50/Ø24/Ø14 (sheet is a reduced print)
  EXPECT_EQ(B->role, "primary");
  auto* p = famOf(*B, "slot");
  ASSERT_TRUE(p);
  EXPECT_EQ(p->tag, "pri");
  EXPECT_EQ(p->countGeom, 8);
  EXPECT_EQ(p->count, 8);
  EXPECT_DOUBLE_EQ(p->pitchDeg, 45);
  EXPECT_NEAR(p->widthGeom, 4, 0.02);
  EXPECT_NEAR(p->offsetGeom, 8, 0.02);
  EXPECT_DOUBLE_EQ(p->width, 4);
  EXPECT_DOUBLE_EQ(p->offset, 8);
  EXPECT_EQ(p->senseView, 1);    // CCW (geometry and note agree)
  EXPECT_EQ(p->senseText, 1);
  EXPECT_TRUE(p->throughWall);
  EXPECT_NEAR(p->rIn, 12, 0.02);
  EXPECT_NEAR(p->rOut, 25, 0.02);
  EXPECT_NEAR(angMod(p->phaseDeg, 45.0), 0, 0.2);
  EXPECT_DOUBLE_EQ(p->x0, 3);
  EXPECT_DOUBLE_EQ(p->x1, 8);
  EXPECT_DOUBLE_EQ(p->height, 5);
  EXPECT_NEAR(B->xMarker, 5.5, 0.6);   // section marker B on SECTION A-A lies inside x = 3..8
  EXPECT_TRUE(p->warnings.empty()) << p->warnings.front();
  auto* s = famOf(*C, "slot");
  ASSERT_TRUE(s);
  EXPECT_EQ(s->tag, "sec");
  EXPECT_EQ(s->countGeom, 10);
  EXPECT_DOUBLE_EQ(s->pitchDeg, 36);
  EXPECT_NEAR(s->widthGeom, 4, 0.02);
  EXPECT_NEAR(s->offsetGeom, 11, 0.02);
  EXPECT_EQ(s->senseView, -1);   // CW
  EXPECT_NEAR(s->rIn, 17, 0.02);
  EXPECT_DOUBLE_EQ(s->x0, 10);
  EXPECT_DOUBLE_EQ(s->x1, 16);
  EXPECT_NEAR(C->xMarker, 13, 0.6);
  EXPECT_TRUE(s->warnings.empty()) << s->warnings.front();
}

TEST(R3Transverse, AtomizerViewB) {
  auto t = load("atomizer_ATM-PS-001.pcdrw.json");
  ASSERT_EQ(t.views.size(), 1u);
  EXPECT_EQ(t.flowSign, -1);   // orifice at the left end of SECTION A-A
  auto& V = t.views[0];
  EXPECT_EQ(V.letter, "B");
  EXPECT_EQ(V.partNo, 2);
  EXPECT_NEAR(V.factor, 2.5 / 10.12, 0.002);
  auto* s = famOf(V, "slot");
  auto* g = famOf(V, "groove");
  ASSERT_TRUE(s && g);
  EXPECT_EQ(s->tag, "slt");
  EXPECT_EQ(s->countGeom, 3);
  EXPECT_DOUBLE_EQ(s->pitchDeg, 120);
  EXPECT_NEAR(s->widthGeom, 0.5, 0.01);
  EXPECT_NEAR(s->offsetGeom, 1.35, 0.01);
  EXPECT_DOUBLE_EQ(s->offset, 1.35);   // from "outer wall tangent to swirl chamber Ø3.2": 3.2/2 - 0.5/2
  EXPECT_DOUBLE_EQ(s->tangentDia, 3.2);
  EXPECT_EQ(s->senseView, -1);         // drawn CW, seen from downstream (front face)
  EXPECT_FALSE(s->throughWall);
  EXPECT_NEAR(s->uIn, 0, 0.02);        // closed end at the tangent point
  EXPECT_TRUE(s->faceRelative);
  EXPECT_DOUBLE_EQ(s->height, 0.5);
  EXPECT_NEAR(angMod(s->phaseDeg - 60, 120.0), 0, 0.3);   // instance at 180° in the view
  EXPECT_EQ(g->tag, "grv");
  EXPECT_EQ(g->countGeom, 3);
  EXPECT_NEAR(g->widthGeom, 0.6, 0.01);
  EXPECT_DOUBLE_EQ(g->width, 0.6);
  EXPECT_NEAR(g->rIn, 2.0, 0.01);
  EXPECT_DOUBLE_EQ(g->height, 0.5);
  EXPECT_NEAR(g->rOut, 2.5, 0.01);
  EXPECT_TRUE(g->alongBody);
  EXPECT_NEAR(angMod(g->phaseDeg - 23.1, 120.0), 0, 0.5);   // 143.1° in the view
}

TEST(R3Transverse, PipelineHookWritesViewAttrs) {
  DrawingIR ir;
  PipelineOptions o;
  o.interp.verifyByReprojection = false;
  StageResult r = runPipeline(fixture("swirl_cup_SWC-RR-001.pcdrw.json"), ir, Stage::Ingest, Stage::Views, o);
  ASSERT_TRUE(r.ok()) << r.message;
  ASSERT_TRUE(ir.views.attrs.count("r3.transverse.pri"));
  ASSERT_TRUE(ir.views.attrs.count("r3.transverse.sec"));
  EXPECT_NE(ir.views.attrs.at("r3.transverse.pri").find("8x slot W4 OFS8 CCW"), std::string::npos) << ir.views.attrs.at("r3.transverse.pri");
  EXPECT_NE(ir.views.attrs.at("r3.transverse.sec").find("10x slot W4 OFS11 CW"), std::string::npos);
}

// ------------------------------------------------------------------------------------------- synthesis
TEST(R3CutFeatures, SwirlCupSlotsVolumeHandednessParams) {
  auto t = load("swirl_cup_SWC-RR-001.pcdrw.json");
  Document d = swirlCupBodies();
  auto mr0 = model::regenerate(d);
  ASSERT_TRUE(mr0.ok);
  const double v0 = vol(mr0, "SwirlCup"), vv0 = vol(mr0, "Venturi");
  synth::CutReport rep = synth::addTransverseCuts(d, t);
  ASSERT_TRUE(rep.ok) << (rep.warnings.empty() ? "" : rep.warnings.front());
  EXPECT_EQ(rep.targetOf.at("pri"), "SwirlCup");
  EXPECT_EQ(rep.targetOf.at("sec"), "SwirlCup");
  for (const char* p : {"N_pri", "W_pri", "H_pri", "OFS_pri", "X0_pri", "X1_pri", "RI_pri", "RO_pri", "PHI_pri", "SENSE_pri",
                        "N_sec", "W_sec", "H_sec", "OFS_sec", "X0_sec", "X1_sec"})
    EXPECT_TRUE(d.params.find(p)) << p;
  auto pv = d.evaluateParams();
  EXPECT_DOUBLE_EQ(pv.at("N_pri"), 8);
  EXPECT_DOUBLE_EQ(pv.at("H_pri"), 5);
  EXPECT_DOUBLE_EQ(pv.at("SENSE_pri"), 1);
  EXPECT_DOUBLE_EQ(pv.at("SENSE_sec"), -1);
  auto mr = model::regenerate(d);
  ASSERT_TRUE(mr.ok);
  // hand computation: N * strip area between bore and OD * axial height
  const double dPri = 8 * stripArea(8, 4, 12, 25) * 5, dSec = 10 * stripArea(11, 4, 17, 25) * 6;
  EXPECT_NEAR(v0 - vol(mr, "SwirlCup"), dPri + dSec, 1e-4 * (dPri + dSec));
  EXPECT_NEAR(vol(mr, "Venturi"), vv0, 1e-6 * vv0);   // the venturi inside the secondary chamber is not cut
  EXPECT_NEAR(vv0, kPI * (10.5 * 10.5 - 49) * 12, 1e-3);
  // handedness: slot 0 of the primary family is drawn along +x at y = +8 (B-B, seen from downstream = +Z)
  const double phi = pv.at("PHI_pri") * kPI / 180;
  const double u = std::sqrt(18.5 * 18.5 - 64);
  auto at = [&](double uu, double n) { return std::make_pair(uu * std::cos(phi) - n * std::sin(phi), uu * std::sin(phi) + n * std::cos(phi)); };
  auto [xa, ya] = at(u, 8);
  EXPECT_FALSE(solidAt(mr, "SwirlCup", xa, ya, 5.5));
  // the mirror-image (CW) slot position is material wherever no CCW instance passes
  int checked = 0;
  for (double uu = 8; uu < 23; uu += 0.5) {
    auto [xb, yb] = at(uu, -8);
    double r = std::hypot(xb, yb);
    if (r < 12.2 || r > 24.8 || inFamily(xb, yb, phi, 8, 8, 4, 6)) continue;
    EXPECT_TRUE(solidAt(mr, "SwirlCup", xb, yb, 5.5)) << uu;
    ++checked;
  }
  EXPECT_GT(checked, 3);
  // inflow along -u at +8 has positive angular momentum about +Z -> CCW seen from downstream
  EXPECT_GT(xa * (-std::sin(phi)) - ya * (-std::cos(phi)), 0);
  // secondary (CW): slot 0 at -11, mirror solid
  const double ps = pv.at("PHI_sec") * kPI / 180, us = std::sqrt(21 * 21 - 121);
  EXPECT_FALSE(solidAt(mr, "SwirlCup", us * std::cos(ps) + 11 * std::sin(ps), us * std::sin(ps) - 11 * std::cos(ps), 13));
  {
    double xm = us * std::cos(ps) - 11 * std::sin(ps), ym = us * std::sin(ps) + 11 * std::cos(ps);
    if (!inFamily(xm, ym, ps, 10, -11, 4, 0)) EXPECT_TRUE(solidAt(mr, "SwirlCup", xm, ym, 13));
    // CW: inflow at -11 along -u has negative angular momentum about +Z
    double xv = us * std::cos(ps) + 11 * std::sin(ps), yv = us * std::sin(ps) - 11 * std::cos(ps);
    EXPECT_LT(xv * (-std::sin(ps)) - yv * (-std::cos(ps)), 0);
  }
  // parametric: 6 primary slots of width 3
  auto mr6 = model::regenerate(d, {{"N_pri", "6"}, {"W_pri", "3"}});
  ASSERT_TRUE(mr6.ok);
  EXPECT_NEAR(v0 - vol(mr6, "SwirlCup"), 6 * stripArea(8, 3, 12, 25) * 5 + dSec, 1e-4 * (dPri + dSec));
}

TEST(R3CutFeatures, AtomizerSlotsAndGrooves) {
  auto t = load("atomizer_ATM-PS-001.pcdrw.json");
  Document d = atomizerBodies();
  auto mr0 = model::regenerate(d);
  ASSERT_TRUE(mr0.ok);
  const double v0 = vol(mr0, "Insert"), vt0 = vol(mr0, "Tip");
  synth::CutReport rep = synth::addTransverseCuts(d, t);
  ASSERT_TRUE(rep.ok) << (rep.warnings.empty() ? "" : rep.warnings.front());
  EXPECT_EQ(rep.targetOf.at("slt"), "Insert");   // "SWIRLER (2)" -> second body
  EXPECT_EQ(rep.targetOf.at("grv"), "Insert");
  auto pv = d.evaluateParams();
  EXPECT_DOUBLE_EQ(pv.at("OFS_slt"), 1.35);      // DSC_slt/2 - W_slt/2
  EXPECT_DOUBLE_EQ(pv.at("X0_slt"), 4.2);        // front (downstream) face of the insert
  EXPECT_DOUBLE_EQ(pv.at("X1_slt"), 4.7);
  EXPECT_DOUBLE_EQ(pv.at("FLOW_R3"), -1);
  EXPECT_DOUBLE_EQ(pv.at("SENSE_slt"), 1);       // CW seen from downstream with flow towards -Z = +Z rotation
  // integration 16.11.2026: the slot runs from the tangent point INTO the feed groove (VIEW B), its centreline ends at
  // the groove mid-depth radius r = 2.5 - 0.5/2 -> length LS = sqrt(2.25^2 - 1.35^2) = 1.8 (whole strip inside the OD)
  const double LS = std::sqrt(2.25 * 2.25 - 1.35 * 1.35);
  EXPECT_NEAR(pv.at("LS_slt"), LS, 1e-12);
  const double aS = 0.5 * LS, aG = stripArea(0, 0.6, 0, 2.5) - 0.6 * 2.0;
  // slots only / grooves only (suppress the other family)
  auto only = [&](const char* off) {
    Document x = d;
    for (auto& f : x.features) if (f.id.find(off) != std::string::npos) f.suppressed = true;
    return model::regenerate(x);
  };
  auto ms = only("_grv");
  auto mg = only("_slt");
  ASSERT_TRUE(ms.ok && mg.ok);
  EXPECT_NEAR(v0 - vol(ms, "Insert"), 3 * aS * 0.5, 1e-4);
  EXPECT_NEAR(v0 - vol(mg, "Insert"), 3 * aG * 5.8, 1e-4);
  // both: union; overlap area of slot strip and groove in the 0.5 deep face layer by fine grid quadrature
  const double ps = pv.at("PHI_slt") * kPI / 180, pg = pv.at("PHI_grv") * kPI / 180;
  double ov = 0;
  const int n = 1600;
  const double h = 5.0 / n;
  for (int i = 0; i < n; ++i)
    for (int j = 0; j < n; ++j) {
      double x = -2.5 + (i + 0.5) * h, y = -2.5 + (j + 0.5) * h;
      if (x * x + y * y > 6.25) continue;
      bool inS = false, inG = false;
      for (int k = 0; k < 3; ++k) {
        double a = ps + k * 2 * kPI / 3, b = pg + k * 2 * kPI / 3;
        double uu = x * std::cos(a) + y * std::sin(a), nn = -x * std::sin(a) + y * std::cos(a);
        if (uu >= 0 && uu <= LS && std::fabs(nn - 1.35) <= 0.25) inS = true;
        double gu = x * std::cos(b) + y * std::sin(b), gn = -x * std::sin(b) + y * std::cos(b);
        if (gu >= 2.0 && std::fabs(gn) <= 0.3) inG = true;
      }
      if (inS && inG) ov += h * h;
    }
  auto mr = model::regenerate(d);
  ASSERT_TRUE(mr.ok);
  EXPECT_NEAR(v0 - vol(mr, "Insert"), 3 * aS * 0.5 + 3 * aG * 5.8 - ov * 0.5, 2e-3);
  EXPECT_NEAR(vol(mr, "Tip"), vt0, 1e-9 * vt0);
  // handedness: slot 0 along +X at Y = +1.35 (view: along -x at y = +1.35, mirrored because the view looks along +Z)
  EXPECT_NEAR(angMod(pv.at("PHI_slt"), 120.0), 0, 0.3);
  EXPECT_FALSE(solidAt(mr, "Insert", 1.0, 1.35, 4.45));
  EXPECT_TRUE(solidAt(mr, "Insert", 0.3, -1.35, 4.45));   // mirror image (clear of the 120/240° instances)
  EXPECT_TRUE(solidAt(mr, "Insert", 1.0, 1.35, 4.9));   // below the 0.5 slot depth
  // groove: view 143.1° -> 36.9° about +Z, full length
  const double g = 36.87 * kPI / 180;
  EXPECT_NEAR(angMod(pv.at("PHI_grv") - 36.87, 120.0), 0, 0.5);
  EXPECT_FALSE(solidAt(mr, "Insert", 2.3 * std::cos(g), 2.3 * std::sin(g), 9.5));
  EXPECT_TRUE(solidAt(mr, "Insert", 1.9 * std::cos(g), 1.9 * std::sin(g), 9.5));
}


// S7 hook: a revolve candidate (here a hand-made half profile standing in for r2's section revolve) gets the slot
// families of the transverse views appended before regeneration.
TEST(R3CutFeatures, SynthesizeHookAddsSlotsToRevolveCandidate) {
  DrawingIR ir;
  PipelineOptions o;
  o.interp.verifyByReprojection = false;
  ASSERT_TRUE(runPipeline(fixture("swirl_cup_SWC-RR-001.pcdrw.json"), ir, Stage::Ingest, Stage::Views, o).ok());
  Document bodies = swirlCupBodies();
  SketchCandidate c;
  c.id = ir.newId();
  c.purpose = "revolve-half-profile";
  c.plane = SketchPlaneKind::XZ;
  c.sketch = *bodies.features[0].sketch;   // swirler half profile (XZ, axis V = Z)
  c.attrs["interp.strategy"] = "SectionRevolve";
  ir.sketches = {c};
  ASSERT_TRUE(synthesize(ir, o.interp).ok());
  const SynthesisCandidate* rc = nullptr;
  for (auto& s : ir.synthesis) if (s.strategy == Strategy::SectionRevolve) rc = &s;
  ASSERT_TRUE(rc);
  ASSERT_TRUE(rc->attrs.count("r3.cuts"));
  EXPECT_NE(rc->attrs.at("r3.cuts").find("PAT_pri"), std::string::npos);
  EXPECT_NE(rc->attrs.at("r3.cuts").find("PAT_sec"), std::string::npos);
  EXPECT_EQ(rc->attrs.at("r3.cuts.ok"), "1");
  ASSERT_TRUE(rc->regenerated);
  Document only;
  only.features = {bodies.features[0], bodies.features[1]};
  const double v0 = model::massProperties(model::regenerate(only).shape).volume;
  EXPECT_NEAR(v0 - rc->volume, 8 * stripArea(8, 4, 12, 25) * 5 + 10 * stripArea(11, 4, 17, 25) * 6, 0.5);
}
