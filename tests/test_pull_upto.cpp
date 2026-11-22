// SC-PULLUPTO (docs/spaceclaim_ux/PULL_UPTO.md): Extrude "Kadar (Up To)" targets (vertex / face / plane / curve of
// another sketch), associative regeneration, Extrude `regions=`, tolerant sketch regions (gaps, T-junctions, overlaps,
// nested loops) — model / sketch level, no GUI.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/Regions.h"
#include "pc/UpTo.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <gtest/gtest.h>

#include <cmath>

using namespace pc;
using namespace pc::model;

namespace {

std::string dump(const ModelResult& r) {
  std::string s;
  for (auto& st : r.status) s += st.featureId + (st.ok ? " ok " : " FAIL ") + st.message + "\n";
  return s;
}
const FeatureStatus* statusOf(const ModelResult& r, const std::string& id) {
  for (auto& s : r.status) if (s.featureId == id) return &s;
  return nullptr;
}
const FeatureInfo* infoOf(const ModelResult& r, const std::string& id) {
  for (auto& s : r.info) if (s.featureId == id) return &s;
  return nullptr;
}
struct Box { double x0, y0, z0, x1, y1, z1; };
Box bbox(const TopoDS_Shape& s) {
  Bnd_Box b;
  BRepBndLib::AddOptimal(s, b, Standard_False, Standard_False);
  Box o{};
  b.Get(o.x0, o.y0, o.z0, o.x1, o.y1, o.z1);
  return o;
}
Feature sketchFeat(const std::string& id, const Sketch& sk) {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  f.sketch = sk;
  return f;
}
Feature extrude(const std::string& id, const std::string& sk, std::map<std::string, std::string> props) {
  Feature f;
  f.id = id;
  f.type = FeatureType::Extrude;
  f.props = std::move(props);
  f.props["sketch"] = sk;
  return f;
}
// closed polyline with shared points
Sketch poly(SketchPlaneKind plane, const std::vector<std::pair<double, double>>& pts) {
  Sketch s = Sketch::create();
  s.plane.kind = plane;
  std::vector<int> ids;
  for (auto& p : pts) ids.push_back(s.addPoint(p.first, p.second));
  for (size_t i = 0; i < ids.size(); ++i) s.addLine(ids[i], ids[(i + 1) % ids.size()]);
  return s;
}
// front profile on XZ (u = X, v = Z, normal = -Y): L shape 60 x 30
Sketch frontProfile() { return poly(SketchPlaneKind::XZ, {{0, 0}, {60, 0}, {60, 10}, {15, 10}, {15, 30}, {0, 30}}); }

// top outline on XY: rectangle x 0..60, y -W..0 with Length(left) = "W"; returns the id of the corner (0,-W)
Sketch topOutline(int* corner) {
  Sketch s = Sketch::create();
  s.plane.kind = SketchPlaneKind::XY;
  const auto L = s.addRectangle(0, -40, 60, 0);   // bottom, right, top, left
  const SkEntity* bottom = s.entity(L[0]);
  const SkEntity* left = s.entity(L[3]);
  // the top-left corner (0,0) at the origin
  int tl = -1;
  for (int pid : s.entity(L[2])->points)
    if (std::fabs(s.point(pid)->x) < 1e-9 && std::fabs(s.point(pid)->y) < 1e-9) tl = pid;
  s.addConstraint(SkConstraintType::Coincident, {0, tl});
  s.addConstraint(SkConstraintType::Length, {bottom->id}, "60");
  s.addConstraint(SkConstraintType::Length, {left->id}, "W");
  *corner = bottom->points[0];   // (0,-40)
  return s;
}

Document twoSketchDoc(int* corner, const std::string& upTo) {
  Document d;
  d.params.add({"W", "40"});
  d.features.push_back(sketchFeat("Sketch1", frontProfile()));
  d.features.push_back(sketchFeat("Sketch2", topOutline(corner)));
  std::string ref = upTo;
  if (ref.empty()) ref = "sketch:Sketch2:p" + std::to_string(*corner);
  d.features.push_back(extrude("Extrude1", "Sketch1", {{"distance", "5"}, {"op", "new"}, {"upTo", ref}}));
  return d;
}

} // namespace

