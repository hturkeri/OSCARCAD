// WP-1: DXF reader (S1b). Reference fixtures written by ezdxf (fixtures/wp1/dxf, make_ezdxf_fixtures.py) plus
// hand-written group-code snippets for edge cases.
#include "wp1_helpers.h"
#include <gtest/gtest.h>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace pc::drawing;

namespace {
const std::string kFix = std::string(PC_DRAWING_TEST_DIR) + "/fixtures/wp1/dxf/";

struct Loaded { PrimitiveSheet s; StageResult r; Vec2 shift{0, 0}; };
Loaded load(const std::string& path, IngestOptions opt = {}) {
  Loaded L; int nextId = 1;
  L.r = ingestDxf(path, opt, L.s, nextId);
  auto it = L.s.attrs.find("dxf.shift");
  if (it != L.s.attrs.end()) { std::sscanf(it->second.c_str(), "%lf,%lf", &L.shift.x, &L.shift.y); }
  return L;
}
const Primitive* findCircle(const PrimitiveSheet& s, Vec2 c, double r, double tol = 1e-6) {
  for (auto& p : s.prims) if (p.kind == PrimKind::Circle && distance(p.center, c) < tol && std::abs(p.radius - r) < tol) return &p;
  return nullptr;
}
const Primitive* findArc(const PrimitiveSheet& s, Vec2 c, double r, double tol = 1e-6) {
  for (auto& p : s.prims) if (p.kind == PrimKind::Arc && distance(p.center, c) < tol && std::abs(p.radius - r) < tol) return &p;
  return nullptr;
}
const Primitive* findLine(const PrimitiveSheet& s, Vec2 a, Vec2 b, double tol = 1e-6) {
  for (auto& p : s.prims)
    if (p.kind == PrimKind::Line && ((distance(p.p0, a) < tol && distance(p.p1, b) < tol) || (distance(p.p0, b) < tol && distance(p.p1, a) < tol)))
      return &p;
  return nullptr;
}
const TextRun* findText(const PrimitiveSheet& s, const std::string& u) {
  for (auto& t : s.texts) if (t.utf8 == u) return &t;
  return nullptr;
}
const NativeDimension* findDim(const PrimitiveSheet& s, NativeDimKind k, double v) {
  for (auto& d : s.nativeDims) if (d.kind == k && std::abs(d.measured - v) < 1e-6) return &d;
  return nullptr;
}
std::string writeTmp(const std::string& name, const std::string& content) {
  const std::string p = wp1::tmpDir("dxf") + "/" + name;
  std::ofstream(p, std::ios::binary) << content;
  return p;
}
// minimal DXF text from (code, value) pairs
std::string dxf(std::initializer_list<std::pair<int, std::string>> g) {
  std::ostringstream o;
  for (auto& [c, v] : g) o << c << "\n" << v << "\n";
  return o.str();
}
}  // namespace

