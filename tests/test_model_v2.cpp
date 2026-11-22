// Tests for src/model v2 (Agent B2): datum planes, OnPlane sketches, multi-body (BODIES contract), Combine.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/ModelNaming.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include <cmath>
#include <set>

using namespace pc;
using namespace pc::model;

namespace {

const double kTol = 1e-9;

std::string statusDump(const ModelResult& r) {
  std::string s;
  for (auto& st : r.status) s += st.featureId + (st.ok ? " ok " : " FAIL ") + st.message + "\n";
  for (auto& b : r.bodies) {
    s += "  body " + b.name + " (" + b.createdBy + "):";
    for (auto& f : b.faces) s += " " + f;
    s += "\n";
  }
  return s;
}

const FeatureStatus* statusOf(const ModelResult& r, const std::string& id) {
  for (auto& s : r.status)
    if (s.featureId == id) return &s;
  return nullptr;
}

const FeatureInfo* infoOf(const ModelResult& r, const std::string& id) {
  for (auto& s : r.info)
    if (s.featureId == id) return &s;
  return nullptr;
}

std::string msgOf(const ModelResult& r, const std::string& id) {
  auto* s = statusOf(r, id);
  return s ? s->message : std::string("<no status>");
}

bool hasName(const ModelResult& r, const std::string& n) { return r.faces.count(n) > 0; }

std::vector<std::string> bodyNames(const ModelResult& r) {
  std::vector<std::string> v;
  for (auto& b : r.bodies) v.push_back(b.name);
  return v;
}

double vol(const TopoDS_Shape& s) { return massProperties(s).volume; }

double bodyVol(const ModelResult& r, const std::string& n) {
  const BodyInfo* b = r.body(n);
  EXPECT_NE(b, nullptr) << "body " << n;
  return b ? vol(b->shape) : -1;
}

Feature sketchFeature(const std::string& id, SketchPlaneKind k, const std::string& ref = "") {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  Sketch s = Sketch::create();
  s.plane.kind = k;
  if (k == SketchPlaneKind::OnFace) s.plane.faceRef = ref;
  if (k == SketchPlaneKind::OnPlane) s.plane.planeRef = ref;
  f.sketch = s;
  return f;
}

std::vector<int> paramRect(Sketch& s, const std::string& x0, const std::string& y0, const std::string& x1,
                           const std::string& y1, double vx0, double vy0, double vx1, double vy1) {
  auto L = s.addRectangle(vx0, vy0, vx1, vy1);
  int a = s.entity(L[0])->points[0];
  int c = s.entity(L[2])->points[0];
  s.addConstraint(SkConstraintType::DistanceX, {0, a}, x0);
  s.addConstraint(SkConstraintType::DistanceY, {0, a}, y0);
  s.addConstraint(SkConstraintType::DistanceX, {0, c}, x1);
  s.addConstraint(SkConstraintType::DistanceY, {0, c}, y1);
  return L;
}

Feature rectSketch(const std::string& id, SketchPlaneKind k, const std::string& ref, double x0, double y0, double x1,
                   double y1) {
  Feature f = sketchFeature(id, k, ref);
  paramRect(*f.sketch, formatNumber(x0), formatNumber(y0), formatNumber(x1), formatNumber(y1), x0, y0, x1, y1);
  return f;
}

Feature feat(const std::string& id, FeatureType t, std::map<std::string, std::string> props,
             std::vector<std::string> refs = {}) {
  Feature f;
  f.id = id;
  f.type = t;
  f.props = std::move(props);
  f.refs = std::move(refs);
  return f;
}

Feature plane(const std::string& id, std::map<std::string, std::string> props, std::vector<std::string> refs) {
  return feat(id, FeatureType::Plane, std::move(props), std::move(refs));
}

// Plate W x D x T centered at origin, Sketch1 + Extrude1 (line ids: e5 bottom(+X), e6 right(+Y), e7 top(-X), e8 left(-Y)).
Document plateDoc(double W = 80, double D = 50, double T = 10) {
  Document d;
  d.params.add({"W", formatNumber(W)});
  d.params.add({"D", formatNumber(D)});
  d.params.add({"T", formatNumber(T)});
  Feature s1 = sketchFeature("Sketch1", SketchPlaneKind::XY);
  paramRect(*s1.sketch, "-W/2", "-D/2", "W/2", "D/2", -W / 2, -D / 2, W / 2, D / 2);
  d.features.push_back(s1);
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "T"}}));
  return d;
}

// Adds a box (XY rectangle extruded from z0 by h) with the given op/props.
void addBox(Document& d, const std::string& sid, const std::string& eid, double x0, double y0, double x1, double y1,
            double z0, double h, std::map<std::string, std::string> props) {
  Feature s = rectSketch(sid, SketchPlaneKind::XY, "", x0, y0, x1, y1);
  s.sketch->plane.offsetExpr = formatNumber(z0);
  d.features.push_back(s);
  props["sketch"] = sid;
  props["distance"] = formatNumber(h);
  d.features.push_back(feat(eid, FeatureType::Extrude, props));
}

const char* E5TOP = "edge(Extrude1.end|Extrude1.side.e5)";
const char* E6TOP = "edge(Extrude1.end|Extrude1.side.e6)";
const char* E7TOP = "edge(Extrude1.end|Extrude1.side.e7)";

void expectFrame(const gp_Ax3& f, gp_Pnt O, gp_Dir N, gp_Dir U, double tol = kTol, const std::string& tag = "") {
  EXPECT_LT(f.Location().Distance(O), tol) << tag << " origin " << f.Location().X() << "," << f.Location().Y() << ","
                                           << f.Location().Z();
  EXPECT_LT(f.Direction().XYZ().Subtracted(N.XYZ()).Modulus(), tol)
      << tag << " normal " << f.Direction().X() << "," << f.Direction().Y() << "," << f.Direction().Z();
  EXPECT_LT(f.XDirection().XYZ().Subtracted(U.XYZ()).Modulus(), tol)
      << tag << " u " << f.XDirection().X() << "," << f.XDirection().Y() << "," << f.XDirection().Z();
  gp_Dir V = N.Crossed(U);
  EXPECT_LT(f.YDirection().XYZ().Subtracted(V.XYZ()).Modulus(), tol) << tag << " v";
}

// Regenerates plateDoc + one plane, returns the datum frame (asserts success).
ModelResult planeOnPlate(std::map<std::string, std::string> props, std::vector<std::string> refs, Document* out = nullptr) {
  Document d = plateDoc();
  d.features.push_back(plane("Plane1", std::move(props), std::move(refs)));
  if (out) *out = d;
  return regenerate(d);
}

