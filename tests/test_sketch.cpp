// Tests for pc_sketch: constraint solver + profile construction.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Vec.hxx>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cmath>
// Performance budgets are tuned on Linux; the Windows (MSYS2 CLANG64) OCCT build measures ~1.5x slower.
#ifdef _WIN32
static constexpr double kPerfScale = 2.0;
#else
static constexpr double kPerfScale = 1.0;
#endif


using namespace pc;
using namespace pc::sketch;
using CT = SkConstraintType;

namespace {

const SkPoint& P(const Sketch& s, int id) { return *s.point(id); }
double dist(const Sketch& s, int a, int b) { return std::hypot(P(s, b).x - P(s, a).x, P(s, b).y - P(s, a).y); }
int lp(const Sketch& s, int line, int k) { return s.entity(line)->points[k]; }
double lineLen(const Sketch& s, int l) { return dist(s, lp(s, l, 0), lp(s, l, 1)); }
double dirX(const Sketch& s, int l) { return P(s, lp(s, l, 1)).x - P(s, lp(s, l, 0)).x; }
double dirY(const Sketch& s, int l) { return P(s, lp(s, l, 1)).y - P(s, lp(s, l, 0)).y; }
double lineDist(const Sketch& s, int p, int l) {  // signed
  double dx = dirX(s, l), dy = dirY(s, l);
  const SkPoint& a = P(s, lp(s, l, 0));
  return (dx * (P(s, p).y - a.y) - dy * (P(s, p).x - a.x)) / std::hypot(dx, dy);
}
double circleR(const Sketch& s, int e) {
  const SkEntity* en = s.entity(e);
  if (en->type == SkEntityType::Circle) return en->radius;
  return dist(s, en->points[0], en->points[1]);
}

constexpr double kTol = 1e-7;

// NOTE: Sketch::addCircle(0, 0, 5) with int literals picks addCircle(int center, double r, bool)!
int circ(Sketch& s, double cx, double cy, double r) { return s.addCircle(cx, cy, r); }

// Fully constrained rectangle W x H centred at origin (like bracket Sketch1). Returns line ids.
std::vector<int> paramRect(Sketch& s, const std::string& W, const std::string& H, double w0, double h0) {
  auto L = s.addRectangle(-w0 / 2, -h0 / 2, w0 / 2, h0 / 2);
  int p1 = lp(s, L[0], 0);
  s.addConstraint(CT::DistanceX, {0, p1}, "-(" + W + ")/2");
  s.addConstraint(CT::DistanceY, {0, p1}, "-(" + H + ")/2");
  s.addConstraint(CT::Length, {L[0]}, W);
  s.addConstraint(CT::Length, {L[1]}, H);
  return L;
}

double faceArea(const TopoDS_Face& f) {
  GProp_GProps g;
  BRepGProp::SurfaceProperties(f, g);
  return g.Mass();
}

gp_Dir faceNormal(const TopoDS_Face& f) {
  BRepAdaptor_Surface s(f);
  gp_Dir n = s.Plane().Axis().Direction();
  if (f.Orientation() == TopAbs_REVERSED) n.Reverse();
  return n;
}

int wireCount(const TopoDS_Face& f) {
  int n = 0;
  for (TopExp_Explorer ex(f, TopAbs_WIRE); ex.More(); ex.Next()) ++n;
  return n;
}

} // namespace

// ------------------------------------------------------------------ individual constraints
TEST(SketchSolver, CoincidentHorizontalVertical) {
  Sketch s = Sketch::create();
  int l1 = s.addLine(0, 0.5, 10, 1.3);
  int l2 = s.addLine(10.4, 1.1, 10.9, 9);
  s.addConstraint(CT::Coincident, {lp(s, l1, 1), lp(s, l2, 0)});
  s.addConstraint(CT::Horizontal, {l1});
  s.addConstraint(CT::Vertical, {l2});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_NEAR(dist(s, lp(s, l1, 1), lp(s, l2, 0)), 0, kTol);
  EXPECT_NEAR(dirY(s, l1), 0, kTol);
  EXPECT_NEAR(dirX(s, l2), 0, kTol);
  EXPECT_EQ(r.status, SolveStatus::UnderConstrained);
  // 8 unknowns - (2 + 1 + 1) = 4
  EXPECT_EQ(r.dof, 4);
}

TEST(SketchSolver, HorizontalVerticalPts) {
  Sketch s = Sketch::create();
  int a = s.addPoint(1, 2), b = s.addPoint(5, 3.5), c = s.addPoint(1.4, 7);
  s.addConstraint(CT::HorizontalPts, {a, b});
  s.addConstraint(CT::VerticalPts, {a, c});
  ASSERT_TRUE(solve(s, {}).ok());
  EXPECT_NEAR(P(s, a).y, P(s, b).y, kTol);
  EXPECT_NEAR(P(s, a).x, P(s, c).x, kTol);
}

TEST(SketchSolver, ParallelPerpendicular) {
  Sketch s = Sketch::create();
  int l1 = s.addLine(0, 0.1, 10, 1);
  int l2 = s.addLine(0, 5, 10, 4);
  int l3 = s.addLine(20, 0, 21, 10);
  s.addConstraint(CT::Parallel, {l1, l2});
  s.addConstraint(CT::Perpendicular, {l1, l3});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  double cr = dirX(s, l1) * dirY(s, l2) - dirY(s, l1) * dirX(s, l2);
  double dt = dirX(s, l1) * dirX(s, l3) + dirY(s, l1) * dirY(s, l3);
  EXPECT_NEAR(cr / (lineLen(s, l1) * lineLen(s, l2)), 0, 1e-9);
  EXPECT_NEAR(dt / (lineLen(s, l1) * lineLen(s, l3)), 0, 1e-9);
  // directions kept (no flip)
  EXPECT_GT(dirX(s, l1), 0);
  EXPECT_GT(dirX(s, l2), 0);
}

