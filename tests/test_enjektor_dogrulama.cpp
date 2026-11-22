// Independent review checks for the 09-DB01 atomizer samples (reviewer's regression tests).
//  * hardware sample = 5 solids, NO fluid volume; fluid volume only in the _akis sample
//  * filter windows agree with the drawing: far-right quadrant (azimuth 0..90 deg) fully open
//  * slot = straight, offset groove on the 110 deg cone; MEASURED throat sections of the fluid volume
//    (slot channel, orifice) against the parameter table
//  * construction planes are flagged show=0; SIMD state helper is callable
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/MfgChecks.h"
#include "pc/Model.h"
#include "pc/SimdState.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Pln.hxx>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>

using namespace pc;
using namespace pc::model;

namespace {
const double kPi = 3.14159265358979323846;
const std::string kDir = PC_SAMPLES_DIR;

struct Loaded {
  Document doc;
  ModelResult r;
  bool ok = false;
};
const Loaded& hardware() {
  static Loaded L;
  static bool init = false;
  if (!init) {
    init = true;
    std::string err;
    if (loadDocument(L.doc, kDir + "/enjektor_09DB01.pcad", &err)) { L.r = regenerate(L.doc); L.ok = true; }
    else ADD_FAILURE() << err;
  }
  return L;
}
const Loaded& fluid() {
  static Loaded L;
  static bool init = false;
  if (!init) {
    init = true;
    std::string err;
    if (loadDocument(L.doc, kDir + "/enjektor_09DB01_akis.pcad", &err)) { L.r = regenerate(L.doc); L.ok = true; }
    else ADD_FAILURE() << err;
  }
  return L;
}
bool inside(const TopoDS_Shape& solid, double x, double y, double z) {
  BRepClass3d_SolidClassifier cl(solid, gp_Pnt(x, y, z), 1e-7);
  return cl.State() == TopAbs_IN;
}
// area of (solid ∩ rectangular patch) ; patch centred at c, normal n, half sizes hu x hv along u / (n x u)
double patchArea(const TopoDS_Shape& solid, const gp_Pnt& c, const gp_Dir& n, const gp_Dir& u, double hu, double hv) {
  gp_Ax3 ax(c, n, u);
  TopoDS_Face f = BRepBuilderAPI_MakeFace(gp_Pln(ax), -hu, hu, -hv, hv).Face();
  BRepAlgoAPI_Common cm(solid, f);
  if (!cm.IsDone() || cm.Shape().IsNull()) return -1;
  GProp_GProps g;
  BRepGProp::SurfaceProperties(cm.Shape(), g);
  return g.Mass();
}
}  // namespace

TEST(EnjektorDogrulama, HardwareSampleHasNoFluidBodyAndFluidSampleIsOneSolid) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok && H.r.ok);
  EXPECT_EQ(H.r.bodies.size(), 5u);
  EXPECT_EQ(H.r.body("AkisHacmi"), nullptr) << "the fluid volume must not clutter the hardware model";
  for (auto& f : H.doc.features) EXPECT_NE(f.prop("role"), "fluid") << f.id;
  const auto& F = fluid();
  ASSERT_TRUE(F.ok && F.r.ok);
  ASSERT_EQ(F.r.bodies.size(), 1u);
  EXPECT_EQ(F.r.bodies[0].name, "AkisHacmi");
  int n = 0;
  for (TopExp_Explorer ex(F.r.bodies[0].shape, TopAbs_SOLID); ex.More(); ex.Next()) ++n;
  EXPECT_EQ(n, 1);
}

TEST(EnjektorDogrulama, ConstructionPlanesAreHiddenByDefault) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok);
  int planes = 0;
  for (auto& f : H.doc.features)
    if (f.type == FeatureType::Plane) { ++planes; EXPECT_EQ(f.prop("show", "1"), "0") << f.id; }
  EXPECT_EQ(planes, 2);
  EXPECT_EQ(H.r.datumPlanes.size(), 2u);   // still evaluated (sketches depend on them)
}

