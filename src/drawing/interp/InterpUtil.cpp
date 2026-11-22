// WP-3 private helpers (geometry, text, dimension grammar adapter).
#include "interp/InterpUtil.h"
#include "pc/Expression.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <regex>
#include <stdexcept>

namespace pc::drawing::interp {

bool isCurve(const Primitive& p) { return p.kind == PrimKind::Line || p.kind == PrimKind::Arc || p.kind == PrimKind::Circle; }

bool angleInArc(double a, double a0, double a1) {
  double s = std::fmod(a - a0, 360.0);
  if (s < 0) s += 360.0;
  return s <= (a1 - a0) + 1e-9;
}

Box2 primBox(const Primitive& p) {
  Box2 b = emptyBox();
  switch (p.kind) {
    case PrimKind::Line: case PrimKind::Arrowhead: expand(b, p.p0); expand(b, p.p1); break;
    case PrimKind::Circle: case PrimKind::Ellipse:
      expand(b, {p.center.x - p.radius, p.center.y - p.radius}); expand(b, {p.center.x + p.radius, p.center.y + p.radius}); break;
    case PrimKind::Arc:
      for (auto& q : samplePrim(p, std::max(0.05, p.radius * 0.05))) expand(b, q);
      break;
    default: for (auto& q : p.pts) expand(b, q);
  }
  return b;
}

double primLength(const Primitive& p) {
  switch (p.kind) {
    case PrimKind::Line: return distance(p.p0, p.p1);
    case PrimKind::Circle: return 2 * kPi * p.radius;
    case PrimKind::Arc: return deg2rad(p.a1 - p.a0) * p.radius;
    case PrimKind::Polyline: {
      double L = 0;
      for (size_t i = 1; i < p.pts.size(); ++i) L += distance(p.pts[i - 1], p.pts[i]);
      return L;
    }
    default: return 0;
  }
}

Vec2 primMid(const Primitive& p) {
  switch (p.kind) {
    case PrimKind::Line: return (p.p0 + p.p1) * 0.5;
    case PrimKind::Circle: return p.center;
    case PrimKind::Arc: { double a = deg2rad((p.a0 + p.a1) / 2); return {p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)}; }
    case PrimKind::Arrowhead: return p.p0;
    default: { Box2 b = primBox(p); return isEmpty(b) ? p.p0 : (b.lo + b.hi) * 0.5; }
  }
}

double distPointSeg(Vec2 q, Vec2 a, Vec2 b, double* tOut) {
  Vec2 d = b - a;
  double L2 = dot(d, d);
  double t = L2 > 1e-30 ? std::clamp(dot(q - a, d) / L2, 0.0, 1.0) : 0.0;
  if (tOut) *tOut = t;
  return distance(q, a + d * t);
}

double distToPrim(const Primitive& p, Vec2 q) {
  switch (p.kind) {
    case PrimKind::Line: return distPointSeg(q, p.p0, p.p1);
    case PrimKind::Circle: return std::abs(distance(q, p.center) - p.radius);
    case PrimKind::Arc: {
      double a = rad2deg(std::atan2(q.y - p.center.y, q.x - p.center.x));
      if (angleInArc(a, p.a0, p.a1)) return std::abs(distance(q, p.center) - p.radius);
      auto e = primEndpoints(p);
      return std::min(distance(q, e[0]), distance(q, e[1]));
    }
    case PrimKind::Polyline: case PrimKind::HatchRegion: {
      double best = 1e300;
      for (size_t i = 1; i < p.pts.size(); ++i) best = std::min(best, distPointSeg(q, p.pts[i - 1], p.pts[i]));
      if ((p.closed || p.kind == PrimKind::HatchRegion) && p.pts.size() > 2) best = std::min(best, distPointSeg(q, p.pts.back(), p.pts.front()));
      return best;
    }
    default: return distance(q, p.p0);
  }
}

