// Tests for src/model (Agent B): regeneration, persistent naming, FeatureInfo, export.
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/ModelNaming.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <set>
// Performance budgets are tuned on Linux; the Windows (MSYS2 CLANG64) OCCT build measures ~1.5x slower.
#ifdef _WIN32
static constexpr double kPerfScale = 2.0;
#else
static constexpr double kPerfScale = 1.0;
#endif


using namespace pc;
using namespace pc::model;

namespace {

Document loadBracket() {
  Document d;
  std::string err;
  EXPECT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  return d;
}

std::set<std::string> faceNames(const ModelResult& r) {
  std::set<std::string> s;
  for (auto& [n, f] : r.faces) s.insert(n);
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

std::string statusDump(const ModelResult& r) {
  std::string s;
  for (auto& st : r.status) s += st.featureId + (st.ok ? " ok " : " FAIL ") + st.message + "\n";
  return s;
}

double bracketVolume(double W, double D, double T, double R, double Dh, double Db, double Hb, double Dbore) {
  double Lbore = Hb + T - 4;
  double rb = Dbore / 2;
  double tip = rb / std::tan(59.0 * M_PI / 180.0);
  return W * D * T - 4 * (R * R - M_PI * R * R / 4) * T - 4 * M_PI * (Dh / 2) * (Dh / 2) * T +
         M_PI * (Db / 2) * (Db / 2) * Hb - (M_PI * rb * rb * Lbore + M_PI * rb * rb * tip / 3);
}

// ---------- programmatic document helpers
Feature sketchFeature(const std::string& id, SketchPlaneKind k, const std::string& faceRef = "") {
  Feature f;
  f.id = id;
  f.type = FeatureType::Sketch;
  Sketch s = Sketch::create();
  s.plane.kind = k;
  s.plane.faceRef = faceRef;
  f.sketch = s;
  return f;
}

// Rectangle with corner positions given by expressions (x0,y0) / (x1,y1). Returns line ids b,r,t,l.
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

int freePoint(Sketch& s, const std::string& xe, const std::string& ye, double x, double y) {
  int p = s.addPoint(x, y);
  s.addConstraint(SkConstraintType::DistanceX, {0, p}, xe);
  s.addConstraint(SkConstraintType::DistanceY, {0, p}, ye);
  return p;
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

// Plate W x D x T centered at origin, Sketch1 + Extrude1.
Document plateDoc(double W, double D, double T) {
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

std::string dumpNames(const ModelResult& r) {
  std::string s;
  for (auto& [n, f] : r.faces) s += n + " ";
  return s;
}

::testing::AssertionResult hasName(const ModelResult& r, const std::string& n) {
  if (r.faces.count(n)) return ::testing::AssertionSuccess();
  return ::testing::AssertionFailure() << "missing face '" << n << "' in: " << dumpNames(r);
}

}  // namespace

// ============================================================ naming toolkit (pure OCCT)

TEST(ModelNaming, SplitFaceGetsSuffixOrderedByCentroid) {
  using namespace pc::model::naming;
  TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 0), 100, 20, 10).Shape();
  NamedShape body;
  body.shape = box;
  int k = 0;
  for (TopExp_Explorer ex(box, TopAbs_FACE); ex.More(); ex.Next()) body.faces.Bind(ex.Current(), "B.f" + std::to_string(k++));
  // tool: a slot across the whole Y width through Z -> splits the box into two solids;
  // use a groove (depth 5) so top face splits into 2 pieces, body stays one solid
  TopoDS_Shape tool = BRepPrimAPI_MakeBox(gp_Pnt(45, -5, 5), 10, 30, 10).Shape();
  NamedShape tl;
  tl.shape = tool;
  k = 0;
  for (TopExp_Explorer ex(tool, TopAbs_FACE); ex.More(); ex.Next()) tl.faces.Bind(ex.Current(), "T.f" + std::to_string(k++));
  BRepAlgoAPI_Cut cut(box, tool);
  ASSERT_TRUE(cut.IsDone());
  std::vector<std::string> log;
  NameMap nm = propagate(cut, {{&body, 0}, {&tl, 1}}, cut.Shape(), {}, "X", &log);
  std::map<std::string, TopoDS_Shape> byN;
  int nf = 0;
  for (TopExp_Explorer ex(cut.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
    ++nf;
    ASSERT_TRUE(nm.IsBound(ex.Current()));
    byN[nm.Find(ex.Current())] = ex.Current();
  }
  EXPECT_EQ((int)byN.size(), nf);  // unique
  EXPECT_TRUE(log.empty());
  // top face of the box (z=10) is B.f5 for MakeBox ordering; find by geometry instead
  std::string topName;
  for (NameMap::Iterator it(body.faces); it.More(); it.Next())
    if (std::abs(faceCentroid(it.Key()).Z() - 10) < 1e-9) topName = it.Value();
  ASSERT_FALSE(topName.empty());
  ASSERT_TRUE(byN.count(topName + ":1"));
  ASSERT_TRUE(byN.count(topName + ":2"));
  EXPECT_FALSE(byN.count(topName));
  EXPECT_LT(faceCentroid(byN[topName + ":1"]).X(), faceCentroid(byN[topName + ":2"]).X());
}

// ============================================================ bracket sample

TEST(Model, BracketRegenerates) {
  Document d = loadBracket();
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  ASSERT_FALSE(r.shape.IsNull());
  EXPECT_EQ(r.shape.ShapeType(), TopAbs_SOLID);
  EXPECT_TRUE(BRepCheck_Analyzer(r.shape).IsValid());
  MassProps m = massProperties(r.shape);
  double V = bracketVolume(80, 50, 8, 5, 6.6, 24, 20, 10);
  EXPECT_NEAR(m.volume, V, V * 0.005);
  EXPECT_NEAR(m.xmin, -40, 1e-4);
  EXPECT_NEAR(m.xmax, 40, 1e-4);
  EXPECT_NEAR(m.zmax, 28, 1e-4);
  EXPECT_EQ(r.status.size(), d.features.size());
}

TEST(Model, BracketExpectedNames) {
  ModelResult r = regenerate(loadBracket());
  ASSERT_TRUE(r.ok) << statusDump(r);
  for (const char* n : {"Extrude1.start", "Extrude1.end", "Extrude1.side.e5", "Extrude1.side.e6", "Extrude1.side.e7",
                        "Extrude1.side.e8", "Fillet1.f0", "Fillet1.f1", "Fillet1.f2", "Fillet1.f3", "Hole1.h0.wall",
                        "Hole1.h1.wall", "Hole1.h2.wall", "Hole1.h3.wall", "Extrude2.side.e1", "Extrude2.end",
                        "Hole2.h0.wall", "Hole2.h0.bottom"})
    EXPECT_TRUE(hasName(r, n)) << n;
  // every face of the shape is named exactly once
  std::set<std::string> seen;
  int nf = 0;
  for (TopExp_Explorer ex(r.shape, TopAbs_FACE); ex.More(); ex.Next()) {
    ++nf;
    std::string n = r.nameOf(TopoDS::Face(ex.Current()));
    EXPECT_FALSE(n.empty());
    seen.insert(n);
  }
  EXPECT_EQ((int)seen.size(), nf);
  EXPECT_EQ((int)r.faces.size(), nf);
  // no fallback names
  for (auto& n : seen) EXPECT_EQ(n.find(".u"), std::string::npos) << n;
  // hole order by (u,v): h0 at (-30,-15)
  gp_Pnt c = pc::model::naming::faceCentroid(r.faces.at("Hole1.h0.wall"));
  EXPECT_NEAR(c.X(), -30, 1e-3);
  EXPECT_NEAR(c.Y(), -15, 1e-3);
  c = pc::model::naming::faceCentroid(r.faces.at("Hole1.h1.wall"));
  EXPECT_NEAR(c.X(), -30, 1e-3);
  EXPECT_NEAR(c.Y(), 15, 1e-3);
  // edges
  EXPECT_TRUE(r.edges.count("edge(Extrude1.end|Hole1.h0.wall)"));
  EXPECT_TRUE(r.edges.count("edge(Extrude1.end|Fillet1.f0)"));
  EXPECT_TRUE(r.edges.count("edge(Extrude1.end|Extrude2.side.e1)"));
  for (auto& [n, e] : r.edges) EXPECT_EQ(r.nameOf(e), n);
}

TEST(Model, BracketFeatureInfo) {
  ModelResult r = regenerate(loadBracket());
  ASSERT_TRUE(r.ok) << statusDump(r);
  ASSERT_EQ(r.info.size(), 9u);
  auto* h1 = infoOf(r, "Hole1");
  ASSERT_TRUE(h1);
  EXPECT_DOUBLE_EQ(h1->values.at("diameter"), 6.6);
  EXPECT_DOUBLE_EQ(h1->values.at("locations"), 4);
  EXPECT_NEAR(h1->values.at("materialDepth"), 8, 1e-6);
  EXPECT_DOUBLE_EQ(h1->values.at("through"), 1);
  EXPECT_FALSE(h1->toolShape.IsNull());
  EXPECT_NEAR(h1->sketchFrame.Location().Z(), 8, 1e-9);
  auto* h2 = infoOf(r, "Hole2");
  ASSERT_TRUE(h2);
  EXPECT_NEAR(h2->values.at("depth"), 24, 1e-9);
  EXPECT_NEAR(h2->values.at("materialDepth"), 24, 1e-6);
  EXPECT_NEAR(h2->sketchFrame.Location().Z(), 28, 1e-9);
  auto* f1 = infoOf(r, "Fillet1");
  ASSERT_TRUE(f1);
  EXPECT_DOUBLE_EQ(f1->values.at("radius"), 5);
  EXPECT_DOUBLE_EQ(f1->values.at("edges"), 4);
  EXPECT_DOUBLE_EQ(f1->values.at("concaveEdges"), 0);
  EXPECT_EQ(f1->createdFaces.size(), 4u);
  auto* e1 = infoOf(r, "Extrude1");
  ASSERT_TRUE(e1);
  EXPECT_DOUBLE_EQ(e1->values.at("distance"), 8);
  auto* s2 = infoOf(r, "Sketch2");
  ASSERT_TRUE(s2);
  EXPECT_NEAR(s2->sketchFrame.Direction().Z(), 1, 1e-12);
  EXPECT_NEAR(s2->sketchFrame.XDirection().X(), 1, 1e-12);
  EXPECT_TRUE(r.sketchResults.count("Sketch1"));
  EXPECT_TRUE(r.solvedSketches.count("Sketch4"));
  // public sketchFrame()
  Document d = loadBracket();
  gp_Ax3 fr;
  std::string err;
  ModelResult part = regenerate(d, r.params, d.indexOf("Extrude2"));
  ASSERT_TRUE(sketchFrame(d, "Sketch4", r.params, part, fr, &err)) << err;
  EXPECT_NEAR(fr.Location().Z(), 28, 1e-9);
}

TEST(Model, BracketNameStabilityUnderParameterChanges) {
  Document d = loadBracket();
  ModelResult base = regenerate(d);
  ASSERT_TRUE(base.ok) << statusDump(base);
  auto ref = faceNames(base);
  std::vector<std::map<std::string, std::string>> cases = {
      {{"W", "150"}}, {{"T", "3"}}, {{"Db", "40"}}, {{"R", "2"}, {"D", "120"}}, {{"Hb", "60"}, {"Dbore", "12"}},
      {{"Dh", "3"}, {"E", "6"}}};
  for (auto& ov : cases) {
    ModelResult r = regenerate(d, ov);
    std::string tag = ov.begin()->first + "=" + ov.begin()->second;
    ASSERT_TRUE(r.ok) << tag << "\n" << statusDump(r);
    EXPECT_EQ(faceNames(r), ref) << tag;
    std::set<std::string> en, eref;
    for (auto& [n, e] : r.edges) en.insert(n);
    for (auto& [n, e] : base.edges) eref.insert(n);
    EXPECT_EQ(en, eref) << tag;
  }
  ModelResult r = regenerate(d, {{"W", "150"}, {"T", "3"}, {"Db", "40"}});
  ASSERT_TRUE(r.ok);
  double V = bracketVolume(150, 50, 3, 5, 6.6, 40, 20, 10);
  EXPECT_NEAR(massProperties(r.shape).volume, V, V * 0.005);
}

TEST(Model, FilletTooBigReportsError) {
  Document d = loadBracket();
  ModelResult r = regenerate(d, {{"R", "30"}});
  EXPECT_FALSE(r.ok);
  auto* st = statusOf(r, "Fillet1");
  ASSERT_TRUE(st);
  EXPECT_FALSE(st->ok);
  EXPECT_NE(st->message.find("Fillet1: yarıçap 30 mm kenar için çok büyük"), std::string::npos) << st->message;
  // the rest still regenerates
  EXPECT_FALSE(r.shape.IsNull());
  EXPECT_TRUE(hasName(r, "Hole2.h0.wall"));
  EXPECT_TRUE(statusOf(r, "Hole1")->ok);
}

TEST(Model, MissingFaceReference) {
  Document d = loadBracket();
  d.feature("Sketch2")->sketch->plane.faceRef = "Extrude1.nope";
  ModelResult r = regenerate(d);
  EXPECT_FALSE(r.ok);
  auto* st = statusOf(r, "Sketch2");
  ASSERT_TRUE(st);
  EXPECT_EQ(st->message, "Sketch2: yüz bulunamadı 'Extrude1.nope'");
  EXPECT_FALSE(statusOf(r, "Hole1")->ok);
  EXPECT_TRUE(statusOf(r, "Hole2")->ok);
}

TEST(Model, SuppressAndRollback) {
  Document d = loadBracket();
  d.feature("Fillet1")->suppressed = true;
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_FALSE(hasName(r, "Fillet1.f0"));
  double V = bracketVolume(80, 50, 8, 0, 6.6, 24, 20, 10);
  EXPECT_NEAR(massProperties(r.shape).volume, V, V * 0.005);
  Document d2 = loadBracket();
  ModelResult r2 = regenerate(d2, d2.evaluateParams(), 1);
  ASSERT_TRUE(r2.ok);
  EXPECT_EQ(r2.status.size(), 2u);
  EXPECT_NEAR(massProperties(r2.shape).volume, 80 * 50 * 8, 1e-6);
  EXPECT_EQ(r2.faces.size(), 6u);
}

TEST(Model, BracketTiming) {
  Document d = loadBracket();
  regenerate(d);  // warm-up
  double best = 1e9;
  for (int i = 0; i < 3; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    ModelResult r = regenerate(d);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    ASSERT_TRUE(r.ok);
    best = std::min(best, ms);
  }
  std::cout << "[ timing ] regenerate(bracket) best of 3: " << best << " ms\n";
  EXPECT_LT(best, 300.0 * kPerfScale);
}

TEST(Model, ExportStepStl) {
  ModelResult r = regenerate(loadBracket());
  ASSERT_TRUE(r.ok);
  auto dir = std::filesystem::temp_directory_path();
  std::string step = (dir / "pc_model_test.step").string(), stl = (dir / "pc_model_test.stl").string();
  std::filesystem::remove(step);
  std::filesystem::remove(stl);
  std::string err;
  EXPECT_TRUE(exportSTEP(r.shape, step, &err)) << err;
  EXPECT_TRUE(exportSTL(r.shape, stl, 0.05, &err)) << err;
  EXPECT_GT(std::filesystem::file_size(step), 1000u);
  EXPECT_GT(std::filesystem::file_size(stl), 1000u);
  EXPECT_FALSE(exportSTEP(TopoDS_Shape(), step, &err));
}

// ============================================================ other features

// Groove across the top face splits Extrude1.end into two named pieces; stable under W change.
TEST(Model, CutSplitsFaceNaming) {
  auto make = [](double W) {
    Document d = plateDoc(W, 50, 10);
    Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
    paramRect(*s2.sketch, "-5", "-D", "5", "D", -5, -50, 5, 50);
    d.features.push_back(s2);
    d.features.push_back(
        feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "3"}, {"direction", "reverse"}, {"op", "cut"}}));
    return d;
  };
  ModelResult r = regenerate(make(80));
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_FALSE(hasName(r, "Extrude1.end"));
  ASSERT_TRUE(hasName(r, "Extrude1.end:1"));
  ASSERT_TRUE(hasName(r, "Extrude1.end:2"));
  EXPECT_LT(naming::faceCentroid(r.faces.at("Extrude1.end:1")).X(), 0);
  EXPECT_GT(naming::faceCentroid(r.faces.at("Extrude1.end:2")).X(), 0);
  EXPECT_TRUE(hasName(r, "Extrude2.end"));          // groove floor
  EXPECT_TRUE(hasName(r, "Extrude2.side.e6"));      // groove wall (right line)
  EXPECT_TRUE(hasName(r, "Extrude2.side.e8"));
  EXPECT_TRUE(hasName(r, "Extrude1.side.e5"));      // notched but single
  EXPECT_NEAR(massProperties(r.shape).volume, 80 * 50 * 10 - 10 * 50 * 3, 1e-6);
  EXPECT_NEAR(infoOf(r, "Extrude2")->values.at("depth"), 3, 1e-12);
  EXPECT_EQ(faceNames(r), faceNames(regenerate(make(150))));
  // a later sketch referencing the (now split) face still resolves, with a warning
  Document d = make(80);
  Feature s3 = sketchFeature("Sketch3", SketchPlaneKind::OnFace, "Extrude1.end");
  freePoint(*s3.sketch, "-20", "0", -20, 0);
  d.features.push_back(s3);
  d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch3"}, {"diameter", "4"}, {"through", "1"}}));
  ModelResult r3 = regenerate(d);
  ASSERT_TRUE(r3.ok) << statusDump(r3);
  EXPECT_NE(statusOf(r3, "Sketch3")->message.find("bölünmüş"), std::string::npos) << statusOf(r3, "Sketch3")->message;
  EXPECT_NEAR(infoOf(r3, "Hole1")->values.at("materialDepth"), 10, 1e-6);
  EXPECT_TRUE(r3.edges.count("edge(Extrude1.end:1|Hole1.h0.wall)"));
}

