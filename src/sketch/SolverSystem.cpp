// Sketch solver equation system: residual kernels (templated for forward-mode AD), system build,
// Levenberg-Marquardt with minimum-norm steps, continuation, rank analysis.
#include "SolverSystem.h"

#include "pc/Expression.h"
#include <Eigen/SparseCholesky>
#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <sstream>

namespace pc::sketch::detail {

namespace {

constexpr int MAXV = 12;
constexpr double kPi = 3.14159265358979323846;

// ---- minimal forward-mode dual number (exact Jacobians) ----
struct Dual {
  double v;
  std::array<double, MAXV> d;
  Dual(double x = 0) : v(x) { d.fill(0.0); }
};
inline Dual operator+(const Dual& a, const Dual& b) { Dual r(a.v + b.v); for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] + b.d[i]; return r; }
inline Dual operator-(const Dual& a, const Dual& b) { Dual r(a.v - b.v); for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] - b.d[i]; return r; }
inline Dual operator-(const Dual& a) { Dual r(-a.v); for (int i = 0; i < MAXV; ++i) r.d[i] = -a.d[i]; return r; }
inline Dual operator*(const Dual& a, const Dual& b) { Dual r(a.v * b.v); for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] * b.v + a.v * b.d[i]; return r; }
inline Dual operator/(const Dual& a, const Dual& b) {
  Dual r(a.v / b.v);
  double ib = 1.0 / b.v, q = a.v * ib * ib;
  for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] * ib - b.d[i] * q;
  return r;
}
inline Dual operator+(const Dual& a, double b) { Dual r(a); r.v += b; return r; }
inline Dual operator-(const Dual& a, double b) { Dual r(a); r.v -= b; return r; }
inline Dual operator*(const Dual& a, double b) { Dual r(a.v * b); for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] * b; return r; }
inline Dual operator*(double b, const Dual& a) { return a * b; }
inline Dual operator/(const Dual& a, double b) { return a * (1.0 / b); }
inline Dual sqrt(const Dual& a) {
  double s = std::sqrt(std::max(a.v, 0.0));
  Dual r(s);
  double k = s > 1e-300 ? 0.5 / s : 0.0;
  for (int i = 0; i < MAXV; ++i) r.d[i] = a.d[i] * k;
  return r;
}
inline Dual atan2(const Dual& y, const Dual& x) {
  Dual r(std::atan2(y.v, x.v));
  double den = x.v * x.v + y.v * y.v;
  if (den > 1e-300)
    for (int i = 0; i < MAXV; ++i) r.d[i] = (x.v * y.d[i] - y.v * x.d[i]) / den;
  return r;
}
inline double val(double x) { return x; }
inline double val(const Dual& x) { return x.v; }

template <class T> T len(const T& x, const T& y) { using std::sqrt; return sqrt(x * x + y * y); }
// signed distance of p from the line a->b (positive on the left)
template <class T> T sdist(const T& px, const T& py, const T& ax, const T& ay, const T& bx, const T& by) {
  T dx = bx - ax, dy = by - ay;
  return (dx * (py - ay) - dy * (px - ax)) / len(dx, dy);
}
// round entity at v: center v[0],v[1]; arc: start v[2],v[3]; circle: radius v[2]
template <class T> T rad(const T* v, bool isArc) { return isArc ? len(v[2] - v[0], v[3] - v[1]) : v[2]; }
inline int roundSize(bool isArc) { return isArc ? 4 : 3; }