std::vector<Vec2> samplePrim(const Primitive& p, double step) {
  std::vector<Vec2> out;
  step = std::max(step, 1e-3);
  if (p.kind == PrimKind::Line) {
    int n = std::max(1, (int)std::ceil(distance(p.p0, p.p1) / step));
    for (int i = 0; i <= n; ++i) out.push_back(p.p0 + (p.p1 - p.p0) * (double(i) / n));
  } else if (p.kind == PrimKind::Arc || p.kind == PrimKind::Circle) {
    double a0 = p.kind == PrimKind::Arc ? p.a0 : 0, a1 = p.kind == PrimKind::Arc ? p.a1 : 360;
    int n = std::max(4, (int)std::ceil(deg2rad(a1 - a0) * p.radius / step));
    for (int i = 0; i <= n; ++i) {
      double a = deg2rad(a0 + (a1 - a0) * i / n);
      out.push_back({p.center.x + p.radius * std::cos(a), p.center.y + p.radius * std::sin(a)});
    }
  } else if (p.kind == PrimKind::Polyline) {
    for (size_t i = 1; i < p.pts.size(); ++i) {
      int n = std::max(1, (int)std::ceil(distance(p.pts[i - 1], p.pts[i]) / step));
      for (int k = (i == 1 ? 0 : 1); k <= n; ++k) out.push_back(p.pts[i - 1] + (p.pts[i] - p.pts[i - 1]) * (double(k) / n));
    }
  }
  return out;
}

std::vector<Vec2> primEndpoints(const Primitive& p) {
  if (p.kind == PrimKind::Line) return {p.p0, p.p1};
  if (p.kind == PrimKind::Arc) {
    double a0 = deg2rad(p.a0), a1 = deg2rad(p.a1);
    return {{p.center.x + p.radius * std::cos(a0), p.center.y + p.radius * std::sin(a0)},
            {p.center.x + p.radius * std::cos(a1), p.center.y + p.radius * std::sin(a1)}};
  }
  if (p.kind == PrimKind::Polyline && !p.pts.empty() && !p.closed) return {p.pts.front(), p.pts.back()};
  return {};
}

bool isDashed(const Primitive& p) { return p.style.dash.size() >= 2; }
bool isCenterDash(const Primitive& p) {
  const auto& d = p.style.dash;
  if (d.size() >= 4) return true;
  return false;
}

std::vector<double> rayHits(const Primitive& p, Vec2 o, Vec2 d) {
  std::vector<double> ts;
  if (p.kind == PrimKind::Line) {
    Vec2 e = p.p1 - p.p0;
    double den = cross(d, e);
    if (std::abs(den) < 1e-12) return ts;
    double t = cross(p.p0 - o, e) / den;
    double s = cross(p.p0 - o, d) / den;
    if (s >= -1e-9 && s <= 1 + 1e-9) ts.push_back(t);
  } else if (p.kind == PrimKind::Circle || p.kind == PrimKind::Arc) {
    Vec2 f = o - p.center;
    double a = dot(d, d), b = 2 * dot(f, d), c = dot(f, f) - p.radius * p.radius;
    double disc = b * b - 4 * a * c;
    if (disc < 0) return ts;
    for (double sgn : {-1.0, 1.0}) {
      double t = (-b + sgn * std::sqrt(disc)) / (2 * a);
      Vec2 q = o + d * t;
      if (p.kind == PrimKind::Arc && !angleInArc(rad2deg(std::atan2(q.y - p.center.y, q.x - p.center.x)), p.a0, p.a1)) continue;
      ts.push_back(t);
    }
  }
  return ts;
}

// ---------------------------------------------------------------- texts
Box2 textBox(const TextRun& t) {
  Box2 b = emptyBox();
  bool any = false;
  for (auto& q : t.quad) if (q.x != 0 || q.y != 0) { expand(b, q); any = true; }
  if (!any) {
    double w = 0.6 * t.height * std::max<size_t>(1, t.utf8.size());
    double c = std::cos(deg2rad(t.rotDeg)), s = std::sin(deg2rad(t.rotDeg));
    expand(b, t.origin);
    expand(b, {t.origin.x + w * c, t.origin.y + w * s});
    expand(b, {t.origin.x + w * c - t.height * s, t.origin.y + w * s + t.height * c});
    expand(b, {t.origin.x - t.height * s, t.origin.y + t.height * c});
  }
  return b;
}
Vec2 textCentre(const TextRun& t) { Box2 b = textBox(t); return (b.lo + b.hi) * 0.5; }

