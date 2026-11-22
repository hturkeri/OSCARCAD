// Named selections (SpaceClaim "Named Selections"): model + persistence + robustness.
// Design: docs/named_selections/MODEL.md. API: src/model/pc/NamedSelections.h, data: src/core/pc/NamedSelection.h.
//
// Every test names faces first, then applies an operation AFTER naming (or edits the history before it) and
// asserts: same name, the right face(s) by an independent geometric check, and the status.
#include "pc/AppPackage.h"
#include "pc/Document.h"
#include "pc/Expression.h"
#include "pc/Model.h"
#include "pc/NamedSelections.h"

#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>

using namespace pc;
using namespace pc::model;
namespace fs = std::filesystem;

namespace {

constexpr double kTol = 1e-5;

// ------------------------------------------------------------------ document helpers (as tests/test_model_v2.cpp)
std::string statusDump(const ModelResult& r) {
  std::string s;
  for (auto& st : r.status) s += st.featureId + (st.ok ? " ok " : " FAIL ") + st.message + "\n";
  return s;
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

Feature rectSketch(const std::string& id, SketchPlaneKind k, const std::string& ref, const std::string& x0,
                   const std::string& y0, const std::string& x1, const std::string& y1, double vx0, double vy0,
                   double vx1, double vy1) {
  Feature f = sketchFeature(id, k, ref);
  paramRect(*f.sketch, x0, y0, x1, y1, vx0, vy0, vx1, vy1);
  return f;
}
Feature rectSketch(const std::string& id, SketchPlaneKind k, const std::string& ref, double x0, double y0, double x1,
                   double y1) {
  return rectSketch(id, k, ref, formatNumber(x0), formatNumber(y0), formatNumber(x1), formatNumber(y1), x0, y0, x1, y1);
}

// circle of diameter expression `dia` centred at (cx, cy) expressions
Feature circleSketch(const std::string& id, SketchPlaneKind k, const std::string& ref, const std::string& cx,
                     const std::string& cy, double vx, double vy, double r, const std::string& dia = "") {
  Feature f = sketchFeature(id, k, ref);
  Sketch& s = *f.sketch;
  int c = s.addPoint(vx, vy);
  s.addConstraint(SkConstraintType::DistanceX, {0, c}, cx);
  s.addConstraint(SkConstraintType::DistanceY, {0, c}, cy);
  int e = s.addCircle(c, r);
  if (!dia.empty()) s.addConstraint(SkConstraintType::Diameter, {e}, dia);
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

// Plate W x D x T centred at the origin: Sketch1 + Extrude1. Lines: e5 (y=-D/2), e6 (x=+W/2), e7 (y=+D/2), e8 (x=-W/2).
Document plateDoc(double W = 80, double D = 50, double T = 10) {
  Document d;
  d.params.add({"W", formatNumber(W)});
  d.params.add({"D", formatNumber(D)});
  d.params.add({"T", formatNumber(T)});
  d.features.push_back(rectSketch("Sketch1", SketchPlaneKind::XY, "", "-W/2", "-D/2", "W/2", "D/2", -W / 2, -D / 2,
                                  W / 2, D / 2));
  d.features.push_back(feat("Extrude1", FeatureType::Extrude, {{"sketch", "Sketch1"}, {"distance", "T"}}));
  return d;
}

// XY rectangle extruded from z0 by h
void addBox(Document& d, const std::string& sid, const std::string& eid, double x0, double y0, double x1, double y1,
            double z0, double h, std::map<std::string, std::string> props, int at = -1) {
  Feature s = rectSketch(sid, SketchPlaneKind::XY, "", x0, y0, x1, y1);
  s.sketch->plane.offsetExpr = formatNumber(z0);
  props["sketch"] = sid;
  if (!props.count("distance")) props["distance"] = formatNumber(h);
  Feature e = feat(eid, FeatureType::Extrude, props);
  if (at < 0) {
    d.features.push_back(s);
    d.features.push_back(e);
  } else {
    d.features.insert(d.features.begin() + at, e);
    d.features.insert(d.features.begin() + at, s);
  }
}

const char* E6TOP = "edge(Extrude1.end|Extrude1.side.e6)";

ModelResult regenOk(const Document& d, const std::map<std::string, std::string>& ov = {}) {
  ModelResult r = regenerate(d, ov);
  EXPECT_TRUE(r.ok) << statusDump(r);
  return r;
}

void nameIt(Document& d, const ModelResult& r, const std::string& n, std::vector<ns::Ref> refs, int stop = -1) {
  std::string err;
  ASSERT_TRUE(ns::create(d, n, refs, r, stop, &err)) << err;
}

NsResolution res(const Document& d, const ModelResult& r, const std::string& n, int stop = -1) {
  const NamedSelection* s = d.namedSelection(n);
  EXPECT_NE(s, nullptr) << n;
  if (!s) return {};
  return resolve(d, *s, r, stop);
}

double area(const ModelResult& r, const std::vector<std::string>& faces) {
  double a = 0;
  for (auto& f : faces) a += faceSignature(r, f).size;
  return a;
}

// Every face: planar, outward normal n, plane at signed distance `off` (= n . point).
void expectPlanar(const ModelResult& r, const std::vector<std::string>& faces, gp_Dir n, double off,
                  const std::string& tag) {
  ASSERT_FALSE(faces.empty()) << tag;
  for (auto& f : faces) {
    NsSignature s = faceSignature(r, f);
    ASSERT_TRUE(s.valid) << tag << " " << f;
    EXPECT_EQ(s.surface, "plane") << tag << " " << f;
    EXPECT_NEAR(s.nx, n.X(), 1e-6) << tag << " " << f;
    EXPECT_NEAR(s.ny, n.Y(), 1e-6) << tag << " " << f;
    EXPECT_NEAR(s.nz, n.Z(), 1e-6) << tag << " " << f;
    EXPECT_NEAR(s.cx * n.X() + s.cy * n.Y() + s.cz * n.Z(), off, 1e-5) << tag << " " << f;
  }
}

void expectCylinders(const ModelResult& r, const std::vector<std::string>& faces, double radius, const std::string& tag) {
  ASSERT_FALSE(faces.empty()) << tag;
  for (auto& f : faces) {
    NsSignature s = faceSignature(r, f);
    EXPECT_EQ(s.surface, "cylinder") << tag << " " << f;
    EXPECT_NEAR(s.radius, radius, 1e-6) << tag << " " << f;
  }
}

std::string dump(const NsResolution& x) {
  std::string s = x.name + " [" + toString(x.status) + "] ";
  for (auto& f : x.faces) s += f + " ";
  return s + "| " + x.diagnostic;
}

// The named plate: "Ust" = top (z=T), "Yan" = +X side, "Alt" = bottom.
Document namedPlate(ModelResult* out = nullptr) {
  Document d = plateDoc();
  ModelResult r = regenOk(d);
  nameIt(d, r, "Ust", {ns::face("Extrude1.end")});
  nameIt(d, r, "Yan", {ns::face("Extrude1.side.e6")});
  nameIt(d, r, "Alt", {ns::face("Extrude1.start")});
  if (out) *out = r;
  return d;
}

void expectPlateUntouched(const Document& d, const ModelResult& r, const std::string& tag, double T = 10) {
  auto u = res(d, r, "Ust");
  EXPECT_EQ(u.status, NsStatus::Ok) << tag << " " << dump(u);
  expectPlanar(r, u.faces, gp_Dir(0, 0, 1), T, tag + " Ust");
  auto a = res(d, r, "Alt");
  EXPECT_EQ(a.status, NsStatus::Ok) << tag << " " << dump(a);
  expectPlanar(r, a.faces, gp_Dir(0, 0, -1), 0, tag + " Alt");
}

}  // namespace

// ================================================================== persistence / data model
TEST(NamedSelectionData, NameRulesAndEditing) {
  ModelResult r;
  Document d = namedPlate(&r);
  std::string err;
  EXPECT_FALSE(ns::create(d, "ust", {ns::face("Extrude1.start")}, r, -1, &err));  // case-insensitive duplicate
  EXPECT_NE(err.find("zaten var"), std::string::npos) << err;
  EXPECT_FALSE(ns::create(d, "", {}, r, -1, &err));
  EXPECT_FALSE(ns::create(d, "a\tb", {}, r, -1, &err));
  EXPECT_FALSE(ns::create(d, "Yok", {ns::face("Extrude9.end")}, r, -1, &err));
  EXPECT_NE(err.find("yüz bulunamadı"), std::string::npos) << err;
  EXPECT_EQ(d.namedSelections.size(), 3u);  // failed creates change nothing
  EXPECT_TRUE(ns::create(d, "  Giriş yüzeyi ", {}, r, -1, &err)) << err;  // trimmed, UTF-8 ok, empty ok
  EXPECT_NE(d.namedSelection("Giriş yüzeyi"), nullptr);
  EXPECT_EQ(res(d, r, "Giriş yüzeyi").status, NsStatus::Empty);
  // ids are stable and unique; rename keeps the id
  std::string id = d.namedSelection("Ust")->id;
  EXPECT_TRUE(ns::rename(d, "Ust", "Üst yüz", &err)) << err;
  EXPECT_EQ(d.namedSelection("Üst yüz")->id, id);
  EXPECT_FALSE(ns::rename(d, "Üst yüz", "YAN", &err));
  EXPECT_TRUE(ns::rename(d, "Üst yüz", "ÜST YÜZ", &err)) << err;  // case change of itself is fine
  EXPECT_FALSE(ns::setColor(d, "Yan", "red", &err));
  EXPECT_TRUE(ns::setColor(d, "Yan", "#112233", &err));
  EXPECT_TRUE(ns::setDescription(d, "Yan", "sıcak duvar", &err));
  EXPECT_TRUE(ns::remove(d, "Alt", &err));
  EXPECT_FALSE(ns::remove(d, "Alt", &err));
  std::string id2;
  EXPECT_TRUE(ns::create(d, "Alt", {ns::face("Extrude1.start")}, r, -1, &err, &id2));
  EXPECT_NE(id2, id);
  std::set<std::string> ids;
  for (auto& s : d.namedSelections) EXPECT_TRUE(ids.insert(s.id).second);
  EXPECT_EQ(ns::suggestName(d), "Grup1");
  EXPECT_TRUE(ns::move(d, "Alt", 0, &err));
  EXPECT_EQ(d.namedSelections.front().name, "Alt");
  // created-at bookkeeping
  EXPECT_EQ(d.namedSelection("Alt")->createdAfter, "Extrude1");
  EXPECT_EQ(d.namedSelection("Alt")->createdAt, 1);
}

TEST(NamedSelectionData, SaveReopenRoundTripAndBackwardCompat) {
  ModelResult r;
  Document d = namedPlate(&r);
  std::string err;
  ASSERT_TRUE(ns::create(d, "Kenar", {ns::edge(E6TOP)}, r, -1, &err)) << err;
  ASSERT_TRUE(ns::create(d, "Plaka", {ns::body("Extrude1")}, r, -1, &err)) << err;
  ASSERT_TRUE(ns::setExportStep(d, "Kenar", false, &err));
  fs::path p = fs::temp_directory_path() / "pc_ns_roundtrip.pcad";
  ASSERT_TRUE(saveDocument(d, p.string(), &err)) << err;
  Document d2;
  ASSERT_TRUE(loadDocument(d2, p.string(), &err)) << err;
  EXPECT_EQ(nlohmann::json(toJson(d)), nlohmann::json(toJson(d2)));
  ASSERT_EQ(d2.namedSelections.size(), 5u);
  EXPECT_FALSE(d2.namedSelection("Kenar")->exportStep);
  EXPECT_EQ(d2.namedSelection("Plaka")->members[0].kind, NsMemberKind::Body);
  EXPECT_FALSE(d2.namedSelection("Plaka")->members[0].anchors.empty());
  ModelResult r2 = regenOk(d2);
  auto a = resolveAll(d, r), b = resolveAll(d2, r2);
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].name, b[i].name);
    EXPECT_EQ(a[i].status, b[i].status);
    EXPECT_EQ(a[i].faces, b[i].faces);
    EXPECT_EQ(a[i].edges, b[i].edges);
  }
  fs::remove(p);
  // old files: no key -> no selections, and saving writes no key (byte-compatible output)
  Document old;
  ASSERT_TRUE(loadDocument(old, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  EXPECT_TRUE(old.namedSelections.empty());
  EXPECT_FALSE(toJson(old).contains("namedSelections"));
  // malformed / duplicate entries are rejected with a message
  nlohmann::json j = toJson(d);
  j["namedSelections"].push_back(j["namedSelections"][0]);
  EXPECT_THROW(documentFromJson(j), std::runtime_error);
  // a minimal hand-written selection (no id, no signature) loads and resolves
  nlohmann::json j2 = toJson(plateDoc());
  j2["namedSelections"] = nlohmann::json::array({{{"name", "El"}, {"members", {{{"kind", "face"}, {"ref", "Extrude1.end"}}}}}});
  Document d3 = documentFromJson(j2);
  ASSERT_EQ(d3.namedSelections.size(), 1u);
  EXPECT_EQ(d3.namedSelections[0].id, "ns1");
  EXPECT_EQ(res(d3, regenOk(d3), "El").status, NsStatus::Ok);
  // a result without lineage (hand-built) degrades to exact names instead of losing everything
  ModelResult bare = regenOk(d);
  bare.faceLineage.clear();
  EXPECT_EQ(res(d, bare, "Ust").status, NsStatus::Ok);
  EXPECT_EQ(res(d, bare, "Ust").faces, std::vector<std::string>{"Extrude1.end"});
}

// ================================================================== operation matrix after naming
TEST(NamedSelectionOps, ExtrudeNewBodyFromSketchOnNamedFaceEdge) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  // datum plane normal to the named top face's +X edge, sketch on it, pull a NEW body
  d.features.push_back(feat("Plane1", FeatureType::Plane, {{"kind", "edgeAngle"}, {"angle", "0"}}, {E6TOP}));
  Feature sk = rectSketch("Sketch2", SketchPlaneKind::OnPlane, "Plane1", -10, 0, 10, 15);
  d.features.push_back(sk);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "5"}, {"op", "new"}, {"bodyName", "Kulak"}}));
  ModelResult r = regenOk(d);
  ASSERT_EQ(r.bodies.size(), 2u);
  expectPlateUntouched(d, r, "new body");
  auto y = res(d, r, "Yan");
  EXPECT_EQ(y.status, NsStatus::Ok) << dump(y);
  expectPlanar(r, y.faces, gp_Dir(1, 0, 0), 40, "Yan");
  EXPECT_EQ(y.bodies, std::vector<std::string>{"Extrude1"});
  for (auto& f : res(d, r, "Ust").faces) EXPECT_EQ(f.rfind("Extrude2", 0), std::string::npos);
  // same pull with op=join on the top face (sketch ON the named face): the face is only trimmed
  Document d2 = d;
  d2.features.resize(2);
  d2.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", -10, -10, 10, 10));
  d2.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "5"}, {"op", "join"}}));
  ModelResult r2 = regenOk(d2);
  auto u = res(d2, r2, "Ust");
  EXPECT_EQ(u.status, NsStatus::Ok) << dump(u);
  EXPECT_EQ(u.faces, std::vector<std::string>{"Extrude1.end"});
  expectPlanar(r2, u.faces, gp_Dir(0, 0, 1), 10, "Ust join");
  EXPECT_NEAR(area(r2, u.faces), 80 * 50 - 20 * 20, 1e-6);
}