template <class T> void evalBlock(const Block& b, const T* v, T* o, double L) {
  using std::atan2;
  switch (b.kind) {
    case Kind::Coinc: o[0] = v[2] - v[0]; o[1] = v[3] - v[1]; break;
    case Kind::Diff: o[0] = v[1] - v[0] - b.t; break;
    case Kind::Fixed: o[0] = v[0] - b.c0x; o[1] = v[1] - b.c0y; break;
    case Kind::Parallel: case Kind::Perp: case Kind::Angle: {
      T dx1 = v[2] - v[0], dy1 = v[3] - v[1], dx2 = v[6] - v[4], dy2 = v[7] - v[5];
      T cr = dx1 * dy2 - dy1 * dx2, dt = dx1 * dx2 + dy1 * dy2;
      if (b.kind == Kind::Parallel) o[0] = L * cr / (len(dx1, dy1) * len(dx2, dy2));
      else if (b.kind == Kind::Perp) o[0] = L * dt / (len(dx1, dy1) * len(dx2, dy2));
      else {
        T a = atan2(cr, dt) - b.t;
        double k = std::round(val(a) / (2 * kPi));
        o[0] = L * (a - k * 2 * kPi);
      }
      break;
    }
    case Kind::Dist: o[0] = len(v[2] - v[0], v[3] - v[1]) - b.t; break;
    case Kind::LineDist: o[0] = sdist(v[0], v[1], v[2], v[3], v[4], v[5]) - b.s * b.t; break;
    case Kind::TanLR: o[0] = sdist(v[4], v[5], v[0], v[1], v[2], v[3]) - b.s * rad(v + 4, b.arc1); break;
    case Kind::TanRR: {
      const T* w = v + roundSize(b.arc1);
      T r1 = rad(v, b.arc1), r2 = rad(w, b.arc2);
      T d = len(w[0] - v[0], w[1] - v[1]);
      o[0] = b.mode == 0 ? d - (r1 + r2) : d - b.s * (r1 - r2);
      break;
    }
    case Kind::EqLen: o[0] = len(v[2] - v[0], v[3] - v[1]) - len(v[6] - v[4], v[7] - v[5]); break;
    case Kind::EqRad: o[0] = rad(v, b.arc1) - rad(v + roundSize(b.arc1), b.arc2); break;
    case Kind::OnRound: o[0] = len(v[0] - v[2], v[1] - v[3]) - rad(v + 2, b.arc1); break;
    case Kind::Mid: o[0] = v[0] - (v[2] + v[4]) * 0.5; o[1] = v[1] - (v[3] + v[5]) * 0.5; break;
    case Kind::Sym: {
      T dx = v[6] - v[4], dy = v[7] - v[5], n = len(dx, dy);
      T mx = (v[0] + v[2]) * 0.5, my = (v[1] + v[3]) * 0.5;
      o[0] = (dx * (my - v[5]) - dy * (mx - v[4])) / n;
      o[1] = ((v[2] - v[0]) * dx + (v[3] - v[1]) * dy) / n;
      break;
    }
    case Kind::Rad: o[0] = rad(v, b.arc1) - b.kfac * b.t; break;
    case Kind::Arc: o[0] = len(v[4] - v[0], v[5] - v[1]) - len(v[2] - v[0], v[3] - v[1]); break;
    case Kind::TanLP: {  // line a(0,1) b(2,3), shared point p(4,5), center c(6,7): radius _|_ line
      T dx = v[2] - v[0], dy = v[3] - v[1], rx = v[4] - v[6], ry = v[5] - v[7];
      o[0] = L * (dx * rx + dy * ry) / (len(dx, dy) * len(rx, ry));
      break;
    }
    case Kind::TanRP: {  // c1(0,1) p1(2,3) c2(4,5) p2(6,7): radii at the shared point collinear
      T ax = v[2] - v[0], ay = v[3] - v[1], bx = v[6] - v[4], by = v[7] - v[5];
      o[0] = L * (ax * by - ay * bx) / (len(ax, ay) * len(bx, by));
      break;
    }
  }
}

void evalD(const Block& b, const std::vector<double>& vals, double* out, double L) {
  double loc[MAXV];
  for (size_t k = 0; k < b.slots.size(); ++k) loc[k] = vals[b.slots[k]];
  evalBlock<double>(b, loc, out, L);
}