TEST(SketchSolver, TangentLineCircle) {
  Sketch s = Sketch::create();
  int l = s.addLine(-10, 5.5, 10, 6.2);
  int c = circ(s, 0, 0, 5);
  s.addConstraint(CT::Tangent, {l, c});
  s.addConstraint(CT::Radius, {c}, "5");
  s.addConstraint(CT::Fixed, {s.entity(c)->points[0]});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  // distance from center to the line == r (line stays above the circle)
  EXPECT_NEAR(std::abs(lineDist(s, s.entity(c)->points[0], l)), 5.0, kTol);
  EXPECT_GT(P(s, lp(s, l, 0)).y, 0);
}

TEST(SketchSolver, TangentCircleCircleExternalInternal) {
  Sketch s = Sketch::create();
  int c1 = circ(s, 0, 0, 10);
  int c2 = circ(s, 16, 1, 5);   // nearly external
  int c3 = circ(s, 4, 1, 5);    // nearly internal to c1
  s.addConstraint(CT::Fixed, {s.entity(c1)->points[0]});
  s.addConstraint(CT::Radius, {c1}, "10");
  s.addConstraint(CT::Tangent, {c1, c2});
  s.addConstraint(CT::Tangent, {c1, c3});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  int k1 = s.entity(c1)->points[0], k2 = s.entity(c2)->points[0], k3 = s.entity(c3)->points[0];
  EXPECT_NEAR(dist(s, k1, k2), circleR(s, c1) + circleR(s, c2), kTol);
  EXPECT_NEAR(dist(s, k1, k3), circleR(s, c1) - circleR(s, c3), kTol);
}

TEST(SketchSolver, ArcLineEndpointTangency) {
  // line from (0,0)->(10,0), arc centered near (10,5) starting at line end (tangent fillet-like)
  Sketch s = Sketch::create();
  int a = s.addPoint(0.0, 0.3), b = s.addPoint(10, 0.2);
  int l = s.addLine(a, b);
  int c = s.addPoint(10.5, 5), e = s.addPoint(15.2, 5.3);
  int arc = s.addArc(c, b, e);
  s.addConstraint(CT::Tangent, {l, arc});
  s.addConstraint(CT::Horizontal, {l});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  // radius vector at the joint perpendicular to the line
  double rx = P(s, b).x - P(s, c).x, ry = P(s, b).y - P(s, c).y;
  EXPECT_NEAR(rx * dirX(s, l) + ry * dirY(s, l), 0, 1e-6);
  EXPECT_NEAR(dist(s, c, e), dist(s, c, b), kTol);             // implicit arc equation
  EXPECT_NEAR(s.entity(arc)->radius, dist(s, c, b), 1e-12);    // radius field kept
}

TEST(SketchSolver, EqualFixedConcentric) {
  Sketch s = Sketch::create();
  int l1 = s.addLine(0, 0, 10, 0), l2 = s.addLine(0, 5, 14, 5);
  int c1 = circ(s, 30, 0, 4), c2 = circ(s, 30.5, 0.4, 7);
  int fp = lp(s, l1, 0);
  s.point(fp)->x = 1.25; s.point(fp)->y = -2.5;
  s.addConstraint(CT::Fixed, {fp});
  s.addConstraint(CT::Equal, {l1, l2});
  s.addConstraint(CT::Equal, {c1, c2});
  s.addConstraint(CT::Concentric, {c1, c2});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_NEAR(lineLen(s, l1), lineLen(s, l2), kTol);
  EXPECT_NEAR(circleR(s, c1), circleR(s, c2), kTol);
  EXPECT_NEAR(dist(s, s.entity(c1)->points[0], s.entity(c2)->points[0]), 0, kTol);
  EXPECT_DOUBLE_EQ(P(s, fp).x, 1.25);
  EXPECT_DOUBLE_EQ(P(s, fp).y, -2.5);
}

TEST(SketchSolver, PointOnEntityMidpointSymmetric) {
  Sketch s = Sketch::create();
  int l = s.addLine(0, 0, 10, 10);
  int c = circ(s, 30, 0, 5);
  int arc;
  {
    int cc = s.addPoint(0, 30), st = s.addPoint(6, 30), en = s.addPoint(0, 36);
    arc = s.addArc(cc, st, en);
  }
  int p1 = s.addPoint(3, 5), p2 = s.addPoint(36, 1), p3 = s.addPoint(4, 34);
  int m = s.addPoint(4, 6);
  int q1 = s.addPoint(-2, 8), q2 = s.addPoint(9, -1);
  s.addConstraint(CT::PointOnEntity, {p1, l});
  s.addConstraint(CT::PointOnEntity, {p2, c});
  s.addConstraint(CT::PointOnEntity, {p3, arc});
  s.addConstraint(CT::Midpoint, {m, l});
  s.addConstraint(CT::Symmetric, {q1, q2, l});
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_NEAR(lineDist(s, p1, l), 0, kTol);
  EXPECT_NEAR(dist(s, p2, s.entity(c)->points[0]), circleR(s, c), kTol);
  EXPECT_NEAR(dist(s, p3, s.entity(arc)->points[0]), circleR(s, arc), kTol);
  EXPECT_NEAR(P(s, m).x, 0.5 * (P(s, lp(s, l, 0)).x + P(s, lp(s, l, 1)).x), kTol);
  EXPECT_NEAR(P(s, m).y, 0.5 * (P(s, lp(s, l, 0)).y + P(s, lp(s, l, 1)).y), kTol);
  // symmetric: midpoint on the line, segment perpendicular to it
  double mx = 0.5 * (P(s, q1).x + P(s, q2).x), my = 0.5 * (P(s, q1).y + P(s, q2).y);
  int ms = s.addPoint(mx, my);
  EXPECT_NEAR(lineDist(s, ms, l), 0, kTol);
  EXPECT_NEAR((P(s, q2).x - P(s, q1).x) * dirX(s, l) + (P(s, q2).y - P(s, q1).y) * dirY(s, l), 0, 1e-6);
}