TEST(NamedSelectionOps, SketchOnNamedFaceCutSplitsFace) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  // slot sketched ON the named face, cut 3 mm deep across the whole face -> the face splits in two
  d.features.push_back(rectSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", -5, -40, 5, 40));
  d.features.push_back(feat("Extrude2", FeatureType::Extrude,
                            {{"sketch", "Sketch2"}, {"distance", "3"}, {"op", "cut"}, {"direction", "reverse"}}));
  ModelResult r = regenOk(d);
  auto u = res(d, r, "Ust");
  EXPECT_EQ(u.status, NsStatus::Split) << dump(u);
  ASSERT_EQ(u.faces.size(), 2u);
  expectPlanar(r, u.faces, gp_Dir(0, 0, 1), 10, "Ust split");
  EXPECT_NEAR(area(r, u.faces), 70 * 50, 1e-6);
  // the slot floor (also +Z, other height) is NOT in the selection
  for (auto& f : u.faces) EXPECT_EQ(f.rfind("Extrude1.end", 0), 0u) << f;
  EXPECT_EQ(res(d, r, "Alt").status, NsStatus::Ok);
  // through cut: the BODY splits too; the name still covers both pieces, one per body
  d.features.back().props["through"] = "1";
  ModelResult r2 = regenOk(d);
  ASSERT_EQ(r2.bodies.size(), 2u);
  for (auto n : {"Ust", "Alt"}) {
    auto x = res(d, r2, n);
    EXPECT_EQ(x.status, NsStatus::Split) << dump(x);
    EXPECT_EQ(x.faces.size(), 2u);
    EXPECT_EQ(x.bodies, (std::vector<std::string>{"Extrude1:1", "Extrude1:2"}));
  }
  expectPlanar(r2, res(d, r2, "Ust").faces, gp_Dir(0, 0, 1), 10, "Ust through");
  EXPECT_NEAR(area(r2, res(d, r2, "Ust").faces), 70 * 50, 1e-6);
}