TEST(Model, ThroughCutDepth) {
  Document d = plateDoc(80, 50, 10);
  Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
  paramRect(*s2.sketch, "-5", "-5", "5", "5", -5, -5, 5, 5);
  d.features.push_back(s2);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"through", "1"}, {"direction", "reverse"}, {"op", "cut"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 80 * 50 * 10 - 100 * 10, 1e-6);
  EXPECT_NEAR(infoOf(r, "Extrude2")->values.at("depth"), 10, 1e-6);
  EXPECT_TRUE(hasName(r, "Extrude2.side.e5"));
  EXPECT_FALSE(hasName(r, "Extrude1.end:1"));
}

TEST(Model, RevolveFullAndPartial) {
  for (double ang : {360.0, 90.0}) {
    Document d;
    d.params.add({"Ri", "10"});
    d.params.add({"Ro", "20"});
    d.params.add({"H", "30"});
    Feature s1 = sketchFeature("Sketch1", SketchPlaneKind::XZ);
    paramRect(*s1.sketch, "Ri", "0", "Ro", "H", 10, 0, 20, 30);
    d.features.push_back(s1);
    d.features.push_back(feat("Revolve1", FeatureType::Revolve,
                              {{"sketch", "Sketch1"}, {"axis", "V"}, {"angle", formatNumber(ang)}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    double V = M_PI * (400 - 100) * 30 * ang / 360.0;
    EXPECT_NEAR(massProperties(r.shape).volume, V, V * 1e-6) << ang;
    for (const char* n : {"Revolve1.rev.e5", "Revolve1.rev.e6", "Revolve1.rev.e7", "Revolve1.rev.e8"})
      EXPECT_TRUE(hasName(r, n)) << n << " " << ang;
    EXPECT_EQ(hasName(r, "Revolve1.start"), ang < 360);
    EXPECT_EQ(hasName(r, "Revolve1.end"), ang < 360);
    EXPECT_EQ(r.faces.size(), ang < 360 ? 6u : 4u);
  }
}

TEST(Model, ShellInward) {
  Document d = plateDoc(40, 30, 20);
  d.features.push_back(feat("Shell1", FeatureType::Shell, {{"thickness", "2"}}, {"Extrude1.end"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 40 * 30 * 20 - 36 * 26 * 18, 1e-3);
  EXPECT_TRUE(hasName(r, "Extrude1.end"));  // the removed face becomes the rim ring (OCCT Modified)
  EXPECT_FALSE(hasName(r, "Shell1.inner.Extrude1.end"));
  for (const char* n : {"Extrude1.start", "Extrude1.side.e5", "Shell1.inner.Extrude1.start", "Shell1.inner.Extrude1.side.e5",
                        "Shell1.inner.Extrude1.side.e6", "Shell1.inner.Extrude1.side.e7", "Shell1.inner.Extrude1.side.e8"})
    EXPECT_TRUE(hasName(r, n)) << n;
  EXPECT_EQ(r.faces.size(), 5u + 5u + 1u);  // outer + inner + rim
  auto* in = infoOf(r, "Shell1");
  ASSERT_TRUE(in);
  EXPECT_DOUBLE_EQ(in->values.at("thickness"), 2);
}

TEST(Model, ChamferFaceSelector) {
  Document d = plateDoc(40, 30, 20);
  d.features.push_back(feat("Chamfer1", FeatureType::Chamfer, {{"distance", "2"}}, {"face:Extrude1.end"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  double V = 40 * 30 * 20 - (4 * 70 - 4 * 8 / 3.0);
  EXPECT_NEAR(massProperties(r.shape).volume, V, 1e-3);
  for (const char* n : {"Chamfer1.c0", "Chamfer1.c1", "Chamfer1.c2", "Chamfer1.c3", "Extrude1.end"})
    EXPECT_TRUE(hasName(r, n)) << n;
  EXPECT_DOUBLE_EQ(infoOf(r, "Chamfer1")->values.at("edges"), 4);
  // c<k> follows sorted edge names: c0 = edge(Extrude1.end|Extrude1.side.e5) -> y=-15 side
  EXPECT_LT(naming::faceCentroid(r.faces.at("Chamfer1.c0")).Y(), -14);
}

TEST(Model, ConcaveEdgeCount) {
  // plate + boss; fillet the boss/plate junction (concave) and the boss top rim (convex)
  Document d = plateDoc(60, 60, 5);
  Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
  int c = s2.sketch->addCircle(0.0, 0.0, 10.0);
  s2.sketch->addConstraint(SkConstraintType::Diameter, {c}, "20");
  d.features.push_back(s2);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "10"}}));
  d.features.push_back(feat("Fillet1", FeatureType::Fillet, {{"radius", "1"}},
                            {"edge(Extrude1.end|Extrude2.side.e" + std::to_string(c) + ")",
                             "edge(Extrude2.end|Extrude2.side.e" + std::to_string(c) + ")"}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  auto* in = infoOf(r, "Fillet1");
  EXPECT_DOUBLE_EQ(in->values.at("edges"), 2);
  EXPECT_DOUBLE_EQ(in->values.at("concaveEdges"), 1);
  EXPECT_TRUE(hasName(r, "Fillet1.f0"));
  EXPECT_TRUE(hasName(r, "Fillet1.f1"));
}

TEST(Model, CounterboreAndCountersink) {
  for (std::string kind : {"counterbore", "countersink"}) {
    Document d = plateDoc(60, 40, 10);
    Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
    freePoint(*s2.sketch, "0", "0", 0, 0);
    d.features.push_back(s2);
    std::map<std::string, std::string> p = {{"sketch", "Sketch2"}, {"diameter", "6"}, {"through", "1"}, {"kind", kind}};
    if (kind == "counterbore") {
      p["cbDiameter"] = "11";
      p["cbDepth"] = "6";
    } else {
      p["csDiameter"] = "12";
      p["csAngle"] = "90";
    }
    d.features.push_back(feat("Hole1", FeatureType::Hole, p));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    double removed = kind == "counterbore" ? M_PI * 5.5 * 5.5 * 6 + M_PI * 9 * 4 : 63 * M_PI + 9 * M_PI * 7;
    EXPECT_NEAR(massProperties(r.shape).volume, 60 * 40 * 10 - removed, 1e-3) << kind;
    EXPECT_TRUE(hasName(r, "Hole1.h0.wall"));
    if (kind == "counterbore") {
      EXPECT_TRUE(hasName(r, "Hole1.h0.cb_wall"));
      EXPECT_TRUE(hasName(r, "Hole1.h0.cb_floor"));
      EXPECT_DOUBLE_EQ(infoOf(r, "Hole1")->values.at("cbDiameter"), 11);
    } else {
      EXPECT_TRUE(hasName(r, "Hole1.h0.cs"));
      EXPECT_DOUBLE_EQ(infoOf(r, "Hole1")->values.at("csDiameter"), 12);
    }
    EXPECT_NEAR(infoOf(r, "Hole1")->values.at("materialDepth"), 10, 1e-6);
  }
}

TEST(Model, BlindFlatHole) {
  Document d = plateDoc(60, 40, 10);
  Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
  freePoint(*s2.sketch, "10", "0", 10, 0);
  int c = s2.sketch->addCircle(-10.0, 0.0, 2.0);  // circle center is a location too
  s2.sketch->addConstraint(SkConstraintType::Diameter, {c}, "4");
  int cp = s2.sketch->entity(c)->points[0];
  s2.sketch->addConstraint(SkConstraintType::DistanceX, {0, cp}, "-10");
  s2.sketch->addConstraint(SkConstraintType::DistanceY, {0, cp}, "0");
  d.features.push_back(s2);
  d.features.push_back(feat("Hole1", FeatureType::Hole,
                            {{"sketch", "Sketch2"}, {"diameter", "5"}, {"depth", "4"}, {"tipAngle", "0"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, 60 * 40 * 10 - 2 * M_PI * 6.25 * 4, 1e-3);
  EXPECT_TRUE(hasName(r, "Hole1.h0.bottom"));
  EXPECT_TRUE(hasName(r, "Hole1.h1.bottom"));
  EXPECT_LT(naming::faceCentroid(r.faces.at("Hole1.h0.wall")).X(), 0);  // ordered by u
  EXPECT_DOUBLE_EQ(infoOf(r, "Hole1")->values.at("locations"), 2);
  EXPECT_NEAR(infoOf(r, "Hole1")->values.at("materialDepth"), 4, 1e-6);
}

TEST(Model, LinearAndCircularPattern) {
  {
    Document d = plateDoc(100, 40, 10);
    Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
    freePoint(*s2.sketch, "-40", "0", -40, 0);
    d.features.push_back(s2);
    d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "6"}, {"through", "1"}}));
    d.features.push_back(feat("Pattern1", FeatureType::Pattern,
                              {{"kind", "linear"}, {"source", "Hole1"}, {"count", "5"}, {"spacing", "20"}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_NEAR(massProperties(r.shape).volume, 100 * 40 * 10 - 5 * M_PI * 9 * 10, 1e-3);
    for (int k = 1; k <= 4; ++k) EXPECT_TRUE(hasName(r, "Pattern1.i" + std::to_string(k) + ".Hole1.h0.wall")) << k;
    EXPECT_NEAR(naming::faceCentroid(r.faces.at("Pattern1.i4.Hole1.h0.wall")).X(), 40, 1e-3);
    EXPECT_DOUBLE_EQ(infoOf(r, "Pattern1")->values.at("count"), 5);
  }
  {
    Document d = plateDoc(100, 100, 10);
    Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
    freePoint(*s2.sketch, "30", "0", 30, 0);
    d.features.push_back(s2);
    d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "6"}, {"through", "1"}}));
    d.features.push_back(feat("Pattern1", FeatureType::Pattern,
                              {{"kind", "circular"}, {"source", "Hole1"}, {"count", "6"}, {"axis", "Z"}}));
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_NEAR(massProperties(r.shape).volume, 100 * 100 * 10 - 6 * M_PI * 9 * 10, 1e-3);
    gp_Pnt c = naming::faceCentroid(r.faces.at("Pattern1.i3.Hole1.h0.wall"));
    EXPECT_NEAR(c.X(), -30, 1e-3);
    EXPECT_NEAR(c.Y(), 0, 1e-3);
  }
}

TEST(Model, ExtrudeSymmetricAndDraft) {
  {
    Document d = plateDoc(40, 30, 20);
    d.feature("Extrude1")->props["direction"] = "symmetric";
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    MassProps m = massProperties(r.shape);
    EXPECT_NEAR(m.zmin, -10, 1e-6);
    EXPECT_NEAR(m.zmax, 10, 1e-6);
  }
  {
    Document d = plateDoc(40, 30, 20);
    d.feature("Extrude1")->props["draft"] = "5";
    ModelResult r = regenerate(d);
    ASSERT_TRUE(r.ok) << statusDump(r);
    EXPECT_TRUE(statusOf(r, "Extrude1")->message.empty()) << statusOf(r, "Extrude1")->message;
    double t = std::tan(5 * M_PI / 180), a = 40, b = 30, h = 20;
    double V = a * b * h - (a + b) * t * h * h + 4 * t * t * h * h * h / 3;
    EXPECT_NEAR(massProperties(r.shape).volume, V, V * 0.005);
    for (const char* n : {"Extrude1.start", "Extrude1.end", "Extrude1.side.e5", "Extrude1.side.e8"})
      EXPECT_TRUE(hasName(r, n)) << n;
  }
}

TEST(Model, IntersectAndNewBody) {
  Document d = plateDoc(40, 40, 10);
  Feature s2 = sketchFeature("Sketch2", SketchPlaneKind::XY);
  int c = s2.sketch->addCircle(0.0, 0.0, 15.0);
  s2.sketch->addConstraint(SkConstraintType::Radius, {c}, "15");
  d.features.push_back(s2);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "30"}, {"op", "intersect"}}));
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_NEAR(massProperties(r.shape).volume, M_PI * 225 * 10, 1e-3);
  EXPECT_TRUE(hasName(r, "Extrude2.side.e" + std::to_string(c)));
  EXPECT_TRUE(hasName(r, "Extrude1.end"));
}