TEST(SketchSolver, DimensionalConstraints) {
  Sketch s = Sketch::create();
  int a = s.addPoint(1, 1), b = s.addPoint(8, 3);
  int l = s.addLine(0, -10, 20, -9);
  int l2 = s.addLine(30, 0, 40, 4);
  int c = circ(s, 50, 50, 3);
  int c2 = circ(s, 70, 50, 3);
  int q = s.addPoint(5, -4), dx1 = s.addPoint(-5, -5), dx2 = s.addPoint(1, 2);
  ParamValues pv{{"A", 12.5}, {"Ang", 30}};
  int cD = s.addConstraint(CT::Distance, {a, b}, "A");
  s.addConstraint(CT::DistanceX, {dx1, dx2}, "-7");
  s.addConstraint(CT::DistanceY, {dx1, dx2}, "A/5");
  s.addConstraint(CT::PointLineDistance, {q, l}, "6");
  s.addConstraint(CT::Length, {l}, "25");
  s.addConstraint(CT::Radius, {c}, "4.5");
  s.addConstraint(CT::Diameter, {c2}, "2*A");
  s.addConstraint(CT::Angle, {l, l2}, "Ang");
  auto r = solve(s, pv);
  ASSERT_TRUE(r.ok()) << r.message;
  (void)cD;
  EXPECT_NEAR(dist(s, a, b), 12.5, kTol);
  EXPECT_NEAR(P(s, dx2).x - P(s, dx1).x, -7, kTol);
  EXPECT_NEAR(P(s, dx2).y - P(s, dx1).y, 2.5, kTol);
  EXPECT_NEAR(lineDist(s, q, l), 6, kTol);  // point stays on the left side (above)
  EXPECT_NEAR(lineLen(s, l), 25, kTol);
  EXPECT_NEAR(circleR(s, c), 4.5, kTol);
  EXPECT_NEAR(circleR(s, c2), 12.5, kTol);
  double ang = std::atan2(dirX(s, l) * dirY(s, l2) - dirY(s, l) * dirX(s, l2),
                          dirX(s, l) * dirX(s, l2) + dirY(s, l) * dirY(s, l2)) * 180 / M_PI;
  EXPECT_NEAR(ang, 30, 1e-7);
}

TEST(SketchSolver, AngleLargeChangeAndNegative) {
  Sketch s = Sketch::create();
  int l1 = s.addLine(0, 0, 10, 0), l2 = s.addLine(0, 0, 8, 6);
  s.addConstraint(CT::Coincident, {lp(s, l1, 0), 0});
  s.addConstraint(CT::Coincident, {lp(s, l2, 0), 0});
  s.addConstraint(CT::Horizontal, {l1});
  s.addConstraint(CT::Length, {l1}, "10");
  s.addConstraint(CT::Length, {l2}, "10");
  s.addConstraint(CT::Angle, {l1, l2}, "a");
  for (double a : {150.0, -60.0, 90.0}) {
    auto r = solve(s, {{"a", a}});
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.status, SolveStatus::Ok);
    double ang = std::atan2(dirY(s, l2), dirX(s, l2)) * 180 / M_PI;
    EXPECT_NEAR(ang, a, 1e-7);
    EXPECT_GT(dirX(s, l1), 0);
  }
}

// ------------------------------------------------------------------ parametric rectangle
TEST(SketchSolver, RectangleBigParameterChangesKeepOrientation) {
  Sketch s = Sketch::create();
  auto L = paramRect(s, "W", "D", 80, 50);
  for (double W : {80.0, 200.0, 30.0, 150.0, 31.0}) {
    auto r = solve(s, {{"W", W}, {"D", 50}});
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.status, SolveStatus::Ok);
    EXPECT_EQ(r.dof, 0);
    int p1 = lp(s, L[0], 0), p3 = lp(s, L[2], 0);
    EXPECT_NEAR(P(s, p1).x, -W / 2, kTol);
    EXPECT_NEAR(P(s, p1).y, -25, kTol);
    EXPECT_NEAR(P(s, p3).x, W / 2, kTol);
    EXPECT_NEAR(P(s, p3).y, 25, kTol);
    EXPECT_GT(dirX(s, L[0]), 0);  // bottom still left->right
    EXPECT_GT(dirY(s, L[1]), 0);  // right still upward
    EXPECT_LT(dirX(s, L[2]), 0);
    EXPECT_LT(dirY(s, L[3]), 0);
  }
}