// Target value at which the block's (first) row would vanish at the current configuration.
double measure(const Block& b, const std::vector<double>& vals, double L) {
  Block c = b;
  double r0[2], r1[2];
  const double dlt = 1e-3;
  evalD(c, vals, r0, L);
  c.t = b.t + dlt;
  evalD(c, vals, r1, L);
  double slope = (r0[0] - r1[0]) / dlt;
  if (std::abs(slope) < 1e-300) return b.t;
  return b.t - r0[0] / slope;
}

std::string idStr(int id) { return std::to_string(id); }

} // namespace

// ---------------------------------------------------------------------------------------------
void System::finalize() {
  m = 0;
  for (auto& b : blocks) {
    b.rowStart = m;
    if (b.active) m += b.rows;
  }
}

void System::residual(const std::vector<double>& vals, Eigen::VectorXd& r) const {
  r.setZero(m);
  double out[2];
  for (const auto& b : blocks) {
    if (!b.active) continue;
    evalD(b, vals, out, L);
    for (int i = 0; i < b.rows; ++i) r[b.rowStart + i] = out[i];
  }
}

double System::blockResidualMax(const Block& b, const std::vector<double>& vals) const {
  double out[2];
  evalD(b, vals, out, L);
  double mx = 0;
  for (int i = 0; i < b.rows; ++i) mx = std::max(mx, std::abs(out[i]));
  return mx;
}

void System::jacobian(const std::vector<double>& vals, Eigen::SparseMatrix<double>& J, Eigen::VectorXd& r) const {
  std::vector<Eigen::Triplet<double>> trip;
  trip.reserve(m * 8);
  r.setZero(m);
  Dual loc[MAXV], out[2];
  for (const auto& b : blocks) {
    if (!b.active) continue;
    const int nv = (int)b.slots.size();
    for (int k = 0; k < nv; ++k) {
      loc[k] = Dual(vals[b.slots[k]]);
      loc[k].d[k] = 1.0;
    }
    evalBlock<Dual>(b, loc, out, L);
    for (int i = 0; i < b.rows; ++i) {
      r[b.rowStart + i] = out[i].v;
      for (int k = 0; k < nv; ++k) {
        int col = slotCol[b.slots[k]];
        double d = out[i].d[k];
        if (col >= 0 && d != 0.0 && std::isfinite(d)) trip.emplace_back(b.rowStart + i, col, d);
      }
    }
  }
  J.resize(m, n);
  J.setFromTriplets(trip.begin(), trip.end());  // duplicates (repeated slots) are summed
}