// Exposed-parameter corners of the bracket: never throws, every face uniquely named, valid result.
TEST(Model, BracketParameterCornersRobust) {
  Document d = loadBracket();
  std::vector<std::map<std::string, std::string>> cases = {
      {{"T", "0.5"}}, {{"Hb", "120"}}, {{"Db", "60"}}, {{"W", "200"}, {"D", "150"}, {"T", "30"}},
      {{"W", "30"}, {"D", "20"}, {"E", "3"}, {"Dh", "2"}, {"Db", "5"}, {"Dbore", "1"}}, {{"R", "20"}},
      {{"Dbore", "30"}}, {{"Dh", "20"}}};
  for (auto& ov : cases) {
    ModelResult r;
    ASSERT_NO_THROW(r = regenerate(d, ov));
    std::string tag;
    for (auto& [k, v] : ov) tag += k + "=" + v + " ";
    ASSERT_FALSE(r.shape.IsNull()) << tag << statusDump(r);
    std::set<std::string> seen;
    int nf = 0;
    for (TopExp_Explorer ex(r.shape, TopAbs_FACE); ex.More(); ex.Next()) {
      ++nf;
      std::string n = r.nameOf(TopoDS::Face(ex.Current()));
      EXPECT_FALSE(n.empty()) << tag;
      seen.insert(n);
    }
    EXPECT_EQ((int)seen.size(), nf) << tag;
    std::cout << "[ corner ] " << tag << (r.ok ? "ok" : "FAILED") << " faces=" << nf << "\n";
    if (!r.ok) std::cout << statusDump(r);
  }
}