TEST(DxfIngest, Ezdxf2010Geometry) {
  auto L = load(kFix + "ezdxf_r2010.dxf");
  ASSERT_TRUE(L.r.ok()) << L.r.message;
  const PrimitiveSheet& s = L.s;
  const Vec2 o = L.shift;
  EXPECT_GT(o.x, 0);   // R5 arc at x=-5 and dims below 0 -> shifted to >= 0
  EXPECT_EQ(s.source, SourceKind::Dxf);
  EXPECT_DOUBLE_EQ(s.unitToMm, 1.0);
  EXPECT_GE(s.sheet.lo.x, 0);
  EXPECT_NE(findCircle(s, Vec2{40, 20} + o, 10), nullptr);
  const Primitive* r5 = findArc(s, Vec2{0, 20} + o, 5);
  ASSERT_NE(r5, nullptr);
  EXPECT_NEAR(r5->a0, 270, 1e-9); EXPECT_NEAR(r5->a1, 450, 1e-9);
  const Primitive* corner = findArc(s, Vec2{70, 30} + o, 10, 1e-6);   // bulge 0.414 = 90° CCW
  ASSERT_NE(corner, nullptr);
  EXPECT_NEAR(corner->a0, 0, 1e-6); EXPECT_NEAR(corner->a1, 90, 1e-6);
  EXPECT_NE(findLine(s, Vec2{0, 0} + o, Vec2{80, 0} + o), nullptr);
  const Primitive* cl = findLine(s, Vec2{40, -5} + o, Vec2{40, 45} + o);
  ASSERT_NE(cl, nullptr);
  EXPECT_EQ(cl->style.linetype, "CENTER");
  EXPECT_FALSE(cl->style.dash.empty());
  EXPECT_EQ(cl->style.layer, "CENTER");
  EXPECT_NEAR(cl->style.width, 0.25, 1e-12);
  const Primitive* hid = findLine(s, Vec2{20, 0} + o, Vec2{20, 40} + o);
  ASSERT_NE(hid, nullptr);
  EXPECT_EQ(hid->style.linetype, "HIDDEN");
  // ELLIPSE + SPLINE approximations
  int ell = 0, spl = 0;
  for (auto& p : s.prims) {
    if (p.kind == PrimKind::Ellipse) { ++ell; EXPECT_NEAR(p.radius, 15, 1e-9); EXPECT_NEAR(p.minorRatio, 0.5, 1e-9); EXPECT_GT(p.pts.size(), 20u); }
    if (p.kind == PrimKind::Polyline && p.attrs.count("dxf.approx") && p.attrs.at("dxf.approx") == "spline") {
      ++spl; EXPECT_NEAR(distance(p.pts.front(), Vec2{100, 50} + o), 0, 1e-6); EXPECT_NEAR(distance(p.pts.back(), Vec2{130, 60} + o), 0, 1e-6);
    }
  }
  EXPECT_EQ(ell, 1); EXPECT_EQ(spl, 1);
  // INSERT: scaled x2, rotated 90 -> circle r=6 at (60,70), cross line vertical
  EXPECT_NE(findCircle(s, Vec2{60, 70} + o, 6), nullptr);
  EXPECT_NE(findLine(s, Vec2{60, 62} + o, Vec2{60, 78} + o), nullptr);
  // 2x1 array
  EXPECT_NE(findCircle(s, Vec2{10, 70} + o, 3), nullptr);
  EXPECT_NE(findCircle(s, Vec2{25, 70} + o, 3), nullptr);
}

TEST(DxfIngest, Ezdxf2010TextAndSymbols) {
  auto L = load(kFix + "ezdxf_r2010.dxf");
  ASSERT_TRUE(L.r.ok());
  EXPECT_NE(findText(L.s, "Ø20 H7"), nullptr);
  EXPECT_NE(findText(L.s, "14±0.2"), nullptr);
  const TextRun* deg = findText(L.s, "45°");
  ASSERT_NE(deg, nullptr);
  EXPECT_NEAR(deg->rotDeg, 90, 1e-9);
  EXPECT_NEAR(deg->origin.x, 100 + L.shift.x, 1e-9);
  EXPECT_NEAR(deg->height, 3.5, 1e-12);
  EXPECT_NE(findText(L.s, "ÖLÇEK 1:1"), nullptr);
  EXPECT_NE(findText(L.s, "Ø16 +0/-0.5"), nullptr);   // MTEXT stacked tolerance
}