// Drawing (section, right half): at both window rows the wall in the section plane (azimuth 0) is missing and
// the mesh is visible through the far wall from the axis out to r = 3.7, i.e. azimuth 0..90 deg is open.
// The rejected layout (windows centred at 315/135 deg) left a 30 deg bridge at 30..60 deg in that quadrant.
TEST(EnjektorDogrulama, FilterWindowsOpenWhereTheDrawingShowsThem) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok && H.r.ok);
  const BodyInfo* fb = H.r.body("Filtre");
  ASSERT_TRUE(fb);
  for (double z : {-9.7, -13.9}) {
    for (int az = 0; az <= 90; az += 5) {
      const double a = az * kPi / 180;
      EXPECT_FALSE(inside(fb->shape, 3.9 * std::cos(a), 3.9 * std::sin(a), z)) << "wall at azimuth " << az << " z " << z;
    }
    // the assumed bridges sit where the drawing cannot show them (removed quarter / behind the outside view)
    for (int az : {135, 315}) {
      const double a = az * kPi / 180;
      EXPECT_TRUE(inside(fb->shape, 3.9 * std::cos(a), 3.9 * std::sin(a), z)) << "bridge at azimuth " << az;
    }
    // lands between the rows are solid all around
  }
  for (int az = 0; az < 360; az += 15) {
    const double a = az * kPi / 180;
    EXPECT_TRUE(inside(fb->shape, 3.9 * std::cos(a), 3.9 * std::sin(a), -11.8)) << az;
  }
}

// The slot is a straight groove: plane floor + two plane walls parallel to (and offset from) a meridian plane.
TEST(EnjektorDogrulama, SlotIsStraightOffsetGrooveOnTheSeatCone) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok && H.r.ok);
  const ParamValues& v = H.r.params;
  const double az = v.at("slot_az") * kPi / 180, e = v.at("e_slot"), w = v.at("w_slot");
  const gp_Dir nrm(-std::sin(az), std::cos(az), 0);   // normal of the meridian plane at slot_az
  int walls = 0, floors = 0;
  double dmin = 1e9, dmax = -1e9;
  for (auto& [name, face] : H.r.faces) {
    if (name.rfind("Slot.", 0) != 0) continue;
    BRepAdaptor_Surface s(face);
    ASSERT_EQ(s.GetType(), GeomAbs_Plane) << name << ": slot faces must be planar (straight groove)";
    const gp_Pln pl = s.Plane();
    const double c = std::abs(pl.Axis().Direction().Dot(nrm));
    if (c > 0.999) {   // wall: parallel to the meridian plane, at signed distance from the axis
      ++walls;
      const double d = gp_Vec(gp_Pnt(0, 0, 0), pl.Location()).Dot(gp_Vec(nrm));
      dmin = std::min(dmin, d);
      dmax = std::max(dmax, d);
    } else if (c < 1e-6) {
      ++floors;
      // floor is parallel to the 110 deg generatrix: its normal makes (90 - 55) deg with the axis
      const double cz = std::abs(pl.Axis().Direction().Z());
      EXPECT_NEAR(cz, std::sin(v.at("koni_on") / 2 * kPi / 180), 1e-6) << name;
    }
  }
  EXPECT_EQ(walls, 2);
  EXPECT_GE(floors, 1);
  EXPECT_NEAR(dmax - dmin, w, 1e-6);                        // width
  EXPECT_NEAR(std::abs((dmax + dmin) / 2), e, 1e-6);        // centre-plane offset from the axis
  std::printf("[dogrulama] slot walls at %.4f / %.4f mm from the axis plane -> w = %.4f, e = %.4f\n", dmin, dmax,
              dmax - dmin, std::abs((dmax + dmin) / 2));
}