TEST(NamedSelectionOps, FilletAndChamferOnNamedFaceEdges) {
  for (bool fillet : {true, false}) {
    ModelResult r0;
    Document d = namedPlate(&r0);
    std::string err;
    ASSERT_TRUE(ns::create(d, "UstKenar", {ns::edge(E6TOP)}, r0, -1, &err)) << err;
    ASSERT_TRUE(ns::create(d, "OnKenar", {ns::edge("edge(Extrude1.end|Extrude1.side.e5)")}, r0, -1, &err)) << err;
    d.features.push_back(fillet ? feat("Fillet1", FeatureType::Fillet, {{"radius", "2"}}, {E6TOP})
                                : feat("Chamfer1", FeatureType::Chamfer, {{"distance", "2"}}, {E6TOP}));
    ModelResult r = regenOk(d);
    const std::string tag = fillet ? "fillet" : "chamfer";
    expectPlateUntouched(d, r, tag);
    auto u = res(d, r, "Ust"), y = res(d, r, "Yan");
    EXPECT_NEAR(area(r, u.faces), 78 * 50, 1e-6) << tag;   // trimmed by 2 mm
    EXPECT_EQ(y.status, NsStatus::Ok) << dump(y);
    expectPlanar(r, y.faces, gp_Dir(1, 0, 0), 40, tag + " Yan");
    EXPECT_NEAR(area(r, y.faces), 50 * 8, 1e-6) << tag;
    for (auto& f : u.faces) EXPECT_TRUE(f.find(".f") == std::string::npos && f.find(".c") == std::string::npos) << f;
    // the blended edge itself is consumed -> lost, never re-bound; the other edge is untouched
    auto k = res(d, r, "UstKenar");
    EXPECT_EQ(k.status, NsStatus::Lost) << dump(k);
    EXPECT_TRUE(k.edges.empty());
    EXPECT_FALSE(k.diagnostic.empty());
    auto o = res(d, r, "OnKenar");
    EXPECT_TRUE(isResolved(o.status)) << dump(o);
    EXPECT_FALSE(o.edges.empty());
  }
}

TEST(NamedSelectionOps, ShellRemovingAnotherFace) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  // remove the +X side; named top/bottom stay, the new inner faces are not added
  d.features.push_back(feat("Shell1", FeatureType::Shell, {{"thickness", "2"}}, {"Extrude1.side.e6"}));
  ModelResult r = regenOk(d);
  expectPlateUntouched(d, r, "shell");
  for (auto n : {"Ust", "Alt"})
    for (auto& f : res(d, r, n).faces) EXPECT_EQ(f.find("Shell1"), std::string::npos) << f;
  EXPECT_NEAR(area(r, res(d, r, "Ust").faces), 80 * 50, 1e-6);
  // the removed face: OCCT turns it into the rim lying in its plane -> the name follows the rim (x = +40)
  auto y = res(d, r, "Yan");
  EXPECT_TRUE(isResolved(y.status)) << dump(y);
  expectPlanar(r, y.faces, gp_Dir(1, 0, 0), 40, "Yan rim");
  EXPECT_NEAR(area(r, y.faces), 50 * 10 - 46 * 6, 1e-6);
}

TEST(NamedSelectionOps, HoleThroughNamedFaceAndDiameterEdit) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.params.add({"Dh", "8"});
  d.features.push_back(circleSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", "10", "5", 10, 5, 4));
  d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "Dh"}, {"through", "1"}}));
  ModelResult r = regenOk(d);
  nameIt(d, r, "Delik", {ns::face("Hole1.h0.wall")});
  for (double dh : {8.0, 3.0, 15.0}) {
    ModelResult rr = regenOk(d, {{"Dh", formatNumber(dh)}});
    expectPlateUntouched(d, rr, "hole");
    EXPECT_NEAR(area(rr, res(d, rr, "Ust").faces), 80 * 50 - M_PI * dh * dh / 4, 1e-4);
    auto h = res(d, rr, "Delik");
    EXPECT_EQ(h.status, NsStatus::Ok) << dump(h);
    expectCylinders(rr, h.faces, dh / 2, "Delik");
  }
}

TEST(NamedSelectionOps, LinearAndCircularPatternCountEdits) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.params.add({"N", "3"});
  d.features.push_back(circleSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", "-30", "0", -30, 0, 3));
  d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "6"}, {"through", "1"}}));
  ModelResult r1 = regenOk(d);
  nameIt(d, r1, "IlkDelik", {ns::face("Hole1.h0.wall")});
  d.features.push_back(feat("Pattern1", FeatureType::Pattern, {{"kind", "linear"}, {"source", "Hole1"}, {"count", "N"}, {"spacing", "12"}}));
  for (int n : {3, 1, 5, 2}) {
    ModelResult r = regenOk(d, {{"N", std::to_string(n)}});
    expectPlateUntouched(d, r, "linear");
    EXPECT_NEAR(area(r, res(d, r, "Ust").faces), 80 * 50 - n * M_PI * 9, 1e-4) << n;
    auto h = res(d, r, "IlkDelik");
    EXPECT_EQ(h.status, NsStatus::Ok) << dump(h);
    ASSERT_EQ(h.faces.size(), 1u);
    NsSignature s = faceSignature(r, h.faces[0]);
    EXPECT_NEAR(s.cx, -30, 1e-6);  // the ORIGINAL hole, never a pattern instance
  }
  // circular pattern of a hole at (15, 0)
  Document c = namedPlate(&r0);
  c.features.push_back(circleSketch("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end", "15", "0", 15, 0, 2));
  c.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "4"}, {"through", "1"}}));
  c.features.push_back(feat("Pattern1", FeatureType::Pattern, {{"kind", "circular"}, {"source", "Hole1"}, {"count", "6"}, {"axis", "Z"}}));
  ModelResult rc = regenOk(c);
  expectPlateUntouched(c, rc, "circular");
  EXPECT_NEAR(area(rc, res(c, rc, "Ust").faces), 80 * 50 - 6 * M_PI * 4, 1e-4);
}