// samples/model_flange.pcad: annulus extrude + hub join + counterbore hole + circular pattern + chamfer.
TEST(Model, FlangeSample) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/model_flange.pcad", &err)) << err;
  auto vol = [](double Do, double Di, double H, double Dhub, double Hhub, int N, double Dh, double Dcb, double Hcb,
                double C) {
    double V = M_PI / 4 * (Do * Do - Di * Di) * H + M_PI / 4 * (Dhub * Dhub - Di * Di) * Hhub;
    V -= N * (M_PI / 4 * Dcb * Dcb * Hcb + M_PI / 4 * Dh * Dh * (H - Hcb));
    V -= 2 * M_PI * (Do / 2 - C / 3) * C * C / 2 + 2 * M_PI * (Dhub / 2 - C / 3) * C * C / 2;
    return V;
  };
  ModelResult r = regenerate(d);
  ASSERT_TRUE(r.ok) << statusDump(r);
  EXPECT_EQ(r.shape.ShapeType(), TopAbs_SOLID);
  double V = vol(100, 40, 12, 60, 20, 6, 9, 15, 5, 1.5);
  EXPECT_NEAR(massProperties(r.shape).volume, V, V * 1e-4);
  for (const char* n : {"Hole1.h0.wall", "Hole1.h0.cb_wall", "Hole1.h0.cb_floor", "Pattern1.i5.Hole1.h0.cb_floor",
                        "Chamfer1.c0", "Chamfer1.c1", "Extrude1.side.e2", "Extrude2.side.e2", "Extrude2.end"})
    EXPECT_TRUE(hasName(r, n)) << n;
  ModelResult r2 = regenerate(d, {{"N", "8"}, {"Do", "140"}, {"Dbc", "110"}});
  ASSERT_TRUE(r2.ok) << statusDump(r2);
  double V2 = vol(140, 40, 12, 60, 20, 8, 9, 15, 5, 1.5);
  EXPECT_NEAR(massProperties(r2.shape).volume, V2, V2 * 1e-4);
  EXPECT_TRUE(hasName(r2, "Pattern1.i7.Hole1.h0.wall"));
  EXPECT_DOUBLE_EQ(infoOf(r2, "Pattern1")->values.at("count"), 8);
}