// Every face of r.shape has a unique name; bodies partition the faces.
void expectConsistentBodies(const ModelResult& r, const std::string& tag = "") {
  std::set<std::string> seen;
  int nf = 0;
  for (TopExp_Explorer ex(r.shape, TopAbs_FACE); ex.More(); ex.Next()) {
    ++nf;
    std::string n = r.nameOf(TopoDS::Face(ex.Current()));
    EXPECT_FALSE(n.empty()) << tag;
    seen.insert(n);
  }
  EXPECT_EQ((int)seen.size(), nf) << tag;
  size_t total = 0;
  std::set<std::string> bnames;
  TopTools_IndexedMapOfShape solids;
  TopExp::MapShapes(r.shape, TopAbs_SOLID, solids);
  EXPECT_EQ((size_t)solids.Extent(), r.bodies.size()) << tag;
  for (auto& b : r.bodies) {
    EXPECT_TRUE(bnames.insert(b.name).second) << tag << " duplicate body " << b.name;
    ASSERT_FALSE(b.shape.IsNull());
    EXPECT_EQ(b.shape.ShapeType(), TopAbs_SOLID) << tag;
    EXPECT_TRUE(solids.Contains(b.shape)) << tag << " body solid not in result shape";
    EXPECT_TRUE(std::is_sorted(b.faces.begin(), b.faces.end()));
    total += b.faces.size();
    for (auto& f : b.faces) {
      EXPECT_EQ(r.bodyOfFace(f), b.name) << tag << " " << f;
      EXPECT_TRUE(r.faces.count(f)) << tag << " " << f;
    }
  }
  EXPECT_EQ(total, r.faces.size()) << tag;
  if (r.bodies.size() == 1) EXPECT_EQ(r.shape.ShapeType(), TopAbs_SOLID) << tag;
  if (r.bodies.size() > 1) EXPECT_EQ(r.shape.ShapeType(), TopAbs_COMPOUND) << tag;
}

}  // namespace

// ============================================================ datum planes