TEST(NamedSelectionOps, BodyBooleansAddSubtractSeparateCombine) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  // Add: a box joined against the +X side covers part of it
  addBox(d, "Sketch2", "Extrude2", 40, -10, 60, 10, 0, 10, {{"op", "join"}});
  ModelResult r = regenOk(d);
  ASSERT_EQ(r.bodies.size(), 1u);
  expectPlateUntouched(d, r, "add");
  auto y = res(d, r, "Yan");
  EXPECT_TRUE(isResolved(y.status)) << dump(y);
  expectPlanar(r, y.faces, gp_Dir(1, 0, 0), 40, "Yan add");
  EXPECT_NEAR(area(r, y.faces), 50 * 10 - 20 * 10, 1e-6);
  // Subtract: notch through the top
  addBox(d, "Sketch3", "Extrude3", -10, -30, 10, -15, 5, 10, {{"op", "cut"}});
  ModelResult r2 = regenOk(d);
  expectPlateUntouched(d, r2, "subtract");
  EXPECT_NEAR(area(r2, res(d, r2, "Ust").faces), 80 * 50 - 20 * 10, 1e-6);
  // Separate: cut with keepTool -> the cutter becomes its own body; the selection does not grow onto it
  addBox(d, "Sketch4", "Extrude4", -30, 10, -20, 20, 5, 10, {{"op", "cut"}, {"keepTool", "1"}, {"bodyName", "Tapa"}});
  ModelResult r3 = regenOk(d);
  ASSERT_EQ(r3.bodies.size(), 2u);
  auto u3 = res(d, r3, "Ust");
  EXPECT_EQ(u3.status, NsStatus::Ok) << dump(u3);
  EXPECT_EQ(u3.bodies, std::vector<std::string>{"Extrude1"});
  // Combine: a second body with its own named face + a body selection, merged into the plate
  addBox(d, "Sketch5", "Extrude5", -40, 25, 40, 35, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  ModelResult r4 = regenOk(d);
  std::string err;
  ASSERT_TRUE(ns::create(d, "KutuUst", {ns::face("Extrude5.end")}, r4, -1, &err)) << err;
  ASSERT_TRUE(ns::create(d, "KutuGovde", {ns::body("Kutu")}, r4, -1, &err)) << err;
  d.features.push_back(feat("Combine1", FeatureType::Combine, {{"op", "join"}, {"target", "Extrude1"}, {"tools", "Kutu"}}));
  ModelResult r5 = regenOk(d);
  auto ku = res(d, r5, "KutuUst");
  EXPECT_TRUE(isResolved(ku.status)) << dump(ku);
  expectPlanar(r5, ku.faces, gp_Dir(0, 0, 1), 10, "KutuUst combined");
  EXPECT_NEAR(area(r5, ku.faces), 80 * 10, 1e-6);
  EXPECT_EQ(ku.bodies, std::vector<std::string>{"Extrude1"});
  auto kg = res(d, r5, "KutuGovde");
  EXPECT_EQ(kg.status, NsStatus::Merged) << dump(kg);
  EXPECT_EQ(kg.bodies, std::vector<std::string>{"Extrude1"});
  expectPlateUntouched(d, r5, "combine");
  // Combine cut with keepTools=1: target trimmed, tool body kept; selections unchanged
  d.features.back().props = {{"op", "cut"}, {"target", "Extrude1"}, {"tools", "Kutu"}, {"keepTools", "1"}};
  ModelResult r6 = regenOk(d);
  auto ku6 = res(d, r6, "KutuUst");
  EXPECT_EQ(ku6.status, NsStatus::Ok) << dump(ku6);
  EXPECT_EQ(ku6.bodies, std::vector<std::string>{"Kutu"});
  EXPECT_EQ(res(d, r6, "KutuGovde").status, NsStatus::Ok);
}

TEST(NamedSelectionOps, SplitBodyAndBodyMembers) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  nameIt(d, r0, "Plaka", {ns::body("Extrude1")});
  addBox(d, "Sketch2", "Extrude2", -5, -50, 5, 50, -1, 12, {{"op", "cut"}});
  ModelResult r = regenOk(d);
  auto p = res(d, r, "Plaka");
  EXPECT_EQ(p.status, NsStatus::Split) << dump(p);
  EXPECT_EQ(p.bodies, (std::vector<std::string>{"Extrude1:1", "Extrude1:2"}));
  EXPECT_EQ(p.faces.size(), r.faces.size());
  auto y = res(d, r, "Yan");
  EXPECT_EQ(y.status, NsStatus::Ok) << dump(y);
  EXPECT_EQ(y.bodies, std::vector<std::string>{"Extrude1:2"});  // +X side lives on the +X piece
  expectPlanar(r, y.faces, gp_Dir(1, 0, 0), 40, "Yan split body");
}

TEST(NamedSelectionOps, MoveTransformAndCopy) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.features.push_back(feat("Transform1", FeatureType::Transform, {{"tz", "20"}, {"axis", "Z"}, {"angle", "90"}}));
  ModelResult r = regenOk(d);
  auto u = res(d, r, "Ust"), y = res(d, r, "Yan");
  EXPECT_EQ(u.status, NsStatus::Ok) << dump(u);
  EXPECT_EQ(y.status, NsStatus::Ok) << dump(y);
  expectPlanar(r, u.faces, gp_Dir(0, 0, 1), 30, "Ust moved");
  expectPlanar(r, y.faces, gp_Dir(0, 1, 0), 40, "Yan rotated");  // +X side rotated 90 deg about Z -> +Y
  // later features still find the moved faces; copy=1 does not extend the selection to the copy
  d.features.push_back(feat("Transform2", FeatureType::Transform, {{"tx", "100"}, {"copy", "1"}}));
  ModelResult r2 = regenOk(d);
  ASSERT_EQ(r2.bodies.size(), 2u);
  EXPECT_TRUE(r2.faces.count("Transform2.Extrude1.end"));
  auto u2 = res(d, r2, "Ust");
  EXPECT_EQ(u2.status, NsStatus::Ok) << dump(u2);
  EXPECT_EQ(u2.faces, std::vector<std::string>{"Extrude1.end"});
  expectPlanar(r2, u2.faces, gp_Dir(0, 0, 1), 30, "Ust after copy");
}

TEST(NamedSelectionOps, RevolveAngleAndRadiusEdits) {
  Document d;
  d.params.add({"Ri", "10"});
  d.params.add({"Ro", "20"});
  d.params.add({"A", "360"});
  d.features.push_back(rectSketch("Sketch1", SketchPlaneKind::XZ, "", "Ri", "0", "Ro", "30", 10, 0, 20, 30));
  d.features.push_back(feat("Revolve1", FeatureType::Revolve, {{"sketch", "Sketch1"}, {"axis", "V"}, {"angle", "A"}}));
  ModelResult r = regenOk(d);
  nameIt(d, r, "DisCidar", {ns::face("Revolve1.rev.e6")});
  nameIt(d, r, "IcCidar", {ns::face("Revolve1.rev.e8")});
  for (auto [ri, ro, a] : std::vector<std::tuple<double, double, double>>{{10, 20, 360}, {5, 40, 270}, {12, 14, 90}}) {
    ModelResult rr = regenOk(d, {{"Ri", formatNumber(ri)}, {"Ro", formatNumber(ro)}, {"A", formatNumber(a)}});
    auto o = res(d, rr, "DisCidar"), i = res(d, rr, "IcCidar");
    EXPECT_EQ(o.status, NsStatus::Ok) << dump(o);
    EXPECT_EQ(i.status, NsStatus::Ok) << dump(i);
    expectCylinders(rr, o.faces, ro, "outer");
    expectCylinders(rr, i.faces, ri, "inner");
  }
  // a later cut through the outer wall splits it
  addBox(d, "Sketch2", "Extrude2", -50, -2, 50, 2, -1, 40, {{"op", "cut"}});
  ModelResult r2 = regenOk(d);
  auto o2 = res(d, r2, "DisCidar");
  EXPECT_EQ(o2.status, NsStatus::Split) << dump(o2);
  expectCylinders(r2, o2.faces, 20, "outer split");
}