// ---------------------------------------------------------------- Up To
TEST(PullUpTo, VertexOfOtherSketch) {
  int corner = -1;
  Document d = twoSketchDoc(&corner, "");
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  const Box b = bbox(r.shape);
  EXPECT_NEAR(b.y0, -40, 1e-6);   // XZ normal = -Y: the profile is pulled up to the corner (0,-40,0)
  EXPECT_NEAR(b.y1, 0, 1e-6);
  EXPECT_NEAR(b.z1, 30, 1e-6);
  EXPECT_NEAR(infoOf(r, "Extrude1")->values.at("distance"), 40, 1e-9);
  EXPECT_NEAR(infoOf(r, "Extrude1")->values.at("upTo"), 40, 1e-9);
}

TEST(PullUpTo, AssociativeWhenTargetSketchDimensionChanges) {
  int corner = -1;
  Document d = twoSketchDoc(&corner, "");
  ModelResult r1 = regenerate(d);
  ASSERT_TRUE(r1.ok) << dump(r1);
  const double v1 = massProperties(r1.shape).volume;
  ModelResult r2 = regenerate(d, {{"W", "55"}});
  ASSERT_TRUE(r2.ok) << dump(r2);
  EXPECT_NEAR(bbox(r2.shape).y0, -55, 1e-6);
  EXPECT_NEAR(massProperties(r2.shape).volume, v1 * 55.0 / 40.0, 1e-6 * v1);
  // editing the dimension expression itself (not the parameter) works the same
  for (auto& c : d.features[1].sketch->constraints)
    if (c.valueExpr == "W") c.valueExpr = "25";
  ModelResult r3 = regenerate(d);
  ASSERT_TRUE(r3.ok) << dump(r3);
  EXPECT_NEAR(bbox(r3.shape).y0, -25, 1e-6);
}

TEST(PullUpTo, CurveOfOtherSketchAndOffset) {
  int corner = -1;
  Document d = twoSketchDoc(&corner, "");
  // the bottom line of the top outline (y = -40 everywhere)
  const SkEntity& bottom = d.features[1].sketch->entities[0];
  d.features[2].props["upTo"] = "sketch:Sketch2:e" + std::to_string(bottom.id);
  d.features[2].props["upToOffset"] = "-10";   // Cetvel: stop 10 mm before it
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.shape).y0, -30, 1e-6);
}

TEST(PullUpTo, FaceAndDatumPlaneAndSketchPlane) {
  // a block 0..60 x -40..0 x 0..30 from the top outline; a second profile on XY offset 30 pulled DOWN to the block's
  // bottom face, then up to a datum plane / the plane of another sketch
  int corner = -1;
  Document d;
  d.params.add({"W", "40"});
  d.features.push_back(sketchFeat("Sketch1", topOutline(&corner)));
  d.features.push_back(extrude("Extrude1", "Sketch1", {{"distance", "30"}, {"op", "new"}}));
  Sketch s2 = poly(SketchPlaneKind::XY, {{100, 0}, {120, 0}, {120, 20}, {100, 20}});
  s2.plane.offsetExpr = "50";
  d.features.push_back(sketchFeat("Sketch2", s2));
  d.features.push_back(extrude("Extrude2", "Sketch2", {{"distance", "1"}, {"op", "new"}, {"upTo", "Extrude1.start"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(infoOf(r, "Extrude2")->values.at("upTo"), -50, 1e-9);
  const BodyInfo* b2 = r.body("Extrude2");
  ASSERT_NE(b2, nullptr);
  EXPECT_NEAR(bbox(b2->shape).z0, 0, 1e-6);
  EXPECT_NEAR(bbox(b2->shape).z1, 50, 1e-6);
  // datum plane (axisAngle X, angle 90 -> plane containing the X axis... use a plain XY-parallel plane instead)
  Feature pl;
  pl.id = "Plane1";
  pl.type = FeatureType::Plane;
  pl.props = {{"kind", "axisAngle"}, {"axis", "X"}, {"angle", "0"}, {"offset", "0"}};
  // axisAngle X angle 0: base u = +Y, v = X, normal = Y x X = -Z -> parallel to XY, through the origin
  d.features.insert(d.features.begin() + 2, pl);
  d.features[4].props["upTo"] = "datum:Plane1";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.body("Extrude2")->shape).z0, 0, 1e-6);
  // plane of another sketch (Sketch1 is on XY at z = 0)
  d.features[4].props["upTo"] = "sketch:Sketch1:plane";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.body("Extrude2")->shape).z0, 0, 1e-6);
  // the block grows (Extrude1 distance 30 -> the bottom stays) / a top face target moves with it
  d.features[4].props["upTo"] = "Extrude1.end";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.body("Extrude2")->shape).z0, 30, 1e-6);
  d.features[1].props["distance"] = "35";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.body("Extrude2")->shape).z0, 35, 1e-6);
}

