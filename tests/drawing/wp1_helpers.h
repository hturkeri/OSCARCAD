#pragma once
// WP-1 test helpers (header-only, file-local use).
#include "pc/drawing/Drawing.h"
#include "pc/drawing/GroundTruth.h"
#include <filesystem>
#include <string>

namespace wp1 {
using namespace pc::drawing;

inline Primitive line(int id, Vec2 a, Vec2 b, double w = 0.5) {
  Primitive p; p.id = id; p.kind = PrimKind::Line; p.p0 = a; p.p1 = b; p.style.width = w; return p;
}
inline Primitive arc(int id, Vec2 c, double r, double a0, double a1) {
  Primitive p; p.id = id; p.kind = PrimKind::Arc; p.center = c; p.radius = r; p.a0 = a0; p.a1 = a1; p.style.width = 0.5; return p;
}
inline Primitive circle(int id, Vec2 c, double r) {
  Primitive p; p.id = id; p.kind = PrimKind::Circle; p.center = c; p.radius = r; p.a0 = 0; p.a1 = 360; p.style.width = 0.5; return p;
}
inline TextRun text(int id, const std::string& s, Vec2 o, double h = 3.5, double rot = 0) {
  TextRun t; t.id = id; t.utf8 = s; t.origin = o; t.height = h; t.rotDeg = rot;
  const double w = 0.6 * h * static_cast<double>(s.size());
  const double c = std::cos(rot * 3.14159265358979 / 180), sn = std::sin(rot * 3.14159265358979 / 180);
  t.quad = {o, Vec2{o.x + w * c, o.y + w * sn}, Vec2{o.x + w * c - h * sn, o.y + w * sn + h * c}, Vec2{o.x - h * sn, o.y + h * c}};
  return t;
}

// 80x40 plate with a Ø20 hole, one R5 arc, a centre line and two dimension texts.
inline GroundTruth samplePlateGt() {
  GroundTruth gt;
  gt.family = "plate"; gt.caseName = "plate_unit";
  auto& s = gt.sheet;
  s.source = SourceKind::VectorPdf; s.sheet = {{0, 0}, {297, 210}}; s.unitToMm = kPtToMm;
  s.prims = {line(1, {50, 50}, {130, 50}), line(2, {130, 50}, {130, 90}), line(3, {130, 90}, {50, 90}),
             line(4, {50, 90}, {50, 50}), circle(5, {90, 70}, 10), arc(6, {130, 70}, 5, -90, 90)};
  Primitive c = line(7, {90, 45}, {90, 95}, 0.25); c.style.dash = {8, 2, 1, 2}; s.prims.push_back(c);
  s.texts = {text(8, "80", {85, 40}), text(9, "Ø20", {95, 82}), text(10, "ÖLÇEK 1:2", {230, 20})};
  for (int id : {1, 2, 3, 4, 5, 6}) gt.roles[id] = LineRole::Visible;
  gt.roles[7] = LineRole::Center;
  GtView v; v.role = ViewRole::Front; v.box = {{45, 40}, {140, 100}}; v.label = ""; gt.views.push_back(v);
  GtDimension d; d.text = "80"; d.kind = DimKind::Linear; d.nominal = 80; d.viewIndex = 0; d.geomPrimIds = {4, 2};
  d.paramName = "W"; d.textId = 8; gt.dims.push_back(d);
  GtDimension d2; d2.text = "Ø20"; d2.kind = DimKind::Diameter; d2.nominal = 20; d2.viewIndex = 0; d2.geomPrimIds = {5};
  d2.paramName = "Dh1"; d2.textId = 9; gt.dims.push_back(d2);
  gt.scale.num = 1; gt.scale.den = 2; gt.scale.conf = 1; gt.scale.evidence = "antet: ÖLÇEK 1:2";
  gt.params = {{"W", 80, ParamTag::FromDimension}, {"Dh1", 20, ParamTag::FromDimension}};
  gt.expectedStrategy = Strategy::ExtrudeTwoView; gt.volume = 1234.5;
  return gt;
}

inline std::string tmpDir(const std::string& sub) {
  const std::filesystem::path p = std::filesystem::path(PC_DRAWING_TMP_DIR) / "wp1" / sub;
  std::filesystem::create_directories(p);
  return p.string();
}
}  // namespace wp1