TEST(NamedSelectionOps, InsertEarlierFeatureSuppressDeleteRollback) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  addBox(d, "Sketch2", "Extrude2", -10, -10, 10, 10, 10, 15, {{"op", "join"}});  // boss
  ModelResult r1 = regenOk(d);
  nameIt(d, r1, "BossUst", {ns::face("Extrude2.end")});
  // insert a slot BEFORE the boss (index 2): the plate top splits; boss untouched
  addBox(d, "Sketch3", "Extrude3", 25, -40, 30, 40, 7, 10, {{"op", "cut"}}, 2);
  ModelResult r2 = regenOk(d);
  auto u = res(d, r2, "Ust");
  EXPECT_EQ(u.status, NsStatus::Split) << dump(u);
  expectPlanar(r2, u.faces, gp_Dir(0, 0, 1), 10, "Ust inserted slot");
  EXPECT_NEAR(area(r2, u.faces), 80 * 50 - 5 * 50 - 20 * 20, 1e-6);
  auto b = res(d, r2, "BossUst");
  EXPECT_EQ(b.status, NsStatus::Ok) << dump(b);
  expectPlanar(r2, b.faces, gp_Dir(0, 0, 1), 25, "BossUst");
  // suppress the slot -> top whole again; unsuppress -> split again
  d.feature("Extrude3")->suppressed = true;
  ModelResult r3 = regenOk(d);
  EXPECT_EQ(res(d, r3, "Ust").status, NsStatus::Ok);
  d.feature("Extrude3")->suppressed = false;
  // suppress the boss -> its selection is "suppressed" (not lost, not re-bound), comes back unchanged
  d.feature("Extrude2")->suppressed = true;
  ModelResult r4 = regenOk(d);
  auto b4 = res(d, r4, "BossUst");
  EXPECT_EQ(b4.status, NsStatus::Suppressed) << dump(b4);
  EXPECT_TRUE(b4.faces.empty());
  d.feature("Extrude2")->suppressed = false;
  ModelResult r5 = regenOk(d);
  EXPECT_EQ(res(d, r5, "BossUst").faces, b.faces);
  // rollback before the boss -> "not yet"
  int bossIdx = d.indexOf("Extrude2");
  ModelResult r6 = regenerate(d, d.evaluateParams(), bossIdx - 1);
  auto b6 = res(d, r6, "BossUst", bossIdx - 1);
  EXPECT_EQ(b6.status, NsStatus::NotYet) << dump(b6);
  EXPECT_TRUE(b6.rolledBack);
  // delete the boss -> lost with diagnostic, nothing re-bound
  Document dd = d;
  dd.features.erase(dd.features.begin() + bossIdx);
  dd.features.erase(dd.features.begin() + bossIdx - 1);
  ModelResult r7 = regenOk(dd);
  auto b7 = res(dd, r7, "BossUst");
  EXPECT_EQ(b7.status, NsStatus::Lost) << dump(b7);
  EXPECT_NE(b7.diagnostic.find("silinmiş"), std::string::npos) << b7.diagnostic;
  EXPECT_TRUE(b7.faces.empty());
}

TEST(NamedSelectionOps, ConsumedFaceIsLostNeverReassigned) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  // cut away the whole +X end of the plate: the +X side face disappears, a new wall appears at x = 30
  addBox(d, "Sketch2", "Extrude2", 30, -40, 60, 40, -1, 12, {{"op", "cut"}});
  ModelResult r = regenOk(d);
  auto y = res(d, r, "Yan");
  EXPECT_EQ(y.status, NsStatus::Lost) << dump(y);
  EXPECT_TRUE(y.faces.empty());
  EXPECT_NE(y.diagnostic.find("Extrude2"), std::string::npos) << y.diagnostic;
  EXPECT_EQ(y.members[0].suggestions.size() > 0, true);  // look-alikes are only SUGGESTED
  EXPECT_EQ(r.removedBy.at("Extrude1.side.e6"), "Extrude2");
  expectPlateUntouched(d, r, "consumed");
  // explicit user re-bind is the only way to move a name
  std::string err;
  ASSERT_TRUE(ns::rebindMember(d, "Yan", 0, ns::face(y.members[0].suggestions[0]), r, &err)) << err;
  auto y2 = res(d, r, "Yan");
  EXPECT_EQ(y2.status, NsStatus::Ok);
  expectPlanar(r, y2.faces, gp_Dir(1, 0, 0), 30, "rebound");
  // undo of the cut (delete it) with the original reference brings the original face back
  Document d0 = namedPlate(&r0);
  ModelResult rb = regenOk(d0);
  EXPECT_EQ(res(d0, rb, "Yan").status, NsStatus::Ok);
}

TEST(NamedSelectionOps, PieceMemberSurvivesRenumbering) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.params.add({"S", "0"});
  // slot at x = S splits the top into Extrude1.end:1 (left) and :2 (right)
  Feature sk = rectSketch("Sketch2", SketchPlaneKind::XY, "", "S-3", "-40", "S+3", "40", -3, -40, 3, 40);
  sk.sketch->plane.offsetExpr = "7";
  d.features.push_back(sk);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "10"}, {"op", "cut"}}));
  ModelResult r = regenOk(d);
  ASSERT_TRUE(r.faces.count("Extrude1.end:2"));
  nameIt(d, r, "SagParca", {ns::face("Extrude1.end:2")});
  auto check = [&](const ModelResult& rr, double s, const std::string& tag) {
    auto p = res(d, rr, "SagParca");
    EXPECT_TRUE(p.status == NsStatus::Ok || p.status == NsStatus::Split) << tag << " " << dump(p);
    expectPlanar(rr, p.faces, gp_Dir(0, 0, 1), 10, tag);
    for (auto& f : p.faces) EXPECT_GT(faceSignature(rr, f).cx, s) << tag << " " << f;  // right of the slot
    EXPECT_NEAR(area(rr, p.faces), (40 - s - 3) * 50, 1e-6) << tag;
  };
  for (double s : {0.0, 20.0, -25.0}) check(regenOk(d, {{"S", formatNumber(s)}}), s, "S=" + formatNumber(s));
  // an EARLIER slot (inserted before) adds a third piece on the left: ":2" now means the middle piece, but the
  // selection still resolves to the right piece only
  addBox(d, "Sketch3", "Extrude3", -25, -40, -20, 40, 7, 10, {{"op", "cut"}}, 2);
  ModelResult r2 = regenOk(d);
  check(r2, 0, "inserted earlier slot");
  // and when the splitting slot is suppressed, the piece re-joins its parent: name kept (Merged)
  d.feature("Extrude2")->suppressed = true;
  d.feature("Extrude3")->suppressed = true;
  ModelResult r3 = regenOk(d);
  auto p3 = res(d, r3, "SagParca");
  EXPECT_EQ(p3.status, NsStatus::Merged) << dump(p3);
  EXPECT_EQ(p3.faces, std::vector<std::string>{"Extrude1.end"});
}

TEST(NamedSelectionOps, RemoveOnePieceAndAddMembers) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  addBox(d, "Sketch2", "Extrude2", -3, -40, 3, 40, 7, 10, {{"op", "cut"}});
  ModelResult r = regenOk(d);
  ASSERT_EQ(res(d, r, "Ust").faces.size(), 2u);
  std::string err;
  // the user deselects the left piece in the UI: the member is replaced by the right piece only
  ASSERT_TRUE(ns::removeMembers(d, "Ust", {ns::face("Extrude1.end:1")}, r, -1, &err)) << err;
  auto u = res(d, r, "Ust");
  EXPECT_EQ(u.faces, std::vector<std::string>{"Extrude1.end:2"});
  // adding faces already covered is a no-op; new faces are appended
  ASSERT_TRUE(ns::addMembers(d, "Ust", {ns::face("Extrude1.end:2"), ns::face("Extrude1.side.e5")}, r, -1, &err)) << err;
  EXPECT_EQ(d.namedSelection("Ust")->members.size(), 2u);
  EXPECT_FALSE(ns::addMembers(d, "Ust", {ns::face("Nope.end")}, r, -1, &err));
  EXPECT_EQ(d.namedSelection("Ust")->members.size(), 2u);
  // removing by stored ref (e.g. a lost member)
  ASSERT_TRUE(ns::removeMembers(d, "Ust", {ns::face("Extrude1.side.e5")}, r, -1, &err)) << err;
  EXPECT_EQ(d.namedSelection("Ust")->members.size(), 1u);
}