TEST(ModelV2Plane, EdgeNormalFrameExact) {
  ModelResult r = planeOnPlate({{"kind", "edgeNormal"}, {"t", "0.25"}}, {E5TOP});
  ASSERT_TRUE(r.ok) << statusDump(r);
  ASSERT_TRUE(r.datumPlanes.count("Plane1"));
  const DatumPlane& dp = r.datumPlanes.at("Plane1");
  EXPECT_EQ(dp.featureId, "Plane1");
  // e5 top edge runs -X..+X at y=-25, z=10: origin at 25% of its length
  expectFrame(dp.frame, gp_Pnt(-20, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  EXPECT_NEAR(dp.displaySize, 0.6 * 80, 1e-6);
  const FeatureInfo* fi = infoOf(r, "Plane1");
  ASSERT_NE(fi, nullptr);
  EXPECT_EQ(fi->type, FeatureType::Plane);
  EXPECT_DOUBLE_EQ(fi->values.at("t"), 0.25);
  EXPECT_DOUBLE_EQ(fi->values.at("offset"), 0);
  expectFrame(fi->sketchFrame, gp_Pnt(-20, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  EXPECT_TRUE(fi->createdFaces.empty());
  // independent check against the edge's own curve parameterisation
  BRepAdaptor_Curve cv(r.edges.at(E5TOP));
  gp_Pnt P;
  gp_Vec D;
  cv.D1(cv.FirstParameter() + 0.25 * (cv.LastParameter() - cv.FirstParameter()), P, D);
  EXPECT_LT(P.Distance(dp.frame.Location()), 1e-9);
  EXPECT_NEAR(gp_Dir(D).Dot(dp.frame.Direction()), 1, 1e-12);
  // default t = 0 -> start of the curve
  ModelResult r0 = planeOnPlate({{"kind", "edgeNormal"}}, {E7TOP});
  ASSERT_TRUE(r0.ok) << statusDump(r0);
  expectFrame(r0.datumPlanes.at("Plane1").frame, gp_Pnt(40, 25, 10), gp_Dir(-1, 0, 0), gp_Dir(0, 1, 0));
  // the body is untouched by a datum plane
  EXPECT_EQ(r.bodies.size(), 1u);
  EXPECT_NEAR(vol(r.shape), 80 * 50 * 10, 1e-6);
}

TEST(ModelV2Plane, FlipRotateOffset) {
  {
    ModelResult r = planeOnPlate({{"t", "0.25"}, {"flip", "1"}}, {E5TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-20, -25, 10), gp_Dir(-1, 0, 0), gp_Dir(0, 1, 0));
    EXPECT_LT(r.datumPlanes.at("Plane1").frame.YDirection().XYZ().Subtracted(gp_XYZ(0, 0, -1)).Modulus(), kTol);
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("flip"), 1);
  }
  {  // offset goes along the FINAL (flipped) normal
    ModelResult r = planeOnPlate({{"t", "0.25"}, {"flip", "1"}, {"offset", "5"}}, {E5TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-25, -25, 10), gp_Dir(-1, 0, 0), gp_Dir(0, 1, 0));
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("offset"), 5);
  }
  {  // rotate u/v by +90 deg about the normal (+X): u Y -> Z, v Z -> -Y
    ModelResult r = planeOnPlate({{"t", "0.25"}, {"rotate", "90"}, {"offset", "-2"}}, {E5TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-22, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 0, 1));
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("rotate"), 90);
  }
  {  // rotate then flip: u kept from the rotation, normal reversed
    ModelResult r = planeOnPlate({{"t", "0.25"}, {"rotate", "90"}, {"flip", "1"}}, {E5TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-20, -25, 10), gp_Dir(-1, 0, 0), gp_Dir(0, 0, 1));
  }
}

TEST(ModelV2Plane, EdgeNormalOnArcs) {
  // bracket fillet arc from (35,-25,8) to (40,-20,8) around (35,-20): midpoint at 45 deg
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  d.features.push_back(plane("PlaneA", {{"kind", "edgeNormal"}, {"t", "0.5"}}, {"edge(Extrude1.end|Fillet1.f0)"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  const double s = std::sqrt(0.5);
  expectFrame(r.datumPlanes.at("PlaneA").frame, gp_Pnt(35 + 5 * s, -20 - 5 * s, 8), gp_Dir(s, s, 0), gp_Dir(s, -s, 0), 1e-7);
  EXPECT_LT(r.datumPlanes.at("PlaneA").frame.YDirection().XYZ().Subtracted(gp_XYZ(0, 0, -1)).Modulus(), 1e-7);

  // full circle (boss top rim): t=0 and t=0.5 are diametrically opposite, tangent perpendicular to radius
  for (double t : {0.0, 0.5, 0.3}) {
    Document d2 = d;
    d2.features.push_back(plane("PlaneC", {{"t", formatNumber(t)}}, {"edge(Extrude2.end|Extrude2.side.e1)"}));
    ModelResult rc = regenerate(d2);
    ASSERT_TRUE(rc.ok) << statusDump(rc);
    const gp_Ax3& f = rc.datumPlanes.at("PlaneC").frame;
    gp_Vec radial(gp_Pnt(0, 0, 28), f.Location());
    EXPECT_NEAR(radial.Magnitude(), 12, 1e-7) << t;
    EXPECT_NEAR(radial.Dot(gp_Vec(f.Direction())), 0, 1e-7) << t;
    EXPECT_NEAR(f.Direction().Z(), 0, 1e-9) << t;
    BRepAdaptor_Curve cv(rc.edges.at("edge(Extrude2.end|Extrude2.side.e1)"));
    gp_Pnt P = cv.Value(cv.FirstParameter() + t * (cv.LastParameter() - cv.FirstParameter()));
    EXPECT_LT(P.Distance(f.Location()), 1e-7) << t;
  }
}

TEST(ModelV2Plane, EdgeAngleFrames) {
  {  // default reference face = Extrude1.end (sorts before Extrude1.side.e6): angle 0 -> top plane
    ModelResult r = planeOnPlate({{"kind", "edgeAngle"}}, {E6TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(40, 0, 10), gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("angle"), 0);
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("t"), 0.5);
  }
  {  // +90 about the edge direction (+Y): +Z -> +X
    ModelResult r = planeOnPlate({{"kind", "edgeAngle"}, {"angle", "90"}}, {E6TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(40, 0, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  }
  {  // 30 deg, t = 0 (start of the edge curve)
    ModelResult r = planeOnPlate({{"kind", "edgeAngle"}, {"angle", "30"}, {"t", "0"}}, {E6TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(40, -25, 10), gp_Dir(0.5, 0, std::sqrt(3.0) / 2),
                gp_Dir(0, 1, 0), 1e-12);
  }
  {  // explicit reference face (side, +X): angle 90 -> -Z; plane still contains the edge
    ModelResult r = planeOnPlate({{"kind", "edgeAngle"}, {"angle", "90"}}, {E6TOP, "Extrude1.side.e6"});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(40, 0, 10), gp_Dir(0, 0, -1), gp_Dir(0, 1, 0));
  }
  {  // offset + flip applied after the angle
    ModelResult r = planeOnPlate({{"kind", "edgeAngle"}, {"angle", "90"}, {"flip", "1"}, {"offset", "3"}}, {E6TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(37, 0, 10), gp_Dir(-1, 0, 0), gp_Dir(0, 1, 0));
  }
}

TEST(ModelV2Plane, TwoEdgesAndFaceOffset) {
  {  // adjacent edges e5 (+X) and e6: normal = d0 x (m1 - m0) = +Z
    ModelResult r = planeOnPlate({{"kind", "twoEdges"}}, {E5TOP, E6TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, -25, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  }
  {  // parallel edges e5 / e7 (top face)
    ModelResult r = planeOnPlate({{"kind", "twoEdges"}}, {E5TOP, E7TOP});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, -25, 10), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  }
  {  // vertical side edge and bottom edge: plane x = 40 (side face plane), normal = (0,0,1)x(0,25,-5)
    ModelResult r = planeOnPlate({{"kind", "twoEdges"}},
                                 {"edge(Extrude1.side.e5|Extrude1.side.e6)", "edge(Extrude1.side.e6|Extrude1.start)"});
    ASSERT_TRUE(r.ok) << statusDump(r);
    const gp_Ax3& f = r.datumPlanes.at("Plane1").frame;
    EXPECT_NEAR(std::abs(f.Direction().X()), 1, 1e-12);
    EXPECT_NEAR(f.Location().X(), 40, 1e-9);
    EXPECT_NEAR(std::abs(f.XDirection().Z()), 1, 1e-12);
  }
  {  // faceOffset = OnFace frame + offset
    ModelResult r = planeOnPlate({{"kind", "faceOffset"}, {"offset", "5"}}, {"Extrude1.end"});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, 0, 15), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
    EXPECT_DOUBLE_EQ(infoOf(r, "Plane1")->values.at("offset"), 5);
  }
  {  // side face y=-25 (outward -Y): origin = projection of the world origin, u = +X, v = +Z
    ModelResult r = planeOnPlate({{"kind", "faceOffset"}}, {"Extrude1.side.e5"});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, -25, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
  }
  {  // faceOffset with rotate 90: u = +Y
    ModelResult r = planeOnPlate({{"kind", "faceOffset"}, {"rotate", "90"}}, {"Extrude1.end"});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, 0, 10), gp_Dir(0, 0, 1), gp_Dir(0, 1, 0));
  }
}

TEST(ModelV2Plane, ErrorMessages) {
  auto expectErr = [](std::map<std::string, std::string> props, std::vector<std::string> refs, const std::string& sub) {
    ModelResult r = planeOnPlate(props, refs);
    EXPECT_FALSE(r.ok) << sub;
    EXPECT_FALSE(statusOf(r, "Plane1")->ok) << sub;
    EXPECT_NE(msgOf(r, "Plane1").find(sub), std::string::npos) << msgOf(r, "Plane1");
    EXPECT_EQ(msgOf(r, "Plane1").rfind("Plane1: ", 0), 0u) << msgOf(r, "Plane1");
    EXPECT_FALSE(r.datumPlanes.count("Plane1"));
    EXPECT_FALSE(r.shape.IsNull());  // the body survives
  };
  expectErr({}, {"edge(Nope|Nada)"}, "kenar bulunamadı 'edge(Nope|Nada)'");
  expectErr({}, {}, "referans eksik");
  expectErr({{"t", "2"}}, {E5TOP}, "t 0 ile 1 arasında olmalı");
  expectErr({{"kind", "sideways"}}, {E5TOP}, "geçersiz düzlem türü 'sideways'");
  expectErr({{"kind", "twoEdges"}}, {E5TOP}, "iki kenar referansı gerekli");
  expectErr({{"kind", "twoEdges"}}, {E5TOP, E5TOP}, "aynı doğru");
  expectErr({{"kind", "twoEdges"}}, {E5TOP, "edge(Extrude1.side.e6|Extrude1.start)"}, "aynı düzlemde değil");
  expectErr({{"kind", "faceOffset"}}, {"Extrude9.end"}, "yüz bulunamadı 'Extrude9.end'");
  expectErr({{"kind", "edgeAngle"}}, {E5TOP, "Extrude1.side.e6"}, "referans yüz normali kenara paralel");
  expectErr({{"kind", "edgeAngle"}, {"angle", "bogus+"}}, {E5TOP}, "'angle' ifadesi değerlendirilemedi");
  // non-straight edge for edgeAngle / non-planar face for faceOffset
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  d.features.push_back(plane("PA", {{"kind", "edgeAngle"}}, {"edge(Extrude1.end|Fillet1.f0)"}));
  d.features.push_back(plane("PB", {{"kind", "faceOffset"}}, {"Extrude2.side.e1"}));
  ModelResult r = regenerate(d);
  EXPECT_NE(msgOf(r, "PA").find("kenar düz değil"), std::string::npos) << msgOf(r, "PA");
  EXPECT_NE(msgOf(r, "PB").find("yüz düzlemsel değil 'Extrude2.side.e1'"), std::string::npos) << msgOf(r, "PB");
  // no body yet
  Document e;
  e.features.push_back(plane("P0", {}, {E5TOP}));
  ModelResult re = regenerate(e);
  EXPECT_NE(msgOf(re, "P0").find("gövde yok"), std::string::npos) << msgOf(re, "P0");
}

TEST(ModelV2Plane, SplitEdgeUsesFirstPieceWithWarning) {
  // a groove across the top splits the e5 top edge into two pieces
  Document d = plateDoc();
  d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", -5, -50, 5, 50));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "3"}, {"direction", "reverse"}, {"op", "cut"}}));
  d.features.push_back(plane("Plane1", {{"t", "0"}}, {E5TOP}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NE(msgOf(r, "Plane1").find("bölünmüş"), std::string::npos) << msgOf(r, "Plane1");
  EXPECT_NE(msgOf(r, "Plane1").find("edge(Extrude1.end:1|Extrude1.side.e5)"), std::string::npos) << msgOf(r, "Plane1");
  expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-40, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
}

TEST(ModelV2Plane, FollowsEdgeWhenParametersChange) {
  Document d = plateDoc();
  d.features.push_back(plane("Plane1", {{"t", "0.25"}}, {E5TOP}));
  d.features.push_back(plane("Plane2", {{"kind", "edgeAngle"}, {"angle", "45"}}, {E6TOP}));
  for (auto [W, D, T] : {std::tuple{80.0, 50.0, 10.0}, {120.0, 70.0, 6.0}, {40.0, 30.0, 20.0}}) {
    ModelResult r = regenerate(d, {{"W", formatNumber(W)}, {"D", formatNumber(D)}, {"T", formatNumber(T)}});
    ASSERT_TRUE(r.ok) << statusDump(r);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(-W / 4, -D / 2, T), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), 1e-9,
                "W=" + formatNumber(W));
    const double s = std::sqrt(0.5);
    expectFrame(r.datumPlanes.at("Plane2").frame, gp_Pnt(W / 2, 0, T), gp_Dir(s, 0, s), gp_Dir(0, 1, 0), 1e-9,
                "W=" + formatNumber(W));
    EXPECT_NEAR(r.datumPlanes.at("Plane1").displaySize, 0.6 * std::max({W, D, T}), 1e-6);
  }
}

// ============================================================ OnPlane sketches

TEST(ModelV2OnPlane, SketchOnDatumPlaneExtrudes) {
  // plane normal to the front top edge at its middle: origin (0,-25,10), u=+Y, v=+Z
  Document d = plateDoc();
  d.features.push_back(plane("Plane1", {{"t", "0.5"}}, {E5TOP}));
  d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnPlane, "Plane1", 0, 0, 10, 20));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "4"}, {"direction", "symmetric"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  expectFrame(infoOf(r, "Sketch2")->sketchFrame, gp_Pnt(0, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  expectFrame(infoOf(r, "Extrude2")->sketchFrame, gp_Pnt(0, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  ASSERT_EQ(r.bodies.size(), 1u);  // join AUTO (touches the plate top)
  EXPECT_NEAR(vol(r.shape), 80 * 50 * 10 + 10 * 20 * 4, 1e-6);
  MassProps m = massProperties(r.shape);
  EXPECT_NEAR(m.zmax, 30, 1e-6);
  for (auto* n : {"Extrude2.side.e7", "Extrude2.start", "Extrude2.end", "Extrude2.side.e6"}) EXPECT_TRUE(hasName(r, n)) << n;
  expectConsistentBodies(r);

  // public sketchFrame() with a partial result (rollback after the plane) + sketch offset
  d.features[3].sketch->plane.offsetExpr = "3";
  ParamValues pv = d.evaluateParams();
  ModelResult partial = regenerate(d, pv, d.indexOf("Plane1"));
  gp_Ax3 fr;
  std::string err;
  ASSERT_TRUE(sketchFrame(d, "Sketch2", pv, partial, fr, &err)) << err;
  expectFrame(fr, gp_Pnt(3, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  expectFrame(infoOf(r2, "Sketch2")->sketchFrame, gp_Pnt(3, -25, 10), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  EXPECT_DOUBLE_EQ(infoOf(r2, "Sketch2")->values.at("offset"), 3);
  MassProps m2 = massProperties(r2.shape);
  EXPECT_NEAR(m2.zmax, 30, 1e-6);
  // sketchFrame() errors for OnPlane
  EXPECT_FALSE(sketchFrame(d, "Sketch2", pv, ModelResult(), fr, &err));
  EXPECT_NE(err.find("kullanılamıyor"), std::string::npos) << err;
}

TEST(ModelV2OnPlane, PlaneRefErrors) {
  auto run = [](const std::string& ref, bool planeAfter = false, bool badPlane = false) {
    Document d = plateDoc();
    Feature p = plane("Plane1", {{"t", "0.5"}}, {badPlane ? "edge(X|Y)" : E5TOP});
    if (!planeAfter) d.features.push_back(p);
    d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnPlane, ref, 0, 0, 10, 20));
    if (planeAfter) d.features.push_back(p);
    return regenerate(d);
  };
  struct Case {
    std::string ref;
    bool after, bad;
    std::string expect;
  };
  for (auto& c : std::vector<Case>{{"", false, false, "düzlem referansı eksik"},
                                   {"Plane9", false, false, "düzlem bulunamadı 'Plane9'"},
                                   {"Extrude1", false, false, "'Extrude1' bir düzlem özelliği değil"},
                                   {"Plane1", true, false, "düzlem 'Plane1' çizimden sonra tanımlı"},
                                   {"Plane1", false, true, "düzlem 'Plane1' kullanılamıyor"}}) {
    ModelResult r = run(c.ref, c.after, c.bad);
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(statusOf(r, "Sketch2")->ok) << c.expect;
    EXPECT_NE(msgOf(r, "Sketch2").find(c.expect), std::string::npos) << msgOf(r, "Sketch2");
  }
}

// ============================================================ bodies

TEST(ModelV2Bodies, NewBodyNamesAndLookup) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  addBox(d, "Sketch3", "Extrude3", -80, -10, -60, 10, 0, 10, {{"op", "new"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Kutu", "Extrude3"}));
  expectConsistentBodies(r);
  EXPECT_EQ(r.body("Kutu")->createdBy, "Extrude2");
  EXPECT_EQ(r.body("Extrude3")->createdBy, "Extrude3");
  EXPECT_EQ(r.body("Nope"), nullptr);
  EXPECT_EQ(r.bodyOfFace("Extrude2.end"), "Kutu");
  EXPECT_EQ(r.bodyOfFace("Extrude1.end"), "Extrude1");
  EXPECT_EQ(r.bodyOfFace("Extrude9.end"), "");
  EXPECT_NEAR(bodyVol(r, "Kutu"), 20 * 20 * 10, 1e-6);
  EXPECT_NEAR(vol(r.shape), 80 * 50 * 10 + 2 * 4000, 1e-6);
  EXPECT_EQ(infoOf(r, "Extrude2")->createdFaces.size(), 6u);
  // a duplicate body name is an error
  Document e = d;
  e.features.back().props["bodyName"] = "Kutu";
  ModelResult re = regenerate(e);
  EXPECT_FALSE(re.ok);
  EXPECT_NE(msgOf(re, "Extrude3").find("gövde adı 'Kutu' zaten kullanılıyor"), std::string::npos) << msgOf(re, "Extrude3");
}

TEST(ModelV2Bodies, JoinAutoMergesIntoOldestAndNonTouchingCreatesBody) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  // bridge touching both -> one body named after the oldest (Extrude1)
  addBox(d, "Sketch3", "Extrude3", 30, -5, 70, 5, 0, 5, {{"op", "join"}});
  // far away join -> new body named after the feature
  addBox(d, "Sketch4", "Extrude4", 0, 100, 10, 110, 0, 5, {{"op", "join"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Extrude4"}));
  expectConsistentBodies(r);
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000 + 4000 + 20 * 10 * 5, 1e-6);
  EXPECT_EQ(r.bodyOfFace("Extrude2.end"), "Extrude1");  // Kutu's faces live on in the merged body
  EXPECT_EQ(r.body("Extrude1")->createdBy, "Extrude1");
  EXPECT_EQ(r.body("Extrude4")->createdBy, "Extrude4");
  // non-touching join with a bodyName
  d.features.back().props["bodyName"] = "Ada";
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_EQ(bodyNames(r2), (std::vector<std::string>{"Extrude1", "Ada"}));
}

TEST(ModelV2Bodies, JoinExplicitTargets) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  // tool overlaps both bodies but only Kutu is targeted
  addBox(d, "Sketch3", "Extrude3", 30, -5, 70, 5, 0, 15, {{"op", "join"}, {"targets", "Kutu"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Kutu"}));
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000, 1e-6);
  EXPECT_NEAR(bodyVol(r, "Kutu"), 4000 + 40 * 10 * 15 - 10 * 10 * 10, 1e-6);
  expectConsistentBodies(r);
  // unknown target
  d.features.back().props["targets"] = "Kutu, Hayalet";
  ModelResult r2 = regenerate(d);
  EXPECT_FALSE(r2.ok);
  EXPECT_NE(msgOf(r2, "Extrude3").find("gövde bulunamadı 'Hayalet'"), std::string::npos) << msgOf(r2, "Extrude3");
}

TEST(ModelV2Bodies, CutKeepToolAndExplicitTargets) {
  Document d = plateDoc();
  d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", -5, -5, 5, 5));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "4"}, {"direction", "reverse"}, {"op", "cut"},
                             {"keepTool", "1"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Extrude2.tool"}));
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000 - 400, 1e-6);
  EXPECT_NEAR(bodyVol(r, "Extrude2.tool"), 400, 1e-6);
  EXPECT_EQ(r.body("Extrude2.tool")->createdBy, "Extrude2");
  for (auto* n : {"Extrude2.tool.start", "Extrude2.tool.end", "Extrude2.tool.side.e5"})
    EXPECT_EQ(r.bodyOfFace(n), "Extrude2.tool") << n << statusDump(r);
  EXPECT_EQ(r.bodyOfFace("Extrude2.end"), "Extrude1");  // pocket floor keeps the v1 name
  expectConsistentBodies(r);
  // bodyName for the kept tool
  d.features.back().props["bodyName"] = "Tapa";
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_EQ(bodyNames(r2), (std::vector<std::string>{"Extrude1", "Tapa"}));

  // explicit targets: tool overlaps two bodies, only one is cut
  Document e = plateDoc();
  addBox(e, "Sketch2", "Extrude2", -40, -25, 40, 25, 10, 10, {{"op", "new"}, {"bodyName", "Ust"}});
  addBox(e, "Sketch3", "Extrude3", -5, -5, 5, 5, 5, 10, {{"op", "cut"}, {"targets", "Ust"}});
  ModelResult re = regenerate(e);
  ASSERT_TRUE(re.ok) << statusDump(re);
  EXPECT_NEAR(bodyVol(re, "Extrude1"), 40000, 1e-6);
  EXPECT_NEAR(bodyVol(re, "Ust"), 40000 - 500, 1e-6);
  // AUTO: both touched bodies are cut; the tool face name is carried by two bodies -> ":1"/":2"
  e.features.back().props.erase("targets");
  ModelResult ra = regenerate(e);
  ASSERT_TRUE(ra.ok) << statusDump(ra);
  EXPECT_NEAR(bodyVol(ra, "Extrude1"), 40000 - 500, 1e-6);
  EXPECT_NEAR(bodyVol(ra, "Ust"), 40000 - 500, 1e-6);
  EXPECT_FALSE(hasName(ra, "Extrude3.side.e5"));
  ASSERT_TRUE(hasName(ra, "Extrude3.side.e5:1"));
  ASSERT_TRUE(hasName(ra, "Extrude3.side.e5:2"));
  EXPECT_EQ(ra.bodyOfFace("Extrude3.side.e5:1"), "Extrude1");  // lower centroid z
  EXPECT_EQ(ra.bodyOfFace("Extrude3.side.e5:2"), "Ust");
  expectConsistentBodies(ra);
  // cut touching nothing: warning, no change
  addBox(e, "Sketch4", "Extrude4", 200, 200, 210, 210, 0, 5, {{"op", "cut"}});
  ModelResult rn = regenerate(e);
  EXPECT_TRUE(rn.ok) << statusDump(rn);
  EXPECT_NE(msgOf(rn, "Extrude4").find("hiçbir gövdeye değmiyor"), std::string::npos) << msgOf(rn, "Extrude4");
}

TEST(ModelV2Bodies, IntersectAutoRemovesUntouched) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 100, 0, 120, 20, 0, 10, {{"op", "new"}});
  addBox(d, "Sketch3", "Extrude3", -10, -10, 10, 10, -5, 30, {{"op", "intersect"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1"}));
  EXPECT_NEAR(vol(r.shape), 20 * 20 * 10, 1e-6);
  EXPECT_NE(msgOf(r, "Extrude3").find("'Extrude2'"), std::string::npos) << msgOf(r, "Extrude3");
  EXPECT_EQ(r.shape.ShapeType(), TopAbs_SOLID);
  // explicit target: only that body is intersected, others kept; keepTool keeps the tool
  d.features.back().props["targets"] = "Extrude1";
  d.features.back().props["keepTool"] = "1";
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_EQ(bodyNames(r2), (std::vector<std::string>{"Extrude1", "Extrude2", "Extrude3.tool"}));
  EXPECT_NEAR(bodyVol(r2, "Extrude1"), 4000, 1e-6);
  EXPECT_NEAR(bodyVol(r2, "Extrude3.tool"), 20 * 20 * 30, 1e-6);
  expectConsistentBodies(r2);
}

TEST(ModelV2Bodies, SplitBodyPiecesAndTargets) {
  Document d = plateDoc();
  // slot through the whole plate across Y splits the body in two
  addBox(d, "Sketch2", "Extrude2", -5, -50, 5, 50, -1, 12, {{"op", "cut"}});
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1:1", "Extrude1:2"}));
  EXPECT_LT(massProperties(r.body("Extrude1:1")->shape).cx, 0);
  EXPECT_GT(massProperties(r.body("Extrude1:2")->shape).cx, 0);
  EXPECT_EQ(r.body("Extrude1:2")->createdBy, "Extrude1");
  expectConsistentBodies(r);
  // targets "Extrude1" selects both pieces (warning); explicit piece only that one
  addBox(d, "Sketch3", "Extrude3", -30, -5, 30, 5, 8, 4, {{"op", "cut"}, {"targets", "Extrude1"}});
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_NE(msgOf(r2, "Extrude3").find("gövde 'Extrude1' bölünmüş"), std::string::npos) << msgOf(r2, "Extrude3");
  EXPECT_NEAR(bodyVol(r2, "Extrude1:1"), 35 * 50 * 10 - 25 * 10 * 2, 1e-6);
  EXPECT_NEAR(bodyVol(r2, "Extrude1:2"), 35 * 50 * 10 - 25 * 10 * 2, 1e-6);
  d.features.back().props["targets"] = "Extrude1:2";
  ModelResult r3 = regenerate(d);
  ASSERT_TRUE(r3.ok) << statusDump(r3);
  EXPECT_NEAR(bodyVol(r3, "Extrude1:1"), 35 * 50 * 10, 1e-6);
  EXPECT_NEAR(bodyVol(r3, "Extrude1:2"), 35 * 50 * 10 - 25 * 10 * 2, 1e-6);
  // stable when W changes
  ModelResult r4 = regenerate(d, {{"W", "120"}});
  ASSERT_TRUE(r4.ok) << statusDump(r4);
  EXPECT_EQ(bodyNames(r4), bodyNames(r3));
  std::set<std::string> a, b;
  for (auto& [n, f] : r3.faces) a.insert(n);
  for (auto& [n, f] : r4.faces) b.insert(n);
  EXPECT_EQ(a, b);
}

TEST(ModelV2Bodies, FilletChamferShellOnOneBodyOfTwo) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  d.features.push_back(feat("Fillet1", FeatureType::Fillet, {{"radius", "2"}},
                            {"edge(Extrude2.end|Extrude2.side.e5)", "edge(Extrude2.end|Extrude2.side.e6)"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Kutu"}));
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000, 1e-6);
  // two 20 mm edges meeting at a corner: removed volume just below 2*20*(r^2 - pi r^2/4)
  EXPECT_LT(bodyVol(r, "Kutu"), 4000 - 2 * 20 * (4 - M_PI) + 1.5);
  EXPECT_GT(bodyVol(r, "Kutu"), 4000 - 2 * 20 * (4 - M_PI));
  EXPECT_EQ(r.bodyOfFace("Fillet1.f0"), "Kutu");
  EXPECT_EQ(r.bodyOfFace("Fillet1.f1"), "Kutu");
  EXPECT_DOUBLE_EQ(infoOf(r, "Fillet1")->values.at("bodies"), 1);
  expectConsistentBodies(r);
  // a chamfer spanning both bodies numbers its faces over both
  d.features.push_back(feat("Chamfer1", FeatureType::Chamfer, {{"distance", "1"}},
                            {"edge(Extrude1.end|Extrude1.side.e5)", "edge(Extrude2.side.e7|Extrude2.start)"}));
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_EQ(r2.bodyOfFace("Chamfer1.c0"), "Extrude1");
  EXPECT_EQ(r2.bodyOfFace("Chamfer1.c1"), "Kutu");
  EXPECT_DOUBLE_EQ(infoOf(r2, "Chamfer1")->values.at("bodies"), 2);
  EXPECT_NEAR(bodyVol(r2, "Extrude1"), 40000 - 80 * 0.5, 1e-6);
  expectConsistentBodies(r2);
  // shell acts on the body owning the removed face only
  Document e = plateDoc();
  addBox(e, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  e.features.push_back(feat("Shell1", FeatureType::Shell, {{"thickness", "1"}}, {"Extrude2.end"}));
  ModelResult r3 = regenerate(e);
  ASSERT_TRUE(r3.ok) << statusDump(r3);
  EXPECT_NEAR(bodyVol(r3, "Extrude1"), 40000, 1e-6);
  EXPECT_NEAR(bodyVol(r3, "Kutu"), 4000 - 18 * 18 * 9, 1e-6);
  EXPECT_EQ(r3.bodyOfFace("Shell1.inner.Extrude2.start"), "Kutu") << statusDump(r3);
  expectConsistentBodies(r3);
}

TEST(ModelV2Bodies, HoleAndPatternHonourTargets) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", -40, -25, 40, 25, 20, 10, {{"op", "new"}, {"bodyName", "Ust"}});
  Feature s = sketchFeature("Sketch3", SketchPlaneKind::OnFace, "Extrude2.end");
  int p = s.sketch->addPoint(0, 0);
  s.sketch->addConstraint(SkConstraintType::DistanceX, {0, p}, "0");
  s.sketch->addConstraint(SkConstraintType::DistanceY, {0, p}, "0");
  d.features.push_back(s);
  d.features.push_back(feat("Hole1", FeatureType::Hole,
                            {{"sketch", "Sketch3"}, {"diameter", "6"}, {"through", "1"}, {"targets", "Extrude1"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NEAR(bodyVol(r, "Ust"), 40000, 1e-6);
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000 - M_PI * 9 * 10, 1e-6);
  EXPECT_NEAR(infoOf(r, "Hole1")->values.at("materialDepth"), 10, 1e-6);
  EXPECT_EQ(r.bodyOfFace("Hole1.h0.wall"), "Extrude1");
  // AUTO: both plates drilled; material depth spans both
  d.features.back().props.erase("targets");
  ModelResult r2 = regenerate(d);
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  EXPECT_NEAR(bodyVol(r2, "Ust"), 40000 - M_PI * 9 * 10, 1e-6);
  EXPECT_NEAR(infoOf(r2, "Hole1")->values.at("materialDepth"), 30, 1e-6);
  EXPECT_TRUE(hasName(r2, "Hole1.h0.wall:1") && hasName(r2, "Hole1.h0.wall:2")) << statusDump(r2);
  // Pattern with own op=new: each instance becomes a body piece "Pattern1:k"
  Document e = plateDoc();
  addBox(e, "Sketch2", "Extrude2", 50, 0, 60, 10, 0, 5, {{"op", "new"}, {"bodyName", "Pim"}});
  e.features.push_back(feat("Pattern1", FeatureType::Pattern,
                            {{"source", "Extrude2"}, {"kind", "linear"}, {"count", "3"}, {"spacing", "20"}, {"op", "new"}}));
  ModelResult re = regenerate(e);
  ASSERT_TRUE(re.ok) << statusDump(re);
  EXPECT_EQ(bodyNames(re), (std::vector<std::string>{"Extrude1", "Pim", "Pattern1:1", "Pattern1:2"}));
  EXPECT_TRUE(hasName(re, "Pattern1.i2.Extrude2.end"));
  EXPECT_EQ(re.bodyOfFace("Pattern1.i2.Extrude2.end"), "Pattern1:2");
  expectConsistentBodies(re);
  // Pattern of a cut with explicit targets
  Document g = plateDoc();
  addBox(g, "Sketch2", "Extrude2", -40, -25, 40, 25, 10, 10, {{"op", "new"}, {"bodyName", "Ust"}});
  addBox(g, "Sketch3", "Extrude3", -30, -5, -20, 5, 5, 10, {{"op", "cut"}, {"targets", "Ust"}});
  g.features.push_back(feat("Pattern1", FeatureType::Pattern,
                            {{"source", "Extrude3"}, {"count", "3"}, {"spacing", "20"}, {"targets", "Ust"}}));
  ModelResult rg = regenerate(g);
  ASSERT_TRUE(rg.ok) << statusDump(rg);
  EXPECT_NEAR(bodyVol(rg, "Extrude1"), 40000, 1e-6);
  EXPECT_NEAR(bodyVol(rg, "Ust"), 40000 - 3 * 500, 1e-6);
}

// AUTO join with a multi-solid tool: touching instances merge, the others form a new body; the
// existing body keeps its name.
TEST(ModelV2Bodies, PatternJoinPartiallyTouching) {
  Document d = plateDoc();
  addBox(d, "Sketch2", "Extrude2", 25, -5, 35, 5, 10, 10, {{"op", "join"}});
  d.features.push_back(feat("Pattern1", FeatureType::Pattern,
                            {{"source", "Extrude2"}, {"count", "3"}, {"spacing", "40"}, {"dirX", "-1"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Pattern1"}));
  EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000 + 2 * 1000, 1e-6);
  EXPECT_NEAR(bodyVol(r, "Pattern1"), 1000, 1e-6);
  EXPECT_EQ(r.bodyOfFace("Pattern1.i1.Extrude2.end"), "Extrude1");
  EXPECT_EQ(r.bodyOfFace("Pattern1.i2.Extrude2.end"), "Pattern1");
  expectConsistentBodies(r);
}

TEST(ModelV2Bodies, CombineJoinCutIntersect) {
  auto base = [] {
    Document d = plateDoc();
    addBox(d, "Sketch2", "Extrude2", 30, -10, 60, 10, 0, 20, {{"op", "new"}, {"bodyName", "Kutu"}});  // overlaps plate
    addBox(d, "Sketch3", "Extrude3", 100, 0, 110, 10, 0, 10, {{"op", "new"}, {"bodyName", "Uzak"}});
    return d;
  };
  {  // join: tool consumed, target keeps its name and position
    Document d = base();
    d.features.push_back(feat("Combine1", FeatureType::Combine, {{"op", "join"}, {"target", "Kutu"}, {"tools", "Extrude1"}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Kutu", "Uzak"}));
    EXPECT_NEAR(bodyVol(r, "Kutu"), 40000 + 30 * 20 * 20 - 10 * 20 * 10, 1e-6);
    EXPECT_EQ(r.bodyOfFace("Extrude1.start:1"), "") << "no split expected";
    EXPECT_EQ(r.bodyOfFace("Extrude1.side.e5"), "Kutu");
    EXPECT_EQ(r.body("Kutu")->createdBy, "Extrude2");
    const FeatureInfo* fi = infoOf(r, "Combine1");
    ASSERT_NE(fi, nullptr);
    EXPECT_DOUBLE_EQ(fi->values.at("tools"), 1);
    EXPECT_DOUBLE_EQ(fi->values.at("keepTools"), 0);
    EXPECT_FALSE(fi->toolShape.IsNull());
    expectConsistentBodies(r);
  }
  {  // cut with keepTools: tool body stays, the target's new faces are "Combine1.<toolFace>"
    Document d = base();
    d.features.push_back(feat("Combine1", FeatureType::Combine,
                              {{"op", "cut"}, {"target", "Kutu"}, {"tools", "Extrude1"}, {"keepTools", "1"}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1", "Kutu", "Uzak"}));
    EXPECT_NEAR(bodyVol(r, "Kutu"), 30 * 20 * 20 - 10 * 20 * 10, 1e-6);
    EXPECT_NEAR(bodyVol(r, "Extrude1"), 40000, 1e-6);
    EXPECT_EQ(r.bodyOfFace("Combine1.Extrude1.end"), "Kutu") << statusDump(r);
    EXPECT_EQ(r.bodyOfFace("Combine1.Extrude1.side.e6"), "Kutu") << statusDump(r);
    EXPECT_EQ(r.bodyOfFace("Extrude1.end"), "Extrude1");
    EXPECT_EQ(infoOf(r, "Combine1")->createdFaces.size(), 2u);
    expectConsistentBodies(r);
  }
  {  // intersect with two tools (union of tools), tools consumed
    Document d = base();
    d.features.push_back(feat("Combine1", FeatureType::Combine,
                              {{"op", "intersect"}, {"target", "Extrude1"}, {"tools", "Kutu, Uzak"}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Extrude1"}));
    EXPECT_NEAR(vol(r.shape), 10 * 20 * 10, 1e-6);
  }
  {  // errors
    auto err = [&](std::map<std::string, std::string> props, const std::string& sub) {
      Document d = base();
      d.features.push_back(feat("Combine1", FeatureType::Combine, props));
      ModelResult r = regenerate(d);
      EXPECT_FALSE(r.ok) << sub;
      EXPECT_NE(msgOf(r, "Combine1").find(sub), std::string::npos) << msgOf(r, "Combine1");
      EXPECT_EQ(r.bodies.size(), 3u);
    };
    err({{"tools", "Kutu"}}, "hedef gövde belirtilmemiş");
    err({{"target", "Kutu"}}, "araç gövdesi belirtilmemiş");
    err({{"target", "Yok"}, {"tools", "Kutu"}}, "gövde bulunamadı 'Yok'");
    err({{"target", "Kutu"}, {"tools", "Uzak,Yok"}}, "gövde bulunamadı 'Yok'");
    err({{"target", "Kutu"}, {"tools", "Kutu"}}, "araç olarak kullanılamaz");
    err({{"target", "Kutu"}, {"tools", "Uzak"}, {"op", "xor"}}, "geçersiz işlem 'xor'");
    err({{"target", "Kutu"}, {"tools", "Uzak"}, {"op", "intersect"}}, "boş gövde");
  }
}

// ============================================================ sample

namespace {
Document loadV2() {
  Document d;
  std::string err;
  EXPECT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/v2_multibody.pcad", &err)) << err;
  return d;
}
double tabVolume(double A, double L, double W, double T, double Dp) {
  return W * L * T - M_PI * Dp * Dp / 4 * T - W * T * T * std::tan(A * M_PI / 180) / 2;
}
}  // namespace

TEST(ModelV2Sample, MultibodySampleRegenerates) {
  Document d = loadV2();
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  for (auto& st : r.status) EXPECT_TRUE(st.message.empty()) << st.featureId << ": " << st.message;
  EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Braket", "Tab", "Tapa"}));
  EXPECT_EQ(r.body("Braket")->createdBy, "Extrude1");
  EXPECT_EQ(r.body("Tab")->createdBy, "Extrude4");
  EXPECT_EQ(r.body("Tapa")->createdBy, "Extrude5");
  expectConsistentBodies(r);
  EXPECT_TRUE(BRepCheck_Analyzer(r.shape).IsValid());
  // (a) rib joined into the bracket
  for (auto* n : {"Extrude3.side.e7", "Extrude3.side.e8", "Extrude3.start", "Extrude3.end", "Fillet2.f0", "Fillet2.f3"})
    EXPECT_EQ(r.bodyOfFace(n), "Braket") << n << statusDump(r);  // flat-topped rib + R2 root fillets
  double vb = bodyVol(r, "Braket");
  EXPECT_GT(vb, 37817.0 + 150);
  EXPECT_LT(vb, 37818.0 + 240);
  // (b,c,d) tab with hole, plug kept, overlap trimmed by Combine1
  EXPECT_NEAR(bodyVol(r, "Tapa"), M_PI * 16 * 4, 1e-6);
  EXPECT_NEAR(bodyVol(r, "Tab"), tabVolume(30, 20, 24, 4, 8), 1e-6);
  EXPECT_EQ(r.bodyOfFace("Combine1.Extrude1.side.e6"), "Tab");
  EXPECT_EQ(r.bodyOfFace("Extrude5.side.e2"), "Tab");
  EXPECT_EQ(r.bodyOfFace("Extrude5.tool.side.e2"), "Tapa");
  // datum planes
  ASSERT_EQ(r.datumPlanes.size(), 2u);
  expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, -25, 8), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  expectFrame(r.datumPlanes.at("Plane2").frame, gp_Pnt(40, 0, 8), gp_Dir(0.5, 0, std::sqrt(3.0) / 2), gp_Dir(0, 1, 0),
              1e-12);
  expectFrame(infoOf(r, "Sketch6")->sketchFrame, gp_Pnt(40, 0, 8), gp_Dir(0.5, 0, std::sqrt(3.0) / 2), gp_Dir(0, 1, 0),
              1e-12);
  // exposed parameters for the app
  int exposed = 0;
  for (auto* n : {"Hr", "tr", "rt", "Rf", "Atab", "Ltab", "Wtab", "Ttab", "Dp"}) {
    const Parameter* p = d.params.find(n);
    ASSERT_NE(p, nullptr) << n;
    exposed += p->exposed ? 1 : 0;
    EXPECT_FALSE(p->label.empty()) << n;
    EXPECT_FALSE(p->group.empty()) << n;
  }
  EXPECT_EQ(exposed, 9);
}

TEST(ModelV2Sample, StableNamesAndPlanesUnderParameterChanges) {
  Document d = loadV2();
  ModelResult r0 = regenerate(d);
  ASSERT_TRUE(r0.ok) << statusDump(r0);
  std::vector<std::map<std::string, std::string>> cases = {
      {{"Atab", "0"}}, {{"Atab", "45"}}, {{"Atab", "60"}, {"Ltab", "30"}}, {{"W", "100"}, {"D", "60"}},
      {{"Hr", "4"}, {"tr", "6"}, {"rt", "5"}}, {{"Dp", "12"}, {"Wtab", "30"}}, {{"T", "12"}, {"Ttab", "6"}}};
  for (auto& ov : cases) {
    std::string tag;
    for (auto& [k, v] : ov) tag += k + "=" + v + " ";
    ModelResult r = regenerate(d, ov);
    ASSERT_TRUE(r.ok) << tag << statusDump(r);
    EXPECT_EQ(bodyNames(r), bodyNames(r0)) << tag;
    expectConsistentBodies(r, tag);
    for (auto& b : r0.bodies) {
      std::set<std::string> fa(b.faces.begin(), b.faces.end());
      std::set<std::string> fb(r.body(b.name)->faces.begin(), r.body(b.name)->faces.end());
      if (ov.count("Atab") && ov.at("Atab") == "0" && b.name == "Tab") continue;  // no overlap to trim at 0 deg
      EXPECT_EQ(fa, fb) << tag << " body " << b.name;
    }
    auto P = [&](const char* k, double def) { return ov.count(k) ? std::stod(ov.at(k)) : def; };
    double W = P("W", 80), D = P("D", 50), T = P("T", 8), A = P("Atab", 30);
    expectFrame(r.datumPlanes.at("Plane1").frame, gp_Pnt(0, -D / 2, T), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0), 1e-9, tag);
    expectFrame(r.datumPlanes.at("Plane2").frame, gp_Pnt(W / 2, 0, T),
                gp_Dir(std::sin(A * M_PI / 180), 0, std::cos(A * M_PI / 180)), gp_Dir(0, 1, 0), 1e-9, tag);
    EXPECT_NEAR(bodyVol(r, "Tab"), tabVolume(A, P("Ltab", 20), P("Wtab", 24), P("Ttab", 4), P("Dp", 8)), 1e-5) << tag;
  }
}

// Exposed-parameter corners of the v2 sample. The v2 inputs (rib + tab groups) must always regenerate
// cleanly; the inherited bracket inputs only must not throw and keep unique names (some bracket extremes,
// e.g. a boss smaller than its bore, are inconsistent designs already in v1).
TEST(ModelV2Sample, ParameterCornersRobust) {
  Document d = loadV2();
  const std::set<std::string> v2 = {"Hr", "tr", "rt", "Rf", "Atab", "Ltab", "Wtab", "Ttab", "Dp"};
  std::vector<std::map<std::string, std::string>> cases;
  for (auto& p : d.params.all()) {
    if (!p.exposed || !std::isfinite(p.minValue) || !std::isfinite(p.maxValue)) continue;
    cases.push_back({{p.name, formatNumber(p.minValue)}});
    cases.push_back({{p.name, formatNumber(p.maxValue)}});
  }
  cases.push_back({{"Atab", "60"}, {"Ltab", "15"}, {"Dp", "12"}, {"Wtab", "16"}, {"Ttab", "10"}});
  cases.push_back({{"Atab", "0"}, {"Ltab", "60"}, {"Wtab", "40"}, {"Ttab", "1"}, {"Dp", "2"}});
  cases.push_back({{"tr", "8"}, {"rt", "8"}, {"Rf", "2.5"}, {"Hr", "3"}});
  cases.push_back({{"Hb", "2"}, {"Rf", "2.5"}});
  for (auto& ov : cases) {
    std::string tag;
    bool strict = true;
    for (auto& [k, v] : ov) {
      tag += k + "=" + v + " ";
      strict &= v2.count(k) > 0 || k == "Hb";
    }
    ModelResult r;
    ASSERT_NO_THROW(r = regenerate(d, ov)) << tag;
    ASSERT_FALSE(r.shape.IsNull()) << tag << statusDump(r);
    expectConsistentBodies(r, tag);
    if (strict) {
      EXPECT_TRUE(r.ok) << tag << statusDump(r);
      EXPECT_EQ(bodyNames(r), (std::vector<std::string>{"Braket", "Tab", "Tapa"})) << tag;
      EXPECT_EQ(r.datumPlanes.size(), 2u) << tag;
    } else if (!r.ok) {
      std::cout << "[ corner ] " << tag << "FAILED (bracket extreme)\n";
    }
  }
}

TEST(ModelV2Sample, V1SamplesStaySingleBody) {
  for (const char* f : {"bracket.pcad", "model_flange.pcad"}) {
    Document d;
    std::string err;
    ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/" + f, &err)) << err;
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << f << statusDump(r);
    ASSERT_EQ(r.bodies.size(), 1u) << f;
    EXPECT_EQ(r.bodies[0].name, "Extrude1");
    EXPECT_EQ(r.bodies[0].createdBy, "Extrude1");
    EXPECT_TRUE(r.bodies[0].shape.IsSame(r.shape));
    EXPECT_EQ(r.shape.ShapeType(), TopAbs_SOLID);
    EXPECT_TRUE(r.datumPlanes.empty());
    expectConsistentBodies(r, f);
  }
}
