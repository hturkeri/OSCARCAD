#pragma once
// PersistCAD drawing import — 2D geometry value types. FROZEN (DrawingIR 1.0).
//
// Coordinate contract (all stages):
//  * SHEET millimetres: origin = lower-left corner of the page/sheet, +x right, +y UP (PDF convention).
//  * VIEW-local (from S5 on) = TRUE PART millimetres = transformPoint(ViewRegion::sheetToView, sheetPoint).
//  * Angles in degrees, CCW positive. Arcs run CCW from a0 to a1 (a1 > a0, a1 - a0 <= 360).
//
// Affine2 convention (same as a PDF matrix [a b c d e f]):  x' = a*x + c*y + e ;  y' = b*x + d*y + f.
// Identity = {1,0,0,1,0,0}. Used by ViewRegion::sheetToView and RasterInfo::pxToSheet.
// (Not named "apply": Affine2 is a std::array, so ADL would also find std::apply.)
#include <array>
#include <cmath>

namespace pc::drawing {

struct Vec2 { double x = 0, y = 0; };
struct Box2 { Vec2 lo, hi; };   // axis-aligned in the coordinate system of its owner; empty if lo > hi

using Affine2 = std::array<double, 6>;
inline constexpr Affine2 kIdentityAffine{1, 0, 0, 1, 0, 0};

inline Vec2 transformPoint(const Affine2& m, Vec2 p) { return {m[0] * p.x + m[2] * p.y + m[4], m[1] * p.x + m[3] * p.y + m[5]}; }
// m1 then m2 (point -> m1 -> m2)
inline Affine2 compose(const Affine2& m1, const Affine2& m2) {
  return {m2[0] * m1[0] + m2[2] * m1[1], m2[1] * m1[0] + m2[3] * m1[1],
          m2[0] * m1[2] + m2[2] * m1[3], m2[1] * m1[2] + m2[3] * m1[3],
          m2[0] * m1[4] + m2[2] * m1[5] + m2[4], m2[1] * m1[4] + m2[3] * m1[5] + m2[5]};
}
// Returns false (and leaves out untouched) if singular.
inline bool invert(const Affine2& m, Affine2& out) {
  const double det = m[0] * m[3] - m[1] * m[2];
  if (std::abs(det) < 1e-300) return false;
  const double a = m[3] / det, b = -m[1] / det, c = -m[2] / det, d = m[0] / det;
  out = {a, b, c, d, -(a * m[4] + c * m[5]), -(b * m[4] + d * m[5])};
  return true;
}

inline Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vec2 operator*(Vec2 a, double s) { return {a.x * s, a.y * s}; }
inline double dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double length(Vec2 a) { return std::hypot(a.x, a.y); }
inline double distance(Vec2 a, Vec2 b) { return length(a - b); }

inline bool isEmpty(const Box2& b) { return b.lo.x > b.hi.x || b.lo.y > b.hi.y; }
inline Box2 emptyBox() { return {{1e300, 1e300}, {-1e300, -1e300}}; }
inline void expand(Box2& b, Vec2 p) {
  b.lo.x = std::fmin(b.lo.x, p.x); b.lo.y = std::fmin(b.lo.y, p.y);
  b.hi.x = std::fmax(b.hi.x, p.x); b.hi.y = std::fmax(b.hi.y, p.y);
}
inline bool contains(const Box2& b, Vec2 p) { return p.x >= b.lo.x && p.x <= b.hi.x && p.y >= b.lo.y && p.y <= b.hi.y; }
inline double width(const Box2& b) { return b.hi.x - b.lo.x; }
inline double height(const Box2& b) { return b.hi.y - b.lo.y; }

inline constexpr double kPtToMm = 25.4 / 72.0;   // PDF user unit -> mm

}  // namespace pc::drawing