// Throat sections MEASURED on the fluid solid (not from formulas): slot channel inside the contact band and
// the orifice. They must be the two smallest passages; everything else is >= 5x the orifice.
TEST(EnjektorDogrulama, MeasuredThroatSectionsOfTheFluidVolume) {
  const auto& F = fluid();
  ASSERT_TRUE(F.ok && F.r.ok);
  const TopoDS_Shape& fl = F.r.bodies[0].shape;
  const ParamValues& v = F.r.params;
  const double az = v.at("slot_az") * kPi / 180, a = v.at("koni_on") / 2 * kPi / 180, e = v.at("e_slot");
  const double w = v.at("w_slot"), h = v.at("h_slot"), zuc = v.at("z_uc"), ruc = v.at("d_uc") / 2;
  const gp_Dir rad(std::cos(az), std::sin(az), 0), nrm(-std::sin(az), std::cos(az), 0);
  // slot direction (down the generatrix) and the in-plane normal of the cone surface
  const gp_Dir g(std::sin(a) * rad.X(), std::sin(a) * rad.Y(), -std::cos(a));
  double amin = 1e9;
  std::printf("[dogrulama] %-10s %10s %10s\n", "r [mm]", "A_slot1", "w*h");
  const double rMouth = v.at("Ds") / 2 + 0.03;   // just inside the contact band = chamber mouth = throat
  double aMouth = 0;
  for (double r : {rMouth, 0.9, 1.2, 1.5, 1.8, 2.1, 2.4, 2.7}) {
    const double s = (r - ruc) / std::sin(a), off = h / 2;
    const double rho = ruc - off * std::cos(a) + s * std::sin(a), z = zuc - off * std::sin(a) - s * std::cos(a);
    // slot centre plane is shifted by -e_slot along the datum-plane normal (sketch offset = -e_slot)
    gp_Pnt c(rho * rad.X(), rho * rad.Y(), z);
    gp_Pnt c1 = c.Translated(gp_Vec(nrm) * e), c2 = c.Translated(gp_Vec(nrm) * -e);
    gp_Pnt cc = inside(fl, c1.X(), c1.Y(), c1.Z()) ? c1 : c2;
    ASSERT_TRUE(inside(fl, cc.X(), cc.Y(), cc.Z())) << "slot channel centre at r = " << r;
    const double A = patchArea(fl, cc, g, nrm, 0.4, 0.4);
    std::printf("[dogrulama] %-10.2f %10.5f %10.5f\n", r, A, w * h);
    EXPECT_GT(A, 0.80 * w * h) << r;
    EXPECT_LT(A, 1.10 * w * h) << r;
    amin = std::min(amin, A);
    if (r == rMouth) aMouth = A;
  }
  // the channel gets shallower towards the tip: the throat is at the chamber mouth and the derived parameter
  // A_slot_etkin reproduces the MEASURED section there (n*w*h overestimates it by 7-9 %)
  EXPECT_NEAR(amin, aMouth, 1e-9);
  EXPECT_NEAR(v.at("A_slot_etkin"), aMouth * v.at("n_slot"), 0.03 * v.at("A_slot_etkin"));
  const double nslot = v.at("n_slot");
  // orifice: section at mid length
  const double zo = (v.at("z_1") + v.at("z_2")) / 2;
  const double Ao = patchArea(fl, gp_Pnt(0, 0, zo), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0), 1.0, 1.0);
  EXPECT_NEAR(Ao, v.at("A_orifis"), 0.01 * v.at("A_orifis"));
  std::printf("[dogrulama] measured: slot throat %.5f mm2 x %g = %.5f (A_slot_etkin = %.5f, nominal n*w*h = %.5f) ; orifice %.5f (table %.5f)\n",
              amin, nslot, amin * nslot, v.at("A_slot_etkin"), v.at("A_slot"), Ao, v.at("A_orifis"));
  EXPECT_NEAR(amin * nslot, v.at("A_slot"), 0.15 * v.at("A_slot"));
  // other passages, measured: annulus screw/bore, screw axial bore, filter bore -- all far larger
  const double Ahalka = patchArea(fl, gp_Pnt(0, 0, 6.0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0), 4.6, 4.6);
  const double Avida = patchArea(fl, gp_Pnt(0, 0, 1.0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0), 2.5, 2.5);
  const double Afiltre = patchArea(fl, gp_Pnt(0, 0, -6.0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0), 3.9, 3.9);
  std::printf("[dogrulama] measured: annulus %.3f (table %.3f) ; screw bore %.3f (table %.3f) ; filter bore %.3f (table %.3f)\n",
              Ahalka, v.at("A_halka"), Avida, v.at("A_vida_delik"), Afiltre, v.at("A_filtre_delik"));
  // (the z = 6 section also cuts the 0.10 mm stem clearance inside the screw bore: a dead-end pocket)
  const double Asap = kPi * (std::pow(v.at("d_vida_delik"), 2) - std::pow(v.at("d_sap"), 2)) / 4;
  EXPECT_NEAR(Ahalka, v.at("A_halka") + Asap, 0.02 * v.at("A_halka"));
  EXPECT_NEAR(Avida, v.at("A_vida_delik"), 0.02 * v.at("A_vida_delik"));
  EXPECT_NEAR(Afiltre, v.at("A_filtre_delik"), 0.02 * v.at("A_filtre_delik"));
  EXPECT_GT(std::min({Ahalka, Avida, Afiltre}), 50 * Ao);
  EXPECT_LT(amin * nslot, Ao) << "slots are the metering section at the drawn geometry";
}

