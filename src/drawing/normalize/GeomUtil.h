#pragma once
// WP-1 private geometry helpers (normalize/, eval/, dxf/). Sheet mm, degrees, CCW arcs a0 < a1.
#include "pc/drawing/Primitives.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace pc::drawing::geom {

inline constexpr double kPi = 3.14159265358979323846;
inline double deg(double r) { return r * 180.0 / kPi; }
inline double rad(double d) { return d * kPi / 180.0; }
inline double norm360(double a) { a = std::fmod(a, 360.0); return a < 0 ? a + 360.0 : a; }
inline Vec2 polar(Vec2 c, double r, double aDeg) { return {c.x + r * std::cos(rad(aDeg)), c.y + r * std::sin(rad(aDeg))}; }
inline Vec2 unit(Vec2 v) { const double l = length(v); return l > 0 ? Vec2{v.x / l, v.y / l} : Vec2{1, 0}; }

// Is angle `a` (deg) inside the CCW sweep [a0, a1]?
inline bool angleIn(double a, double a0, double a1, double epsDeg = 1e-9) {
  if (a1 - a0 >= 360.0 - epsDeg) return true;
  const double d = norm360(a - a0);
  return d <= (a1 - a0) + epsDeg || d >= 360.0 - epsDeg;
}

inline Vec2 bezier(const Vec2& p0, const Vec2& c1, const Vec2& c2, const Vec2& p1, double t) {
  const double u = 1 - t;
  return p0 * (u * u * u) + c1 * (3 * u * u * t) + c2 * (3 * u * t * t) + p1 * (t * t * t);
}

inline double ellipseParamToAngle(double t) { return t; }
inline Vec2 ellipsePoint(const Primitive& p, double tDeg) {
  const double ux = std::cos(rad(p.rotDeg)), uy = std::sin(rad(p.rotDeg));
  const double a = p.radius, b = p.radius * p.minorRatio;
  const double x = a * std::cos(rad(tDeg)), y = b * std::sin(rad(tDeg));
  return {p.center.x + x * ux - y * uy, p.center.y + x * uy + y * ux};
}

// Polyline approximation (for distance / Hausdorff / drawing). `step` = max chord length in mm.
inline std::vector<Vec2> samplePrim(const Primitive& p, double step = 0.25) {
  std::vector<Vec2> s;
  auto seg = [&](Vec2 a, Vec2 b, bool first) {
    const int n = std::max(1, static_cast<int>(std::ceil(distance(a, b) / step)));
    for (int i = first ? 0 : 1; i <= n; ++i) s.push_back(a + (b - a) * (static_cast<double>(i) / n));
  };
  switch (p.kind) {
    case PrimKind::Line: case PrimKind::Arrowhead: seg(p.p0, p.p1, true); break;
    case PrimKind::Arc: case PrimKind::Circle: {
      const double a0 = p.kind == PrimKind::Circle ? 0 : p.a0, a1 = p.kind == PrimKind::Circle ? 360 : p.a1;
      const int n = std::max(4, static_cast<int>(std::ceil(rad(a1 - a0) * p.radius / step)));
      for (int i = 0; i <= n; ++i) s.push_back(polar(p.center, p.radius, a0 + (a1 - a0) * i / n));
      break;
    }
    case PrimKind::Ellipse: {
      const double a0 = p.a0, a1 = p.a1 > p.a0 ? p.a1 : p.a0 + 360;
      const int n = std::max(8, static_cast<int>(std::ceil(rad(a1 - a0) * p.radius / step)));
      for (int i = 0; i <= n; ++i) s.push_back(ellipsePoint(p, a0 + (a1 - a0) * i / n));
      break;
    }
    case PrimKind::Polyline: case PrimKind::HatchRegion: case PrimKind::Image:
      for (std::size_t i = 0; i + 1 < p.pts.size(); ++i) seg(p.pts[i], p.pts[i + 1], i == 0);
      if ((p.closed || p.kind != PrimKind::Polyline) && p.pts.size() > 2) seg(p.pts.back(), p.pts.front(), false);
      if (p.pts.size() == 1) s.push_back(p.pts[0]);
      break;
    case PrimKind::Bezier:
      if (p.pts.size() >= 4) {
        s.push_back(p.pts[0]);
        for (std::size_t i = 0; i + 3 < p.pts.size(); i += 3) {
          const double len = distance(p.pts[i], p.pts[i + 1]) + distance(p.pts[i + 1], p.pts[i + 2]) + distance(p.pts[i + 2], p.pts[i + 3]);
          const int n = std::max(4, static_cast<int>(std::ceil(len / step)));
          for (int k = 1; k <= n; ++k) s.push_back(bezier(p.pts[i], p.pts[i + 1], p.pts[i + 2], p.pts[i + 3], static_cast<double>(k) / n));
        }
      }
      break;
  }
  return s;
}

inline double distPointSeg(Vec2 q, Vec2 a, Vec2 b) {
  const Vec2 d = b - a;
  const double l2 = dot(d, d);
  if (l2 <= 0) return distance(q, a);
  const double t = std::clamp(dot(q - a, d) / l2, 0.0, 1.0);
  return distance(q, a + d * t);
}