TEST(NamedSelectionOps, UndoRedoBySnapshotsAndNotifications) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  NamedSelectionManager mgr;
  std::vector<NsChange> ev;
  int tok = mgr.subscribe([&](const NsChange& c) { ev.push_back(c); });
  mgr.update(d, r0);
  ASSERT_EQ(ev.size(), 3u);
  for (auto& e : ev) EXPECT_EQ(e.kind, NsChange::Kind::Added);
  EXPECT_EQ(mgr.selectionsOfFace("Extrude1.end"), std::vector<std::string>{"Ust"});
  EXPECT_EQ(mgr.selectionsOfFace(r0, r0.faces.at("Extrude1.side.e6")), std::vector<std::string>{"Yan"});
  // the designer's undo stack stores whole-document snapshots: emulate commit / undo / redo
  std::vector<Document> undo{d};
  std::string err;
  Document c = d;
  ASSERT_TRUE(ns::rename(c, "Ust", "Kapak", &err));
  undo.push_back(c);
  d = c;
  ev.clear();
  mgr.update(d, r0);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(ev[0].kind, NsChange::Kind::Renamed);
  EXPECT_EQ(ev[0].oldName, "Ust");
  EXPECT_EQ(ev[0].name, "Kapak");
  d = undo[0];  // undo
  ev.clear();
  mgr.update(d, r0);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(ev[0].name, "Ust");
  d = undo[1];  // redo
  mgr.update(d, r0);
  EXPECT_NE(mgr.find("Kapak"), nullptr);
  EXPECT_EQ(mgr.find("kapak")->faces, std::vector<std::string>{"Extrude1.end"});
  // geometry change -> status + faces notifications
  addBox(d, "Sketch2", "Extrude2", -3, -40, 3, 40, 7, 10, {{"op", "cut"}});
  ModelResult r = regenOk(d);
  ev.clear();
  mgr.update(d, r);
  bool st = false, fc = false;
  for (auto& e : ev) {
    EXPECT_EQ(e.name, "Kapak");
    st |= e.kind == NsChange::Kind::StatusChanged && e.oldStatus == NsStatus::Ok && e.newStatus == NsStatus::Split;
    fc |= e.kind == NsChange::Kind::FacesChanged && e.newFaces.size() == 2;
  }
  EXPECT_TRUE(st && fc);
  // no change -> no event; removal -> Removed
  ev.clear();
  mgr.update(d, r);
  EXPECT_TRUE(ev.empty());
  ASSERT_TRUE(ns::remove(d, "Yan", &err));
  mgr.update(d, r);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(ev[0].kind, NsChange::Kind::Removed);
  mgr.unsubscribe(tok);
  ASSERT_TRUE(ns::remove(d, "Alt", &err));
  ev.clear();
  mgr.update(d, r);
  EXPECT_TRUE(ev.empty());
}

TEST(NamedSelectionOps, RefreshSignaturesTracksLongEditChains) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.params.add({"S", "0"});
  Feature sk = rectSketch("Sketch2", SketchPlaneKind::XY, "", "S-3", "-40", "S+3", "40", -3, -40, 3, 40);
  sk.sketch->plane.offsetExpr = "7";
  d.features.push_back(sk);
  d.features.push_back(feat("Extrude2", FeatureType::Extrude, {{"sketch", "Sketch2"}, {"distance", "10"}, {"op", "cut"}}));
  ModelResult r = regenOk(d);
  nameIt(d, r, "Sol", {ns::face("Extrude1.end:1")});
  // walk the slot in small steps far to the right, refreshing like the UI does after each edit
  for (int k = 1; k <= 10; ++k) {
    double s = 3.0 * k;
    d.params.find("S")->expression = formatNumber(s);
    ModelResult rr = regenOk(d);
    auto all = resolveAll(d, rr);
    refreshSignatures(d, rr, all);
    auto p = res(d, rr, "Sol");
    ASSERT_EQ(p.faces.size(), 1u) << s;
    EXPECT_LT(faceSignature(rr, p.faces[0]).cx, s) << s;
    EXPECT_NEAR(area(rr, p.faces), (40 + s - 3) * 50, 1e-6);
  }
}

TEST(NamedSelectionOps, IndexedHoleNameReorderIsFlaggedNeverSwitched) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  d.params.add({"A", "20"});
  Feature sk = sketchFeature("Sketch2", SketchPlaneKind::OnFace, "Extrude1.end");
  Sketch& s = *sk.sketch;
  int p1 = s.addPoint(20, 0), p2 = s.addPoint(-20, 0);
  s.addConstraint(SkConstraintType::DistanceX, {0, p1}, "A");
  s.addConstraint(SkConstraintType::DistanceY, {0, p1}, "0");
  s.addConstraint(SkConstraintType::DistanceX, {0, p2}, "-20");
  s.addConstraint(SkConstraintType::DistanceY, {0, p2}, "0");
  d.features.push_back(sk);
  d.features.push_back(feat("Hole1", FeatureType::Hole, {{"sketch", "Sketch2"}, {"diameter", "6"}, {"through", "1"}}));
  ModelResult r = regenOk(d);
  nameIt(d, r, "SolDelik", {ns::face("Hole1.h0.wall")});   // the hole at x = -20 (smallest u -> h0)
  ASSERT_NEAR(faceSignature(r, "Hole1.h0.wall").cx, -20, 1e-6);
  ModelResult r1 = regenOk(d, {{"A", "25"}});
  EXPECT_EQ(res(d, r1, "SolDelik").status, NsStatus::Ok);
  // the moving hole passes the fixed one: "h0" now denotes the OTHER hole (contract order). The selection keeps
  // its reference (never switches by itself) but is flagged, and the right face is offered as a suggestion.
  ModelResult r2 = regenOk(d, {{"A", "-30"}});
  auto x = res(d, r2, "SolDelik");
  EXPECT_EQ(x.status, NsStatus::Suspect) << dump(x);
  EXPECT_EQ(x.faces, std::vector<std::string>{"Hole1.h0.wall"});
  ASSERT_EQ(x.members[0].suggestions, std::vector<std::string>{"Hole1.h1.wall"});
  EXPECT_NEAR(faceSignature(r2, x.members[0].suggestions[0]).cx, -20, 1e-6);
}

TEST(NamedSelectionOps, BodyRenameFollowsAndBodyMembers) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  addBox(d, "Sketch2", "Extrude2", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  ModelResult r = regenOk(d);
  nameIt(d, r, "KutuG", {ns::body("Kutu")});
  // what the designer does on "rename body": creator bodyName + references + named selections
  d.feature("Extrude2")->props["bodyName"] = "Blok";
  EXPECT_EQ(ns::renameBodyRefs(d, "Kutu", "Blok"), 1);
  ModelResult r2 = regenOk(d);
  auto k = res(d, r2, "KutuG");
  EXPECT_EQ(k.status, NsStatus::Ok) << dump(k);
  EXPECT_EQ(k.bodies, std::vector<std::string>{"Blok"});
  EXPECT_EQ(k.faces.size(), 6u);
  // without the rewrite the body is still found through its anchor faces (reported as Merged, never lost)
  Document d2 = d;
  d2.feature("Extrude2")->props["bodyName"] = "Baska";
  ModelResult r3 = regenOk(d2);
  auto k3 = res(d2, r3, "KutuG");
  EXPECT_EQ(k3.status, NsStatus::Merged) << dump(k3);
  EXPECT_EQ(k3.bodies, std::vector<std::string>{"Baska"});
}