TEST(SketchSolver, UnderConstrainedRectangleMovesMinimally) {
  Sketch s = Sketch::create();
  auto L = s.addRectangle(0, 0, 40, 20);
  int cl = s.addConstraint(CT::Length, {L[0]}, "60");
  (void)cl;
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.status, SolveStatus::UnderConstrained);
  EXPECT_EQ(r.dof, 3);
  EXPECT_EQ(r.message, "3 serbestlik derecesi kaldı");
  EXPECT_NEAR(lineLen(s, L[0]), 60, kTol);
  // symmetric minimum-norm growth: both bottom corners move by 10
  EXPECT_NEAR(P(s, lp(s, L[0], 0)).x, -10, 1e-6);
  EXPECT_NEAR(P(s, lp(s, L[0], 1)).x, 50, 1e-6);
  EXPECT_NEAR(P(s, lp(s, L[0], 0)).y, 0, 1e-6);
  EXPECT_EQ(degreesOfFreedom(s, {}), 3);
}

// ------------------------------------------------------------------ bracket sample
TEST(SketchSolver, BracketSketchesFullyConstrained) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  ParamValues pv = d.evaluateParams();
  Feature* f = d.feature("Sketch1");
  ASSERT_TRUE(f && f->sketch);
  Sketch sk = *f->sketch;
  Sketch before = sk;
  EXPECT_EQ(degreesOfFreedom(sk, pv), 0);
  EXPECT_EQ(sk.points.size(), before.points.size());
  for (size_t i = 0; i < sk.points.size(); ++i) EXPECT_EQ(sk.points[i].x, before.points[i].x);
  auto r = solve(sk, pv);
  EXPECT_EQ(r.status, SolveStatus::Ok) << r.message;
  EXPECT_EQ(r.dof, 0);
  EXPECT_EQ(r.message, "Tam tanımlı");
  // W changes 80 -> 200 -> 30
  for (double W : {200.0, 30.0}) {
    ParamValues p2 = d.evaluateParams({{"W", formatNumber(W)}});
    auto r2 = solve(sk, p2);
    ASSERT_TRUE(r2.ok()) << r2.message;
    EXPECT_NEAR(sk.point(2)->x - sk.point(1)->x, W, kTol);
    EXPECT_NEAR(sk.point(3)->x, W / 2, kTol);
  }
  for (const char* id : {"Sketch2", "Sketch3", "Sketch4"}) {
    Sketch s2 = *d.feature(id)->sketch;
    auto r3 = solve(s2, pv);
    EXPECT_EQ(r3.status, SolveStatus::Ok) << id << ": " << r3.message;
    EXPECT_EQ(r3.dof, 0) << id;
  }
}

// ------------------------------------------------------------------ redundancy / conflicts / failures
TEST(SketchSolver, RedundantConstraintIsOverConstrained) {
  Sketch s = Sketch::create();
  auto L = paramRect(s, "40", "20", 40, 20);
  int par = s.addConstraint(CT::Parallel, {L[0], L[2]});
  auto r = solve(s, {});
  EXPECT_TRUE(r.converged);
  EXPECT_EQ(r.status, SolveStatus::OverConstrained);
  ASSERT_EQ(r.conflicting.size(), 1u);
  EXPECT_EQ(r.conflicting[0], par);
  EXPECT_EQ(r.dof, 0);
}

TEST(SketchSolver, ConflictingConstraintsDetected) {
  Sketch s = Sketch::create();
  auto L = paramRect(s, "40", "20", 40, 20);
  Sketch before = s;
  int bad = s.addConstraint(CT::DistanceX, {lp(s, L[0], 0), lp(s, L[0], 1)}, "55");
  auto r = solve(s, {});
  EXPECT_FALSE(r.ok());
  EXPECT_EQ(r.status, SolveStatus::Inconsistent);
  EXPECT_NE(std::find(r.conflicting.begin(), r.conflicting.end(), bad), r.conflicting.end());
  // the Length(bottom)=40 constraint is part of the conflict too
  int lenId = s.constraints[6].id;
  ASSERT_EQ(s.constraints[6].type, CT::Length);
  EXPECT_NE(std::find(r.conflicting.begin(), r.conflicting.end(), lenId), r.conflicting.end());
  EXPECT_NE(r.message.find("Çelişen kısıtlar"), std::string::npos) << r.message;
  // geometry untouched
  for (size_t i = 0; i < before.points.size(); ++i) {
    EXPECT_EQ(s.points[i].x, before.points[i].x);
    EXPECT_EQ(s.points[i].y, before.points[i].y);
  }
}

TEST(SketchSolver, FailedOnBadExpressionOrRefs) {
  Sketch s = Sketch::create();
  int l = s.addLine(0, 0, 10, 0);
  s.addConstraint(CT::Length, {l}, "Q*2");
  auto r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::Failed);
  EXPECT_FALSE(r.message.empty());
  EXPECT_EQ(degreesOfFreedom(s, {}), 3);  // falls back to measured values
  Sketch s2 = Sketch::create();
  int l2 = s2.addLine(0, 0, 10, 0);
  s2.addConstraint(CT::Horizontal, {l2 + 100});
  EXPECT_EQ(solve(s2, {}).status, SolveStatus::Failed);
}

TEST(SketchSolver, Deterministic) {
  Sketch s = Sketch::create();
  auto L = s.addRectangle(0, 0, 40, 20);
  s.addConstraint(CT::Length, {L[0]}, "70");
  s.addConstraint(CT::Angle, {L[0], L[3]}, "-80");
  Sketch a = s, b = s;
  solve(a, {});
  solve(b, {});
  for (size_t i = 0; i < a.points.size(); ++i) {
    EXPECT_EQ(a.points[i].x, b.points[i].x);
    EXPECT_EQ(a.points[i].y, b.points[i].y);
  }
}