TEST(DxfIngest, Ezdxf2010Dimensions) {
  auto L = load(kFix + "ezdxf_r2010.dxf");
  ASSERT_TRUE(L.r.ok());
  const auto& s = L.s;
  ASSERT_EQ(s.nativeDims.size(), 6u);
  const NativeDimension* lin = findDim(s, NativeDimKind::Linear, 80);
  ASSERT_NE(lin, nullptr);
  EXPECT_TRUE(lin->rotatedLinear);
  EXPECT_FALSE(lin->blockPrimitiveIds.empty());
  EXPECT_FALSE(lin->blockTextIds.empty());
  for (int id : lin->blockPrimitiveIds) { const Primitive* p = findPrim(s, id); ASSERT_NE(p, nullptr); EXPECT_TRUE(p->fromDimBlock); }
  const TextRun* t = findText(s, lin->blockTextIds.at(0));
  ASSERT_NE(t, nullptr);
  EXPECT_TRUE(t->fromDimBlock);
  EXPECT_EQ(t->utf8, "8000");                        // ezdxf style EZDXF: DIMLFAC 100 (m -> cm)
  EXPECT_EQ(lin->attrs.at("dxf.dimlfac"), "100");
  EXPECT_NE(findDim(s, NativeDimKind::Linear, 40), nullptr);
  EXPECT_NE(findDim(s, NativeDimKind::Linear, std::hypot(10, 10)), nullptr);   // ezdxf writes aligned as rotated linear
  EXPECT_NE(findDim(s, NativeDimKind::Diameter, 20), nullptr);
  EXPECT_NE(findDim(s, NativeDimKind::Radius, 5), nullptr);
  EXPECT_NE(findDim(s, NativeDimKind::Angular, 45), nullptr);
  int arrows = 0;
  for (auto& p : s.prims) arrows += p.kind == PrimKind::Arrowhead && p.fromDimBlock;
  EXPECT_GE(arrows, 2);   // EZDXF linear dims use ARCHTICK (no arrowhead); radius/diameter use filled arrows
  // explosion disabled
  IngestOptions o; o.explodeDxfDimBlocks = false;
  auto L2 = load(kFix + "ezdxf_r2010.dxf", o);
  ASSERT_TRUE(L2.r.ok());
  for (auto& p : L2.s.prims) EXPECT_FALSE(p.fromDimBlock);
  EXPECT_EQ(L2.s.nativeDims.size(), 6u);
}

TEST(DxfIngest, EzdxfR12) {
  auto L = load(kFix + "ezdxf_r12.dxf");
  ASSERT_TRUE(L.r.ok()) << L.r.message;
  const auto& s = L.s;
  const Vec2 o = L.shift;
  EXPECT_EQ(s.attrs.at("dxf.acadver"), "AC1009");
  EXPECT_NE(findCircle(s, Vec2{40, 20} + o, 10), nullptr);
  EXPECT_NE(findArc(s, Vec2{70, 30} + o, 10), nullptr);     // POLYLINE/VERTEX bulge
  EXPECT_NE(findText(s, "Ø20 H7"), nullptr);
  EXPECT_NE(findText(s, "ÖLÇEK 1:1"), nullptr);             // ANSI_1252 decoded
  EXPECT_NE(findDim(s, NativeDimKind::Linear, 80), nullptr);
  EXPECT_NE(findDim(s, NativeDimKind::Diameter, 20), nullptr);
  EXPECT_NE(findDim(s, NativeDimKind::Radius, 5), nullptr);
  EXPECT_NE(findCircle(s, Vec2{60, 70} + o, 6), nullptr);
}