// ---------------------------------------------------------------------------------------------
bool build(const Sketch& sk, const ParamValues& params, bool measuredFallback, Built& B) {
  System& S = B.S;
  B.vals.clear();
  for (size_t i = 0; i < sk.points.size(); ++i) {
    const auto& p = sk.points[i];
    if (B.ptIndex.count(p.id)) { B.error = "Yinelenen nokta kimliği: " + idStr(p.id); return false; }
    B.ptIndex[p.id] = (int)i;
    B.pointIds.push_back(p.id);
    B.vals.push_back(p.x);
    B.vals.push_back(p.y);
  }
  for (const auto& e : sk.entities)
    if (e.type == SkEntityType::Circle) {
      B.circleSlot[e.id] = (int)B.vals.size();
      B.vals.push_back(e.radius);
    }
  S.slotCol.assign(B.vals.size(), -1);
  S.colSlot.clear();
  for (size_t s = 0; s < B.vals.size(); ++s) {
    bool fixed = s < 2 * sk.points.size() && B.pointIds[s / 2] == 0;
    if (!fixed) { S.slotCol[s] = (int)S.colSlot.size(); S.colSlot.push_back((int)s); }
  }
  S.n = (int)S.colSlot.size();

  auto px = [&](int pid) { return 2 * B.ptIndex.at(pid); };
  // entity validation
  for (const auto& e : sk.entities) {
    size_t need = e.type == SkEntityType::Line ? 2 : e.type == SkEntityType::Circle ? 1 : 3;
    if (e.points.size() != need) { B.error = "Varlık " + idStr(e.id) + ": nokta sayısı hatalı"; return false; }
    for (int p : e.points)
      if (!B.ptIndex.count(p)) { B.error = "Varlık " + idStr(e.id) + ": bilinmeyen nokta " + idStr(p); return false; }
    if (e.type == SkEntityType::Line) B.lines.push_back({B.ptIndex[e.points[0]], B.ptIndex[e.points[1]]});
    if (e.type == SkEntityType::Arc) {
      B.arcs.push_back({B.ptIndex[e.points[0]], B.ptIndex[e.points[1]], B.ptIndex[e.points[2]]});
      Block b; b.kind = Kind::Arc; b.cid = -1; b.rows = 1;
      int c = px(e.points[0]), s = px(e.points[1]), en = px(e.points[2]);
      b.slots = {c, c + 1, s, s + 1, en, en + 1};
      S.blocks.push_back(b);
    }
  }

  std::string err;
  auto fail = [&](const SkConstraint& c, const std::string& what) {
    B.error = "Kısıt " + idStr(c.id) + " (" + toString(c.type) + "): " + what;
    return false;
  };
  auto addPt = [&](std::vector<int>& sl, int pid) {
    int i = px(pid); sl.push_back(i); sl.push_back(i + 1);
  };
  auto addLine = [&](std::vector<int>& sl, const SkEntity* e) { addPt(sl, e->points[0]); addPt(sl, e->points[1]); };
  auto addRound = [&](std::vector<int>& sl, const SkEntity* e) {
    addPt(sl, e->points[0]);
    if (e->type == SkEntityType::Arc) addPt(sl, e->points[1]);
    else sl.push_back(B.circleSlot.at(e->id));
    return e->type == SkEntityType::Arc;
  };

  // point equivalence through shared ids / Coincident constraints (endpoint tangency detection)
  std::map<int, int> eq;
  std::function<int(int)> rootOf = [&](int id) {
    auto it = eq.find(id);
    if (it == eq.end() || it->second == id) return id;
    int r = rootOf(it->second);
    eq[id] = r;
    return r;
  };
  for (const auto& c : sk.constraints)
    if (c.type == SkConstraintType::Coincident && c.refs.size() == 2) {
      int a = rootOf(c.refs[0]), b = rootOf(c.refs[1]);
      if (a != b) eq[std::max(a, b)] = std::min(a, b);
    }
  auto endpoints = [](const SkEntity* e) -> std::vector<int> {
    if (e->type == SkEntityType::Line) return {e->points[0], e->points[1]};
    if (e->type == SkEntityType::Arc) return {e->points[1], e->points[2]};
    return {};
  };
  // returns (pointOfE1, pointOfE2) that are the same location by construction, or (-1,-1)
  auto sharedPoint = [&](const SkEntity* e1, const SkEntity* e2) -> std::pair<int, int> {
    for (int a : endpoints(e1))
      for (int b : endpoints(e2))
        if (rootOf(a) == rootOf(b)) return {a, b};
    return {-1, -1};
  };

  struct Pending { size_t block; bool needMeasure; };
  std::vector<Pending> pend;

  for (const auto& c : sk.constraints) {
    auto ent = [&](size_t k) -> const SkEntity* { return k < c.refs.size() ? sk.entity(c.refs[k]) : nullptr; };
    auto isPt = [&](size_t k) { return k < c.refs.size() && B.ptIndex.count(c.refs[k]) > 0; };
    auto isLine = [&](size_t k) { auto e = ent(k); return e && e->type == SkEntityType::Line; };
    auto isRound = [&](size_t k) { auto e = ent(k); return e && e->type != SkEntityType::Line; };
    auto nref = [&](size_t n) { return c.refs.size() == n; };
    const std::string badRef = "geçersiz referans";

    Block b; b.cid = c.id;
    double target = 0; bool needMeasure = false;
    if (isDimensional(c.type)) {
      b.dimensional = true;
      if (c.valueExpr.empty()) {
        if (!measuredFallback) return fail(c, "değer ifadesi yok");
        needMeasure = true;
      } else {
        try {
          target = pc::evalWith(c.valueExpr, params);
          if (!std::isfinite(target)) throw pc::ExprError("sonuç sayı değil");
        } catch (const std::exception& ex) {
          if (!measuredFallback) return fail(c, "ifade hatası '" + c.valueExpr + "': " + ex.what());
          needMeasure = true;
        }
      }
    }
    bool skip = false;
    switch (c.type) {
      case SkConstraintType::Coincident:
        if (!nref(2) || !isPt(0) || !isPt(1)) return fail(c, badRef);
        if (c.refs[0] == c.refs[1]) { skip = true; break; }
        b.kind = Kind::Coinc; b.rows = 2; addPt(b.slots, c.refs[0]); addPt(b.slots, c.refs[1]);
        break;
      case SkConstraintType::Horizontal: case SkConstraintType::Vertical: {
        if (!nref(1) || !isLine(0)) return fail(c, badRef);
        auto e = ent(0); int o = c.type == SkConstraintType::Horizontal ? 1 : 0;
        b.kind = Kind::Diff; b.slots = {px(e->points[0]) + o, px(e->points[1]) + o};
        break;
      }
      case SkConstraintType::HorizontalPts: case SkConstraintType::VerticalPts: {
        if (!nref(2) || !isPt(0) || !isPt(1)) return fail(c, badRef);
        int o = c.type == SkConstraintType::HorizontalPts ? 1 : 0;
        b.kind = Kind::Diff; b.slots = {px(c.refs[0]) + o, px(c.refs[1]) + o};
        break;
      }
      case SkConstraintType::Parallel: case SkConstraintType::Perpendicular:
        if (!nref(2) || !isLine(0) || !isLine(1)) return fail(c, badRef);
        b.kind = c.type == SkConstraintType::Parallel ? Kind::Parallel : Kind::Perp;
        addLine(b.slots, ent(0)); addLine(b.slots, ent(1));
        break;
      case SkConstraintType::Tangent: {
        if (!nref(2) || !ent(0) || !ent(1)) return fail(c, badRef);
        if (isLine(0) && isLine(1)) return fail(c, "iki doğru arasında teğetlik tanımsız");
        if (isLine(0) || isLine(1)) {
          const SkEntity* l = isLine(0) ? ent(0) : ent(1);
          const SkEntity* r = isLine(0) ? ent(1) : ent(0);
          auto sp = sharedPoint(l, r);
          if (sp.first >= 0) {  // endpoint tangency: radius at the joint perpendicular to the line
            b.kind = Kind::TanLP; addLine(b.slots, l); addPt(b.slots, sp.second); addPt(b.slots, r->points[0]);
          } else {
            b.kind = Kind::TanLR; addLine(b.slots, l); b.arc1 = addRound(b.slots, r);
            b.s = 0;  // sign chosen below from the current configuration
          }
        } else {
          auto sp = sharedPoint(ent(0), ent(1));
          if (sp.first >= 0) {  // endpoint tangency: centers and joint collinear
            b.kind = Kind::TanRP;
            addPt(b.slots, ent(0)->points[0]); addPt(b.slots, sp.first);
            addPt(b.slots, ent(1)->points[0]); addPt(b.slots, sp.second);
          } else {
            b.kind = Kind::TanRR; b.arc1 = addRound(b.slots, ent(0)); b.arc2 = addRound(b.slots, ent(1));
          }
        }
        break;
      }
      case SkConstraintType::Equal:
        if (!nref(2) || !ent(0) || !ent(1)) return fail(c, badRef);
        if (isLine(0) && isLine(1)) { b.kind = Kind::EqLen; addLine(b.slots, ent(0)); addLine(b.slots, ent(1)); }
        else if (isRound(0) && isRound(1)) { b.kind = Kind::EqRad; b.arc1 = addRound(b.slots, ent(0)); b.arc2 = addRound(b.slots, ent(1)); }
        else return fail(c, "doğru ile yay/daire eşitlenemez");
        break;
      case SkConstraintType::Fixed: {
        if (!nref(1) || !isPt(0)) return fail(c, badRef);
        if (c.refs[0] == 0) { skip = true; break; }
        b.kind = Kind::Fixed; b.rows = 2; addPt(b.slots, c.refs[0]);
        b.c0x = B.vals[b.slots[0]]; b.c0y = B.vals[b.slots[1]];
        break;
      }
      case SkConstraintType::PointOnEntity:
        if (!nref(2) || !isPt(0) || !ent(1)) return fail(c, badRef);
        if (isLine(1)) { b.kind = Kind::LineDist; b.s = 1; b.t = 0; addPt(b.slots, c.refs[0]); addLine(b.slots, ent(1)); }
        else { b.kind = Kind::OnRound; addPt(b.slots, c.refs[0]); b.arc1 = addRound(b.slots, ent(1)); }
        break;
      case SkConstraintType::Midpoint:
        if (!nref(2) || !isPt(0) || !isLine(1)) return fail(c, badRef);
        b.kind = Kind::Mid; b.rows = 2; addPt(b.slots, c.refs[0]); addLine(b.slots, ent(1));
        break;
      case SkConstraintType::Symmetric:
        if (!nref(3) || !isPt(0) || !isPt(1) || !isLine(2)) return fail(c, badRef);
        b.kind = Kind::Sym; b.rows = 2; addPt(b.slots, c.refs[0]); addPt(b.slots, c.refs[1]); addLine(b.slots, ent(2));
        break;
      case SkConstraintType::Concentric:
        if (!nref(2) || !isRound(0) || !isRound(1)) return fail(c, badRef);
        if (ent(0)->points[0] == ent(1)->points[0]) { skip = true; break; }
        b.kind = Kind::Coinc; b.rows = 2; addPt(b.slots, ent(0)->points[0]); addPt(b.slots, ent(1)->points[0]);
        break;
      case SkConstraintType::Distance:
        if (!nref(2) || !isPt(0) || !isPt(1)) return fail(c, badRef);
        if (!needMeasure && target <= 0) return fail(c, "mesafe pozitif olmalı");
        b.kind = Kind::Dist; addPt(b.slots, c.refs[0]); addPt(b.slots, c.refs[1]);
        break;
      case SkConstraintType::DistanceX: case SkConstraintType::DistanceY: {
        if (!nref(2) || !isPt(0) || !isPt(1)) return fail(c, badRef);
        int o = c.type == SkConstraintType::DistanceY ? 1 : 0;
        b.kind = Kind::Diff; b.slots = {px(c.refs[0]) + o, px(c.refs[1]) + o};
        break;
      }
      case SkConstraintType::PointLineDistance:
        if (!nref(2) || !isPt(0) || !isLine(1)) return fail(c, badRef);
        if (!needMeasure && target < 0) return fail(c, "mesafe negatif olamaz");
        b.kind = Kind::LineDist; b.s = 0; addPt(b.slots, c.refs[0]); addLine(b.slots, ent(1));
        break;
      case SkConstraintType::Length:
        if (!nref(1) || !isLine(0)) return fail(c, badRef);
        if (!needMeasure && target <= 0) return fail(c, "uzunluk pozitif olmalı");
        b.kind = Kind::Dist; addLine(b.slots, ent(0));
        break;
      case SkConstraintType::Radius: case SkConstraintType::Diameter:
        if (!nref(1) || !isRound(0)) return fail(c, badRef);
        if (!needMeasure && target <= 0) return fail(c, "değer pozitif olmalı");
        b.kind = Kind::Rad; b.kfac = c.type == SkConstraintType::Diameter ? 0.5 : 1.0; b.arc1 = addRound(b.slots, ent(0));
        break;
      case SkConstraintType::Angle:
        if (!nref(2) || !isLine(0) || !isLine(1)) return fail(c, badRef);
        b.kind = Kind::Angle; addLine(b.slots, ent(0)); addLine(b.slots, ent(1));
        target = target * kPi / 180.0;
        break;
    }
    if (skip) continue;
    b.t = b.tFull = target;
    pend.push_back({S.blocks.size(), needMeasure});
    S.blocks.push_back(b);
  }

  // characteristic scale
  double minx = 1e300, miny = 1e300, maxx = -1e300, maxy = -1e300, maxT = 0;
  for (size_t i = 0; i < sk.points.size(); ++i) {
    minx = std::min(minx, B.vals[2 * i]); maxx = std::max(maxx, B.vals[2 * i]);
    miny = std::min(miny, B.vals[2 * i + 1]); maxy = std::max(maxy, B.vals[2 * i + 1]);
  }
  for (const auto& e : sk.entities)
    if (e.type == SkEntityType::Circle) maxT = std::max(maxT, 2 * std::abs(e.radius));
  for (const auto& b : S.blocks)
    if (b.dimensional && b.kind != Kind::Angle) maxT = std::max(maxT, std::abs(b.tFull) * (b.kind == Kind::Rad ? 2 * b.kfac : 1.0));
  double diag = maxx >= minx ? std::hypot(maxx - minx, maxy - miny) : 0.0;
  S.L = std::max({1.0, diag, maxT});
  S.tol = 1e-9 * S.L;

  // branch choices + measured values at the initial configuration
  for (auto& b : S.blocks) {
    double loc[MAXV];
    for (size_t k = 0; k < b.slots.size(); ++k) loc[k] = B.vals[b.slots[k]];
    if (b.kind == Kind::LineDist && b.s == 0) {
      double sd = sdist(loc[0], loc[1], loc[2], loc[3], loc[4], loc[5]);
      b.s = sd >= 0 ? 1 : -1;
    } else if (b.kind == Kind::TanLR) {
      double sd = sdist(loc[4], loc[5], loc[0], loc[1], loc[2], loc[3]);
      b.s = sd >= 0 ? 1 : -1;
    } else if (b.kind == Kind::TanRR) {
      const double* w = loc + roundSize(b.arc1);
      double r1 = rad(loc, b.arc1), r2 = rad(w, b.arc2), d = std::hypot(w[0] - loc[0], w[1] - loc[1]);
      if (std::abs(d - (r1 + r2)) <= std::abs(d - std::abs(r1 - r2))) b.mode = 0;
      else { b.mode = 1; b.s = r1 >= r2 ? 1 : -1; }
    }
  }
  for (auto& p : pend) {
    Block& b = S.blocks[p.block];
    b.t0 = measure(b, B.vals, S.L);
    if (p.needMeasure) b.t = b.tFull = b.t0;
  }
  S.finalize();
  return true;
}