// ------------------------------------------------------------------ profiles
TEST(SketchProfile, RectangleWithTwoHoles) {
  Sketch s = Sketch::create();
  auto L = s.addRectangle(-50, -25, 50, 25);
  int c1 = circ(s, -20, 0, 5), c2 = circ(s, 20, 0, 8);
  gp_Ax3 fr = standardFrame(SketchPlaneKind::XY, 3.0);
  Profile p = buildProfile(s, fr);
  ASSERT_TRUE(p.ok) << p.message;
  ASSERT_EQ(p.faces.size(), 1u);
  EXPECT_EQ(wireCount(p.faces[0]), 3);
  EXPECT_NEAR(faceArea(p.faces[0]), 100 * 50 - M_PI * (25 + 64), 1e-6);
  EXPECT_TRUE(BRepCheck_Analyzer(p.faces[0]).IsValid());
  EXPECT_TRUE(faceNormal(p.faces[0]).IsEqual(fr.Direction(), 1e-12));
  ASSERT_EQ(p.edges.size(), 6u);
  std::set<int> ids;
  for (auto& e : p.edges) ids.insert(e.entityId);
  EXPECT_EQ(ids, (std::set<int>{L[0], L[1], L[2], L[3], c1, c2}));
  // edges are the very edges of the face -> prism Generated() works
  BRepPrimAPI_MakePrism prism(p.faces[0], gp_Vec(0, 0, 10));
  ASSERT_TRUE(prism.IsDone());
  for (auto& e : p.edges) EXPECT_FALSE(prism.Generated(e.edge).IsEmpty()) << e.entityId;
  // line edge geometry lies on the plane z=3
  for (auto& e : p.edges) {
    for (TopExp_Explorer ex(e.edge, TopAbs_VERTEX); ex.More(); ex.Next())
      EXPECT_NEAR(BRep_Tool::Pnt(TopoDS::Vertex(ex.Current())).Z(), 3.0, 1e-9);
  }
}

TEST(SketchProfile, NestedIslandsAndOtherPlanes) {
  Sketch s = Sketch::create();
  s.addRectangle(-50, -50, 50, 50);   // outer
  s.addRectangle(-30, -30, 30, 30);   // hole
  circ(s, 0, 0, 10);              // island inside the hole
  circ(s, 0, 0, 4);               // hole in the island
  circ(s, 80, 0, 5);              // separate face
  for (auto kind : {SketchPlaneKind::XY, SketchPlaneKind::XZ, SketchPlaneKind::YZ}) {
    gp_Ax3 fr = standardFrame(kind, 2.0);
    EXPECT_TRUE(fr.XDirection().Crossed(fr.YDirection()).IsEqual(fr.Direction(), 1e-12));
    Profile p = buildProfile(s, fr);
    ASSERT_TRUE(p.ok) << p.message;
    ASSERT_EQ(p.faces.size(), 3u);
    double total = 0;
    for (auto& f : p.faces) {
      EXPECT_TRUE(faceNormal(f).IsEqual(fr.Direction(), 1e-12));
      EXPECT_TRUE(BRepCheck_Analyzer(f).IsValid());
      total += faceArea(f);
    }
    double expect = (10000 - 3600) + M_PI * (100 - 16) + M_PI * 25;
    EXPECT_NEAR(total, expect, 1e-6);
  }
  gp_Ax3 xz = standardFrame(SketchPlaneKind::XZ, 2.0);
  EXPECT_TRUE(xz.Direction().IsEqual(gp_Dir(0, -1, 0), 1e-12));
  EXPECT_TRUE(xz.YDirection().IsEqual(gp_Dir(0, 0, 1), 1e-12));
  EXPECT_NEAR(xz.Location().Y(), -2.0, 1e-12);
}