TEST(DxfIngest, UnitsCodepageAndCodes) {
  // inches, ANSI_1254 (Turkish), %%nnn, \U+2300, MTEXT formatting, R2000 version string
  std::string turkish = "\xD6L\xC7""EK \xDD\xDE\xD0";   // ÖLÇEK İŞĞ in cp1254
  const std::string content = dxf({{0, "SECTION"}, {2, "HEADER"}, {9, "$ACADVER"}, {1, "AC1015"}, {9, "$DWGCODEPAGE"}, {3, "ANSI_1254"},
                                   {9, "$INSUNITS"}, {70, "1"}, {0, "ENDSEC"},
                                   {0, "SECTION"}, {2, "ENTITIES"},
                                   {0, "LINE"}, {8, "0"}, {10, "1"}, {20, "1"}, {11, "2"}, {21, "1"},
                                   {0, "TEXT"}, {8, "0"}, {10, "1"}, {20, "2"}, {40, "0.1"}, {1, turkish},
                                   {0, "TEXT"}, {8, "0"}, {10, "1"}, {20, "3"}, {40, "0.1"}, {1, "\\U+2300""5 %%177%%0370.1"},
                                   {0, "MTEXT"}, {8, "0"}, {10, "1"}, {20, "4"}, {40, "0.1"}, {71, "7"},
                                   {1, "{\\fArial|b0;\\H1.2x;R3}\\Pline two\\~x"},
                                   {0, "ENDSEC"}, {0, "EOF"}});
  auto L = load(writeTmp("units.dxf", content));
  ASSERT_TRUE(L.r.ok()) << L.r.message;
  EXPECT_DOUBLE_EQ(L.s.unitToMm, 25.4);
  ASSERT_NE(findLine(L.s, {25.4, 25.4}, {50.8, 25.4}), nullptr);
  EXPECT_NE(findText(L.s, "ÖLÇEK İŞĞ"), nullptr);
  EXPECT_NE(findText(L.s, "Ø5 ±%0.1"), nullptr);
  EXPECT_NE(findText(L.s, "R3"), nullptr);
  EXPECT_NE(findText(L.s, "line two x"), nullptr);
  const TextRun* t = findText(L.s, "R3");
  ASSERT_NE(t, nullptr);
  EXPECT_NEAR(t->height, 2.54, 1e-9);
}

TEST(DxfIngest, BlocksByLayerByBlockAndMirroredArcs) {
  const std::string content = dxf({{0, "SECTION"}, {2, "TABLES"}, {0, "TABLE"}, {2, "LTYPE"},
                                   {0, "LTYPE"}, {2, "DASHED"}, {73, "2"}, {40, "10"}, {49, "6"}, {49, "-4"}, {0, "ENDTAB"},
                                   {0, "TABLE"}, {2, "LAYER"},
                                   {0, "LAYER"}, {2, "HID"}, {70, "0"}, {62, "5"}, {6, "DASHED"}, {370, "35"},
                                   {0, "LAYER"}, {2, "OFF"}, {70, "0"}, {62, "-1"}, {6, "CONTINUOUS"}, {0, "ENDTAB"},
                                   {0, "ENDSEC"},
                                   {0, "SECTION"}, {2, "BLOCKS"}, {0, "BLOCK"}, {2, "B"}, {70, "0"}, {10, "1"}, {20, "0"},
                                   {0, "ARC"}, {8, "0"}, {6, "BYBLOCK"}, {10, "1"}, {20, "0"}, {40, "2"}, {50, "0"}, {51, "90"},
                                   {0, "ENDBLK"}, {0, "ENDSEC"},
                                   {0, "SECTION"}, {2, "ENTITIES"},
                                   {0, "INSERT"}, {8, "HID"}, {6, "BYLAYER"}, {2, "B"}, {10, "10"}, {20, "10"}, {41, "-1"}, {42, "1"},
                                   {0, "LINE"}, {8, "OFF"}, {10, "0"}, {20, "0"}, {11, "5"}, {21, "0"},
                                   {0, "CIRCLE"}, {8, "0"}, {10, "3"}, {20, "3"}, {40, "1"}, {210, "0"}, {220, "0"}, {230, "-1"},
                                   {0, "SPLINEX"}, {8, "0"},
                                   {0, "ENDSEC"}, {0, "EOF"}});
  auto L = load(writeTmp("blocks.dxf", content));
  ASSERT_TRUE(L.r.ok()) << L.r.message;
  const Vec2 o = L.shift;
  // mirrored in x: arc 0..90 about (10,10) becomes 90..180
  const Primitive* a = findArc(L.s, Vec2{10, 10} + o, 2);
  ASSERT_NE(a, nullptr);
  EXPECT_NEAR(a->a0, 90, 1e-9); EXPECT_NEAR(a->a1, 180, 1e-9);
  EXPECT_EQ(a->style.layer, "HID");
  EXPECT_EQ(a->style.linetype, "DASHED");
  EXPECT_EQ(a->style.dash, (std::vector<double>{6, 4}));
  EXPECT_NEAR(a->style.width, 0.35, 1e-12);
  EXPECT_EQ(a->style.rgb, 0x0000FFu);
  EXPECT_EQ(a->attrs.at("dxf.block"), "B");
  EXPECT_EQ(findLine(L.s, Vec2{0, 0} + o, Vec2{5, 0} + o), nullptr);   // layer off
  EXPECT_NE(findCircle(L.s, Vec2{-3, 3} + o, 1), nullptr);              // OCS extrusion -Z
  bool warned = false;
  for (auto& d : L.r.diagnostics) warned |= d.code == "dxf.entity.unsupported";
  EXPECT_TRUE(warned);
}