TEST(PullUpTo, ModelVertexAndUpToDistanceApi) {
  int corner = -1;
  Document d;
  d.params.add({"W", "40"});
  d.features.push_back(sketchFeat("Sketch1", topOutline(&corner)));
  d.features.push_back(extrude("Extrude1", "Sketch1", {{"distance", "30"}, {"op", "new"}}));
  ModelResult r0 = regenerate(d);
  ASSERT_TRUE(r0.ok) << dump(r0);
  // any vertical edge: its upper vertex is at z = 30
  std::string vref;
  for (const auto& [n, e] : r0.edges) {
    double s0 = 0, s1 = 0;
    std::string err;
    const gp_Ax3 xy = sketch::standardFrame(SketchPlaneKind::XY, 0);
    if (upToDistance(r0, xy, "vtx:" + n + ":0", &s0, &err) && upToDistance(r0, xy, "vtx:" + n + ":1", &s1, &err) &&
        std::fabs(std::fabs(s1 - s0) - 30) < 1e-6) {
      vref = "vtx:" + n + (s1 > s0 ? ":1" : ":0");
      break;
    }
  }
  ASSERT_FALSE(vref.empty());
  Sketch s2 = poly(SketchPlaneKind::XY, {{100, 0}, {110, 0}, {110, 10}, {100, 10}});
  d.features.push_back(sketchFeat("Sketch2", s2));
  d.features.push_back(extrude("Extrude2", "Sketch2", {{"distance", "1"}, {"op", "new"}, {"upTo", vref}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.body("Extrude2")->shape).z1, 30, 1e-6);
  // errors: non-parallel plane, unknown target, target after the extrude
  double s = 0;
  std::string err;
  EXPECT_FALSE(upToDistance(r, sketch::standardFrame(SketchPlaneKind::XZ, 0), "sketch:Sketch1:plane", &s, &err));
  EXPECT_NE(err.find("paralel"), std::string::npos);
  d.features[3].props["upTo"] = "sketch:Nope:p1";
  r = regenerate(d);
  EXPECT_FALSE(statusOf(r, "Extrude2")->ok);
}

TEST(PullUpTo, ReverseDirectionFromSign) {
  // target on the other side of the sketch plane -> direction reverse
  int corner = -1;
  Document d;
  d.params.add({"W", "40"});
  Sketch top = topOutline(&corner);
  top.plane.offsetExpr = "-20";   // z = -20
  d.features.push_back(sketchFeat("Sketch1", top));
  d.features.push_back(sketchFeat("Sketch2", poly(SketchPlaneKind::XY, {{0, 0}, {10, 0}, {10, 10}, {0, 10}})));
  d.features.push_back(extrude("Extrude1", "Sketch2", {{"distance", "5"}, {"op", "new"}, {"upTo", "sketch:Sketch1:plane"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(bbox(r.shape).z0, -20, 1e-6);
  EXPECT_NEAR(bbox(r.shape).z1, 0, 1e-6);
}

// ---------------------------------------------------------------- tolerant regions
namespace {
// "imported drawing": every line has its own end points (no shared ids), displaced by `gap`
Sketch loose(const std::vector<std::pair<double, double>>& pts, double gap, double overlap = 0) {
  Sketch s = Sketch::create();
  for (size_t i = 0; i < pts.size(); ++i) {
    auto a = pts[i], b = pts[(i + 1) % pts.size()];
    const double dx = b.first - a.first, dy = b.second - a.second, l = std::hypot(dx, dy);
    const double ux = dx / l, uy = dy / l;
    // start shifted along the edge by `gap` (a gap), end extended by `overlap` (an overshoot)
    s.addLine(a.first + ux * gap, a.second + uy * gap, b.first + ux * overlap, b.second + uy * overlap);
  }
  return s;
}
const gp_Ax3 kXY = sketch::standardFrame(SketchPlaneKind::XY, 0);
}

TEST(TolerantRegions, GapsAreClosed) {
  for (double gap : {0.01, 0.02, 0.05}) {
    Sketch s = loose({{0, 0}, {50, 0}, {50, 30}, {0, 30}}, gap);
    EXPECT_FALSE(sketch::buildProfile(s, kXY).ok) << "classic profile must reject gap " << gap;
    const auto R = sketch::sketchRegions(s, kXY);
    ASSERT_EQ(R.profile.faces.size(), 1u) << "gap " << gap << ": " << R.profile.message;
    EXPECT_FALSE(R.legacy);
    EXPECT_TRUE(R.openPoints.empty());
    EXPECT_GE(R.healedGaps, 4);
    EXPECT_NEAR(R.areas[0], 1500, 1500 * 0.01);
  }
  // larger than the tolerance: open, the gap is reported
  Sketch s = loose({{0, 0}, {50, 0}, {50, 30}, {0, 30}}, 0.5);
  const auto R = sketch::sketchRegions(s, kXY, 0.05);
  EXPECT_TRUE(R.profile.faces.empty());
  EXPECT_EQ(R.openPoints.size(), 8u);
  EXPECT_NEAR(R.nearestGap, 0.5, 1e-9);
  EXPECT_NE(R.profile.message.find("Açık kontur"), std::string::npos);
  // ... and closes with a larger explicit tolerance
  EXPECT_EQ(sketch::sketchRegions(s, kXY, 0.6).profile.faces.size(), 1u);
}

TEST(TolerantRegions, OverlapsAndTJunctions) {
  // overshooting corners (each line runs 0.03 past the corner): the ends are merged with the next start
  Sketch s = loose({{0, 0}, {50, 0}, {50, 30}, {0, 30}}, 0, 0.03);
  auto R = sketch::sketchRegions(s, kXY);
  ASSERT_EQ(R.profile.faces.size(), 1u) << R.profile.message;
  EXPECT_NEAR(R.areas[0], 1500, 3);
  // overshoot larger than the tolerance (0.5 mm, tolerance 0.1): real crossings, the stubs are pruned
  s = loose({{0, 0}, {50, 0}, {50, 30}, {0, 30}}, 0, 0.5);
  R = sketch::sketchRegions(s, kXY);
  ASSERT_EQ(R.profile.faces.size(), 1u) << R.profile.message;
  EXPECT_NEAR(R.areas[0], 1500, 1e-6);
  EXPECT_EQ(R.openPoints.size(), 4u);   // the stub ends are reported (not a closed loop of their own)
  // a rectangle + a middle line ending 0.02 short of the top and bottom edges (T-junctions) -> two regions
  Sketch t = Sketch::create();
  t.addRectangle(0, 0, 60, 40);
  t.addLine(30, 0.02, 30, 39.98);
  R = sketch::sketchRegions(t, kXY);
  ASSERT_EQ(R.profile.faces.size(), 2u) << R.profile.message;
  EXPECT_GE(R.tJunctions, 2);
  EXPECT_NEAR(R.areas[0], 1200, 1);
  EXPECT_NEAR(R.areas[1], 1200, 1);
  // a duplicated (collinear, overlapping) edge is dropped
  Sketch u = Sketch::create();
  u.addRectangle(0, 0, 20, 10);
  u.addLine(5, 0, 25, 0);   // overlaps the bottom edge and sticks out
  R = sketch::sketchRegions(u, kXY);
  ASSERT_EQ(R.profile.faces.size(), 1u) << R.profile.message;
  EXPECT_GE(R.overlaps, 1);
  EXPECT_NEAR(R.areas[0], 200, 1e-6);
  // two crossing rectangles -> 3 regions
  Sketch v = Sketch::create();
  v.addRectangle(0, 0, 20, 20);
  v.addRectangle(10, 10, 30, 30);
  R = sketch::sketchRegions(v, kXY);
  EXPECT_EQ(R.profile.faces.size(), 3u) << R.profile.message;
  EXPECT_EQ(R.crossings, 2);
}

TEST(TolerantRegions, NestedLoopsAndDisjoint) {
  // plate with a hole whose circle is an "imported" closed loop + an island inside the hole + a separate square
  Sketch s = loose({{0, 0}, {80, 0}, {80, 50}, {0, 50}}, 0.02);
  s.addCircle(30.0, 25.0, 15.0);
  s.addCircle(30.0, 25.0, 5.0);
  Sketch sq = loose({{100, 0}, {110, 0}, {110, 10}, {100, 10}}, 0.01);
  for (const auto& e : sq.entities) {
    const SkPoint* a = sq.point(e.points[0]);
    const SkPoint* b = sq.point(e.points[1]);
    s.addLine(a->x, a->y, b->x, b->y);
  }
  const auto R = sketch::sketchRegions(s, kXY);
  ASSERT_EQ(R.profile.faces.size(), 4u) << R.profile.message;
  // order: outer-boundary area descending -> plate (with hole), ring, square, island
  EXPECT_EQ(R.holes[0], 1);
  EXPECT_NEAR(R.areas[0], 4000 - M_PI * 225, 2);
  EXPECT_EQ(R.holes[1], 1);
  EXPECT_NEAR(R.areas[1], M_PI * (225 - 25), 1);
  EXPECT_NEAR(R.areas[2], 100, 0.5);
  EXPECT_EQ(R.holes[3], 0);
  EXPECT_NEAR(R.areas[3], M_PI * 25, 0.5);
  // legacy-clean sketches keep the classic profile (unchanged documents)
  Sketch c = Sketch::create();
  c.addRectangle(0, 0, 10, 10);
  EXPECT_TRUE(sketch::sketchRegions(c, kXY).legacy);
}

TEST(TolerantRegions, ExtrudeSelectedRegionsWithNames) {
  // an imported L profile with gaps on XZ + a separate square; Extrude only region 1 (the square) / region 0
  Document d;
  Sketch s = loose({{0, 0}, {60, 0}, {60, 10}, {15, 10}, {15, 30}, {0, 30}}, 0.02);
  s.plane.kind = SketchPlaneKind::XZ;
  Sketch sq = loose({{100, 0}, {110, 0}, {110, 10}, {100, 10}}, 0.02);
  for (const auto& e : sq.entities) {
    const SkPoint* a = sq.point(e.points[0]);
    const SkPoint* b = sq.point(e.points[1]);
    s.addLine(a->x, a->y, b->x, b->y);
  }
  d.features.push_back(sketchFeat("Sketch1", s));
  d.features.push_back(extrude("Extrude1", "Sketch1", {{"distance", "10"}, {"op", "new"}, {"regions", "1"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 1000, 1e-6);   // corners rebuilt exactly
  d.features[1].props["regions"] = "0";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, (60 * 10 + 15 * 20) * 10, 10);
  // side faces keep the entity-based names
  int sides = 0;
  for (const auto& [n, f] : r.faces) if (n.rfind("Extrude1.side.e", 0) == 0) ++sides;
  EXPECT_EQ(sides, 6);
  EXPECT_TRUE(r.faces.count("Extrude1.end") && r.faces.count("Extrude1.start"));
  // both regions, then a region index that does not exist
  d.features[1].props["regions"] = "0,1";
  r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 9000 + 1000, 12);
  d.features[1].props["regions"] = "5";
  r = regenerate(d);
  EXPECT_FALSE(statusOf(r, "Extrude1")->ok);
  // the gap tolerance stored on the sketch feature is honoured: 0.001 mm leaves the profile open
  d.features[1].props["regions"] = "0";
  d.features[0].props["gapTol"] = "0.001";
  r = regenerate(d);
  EXPECT_FALSE(statusOf(r, "Extrude1")->ok);
}

TEST(TolerantRegions, ArcsWithGaps) {
  // a slot 40 x 20 (two lines + two half circles) drawn with 0.03 mm gaps at every joint, on XZ (frame u = X, v = Z)
  Sketch s = Sketch::create();
  s.plane.kind = SketchPlaneKind::XZ;
  s.addLine(0, -10, 40, -10);
  s.addLine(40.03, 10, 0, 10);
  {
    const int c = s.addPoint(40, 0), a = s.addPoint(40, -9.97), b = s.addPoint(40, 10);
    s.addArc(c, a, b);
  }
  {
    const int c = s.addPoint(0, 0), a = s.addPoint(-0.03, 10), b = s.addPoint(0, -10.03);
    s.addArc(c, a, b);
  }
  const gp_Ax3 xz = sketch::standardFrame(SketchPlaneKind::XZ, 0);
  const auto R = sketch::sketchRegions(s, xz);
  ASSERT_EQ(R.profile.faces.size(), 1u) << R.profile.message;
  EXPECT_NEAR(R.areas[0], 800 + M_PI * 100, 2);
  // the region extrudes into a valid solid with entity-named side faces (2 planes + 2 half cylinders)
  Document d;
  d.features.push_back(sketchFeat("Sketch1", s));
  d.features.push_back(extrude("Extrude1", "Sketch1", {{"distance", "5"}, {"op", "new"}, {"regions", "0"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << dump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 5 * (800 + M_PI * 100), 10);
  int sides = 0;
  for (const auto& [n, f] : r.faces) if (n.rfind("Extrude1.side.e", 0) == 0) ++sides;
  EXPECT_EQ(sides, 4);
}