TEST(SketchProfile, OpenContourAndIntersections) {
  Sketch s = Sketch::create();
  int a = s.addPoint(0, 0), b = s.addPoint(10, 0), c = s.addPoint(10, 10), d = s.addPoint(0, 10);
  s.addLine(a, b); s.addLine(b, c); s.addLine(c, d);
  circ(s, 50, 0, 3);  // valid face still returned
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_FALSE(p.ok);
  EXPECT_NE(p.message.find("Açık kontur"), std::string::npos) << p.message;
  EXPECT_NE(p.message.find(std::to_string(a)), std::string::npos);
  EXPECT_EQ(p.faces.size(), 1u);

  Sketch bow = Sketch::create();
  int p1 = bow.addPoint(0, 0), p2 = bow.addPoint(10, 10), p3 = bow.addPoint(10, 0), p4 = bow.addPoint(0, 10);
  bow.addLine(p1, p2); bow.addLine(p2, p3); bow.addLine(p3, p4); bow.addLine(p4, p1);
  Profile q = buildProfile(bow, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_FALSE(q.ok);
  EXPECT_NE(q.message.find("Kesişen"), std::string::npos) << q.message;
  EXPECT_TRUE(q.faces.empty());

  // dangling edge on a rectangle
  Sketch dg = Sketch::create();
  auto L = dg.addRectangle(0, 0, 10, 10);
  int tip = dg.addPoint(20, 10);
  dg.addLine(dg.entity(L[1])->points[1], tip);
  Profile r = buildProfile(dg, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.message.find(std::to_string(tip)), std::string::npos) << r.message;
}

TEST(SketchProfile, ConstructionIgnoredAndCoordinateConnectivity) {
  Sketch s = Sketch::create();
  // triangle with separate (unshared) but coincident endpoints, plus a construction line
  int l1 = s.addLine(0, 0, 10, 0), l2 = s.addLine(10, 0, 0, 8), l3 = s.addLine(0, 8, 0, 0);
  s.addConstraint(CT::Coincident, {lp(s, l1, 1), lp(s, l2, 0)});
  s.addConstraint(CT::Coincident, {lp(s, l2, 1), lp(s, l3, 0)});
  s.addConstraint(CT::Coincident, {lp(s, l3, 1), lp(s, l1, 0)});
  s.addLine(-5, -5, 30, 30, true);
  s.point(lp(s, l2, 0))->x += 1e-3;  // unsolved -> solver closes it
  ASSERT_TRUE(solve(s, {}).converged);
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  ASSERT_TRUE(p.ok) << p.message;
  ASSERT_EQ(p.faces.size(), 1u);
  EXPECT_EQ(p.edges.size(), 3u);
  EXPECT_TRUE(BRepCheck_Analyzer(p.faces[0]).IsValid());
}

TEST(SketchProfile, ArcSlot) {
  // slot: two horizontal lines + two tangent semicircular arcs, parameter driven
  Sketch s = Sketch::create();
  int a = s.addPoint(-20, -5), b = s.addPoint(20, -5), c = s.addPoint(20, 5), d = s.addPoint(-20, 5);
  int cr = s.addPoint(20, 0), cl = s.addPoint(-20, 0);
  int bot = s.addLine(a, b), top = s.addLine(c, d);
  int arcR = s.addArc(cr, b, c), arcL = s.addArc(cl, d, a);
  s.addConstraint(CT::Tangent, {bot, arcR});
  s.addConstraint(CT::Tangent, {top, arcR});
  s.addConstraint(CT::Tangent, {top, arcL});
  s.addConstraint(CT::Tangent, {bot, arcL});
  s.addConstraint(CT::Horizontal, {bot});
  s.addConstraint(CT::Radius, {arcR}, "Rs");
  s.addConstraint(CT::Distance, {cl, cr}, "Ls");
  s.addConstraint(CT::HorizontalPts, {cl, cr});
  s.addConstraint(CT::Midpoint, {0, s.addLine(cl, cr, true)});
  auto r = solve(s, {{"Rs", 6}, {"Ls", 50}});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.dof, 0) << r.message;
  EXPECT_NEAR(circleR(s, arcL), 6, kTol);
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  ASSERT_TRUE(p.ok) << p.message;
  ASSERT_EQ(p.faces.size(), 1u);
  EXPECT_NEAR(faceArea(p.faces[0]), 12 * 50 + M_PI * 36, 1e-6);
  EXPECT_TRUE(BRepCheck_Analyzer(p.faces[0]).IsValid());
  EXPECT_EQ(p.edges.size(), 4u);
  std::set<int> ids;
  for (auto& e : p.edges) ids.insert(e.entityId);
  EXPECT_EQ(ids, (std::set<int>{bot, top, arcR, arcL}));
  BRepPrimAPI_MakePrism prism(p.faces[0], gp_Vec(0, 0, 5));
  ASSERT_TRUE(prism.IsDone());
  for (auto& e : p.edges) EXPECT_FALSE(prism.Generated(e.edge).IsEmpty());
}

TEST(SketchProfile, FreePointsOnlySketch) {
  Document d;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad"));
  Sketch s = *d.feature("Sketch2")->sketch;
  ASSERT_TRUE(solve(s, d.evaluateParams()).ok());
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 8));
  EXPECT_TRUE(p.ok) << p.message;
  EXPECT_TRUE(p.faces.empty());
  ASSERT_EQ(p.freePoints.size(), 4u);
  ASSERT_EQ(p.freePoints2d.size(), 4u);
  // ordered by (u, then v)
  EXPECT_NEAR(p.freePoints2d[0].second.X(), -30, 1e-9);
  EXPECT_NEAR(p.freePoints2d[0].second.Y(), -15, 1e-9);
  EXPECT_NEAR(p.freePoints2d[1].second.Y(), 15, 1e-9);
  EXPECT_NEAR(p.freePoints2d[3].second.X(), 30, 1e-9);
  EXPECT_NEAR(p.freePoints[0].Z(), 8, 1e-12);
  // Sketch4: free point coincident with the origin
  Sketch s4 = *d.feature("Sketch4")->sketch;
  Profile p4 = buildProfile(s4, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_TRUE(p4.ok);
  ASSERT_EQ(p4.freePoints2d.size(), 1u);
  EXPECT_EQ(p4.freePoints2d[0].first, 1);
}