std::string foldUpper(const std::string& s) {
  static const std::pair<const char*, char> map[] = {
      {"Ö", 'O'}, {"ö", 'O'}, {"Ç", 'C'}, {"ç", 'C'}, {"İ", 'I'}, {"ı", 'I'}, {"Ş", 'S'}, {"ş", 'S'},
      {"Ğ", 'G'}, {"ğ", 'G'}, {"Ü", 'U'}, {"ü", 'U'}, {"Â", 'A'}, {"â", 'A'}};
  std::string out;
  for (size_t i = 0; i < s.size();) {
    bool done = false;
    if ((unsigned char)s[i] >= 0x80) {
      for (auto& m : map) {
        size_t L = std::strlen(m.first);
        if (s.compare(i, L, m.first) == 0) { out += m.second; i += L; done = true; break; }
      }
      if (!done) { out += s[i]; ++i; }
    } else {
      out += (char)std::toupper((unsigned char)s[i]);
      ++i;
    }
  }
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n:");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

bool parseScaleText(const std::string& s, double& num, double& den) {
  static const std::regex re(R"((\d+(?:[.,]\d+)?)\s*:\s*(\d+(?:[.,]\d+)?))");
  std::smatch m;
  if (!std::regex_search(s, m, re)) return false;
  auto conv = [](std::string v) { std::replace(v.begin(), v.end(), ',', '.'); return std::stod(v); };
  num = conv(m[1]);
  den = conv(m[2]);
  return num > 0 && den > 0;
}

// ---------------------------------------------------------------- dimension grammar (WP-2)
DimText parseDim(const std::string& text, const DimTextContext& ctx) { return parseDimText(text, ctx); }

// ---------------------------------------------------------------- views
double viewScaleFactor(const ViewRegion& v) { return v.scale.num > 0 && v.scale.den > 0 ? v.scale.factor() : 1.0; }

std::vector<const Primitive*> viewPrims(const DrawingIR& ir, const ViewRegion& v, std::initializer_list<LineRole> roles) {
  std::vector<const Primitive*> out;
  for (int id : v.primIds) {
    const Primitive* p = findPrim(ir.norm, id);
    if (!p) continue;
    LineRole r = roleOf(ir, id);
    for (LineRole w : roles) if (r == w) { out.push_back(p); break; }
  }
  return out;
}

Box2 visibleBox(const DrawingIR& ir, const ViewRegion& v) {
  Box2 b = emptyBox();
  for (auto* p : viewPrims(ir, v, {LineRole::Visible})) {
    Box2 pb = primBox(*p);
    expand(b, pb.lo); expand(b, pb.hi);
  }
  return isEmpty(b) ? v.box : b;
}

// ---------------------------------------------------------------- misc
std::string fmt(double v, int prec) {
  if (std::abs(v) < 1e-12) v = 0;
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.*f", prec, v);
  std::string s = buf;
  if (s.find('.') != std::string::npos) {
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
  }
  if (s == "-0") s = "0";
  return s;
}
double attrNum(const Attrs& a, const std::string& k, double def) {
  auto it = a.find(k);
  if (it == a.end()) return def;
  try { return std::stod(it->second); } catch (...) { return def; }
}
int attrInt(const Attrs& a, const std::string& k, int def) {
  auto it = a.find(k);
  if (it == a.end()) return def;
  try { return std::stoi(it->second); } catch (...) { return def; }
}

void diag(StageResult& r, Severity s, const std::string& code, const std::string& msg, Stage st, std::vector<int> ids) {
  r.diagnostics.push_back({s, code, msg, static_cast<int>(st), std::move(ids)});
}

ParamValues proposalValues(const std::vector<ParamProposal>& ps) {
  ParamTable t;
  for (auto& p : ps) t.add(p.param);
  auto v = t.evaluate();
  return v ? *v : ParamValues{};
}

}  // namespace pc::drawing::interp