// ---------------------------------------------------------------------------------------------
LMStats runLM(const System& S, std::vector<double>& vals, int maxIter) {
  LMStats st;
  Eigen::VectorXd r, rn;
  S.residual(vals, r);
  if (S.m == 0 || S.n == 0) {
    st.maxRes = S.m ? r.lpNorm<Eigen::Infinity>() : 0.0;
    st.converged = st.maxRes <= S.tol;
    return st;
  }
  double cost = r.squaredNorm();
  Eigen::SparseMatrix<double> J, A, I(S.m, S.m);
  I.setIdentity();
  Eigen::SimplicialLDLT<Eigen::SparseMatrix<double>> ldlt;
  const double maxStep = 0.5 * S.L;
  double mu = -1, muMin = 0;
  std::vector<double> trial(vals.size());
  int polish = 0;
  for (int it = 0; it < maxIter; ++it) {
    double mr = r.lpNorm<Eigen::Infinity>();
    if (mr <= S.tol) {
      st.converged = true;
      if (mr <= 1e-4 * S.tol || polish >= 3) break;
      ++polish;
    }
    st.iterations++;
    S.jacobian(vals, J, r);
    A = J * J.transpose();
    double maxDiag = 1.0;
    for (int k = 0; k < A.outerSize(); ++k) maxDiag = std::max(maxDiag, A.coeff(k, k));
    if (mu < 0) { muMin = 1e-13 * maxDiag; mu = muMin; }
    bool accepted = false;
    bool tiny = false;
    for (int inner = 0; inner < 40; ++inner) {
      Eigen::SparseMatrix<double> M = A + mu * I;
      ldlt.compute(M);
      if (ldlt.info() != Eigen::Success) { mu *= 10; continue; }
      Eigen::VectorXd y = ldlt.solve(r);
      Eigen::VectorXd dx = -(J.transpose() * y);
      double dn = dx.lpNorm<Eigen::Infinity>();
      if (!std::isfinite(dn)) { mu *= 10; continue; }
      if (dn > maxStep) dx *= maxStep / dn;
      if (dn < 1e-15 * S.L) { tiny = true; break; }
      trial = vals;
      for (int c = 0; c < S.n; ++c) trial[S.colSlot[c]] += dx[c];
      S.residual(trial, rn);
      double cn = rn.squaredNorm();
      if (std::isfinite(cn) && cn < cost) {
        vals.swap(trial); r = rn; cost = cn; accepted = true;
        mu = std::max(mu * 0.1, muMin);
        break;
      }
      mu *= 10;
      if (mu > 1e12 * maxDiag) break;
    }
    if (!accepted || tiny) break;  // stalled (least-squares minimum) or no progress
  }
  st.maxRes = r.lpNorm<Eigen::Infinity>();
  st.converged = st.maxRes <= S.tol;
  return st;
}