// ------------------------------------------------------------------ performance
TEST(SketchSolver, Performance60Entities) {
  Sketch s = Sketch::create();
  ParamValues pv;
  // 12 dimensioned rectangles (48 lines) + 12 circles = 60 entities
  for (int k = 0; k < 12; ++k) {
    double x = (k % 4) * 60.0, y = (k / 4) * 60.0;
    auto L = s.addRectangle(x, y, x + 40, y + 30);
    int p1 = s.entity(L[0])->points[0];
    std::string w = "W" + std::to_string(k);
    pv[w] = 45 + k;
    s.addConstraint(CT::DistanceX, {0, p1}, std::to_string(x));
    s.addConstraint(CT::DistanceY, {0, p1}, std::to_string(y));
    s.addConstraint(CT::Length, {L[0]}, w);
    s.addConstraint(CT::Length, {L[1]}, w + "*0.6");
    int c = circ(s, x + 20, y + 15, 5);
    int cc = s.entity(c)->points[0];
    s.addConstraint(CT::Diameter, {c}, w + "/4");
    s.addConstraint(CT::DistanceX, {p1, cc}, w + "/2");
    s.addConstraint(CT::DistanceY, {p1, cc}, w + "*0.3");
  }
  // perturb all points
  for (auto& p : s.points)
    if (p.id) { p.x += 0.37 * std::sin(p.id); p.y += 0.29 * std::cos(p.id); }
  ASSERT_EQ(s.entities.size(), 60u);
  auto t0 = std::chrono::steady_clock::now();
  auto r = solve(s, pv);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.status, SolveStatus::Ok);
  EXPECT_LT(ms, 50.0 * kPerfScale) << "solve took " << ms << " ms";
  std::printf("60-entity solve: %.2f ms, %d iterations\n", ms, r.iterations);
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_TRUE(p.ok) << p.message;
  EXPECT_EQ(p.faces.size(), 12u);
}

// ------------------------------------------------------------------ hardening
namespace {
struct RoundRect { std::vector<int> lines, arcs; };
// Rounded rectangle centred at origin: 4 lines + 4 tangent corner arcs, fully constrained by W,H,R.
RoundRect roundRect(Sketch& s, double w, double h, double r) {
  double x0 = -w / 2, x1 = w / 2, y0 = -h / 2, y1 = h / 2;
  int b0 = s.addPoint(x0 + r, y0), b1 = s.addPoint(x1 - r, y0);
  int r0 = s.addPoint(x1, y0 + r), r1 = s.addPoint(x1, y1 - r);
  int t0 = s.addPoint(x1 - r, y1), t1 = s.addPoint(x0 + r, y1);
  int l0 = s.addPoint(x0, y1 - r), l1 = s.addPoint(x0, y0 + r);
  int cBR = s.addPoint(x1 - r, y0 + r), cTR = s.addPoint(x1 - r, y1 - r);
  int cTL = s.addPoint(x0 + r, y1 - r), cBL = s.addPoint(x0 + r, y0 + r);
  RoundRect rr;
  rr.lines = {s.addLine(b0, b1), s.addLine(r0, r1), s.addLine(t0, t1), s.addLine(l0, l1)};
  rr.arcs = {s.addArc(cBR, b1, r0), s.addArc(cTR, r1, t0), s.addArc(cTL, t1, l0), s.addArc(cBL, l1, b0)};
  s.addConstraint(CT::Horizontal, {rr.lines[0]});
  s.addConstraint(CT::Horizontal, {rr.lines[2]});
  s.addConstraint(CT::Vertical, {rr.lines[1]});
  s.addConstraint(CT::Vertical, {rr.lines[3]});
  for (int k = 0; k < 4; ++k) {
    s.addConstraint(CT::Tangent, {rr.lines[k], rr.arcs[k]});
    s.addConstraint(CT::Tangent, {rr.arcs[k], rr.lines[(k + 1) % 4]});
  }
  s.addConstraint(CT::Radius, {rr.arcs[0]}, "R");
  for (int k = 1; k < 4; ++k) s.addConstraint(CT::Equal, {rr.arcs[0], rr.arcs[k]});
  s.addConstraint(CT::DistanceX, {l0, r0}, "W");
  s.addConstraint(CT::DistanceY, {b0, t0}, "H");
  s.addConstraint(CT::DistanceX, {0, l0}, "-W/2");
  s.addConstraint(CT::DistanceY, {0, b0}, "-H/2");
  return rr;
}
} // namespace

TEST(SketchSolver, RoundedRectangleFullyConstrainedBigChanges) {
  Sketch s = Sketch::create();
  RoundRect rr = roundRect(s, 80, 50, 5);
  struct Case { double W, H, R; };
  for (Case c : {Case{80, 50, 5}, Case{200, 50, 5}, Case{30, 50, 12}, Case{150, 120, 20}, Case{40, 25, 2}}) {
    ParamValues pv{{"W", c.W}, {"H", c.H}, {"R", c.R}};
    auto r = solve(s, pv);
    ASSERT_TRUE(r.ok()) << "W=" << c.W << " " << r.message;
    EXPECT_EQ(r.status, SolveStatus::Ok) << r.message;
    EXPECT_EQ(r.dof, 0);
    for (int a : rr.arcs) EXPECT_NEAR(circleR(s, a), c.R, kTol);
    EXPECT_NEAR(lineLen(s, rr.lines[0]), c.W - 2 * c.R, 1e-6);
    EXPECT_GT(dirX(s, rr.lines[0]), 0);
    EXPECT_LT(dirX(s, rr.lines[2]), 0);
    Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
    ASSERT_TRUE(p.ok) << p.message;
    ASSERT_EQ(p.faces.size(), 1u);
    EXPECT_NEAR(faceArea(p.faces[0]), c.W * c.H - (4 - M_PI) * c.R * c.R, 1e-5);
    EXPECT_EQ(p.edges.size(), 8u);
    EXPECT_TRUE(BRepCheck_Analyzer(p.faces[0]).IsValid());
  }
}