// ================================================================== STEP export with names
TEST(NamedSelectionStep, FaceNamesBodiesColoursAndRoundTrip) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  std::string err;
  ASSERT_TRUE(ns::rename(d, "Ust", "Giriş", &err));      // Turkish -> "Giris" in STEP
  addBox(d, "Sketch2", "Extrude2", -3, -40, 3, 40, 7, 10, {{"op", "cut"}});   // split top
  addBox(d, "Sketch3", "Extrude3", 60, -10, 80, 10, 0, 10, {{"op", "new"}, {"bodyName", "Kutu"}});
  ModelResult r = regenOk(d);
  ASSERT_TRUE(ns::create(d, "kutu duvar", {ns::face("Extrude3.side.e6")}, r, -1, &err)) << err;
  fs::path p = fs::temp_directory_path() / "pc_ns_names.step";
  ASSERT_TRUE(exportSTEPNamed(d, r, p.string(), {}, &err)) << err;
  std::string txt;
  {  // close the stream before the file is rewritten/removed (Windows keeps open files locked)
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    txt = ss.str();
  }
  EXPECT_NE(txt.find("'Giris'"), std::string::npos);
  EXPECT_NE(txt.find("'kutu_duvar'"), std::string::npos);
  EXPECT_NE(txt.find("'Yan'"), std::string::npos);
  EXPECT_NE(txt.find("'Kutu'"), std::string::npos);
  StepNames sn;
  ASSERT_TRUE(readSTEPNames(p.string(), sn, &err)) << err;
  std::map<std::string, int> cnt;
  std::map<std::string, double> ar;
  for (auto& f : sn.faces) {
    cnt[f.name]++;
    GProp_GProps g;
    BRepGProp::SurfaceProperties(f.face, g);
    ar[f.name] += g.Mass();
  }
  EXPECT_EQ(cnt["Giris"], 2);   // both pieces of the split face carry the name
  EXPECT_NEAR(ar["Giris"], 74 * 50, 1e-3);
  EXPECT_EQ(cnt["Yan"], 1);
  EXPECT_EQ(cnt["Alt"], 1);
  EXPECT_EQ(cnt["kutu_duvar"], 1);
  EXPECT_EQ(sn.bodies.size(), 2u);
  EXPECT_TRUE(std::count(sn.bodies.begin(), sn.bodies.end(), "Kutu"));
  // body filter + persistent names for the other faces, no colours, unicode names kept
  StepExportOptions o;
  o.bodies = {"Kutu"};
  o.persistentFaceNames = true;
  o.colors = false;
  ASSERT_TRUE(exportSTEPNamed(d, r, p.string(), o, &err)) << err;
  ASSERT_TRUE(readSTEPNames(p.string(), sn, &err)) << err;
  EXPECT_EQ(sn.bodies, std::vector<std::string>{"Kutu"});
  EXPECT_EQ(sn.faces.size(), 6u);   // every face of the box named
  int kd = 0;
  for (auto& f : sn.faces) kd += f.name == "kutu_duvar";
  EXPECT_EQ(kd, 1);
  EXPECT_EQ(stepName("Giriş-1 sıcak"), "Giris-1_sicak");
  EXPECT_EQ(stepName("ÇĞİÖŞÜ"), "CGIOSU");
  std::error_code ec;
  fs::remove(p, ec);
}

// ================================================================== Save as App + runner
TEST(NamedSelectionApp, SurvivesSaveAsAppAndParameterChanges) {
  ModelResult r0;
  Document d = namedPlate(&r0);
  for (auto n : {"W", "D", "T"}) {
    Parameter* p = d.params.find(n);
    p->exposed = true;
    p->minValue = 5;
    p->maxValue = 200;
  }
  pc::app::AppDefinition a;
  a.appName = "Plaka";
  a.document = d;
  fs::path pk = fs::temp_directory_path() / "pc_ns_app.pcapp";
  std::string err;
  ASSERT_TRUE(pc::app::savePackage(a, pk.string(), &err)) << err;
  auto b = pc::app::loadPackage(pk.string(), &err);
  ASSERT_TRUE(b) << err;
  ASSERT_EQ(b->document.namedSelections.size(), 3u);
  for (auto [w, t] : std::vector<std::pair<double, double>>{{120, 4}, {40, 25}}) {
    ModelResult r = regenOk(b->document, {{"W", formatNumber(w)}, {"T", formatNumber(t)}});
    expectPlateUntouched(b->document, r, "app", t);
    auto y = res(b->document, r, "Yan");
    expectPlanar(r, y.faces, gp_Dir(1, 0, 0), w / 2, "app Yan");
  }
#ifdef PC_RUNNER_EXE
  if (fs::exists(PC_RUNNER_EXE)) {
    fs::path js = fs::temp_directory_path() / "pc_ns_runner.json";
    fs::path st = fs::temp_directory_path() / "pc_ns_runner.step";
    std::string cmd = std::string("\"") + PC_RUNNER_EXE + "\" \"" + pk.string() + "\" --set W=120 --set T=4 --named-selections --export-step \"" +
                      st.string() + "\" > \"" + js.string() + "\" 2>/dev/null";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << cmd;
    std::ifstream in(js);
    nlohmann::json j = nlohmann::json::parse(in);
    ASSERT_EQ(j["selections"].size(), 3u);
    EXPECT_EQ(j["selections"][0]["name"], "Ust");
    EXPECT_EQ(j["selections"][0]["status"], "ok");
    EXPECT_EQ(j["selections"][0]["faces"], nlohmann::json::array({"Extrude1.end"}));
    StepNames sn;
    ASSERT_TRUE(readSTEPNames(st.string(), sn, &err)) << err;
    bool found = false;
    for (auto& f : sn.faces)
      if (f.name == "Yan") {
        GProp_GProps g;
        BRepGProp::SurfaceProperties(f.face, g);
        EXPECT_NEAR(g.CentreOfMass().X(), 60, 1e-6);   // W=120 applied
        EXPECT_NEAR(g.Mass(), 50 * 4, 1e-6);           // T=4 applied
        found = true;
      }
    EXPECT_TRUE(found);
    fs::remove(js);
    fs::remove(st);
  }
#endif
  fs::remove(pk);
}

// ================================================================== randomized regression on the samples
namespace {
double paramMinMax(const Parameter& p, std::mt19937& g) {
  std::uniform_real_distribution<double> u(p.minValue, p.maxValue);
  return u(g);
}
}  // namespace