bool runContinuation(System& S, std::vector<double>& vals, int steps, int& iterations) {
  bool ok = true;
  for (int k = 1; k <= steps && ok; ++k) {
    double f = double(k) / steps;
    for (auto& b : S.blocks)
      if (b.dimensional) {
        if (b.kind == Kind::Angle) {
          double d = std::remainder(b.tFull - b.t0, 2 * kPi);
          b.t = b.t0 + d * f;
        } else {
          b.t = b.t0 + (b.tFull - b.t0) * f;
        }
      }
    LMStats st = runLM(S, vals);
    iterations += st.iterations;
    ok = st.converged;
  }
  for (auto& b : S.blocks) b.t = b.tFull;
  if (ok) {  // final polish at the exact targets
    LMStats st = runLM(S, vals);
    iterations += st.iterations;
    ok = st.converged;
  }
  return ok;
}

void rankAnalysis(const System& S, const std::vector<double>& vals, int& rank, std::vector<int>& redundant) {
  rank = 0;
  redundant.clear();
  if (S.m == 0 || S.n == 0) {
    for (size_t i = 0; i < S.blocks.size(); ++i)
      if (S.blocks[i].active && S.blocks[i].cid >= 0) redundant.push_back((int)i);
    return;
  }
  Eigen::SparseMatrix<double> Js;
  Eigen::VectorXd r;
  S.jacobian(vals, Js, r);
  Eigen::MatrixXd J = Eigen::MatrixXd(Js);
  Eigen::MatrixXd Q(S.n, std::min(S.m, S.n));
  int k = 0;
  for (size_t bi = 0; bi < S.blocks.size(); ++bi) {
    const Block& b = S.blocks[bi];
    if (!b.active) continue;
    bool dep = false;
    for (int i = 0; i < b.rows; ++i) {
      Eigen::VectorXd v = J.row(b.rowStart + i).transpose();
      double nr = v.norm();
      if (nr < 1e-12 || k >= Q.cols()) { dep = true; continue; }
      for (int pass = 0; pass < 2 && k > 0; ++pass) v -= Q.leftCols(k) * (Q.leftCols(k).transpose() * v);
      double nv = v.norm();
      if (nv > 1e-8 * nr) Q.col(k++) = v / nv;
      else dep = true;
    }
    if (dep && b.cid >= 0) redundant.push_back((int)bi);
  }
  rank = k;
}

} // namespace pc::sketch::detail