TEST(DxfIngest, AngularAndOrdinateFromDefpoints) {
  const std::string content = dxf({{0, "SECTION"}, {2, "ENTITIES"},
                                   {0, "DIMENSION"}, {8, "0"}, {70, "5"}, {10, "0"}, {20, "0"}, {13, "10"}, {23, "0"}, {14, "0"}, {24, "10"},
                                   {15, "0"}, {25, "0"}, {1, "<>"},
                                   {0, "DIMENSION"}, {8, "0"}, {70, "70"}, {10, "1"}, {20, "1"}, {13, "8"}, {23, "5"}, {14, "8"}, {24, "-5"},
                                   {0, "DIMENSION"}, {8, "0"}, {70, "0"}, {50, "30"}, {10, "0"}, {20, "0"}, {13, "0"}, {23, "0"}, {14, "10"}, {24, "0"},
                                   {1, "%%c<> \\S+0.1^-0;"},
                                   {0, "ENDSEC"}, {0, "EOF"}});
  auto L = load(writeTmp("dims.dxf", content));
  ASSERT_TRUE(L.r.ok());
  ASSERT_EQ(L.s.nativeDims.size(), 3u);
  EXPECT_EQ(L.s.nativeDims[0].kind, NativeDimKind::Angular3P);
  EXPECT_NEAR(L.s.nativeDims[0].measured, 90, 1e-9);
  EXPECT_EQ(L.s.nativeDims[0].textOverride, "<>");
  EXPECT_EQ(L.s.nativeDims[1].kind, NativeDimKind::Ordinate);
  EXPECT_NEAR(L.s.nativeDims[1].measured, 7, 1e-9);    // X-type (bit 64): |13.x - 10.x|
  EXPECT_NEAR(L.s.nativeDims[2].measured, 10 * std::cos(30 * 3.14159265358979323846 / 180), 1e-9);
  EXPECT_EQ(L.s.nativeDims[2].textOverride, "Ø<> +0.1/-0");
}

TEST(DxfIngest, Errors) {
  PrimitiveSheet s; int id = 1;
  EXPECT_EQ(ingestDxf(kFix + "missing.dxf", {}, s, id).status, Status::InvalidInput);
  EXPECT_EQ(ingestDxf(writeTmp("bin.dxf", std::string("AutoCAD Binary DXF\r\n\x1a", 22)), {}, s, id).status, Status::InvalidInput);
  EXPECT_EQ(ingestDxf(writeTmp("junk.dxf", "hello\nworld\n"), {}, s, id).status, Status::InvalidInput);
  ProgressSink cancel; cancel.cancelled = [] { return true; };
  EXPECT_EQ(ingestDxf(kFix + "ezdxf_r2010.dxf", {}, s, id, &cancel).status, Status::Cancelled);
}