TEST(NamedSelectionRandom, BracketParameterSweepsKeepNames) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/bracket.pcad", &err)) << err;
  ModelResult r0 = regenOk(d);
  std::vector<ns::Ref> holes;
  for (auto& [n, f] : r0.faces)
    if (n.rfind("Hole1.h", 0) == 0 && n.find(".wall") != std::string::npos) holes.push_back(ns::face(n));
  ASSERT_EQ(holes.size(), 4u);
  nameIt(d, r0, "PlakaUst", {ns::face("Extrude1.end")});
  nameIt(d, r0, "BaglantiDelikleri", holes);
  nameIt(d, r0, "BossUst", {ns::face("Extrude2.end")});
  nameIt(d, r0, "Bore", {ns::face("Hole2.h0.wall")});
  nameIt(d, r0, "Taban", {ns::face("Extrude1.start")});
  std::mt19937 g(20261116);
  int tested = 0;
  for (int it = 0; it < 40; ++it) {
    std::map<std::string, std::string> ov;
    std::map<std::string, double> v;
    for (auto n : {"W", "D", "T", "R", "E", "Dh", "Db", "Hb", "Dbore"}) {
      const Parameter* p = d.params.find(n);
      v[n] = paramMinMax(*p, g);
    }
    // keep the random part valid (the generator also produces impossible combinations)
    v["D"] = std::max(v["D"], v["Db"] + 4 * v["R"] + 4);
    v["W"] = std::max(v["W"], v["Db"] + 4 * v["E"] + 30);
    v["D"] = std::max(v["D"], 2 * v["E"] + 2 * v["Dh"] + 4);
    v["Dbore"] = std::min(v["Dbore"], v["Db"] - 2);
    v["Dh"] = std::min(v["Dh"], 8.0);
    v["R"] = std::min(v["R"], 8.0);
    v["E"] = std::max(3.0, std::min(v["E"], 12.0));
    v["E"] = std::max(v["E"], v["R"] + v["Dh"] / 2 + 1);   // holes inside the material, clear of the corner fillets
    for (auto& [k, x] : v) ov[k] = formatNumber(x);
    ModelResult r = regenerate(d, ov);
    if (!r.ok) continue;
    ++tested;
    const std::string tag = "it " + std::to_string(it);
    const double T = d.evaluateParams(ov).at("T"), Hb = v["Hb"];
    auto a = res(d, r, "PlakaUst");
    EXPECT_TRUE(a.status == NsStatus::Ok) << tag << dump(a);
    expectPlanar(r, a.faces, gp_Dir(0, 0, 1), T, tag + " PlakaUst");
    auto b = res(d, r, "BossUst");
    EXPECT_TRUE(isResolved(b.status)) << tag << dump(b);
    expectPlanar(r, b.faces, gp_Dir(0, 0, 1), T + Hb, tag + " BossUst");
    auto h = res(d, r, "BaglantiDelikleri");
    if (h.faces.empty() && std::getenv("PC_NS_DEBUG")) {
      for (auto& [k, x] : ov) std::cerr << k << "=" << x << " ";
      std::cerr << "\n";
      for (auto& [n, f] : r.faces) if (n.find("Hole") != std::string::npos) std::cerr << "  " << n << "\n";
      for (auto& [n, f] : r.removedBy) std::cerr << "  removed " << n << " by " << f << "\n";
    }
    EXPECT_EQ(h.status, NsStatus::Ok) << tag << dump(h);
    EXPECT_EQ(h.faces.size(), 4u) << tag;
    expectCylinders(r, h.faces, v["Dh"] / 2, tag + " holes");
    auto o = res(d, r, "Bore");
    EXPECT_EQ(o.status, NsStatus::Ok) << tag << dump(o);
    expectCylinders(r, o.faces, v["Dbore"] / 2, tag + " bore");
    auto t = res(d, r, "Taban");
    EXPECT_EQ(t.status, NsStatus::Ok) << tag << dump(t);
    expectPlanar(r, t.faces, gp_Dir(0, 0, -1), 0, tag + " Taban");
  }
  EXPECT_GE(tested, 20);
}

TEST(NamedSelectionRandom, InjectorParameterSweepsKeepNames) {
  Document d;
  std::string err;
  ASSERT_TRUE(loadDocument(d, std::string(PC_SAMPLES_DIR) + "/enjektor_09DB01.pcad", &err)) << err;
  ModelResult r0 = regenOk(d);
  ASSERT_TRUE(r0.ok);
  // name every face of the orifice hole wall, the swirler's outer revolve faces, the body's hexagon flats
  std::vector<ns::Ref> orifice, slots;
  for (auto& [n, f] : r0.faces) {
    if (n.rfind("Orifis.h0.wall", 0) == 0) orifice.push_back(ns::face(n));
    if (n.rfind("Slot.side", 0) == 0) slots.push_back(ns::face(n));
  }
  ASSERT_FALSE(orifice.empty());
  ASSERT_FALSE(slots.empty());
  nameIt(d, r0, "Orifis", orifice);
  nameIt(d, r0, "SlotDuvarlari", slots);
  nameIt(d, r0, "Govde", {ns::body("Govde")});
  const auto base = resolveAll(d, r0);
  for (auto& x : base) EXPECT_TRUE(isResolved(x.status)) << dump(x);
  std::vector<const Parameter*> exposed;
  for (auto& p : d.params.all())
    if (p.exposed && std::isfinite(p.minValue) && std::isfinite(p.maxValue)) exposed.push_back(&p);
  ASSERT_FALSE(exposed.empty());
  std::mt19937 g(9);
  int tested = 0;
  for (int it = 0; it < 6; ++it) {
    std::map<std::string, std::string> ov;
    for (auto* p : exposed) {
      double x = paramMinMax(*p, g);
      if (p->kind == ParamKind::Count) x = std::round(x);
      ov[p->name] = formatNumber(x);
    }
    ModelResult r = regenerate(d, ov);
    if (!r.ok) continue;
    ++tested;
    auto all = resolveAll(d, r);
    for (size_t i = 0; i < all.size(); ++i) {
      EXPECT_EQ(all[i].name, base[i].name);
      EXPECT_TRUE(isResolved(all[i].status)) << it << " " << dump(all[i]);
    }
    auto o = res(d, r, "Orifis");
    const double d0 = d.evaluateParams(ov).at("d0");
    expectCylinders(r, o.faces, d0 / 2, "orifice it " + std::to_string(it));
    for (auto& f : res(d, r, "SlotDuvarlari").faces) EXPECT_EQ(f.rfind("Slot.side", 0), 0u) << f;
    EXPECT_EQ(res(d, r, "Govde").bodies, std::vector<std::string>{"Govde"});
  }
  EXPECT_GE(tested, 3);
}

// Random edit sequences on the plate: insert / suppress / delete / param changes in any order. The name of every
// selection never changes and each resolved face is always the geometric face the user named.
TEST(NamedSelectionRandom, RandomHistoryEditsNeverMoveNames) {
  std::mt19937 g(42);
  for (int round = 0; round < 6; ++round) {
    ModelResult r0;
    Document d = namedPlate(&r0);
    std::uniform_int_distribution<int> op(0, 5);
    int k = 2;
    for (int step = 0; step < 6; ++step) {
      const std::string sid = "SketchR" + std::to_string(k), eid = "ExtrudeR" + std::to_string(k);
      ++k;
      int o = op(g);
      std::uniform_real_distribution<double> ux(-30, 25);
      double x = ux(g);
      switch (o) {
        case 0: addBox(d, sid, eid, x, -40, x + 4, 40, 7, 10, {{"op", "cut"}}); break;                       // split top
        case 1: addBox(d, sid, eid, x, -10, x + 8, 10, 10, 6, {{"op", "join"}}); break;                      // boss
        case 2: addBox(d, sid, eid, x, -10, x + 8, 10, 0, 10, {{"op", "new"}, {"bodyName", eid}}); break;  // body
        case 3: addBox(d, sid, eid, x, -40, x + 4, 40, 7, 10, {{"op", "cut"}}, 2); break;                    // earlier
        case 4:
          if (d.features.size() > 2) d.features.back().suppressed = !d.features.back().suppressed;
          break;
        default:
          d.params.find("W")->expression = formatNumber(std::uniform_real_distribution<double>(70, 120)(g));
      }
      ModelResult r = regenerate(d);
      if (!r.ok) continue;
      const double W = d.evaluateParams().at("W");
      const std::string tag = "round " + std::to_string(round) + " step " + std::to_string(step);
      auto u = res(d, r, "Ust");
      ASSERT_TRUE(u.status == NsStatus::Ok || u.status == NsStatus::Split) << tag << dump(u);
      expectPlanar(r, u.faces, gp_Dir(0, 0, 1), 10, tag + " Ust");
      for (auto& f : u.faces) EXPECT_EQ(f.rfind("Extrude1.end", 0), 0u) << tag << " " << f;
      auto a = res(d, r, "Alt");
      ASSERT_TRUE(isResolved(a.status)) << tag << dump(a);
      expectPlanar(r, a.faces, gp_Dir(0, 0, -1), 0, tag + " Alt");
      auto y = res(d, r, "Yan");
      if (isResolved(y.status)) expectPlanar(r, y.faces, gp_Dir(1, 0, 0), W / 2, tag + " Yan");
      else EXPECT_TRUE(y.faces.empty()) << tag;
    }
  }
}