TEST(EnjektorDogrulama, SimdStateResetIsHarmless) {
  double x = 0.1;
  for (int i = 0; i < 3; ++i) { pc::resetSimdUpperState(); x = std::sqrt(x + 1.0); }
  EXPECT_NEAR(x, std::sqrt(std::sqrt(std::sqrt(1.1) + 1.0) + 1.0), 1e-15);
}

// Design relations are derived, range-checked parameters. A violation must (a) be an error, (b) name an INPUT
// parameter with a suggested value (a derived quantity cannot be typed in), (c) disappear when applied.
TEST(EnjektorDogrulama, DesignRelationViolationSuggestsAnInputValue) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok);
  // both values are inside their own ranges, the combination is not a workable slot
  auto vr = pc::mfg::validateChange(H.doc, {{"h_slot", "0.3"}, {"w_slot", "0.5"}}, H.doc.mfg);
  EXPECT_FALSE(vr.acceptable());
  std::map<std::string, std::string> fix = {{"h_slot", "0.3"}, {"w_slot", "0.5"}};
  int n = 0;
  for (auto& i : vr.report.issues) {
    if (i.ruleId != "PARAM.RANGE") continue;
    ++n;
    std::printf("[dogrulama] %s\n", i.message.c_str());
    ASSERT_FALSE(i.params.empty());
    const Parameter* q = H.doc.params.find(i.params[0]);
    ASSERT_TRUE(q) << i.params[0];
    EXPECT_TRUE(pc::referencedNames(q->expression).empty()) << "suggestion must target an input, not " << q->name;
    ASSERT_TRUE(i.suggestedValue.has_value()) << i.message;
    EXPECT_GE(*i.suggestedValue, q->minValue - 1e-9);
    EXPECT_LE(*i.suggestedValue, q->maxValue + 1e-9);
    fix[i.params[0]] = formatNumber(*i.suggestedValue);
  }
  EXPECT_EQ(n, 2) << "slot_uc_payi and slot_ic_payi";
  auto vr2 = pc::mfg::validateChange(H.doc, fix, H.doc.mfg);
  for (auto& i : vr2.report.issues) EXPECT_NE(i.ruleId, "PARAM.RANGE") << i.message;
  EXPECT_TRUE(vr2.acceptable());
  // defaults satisfy every relation
  auto v = H.doc.evaluateParams();
  EXPECT_GT(v.at("slot_uc_payi"), 0);
  EXPECT_GT(v.at("slot_ic_payi"), 0);
  EXPECT_GT(v.at("slot_dis_payi"), 0);
}

// Plain (input) parameters keep the classic "set it to the limit" suggestion.
TEST(EnjektorDogrulama, InputRangeViolationStillSuggestsTheLimit) {
  const auto& H = hardware();
  ASSERT_TRUE(H.ok);
  auto vr = pc::mfg::validateChange(H.doc, {{"Q", "9"}}, H.doc.mfg);
  bool found = false;
  for (auto& i : vr.report.issues)
    if (i.ruleId == "PARAM.RANGE" && !i.params.empty() && i.params[0] == "Q") {
      found = true;
      ASSERT_TRUE(i.suggestedValue.has_value());
      EXPECT_NEAR(*i.suggestedValue, 7.5, 1e-9);
    }
  EXPECT_TRUE(found);
}