TEST(SketchSolver, RoughlyDrawnSketchConverges) {
  // constraints added to a sloppy drawing: far from satisfied initially
  Sketch s = Sketch::create();
  RoundRect rr = roundRect(s, 80, 50, 5);
  for (auto& p : s.points)
    if (p.id) { p.x += 3.0 * std::sin(1.7 * p.id); p.y += 3.0 * std::cos(2.3 * p.id); }
  auto r = solve(s, {{"W", 90}, {"H", 60}, {"R", 8}});
  ASSERT_TRUE(r.ok()) << r.message;
  EXPECT_EQ(r.dof, 0);
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  ASSERT_TRUE(p.ok) << p.message;
  EXPECT_NEAR(faceArea(p.faces[0]), 90 * 60 - (4 - M_PI) * 64, 1e-5);
}

TEST(SketchSolver, ConflictHorizontalVerticalSameLine) {
  Sketch s = Sketch::create();
  int l = s.addLine(0, 0, 10, 1);
  int h = s.addConstraint(CT::Horizontal, {l});
  int v = s.addConstraint(CT::Vertical, {l});
  int len = s.addConstraint(CT::Length, {l}, "10");
  auto r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::Inconsistent) << r.message;
  std::set<int> got(r.conflicting.begin(), r.conflicting.end());
  EXPECT_EQ(got, (std::set<int>{h, v, len})) << r.message;
}

TEST(SketchSolver, ConflictTwoFixedDistances) {
  Sketch s = Sketch::create();
  int a = s.addPoint(0, 0), b = s.addPoint(10, 0);
  s.addConstraint(CT::Fixed, {a});
  int d1 = s.addConstraint(CT::Distance, {a, b}, "10");
  int d2 = s.addConstraint(CT::Distance, {a, b}, "12");
  auto r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::Inconsistent);
  std::set<int> got(r.conflicting.begin(), r.conflicting.end());
  EXPECT_EQ(got, (std::set<int>{d1, d2})) << r.message;
  EXPECT_EQ(r.message, "Çelişen kısıtlar: " + std::to_string(d1) + ", " + std::to_string(d2));
}

TEST(SketchSolver, EmptyAndPointOnlySketches) {
  Sketch s = Sketch::create();
  auto r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::Ok);
  EXPECT_EQ(r.dof, 0);
  int p = s.addPoint(3, 4);
  r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::UnderConstrained);
  EXPECT_EQ(r.dof, 2);
  s.addConstraint(CT::DistanceX, {0, p}, "5");
  s.addConstraint(CT::DistanceY, {0, p}, "-6");
  r = solve(s, {});
  EXPECT_EQ(r.status, SolveStatus::Ok);
  EXPECT_NEAR(s.point(p)->x, 5, kTol);
  EXPECT_NEAR(s.point(p)->y, -6, kTol);
}

TEST(SketchProfile, LeftHandedFrameNormal) {
  Sketch s = Sketch::create();
  s.addRectangle(0, 0, 10, 5);
  // left-handed: X=+X, Y=+Y, N=-Z
  gp_Ax3 fr(gp_Pnt(0, 0, 0), gp_Dir(0, 0, -1), gp_Dir(1, 0, 0));
  fr.YReverse();
  ASSERT_FALSE(fr.Direct());
  Profile p = buildProfile(s, fr);
  ASSERT_TRUE(p.ok) << p.message;
  ASSERT_EQ(p.faces.size(), 1u);
  EXPECT_TRUE(faceNormal(p.faces[0]).IsEqual(gp_Dir(0, 0, -1), 1e-12));
  EXPECT_TRUE(BRepCheck_Analyzer(p.faces[0]).IsValid());
  EXPECT_NEAR(faceArea(p.faces[0]), 50, 1e-9);
}

TEST(SketchSolver, ArcArcEndpointTangencySCurve) {
  Sketch s = Sketch::create();
  // arc1 CCW around (0,0) from (10,0)... ends at joint near (0,10); arc2 around (0,20.5) from (0,30) to joint
  int c1 = s.addPoint(0, 0), a1s = s.addPoint(10, 0), j = s.addPoint(0.3, 10.2);
  int c2 = s.addPoint(0.2, 20.5), a2s = s.addPoint(-0.5, 30.4);
  int arc1 = s.addArc(c1, a1s, j);
  int arc2 = s.addArc(c2, a2s, j);  // CCW from top (0,30) around to the joint (0,10)
  s.addConstraint(CT::Fixed, {c1});
  s.addConstraint(CT::Tangent, {arc1, arc2});
  s.addConstraint(CT::Radius, {arc1}, "10");
  s.addConstraint(CT::Radius, {arc2}, "10");
  auto r = solve(s, {});
  ASSERT_TRUE(r.ok()) << r.message;
  double ax = P(s, j).x - P(s, c1).x, ay = P(s, j).y - P(s, c1).y;
  double bx = P(s, j).x - P(s, c2).x, by = P(s, j).y - P(s, c2).y;
  EXPECT_NEAR((ax * by - ay * bx) / 100.0, 0, 1e-9);   // centers and joint collinear
  EXPECT_NEAR(dist(s, c1, c2), 20, 1e-7);              // external (S-curve) branch kept
}

TEST(SketchProfile, SharedEdgeRegionsReported) {
  Sketch s = Sketch::create();
  auto L = s.addRectangle(0, 0, 20, 10);
  s.addLine(s.entity(L[0])->points[0], s.entity(L[1])->points[1]);  // diagonal splits the rectangle
  Profile p = buildProfile(s, standardFrame(SketchPlaneKind::XY, 0));
  EXPECT_FALSE(p.ok);
  EXPECT_NE(p.message.find("Dallanan"), std::string::npos) << p.message;
}