inline double distPointPrim(Vec2 q, const Primitive& p) {
  switch (p.kind) {
    case PrimKind::Line: case PrimKind::Arrowhead: return distPointSeg(q, p.p0, p.p1);
    case PrimKind::Circle: return std::abs(distance(q, p.center) - p.radius);
    case PrimKind::Arc: {
      const double a = deg(std::atan2(q.y - p.center.y, q.x - p.center.x));
      if (angleIn(a, p.a0, p.a1)) return std::abs(distance(q, p.center) - p.radius);
      return std::min(distance(q, polar(p.center, p.radius, p.a0)), distance(q, polar(p.center, p.radius, p.a1)));
    }
    default: {
      const auto s = samplePrim(p, 0.1);
      double best = 1e300;
      for (std::size_t i = 0; i + 1 < s.size(); ++i) best = std::min(best, distPointSeg(q, s[i], s[i + 1]));
      if (s.size() == 1) best = distance(q, s[0]);
      return best;
    }
  }
}

inline Box2 primBox(const Primitive& p) {
  Box2 b = emptyBox();
  switch (p.kind) {
    case PrimKind::Line: case PrimKind::Arrowhead: expand(b, p.p0); expand(b, p.p1); break;
    case PrimKind::Circle: case PrimKind::Ellipse:
      expand(b, p.center - Vec2{p.radius, p.radius}); expand(b, p.center + Vec2{p.radius, p.radius}); break;
    case PrimKind::Arc: {
      expand(b, polar(p.center, p.radius, p.a0)); expand(b, polar(p.center, p.radius, p.a1));
      for (int q = 0; q < 8; ++q) if (angleIn(q * 90.0, p.a0, p.a1)) expand(b, polar(p.center, p.radius, q * 90.0));
      break;
    }
    default: for (auto& v : p.pts) expand(b, v); break;
  }
  return b;
}

// Kasa algebraic circle fit (then 1 Gauss-Newton refinement pass). Returns false if degenerate.
inline bool fitCircle(const std::vector<Vec2>& pts, Vec2& c, double& r) {
  const std::size_t n = pts.size();
  if (n < 3) return false;
  double mx = 0, my = 0;
  for (auto& p : pts) { mx += p.x; my += p.y; }
  mx /= n; my /= n;
  double suu = 0, svv = 0, suv = 0, suuu = 0, svvv = 0, suvv = 0, svuu = 0;
  for (auto& p : pts) {
    const double u = p.x - mx, v = p.y - my;
    suu += u * u; svv += v * v; suv += u * v; suuu += u * u * u; svvv += v * v * v; suvv += u * v * v; svuu += v * u * u;
  }
  const double det = suu * svv - suv * suv;
  if (std::abs(det) < 1e-18 * (suu + svv + 1e-30) * (suu + svv + 1e-30)) return false;
  const double b1 = 0.5 * (suuu + suvv), b2 = 0.5 * (svvv + svuu);
  const double uc = (b1 * svv - b2 * suv) / det, vc = (suu * b2 - suv * b1) / det;
  c = {uc + mx, vc + my};
  r = std::sqrt(uc * uc + vc * vc + (suu + svv) / n);
  // Gauss-Newton refinement of geometric distance
  for (int it = 0; it < 5; ++it) {
    double JtJ[3][3] = {{0}}, Jtr[3] = {0};
    for (auto& p : pts) {
      const double dx = p.x - c.x, dy = p.y - c.y, d = std::hypot(dx, dy);
      if (d < 1e-12) continue;
      const double res = d - r, J[3] = {-dx / d, -dy / d, -1};
      for (int a = 0; a < 3; ++a) { Jtr[a] += J[a] * res; for (int b = 0; b < 3; ++b) JtJ[a][b] += J[a] * J[b]; }
    }
    // solve 3x3
    double A[3][4];
    for (int a = 0; a < 3; ++a) { for (int b = 0; b < 3; ++b) A[a][b] = JtJ[a][b]; A[a][3] = -Jtr[a]; }
    bool ok = true;
    for (int col = 0; col < 3 && ok; ++col) {
      int piv = col;
      for (int rr = col + 1; rr < 3; ++rr) if (std::abs(A[rr][col]) > std::abs(A[piv][col])) piv = rr;
      if (std::abs(A[piv][col]) < 1e-20) { ok = false; break; }
      for (int k = 0; k < 4; ++k) std::swap(A[col][k], A[piv][k]);
      for (int rr = 0; rr < 3; ++rr) if (rr != col) {
        const double f = A[rr][col] / A[col][col];
        for (int k = col; k < 4; ++k) A[rr][k] -= f * A[col][k];
      }
    }
    if (!ok) break;
    c.x += A[0][3] / A[0][0]; c.y += A[1][3] / A[1][1]; r += A[2][3] / A[2][2];
  }
  return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(r) && r > 0;
}

// Symmetric Hausdorff distance between two primitives (sampled).
inline double hausdorff(const Primitive& a, const Primitive& b, double step = 0.2) {
  double h = 0;
  for (auto& q : samplePrim(a, step)) { h = std::max(h, distPointPrim(q, b)); if (h > 1e9) break; }
  for (auto& q : samplePrim(b, step)) h = std::max(h, distPointPrim(q, a));
  return h;
}

}  // namespace pc::drawing::geom
