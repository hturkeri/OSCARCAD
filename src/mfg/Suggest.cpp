// PersistCAD mfg — driving-parameter detection and suggested values.
//
// A rule limit on a derived quantity q (wall, L/D, extent, ...) is turned into a parameter value by
// solving q(p) = limit for one parameter p. Dependent parameters are re-evaluated through the
// document (ParamTable::evaluate with an override), and sketch-derived quantities re-solve the sketches
// (buildModel) — so relationships like "hole centre = W/2 - E" are handled generically.
#include "MfgInternal.h"

#include "pc/Expression.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>

namespace pc::mfg::detail {

std::vector<std::string> orderCandidates(const Document& doc, const std::vector<std::string>& exprs) {
  std::vector<std::string> direct;
  std::set<std::string> seen;
  for (auto& e : exprs) {
    if (e.empty()) continue;
    std::vector<std::string> names;
    try {
      names = referencedNames(e);
    } catch (const std::exception&) {
      continue;
    }
    for (auto& n : names)
      if (doc.params.find(n) && seen.insert(n).second) direct.push_back(n);
  }
  // transitive dependencies of non-exposed directs
  std::vector<std::string> depsExposed, depsHidden;
  std::set<std::string> visited(seen.begin(), seen.end());
  std::function<void(const std::string&)> walk = [&](const std::string& n) {
    const Parameter* p = doc.params.find(n);
    if (!p) return;
    std::vector<std::string> names;
    try {
      names = referencedNames(p->expression);
    } catch (const std::exception&) {
      return;
    }
    for (auto& d : names) {
      const Parameter* q = doc.params.find(d);
      if (!q || !visited.insert(d).second) continue;
      (q->exposed ? depsExposed : depsHidden).push_back(d);
      walk(d);
    }
  };
  for (auto& n : direct)
    if (!doc.params.find(n)->exposed) walk(n);
  std::vector<std::string> out;
  for (auto& n : direct)
    if (doc.params.find(n)->exposed) out.push_back(n);
  for (auto& n : depsExposed) out.push_back(n);
  for (auto& n : direct)
    if (!doc.params.find(n)->exposed) out.push_back(n);
  for (auto& n : depsHidden) out.push_back(n);
  return out;
}

// Parameters whose current value does not follow from their own expression were overridden by the
// caller (designer/app edit): they stay fixed while another parameter is probed.
std::map<std::string, std::string> fixedOverrides(const Document& doc, const ParamValues& values) {
  std::map<std::string, std::string> fixed;
  char buf[64];
  for (auto& P : doc.params.all()) {
    auto it = values.find(P.name);
    if (it == values.end()) continue;
    double e = std::numeric_limits<double>::quiet_NaN();
    try {
      e = evalWith(P.expression, values);
    } catch (const std::exception&) {}
    if (!(std::fabs(e - it->second) <= 1e-12 * std::max(1.0, std::fabs(it->second)))) {
      std::snprintf(buf, sizeof(buf), it->second < 0 ? "(%.17g)" : "%.17g", it->second);
      fixed[P.name] = buf;
    }
  }
  return fixed;
}

std::optional<ParamValues> evalWithOverride(const Document& doc, const ParamValues& values, const std::string& param,
                                            double value) {
  auto ov = fixedOverrides(doc, values);
  char buf[64];
  std::snprintf(buf, sizeof(buf), value < 0 ? "(%.17g)" : "%.17g", value);
  ov[param] = buf;
  return doc.params.evaluate(nullptr, ov);
}

namespace {

using F1 = std::function<std::optional<double>(double)>;

// Root of g on [lo, hi] near x0: secant first, then geometric bracketing + bisection.
std::optional<double> solve1D(const F1& g, double x0, double lo, double hi, int maxBracket) {
  auto g0 = g(x0);
  if (!g0) return std::nullopt;
  const double ftol = 1e-9;
  if (std::fabs(*g0) < ftol) return x0;
  // --- secant
  {
    double xa = x0, ga = *g0;
    double h = std::max(1e-6, 1e-3 * std::fabs(x0));
    double xb = std::clamp(x0 + h, lo, hi);
    if (xb == xa) xb = std::clamp(x0 - h, lo, hi);
    auto gb = g(xb);
    for (int it = 0; gb && it < 30; ++it) {
      if (std::fabs(*gb) < ftol) return xb;
      double den = *gb - ga;
      if (std::fabs(den) < 1e-300) break;
      double xn = xb - *gb * (xb - xa) / den;
      if (!std::isfinite(xn)) break;
      xn = std::clamp(xn, lo, hi);
      if (std::fabs(xn - xb) < 1e-12 * std::max(1.0, std::fabs(xb))) {
        auto gn = g(xn);
        if (gn && std::fabs(*gn) < 1e-6) return xn;
        break;
      }
      xa = xb;
      ga = *gb;
      xb = xn;
      gb = g(xb);
    }
  }
  // --- bracketing on a geometric grid around x0, nearest sign change first
  if (maxBracket <= 0) return std::nullopt;
  double base = std::fabs(x0) > 1e-9 ? std::fabs(x0) : 1.0;
  double prevX = x0, prevG = *g0;
  for (int dirSign : {+1, -1}) {
    prevX = x0;
    prevG = *g0;
    for (int k = 1; k <= maxBracket; ++k) {
      double step = base * (std::pow(1.5, k) - 1.0);
      double x = std::clamp(x0 + dirSign * step, lo, hi);
      auto gx = g(x);
      if (!gx) break;
      if ((*gx > 0) != (prevG > 0)) {
        double a = prevX, b = x, ga = prevG;
        for (int it = 0; it < 100; ++it) {
          double mid = 0.5 * (a + b);
          auto gm = g(mid);
          if (!gm) return std::nullopt;
          if (std::fabs(*gm) < ftol || std::fabs(b - a) < 1e-12 * std::max(1.0, std::fabs(mid))) return mid;
          if ((*gm > 0) == (ga > 0)) { a = mid; ga = *gm; } else b = mid;
        }
        return 0.5 * (a + b);
      }
      prevX = x;
      prevG = *gx;
      if (x == lo || x == hi) break;
    }
  }
  return std::nullopt;
}

bool satisfies(double m, double limit, Want want) {
  const double tol = 1e-9 * std::max(1.0, std::fabs(limit));
  return want == Want::AtLeast ? m >= limit - tol : m <= limit + tol;
}

using MetricV = std::function<std::optional<double>(const ParamValues&)>;

Suggestion suggestCore(const Document& doc, const ParamValues& values, const std::vector<std::string>& candidates,
                       const MetricV& metric, double limit, Want want, int maxCand, bool exact = false) {
  Suggestion out;
  const auto fixed = fixedOverrides(doc, values);
  auto evalAt = [&](const std::string& p, double x) -> std::optional<ParamValues> {
    auto ov = fixed;
    char buf[64];
    std::snprintf(buf, sizeof(buf), x < 0 ? "(%.17g)" : "%.17g", x);
    ov[p] = buf;
    return doc.params.evaluate(nullptr, ov);
  };
  auto m0 = metric(values);
  std::vector<std::string> drivers;
  std::string chosen;
  int tried = 0;
  // apps only offer EXPOSED parameters: probe them first (derived ones are reached through their
  // expressions because dependent parameters are re-evaluated by evalAt)
  std::vector<std::string> ordered;
  for (auto& p : candidates)
    if (auto* P = doc.params.find(p); P && P->exposed) ordered.push_back(p);
  for (auto& p : candidates)
    if (auto* P = doc.params.find(p); !P || !P->exposed) ordered.push_back(p);
  for (auto& p : ordered) {
    if (tried >= maxCand) break;
    const Parameter* P = doc.params.find(p);
    auto itv = values.find(p);
    if (!P || itv == values.end() || !m0) continue;
    ++tried;
    double x0 = itv->second;
    double h = std::max(1e-4, 1e-3 * std::fabs(x0));
    std::optional<double> m1;
    double xh = x0 + h;
    if (auto v1 = evalAt(p, xh)) m1 = metric(*v1);
    if (!m1 || std::fabs(*m1 - *m0) < 1e-10 * std::max(1.0, std::fabs(*m0))) {
      xh = x0 - h;
      m1.reset();
      if (auto v1 = evalAt(p, xh)) m1 = metric(*v1);
    }
    if (!m1 || std::fabs(*m1 - *m0) < 1e-10 * std::max(1.0, std::fabs(*m0))) continue;
    drivers.push_back(p);
    if (!chosen.empty()) continue;
    // solve metric(p) = limit
    double lo = P->kind == ParamKind::Length || P->kind == ParamKind::Count ? 1e-6 : -1e6, hi = 1e6;
    F1 g = [&](double x) -> std::optional<double> {
      auto v = evalAt(p, x);
      if (!v) return std::nullopt;
      auto mm = metric(*v);
      if (!mm) return std::nullopt;
      return *mm - limit;
    };
    auto root = solve1D(g, x0, lo, hi, 12);
    if (!root) continue;
    double x = *root;
    auto ok = [&](double c) {
      auto v = evalAt(p, c);
      if (!v) return false;
      auto mm = metric(*v);
      return mm && satisfies(*mm, limit, want);
    };
    std::optional<double> pick;
    if (exact) {
      pick = std::round(x / 1e-3) * 1e-3;
    } else {
      // the current value violates the limit -> the admissible side is the side of the root away from x0
      const int side = x >= x0 ? +1 : -1;
      std::vector<double> grids;
      if (P->kind == ParamKind::Count) grids = {1.0};
      else if (P->kind == ParamKind::Angle) grids = {0.5, 0.1, 0.01};
      else if (std::fabs(x) >= 5) grids = {0.1, 0.05, 0.01, 0.001};
      else if (std::fabs(x) >= 0.5) grids = {0.05, 0.01, 0.001};
      else grids = {0.01, 0.001};
      for (double st : grids) {
        // nearest grid value on the admissible side (tiny margin against round-off), verified
        double c = side > 0 ? std::ceil(x / st - 1e-9) * st : std::floor(x / st + 1e-9) * st;
        for (int n = 0; n < 3 && !pick; ++n, c += side * st) {
          if (P->kind == ParamKind::Length && c <= 0) break;
          if (ok(c)) pick = c;
        }
        if (pick) break;
      }
      if (!pick && ok(x)) pick = x;
      if (!pick) continue;
    }
    bool curIn = x0 >= P->minValue - 1e-12 && x0 <= P->maxValue + 1e-12;
    bool solIn = *pick >= P->minValue - 1e-12 && *pick <= P->maxValue + 1e-12;
    if (!solIn && curIn) continue;  // unreachable within the user range: try the next driver
    chosen = p;
    out.value = *pick;
    out.relation = exact ? 0 : (*pick >= x0 ? +1 : -1);
  }
  if (!chosen.empty()) out.params.push_back(chosen);
  for (int pass = 0; pass < 2; ++pass)
    for (auto& d : drivers) {
      const Parameter* P = doc.params.find(d);
      bool ex = P && P->exposed;
      if (d != chosen && ex == (pass == 0)) out.params.push_back(d);
    }
  if (out.params.empty() && !candidates.empty()) out.params.push_back(candidates.front());
  return out;
}

}  // namespace

Suggestion suggestByProbe(const Document& doc, const ParamValues& values, const std::vector<std::string>& candidates,
                          const Metric& metric, double limit, Want want) {
  MetricV mv = [&](const ParamValues& v) -> std::optional<double> {
    AModel m = buildModel(doc, v);
    return metric(m);
  };
  return suggestCore(doc, values, candidates, mv, limit, want, 8);
}

Suggestion suggestByExpr(const Document& doc, const ParamValues& values, const std::string& expr, double limit,
                         Want want) {
  auto cands = orderCandidates(doc, {expr});
  MetricV mv = [&](const ParamValues& v) -> std::optional<double> {
    try {
      double r = evalWith(expr, v);
      if (!std::isfinite(r)) return std::nullopt;
      return r;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  return suggestCore(doc, values, cands, mv, limit, want, 16);
}

Suggestion suggestExactByExpr(const Document& doc, const ParamValues& values, const std::string& expr, double target) {
  auto cands = orderCandidates(doc, {expr});
  MetricV mv = [&](const ParamValues& v) -> std::optional<double> {
    try {
      return evalWith(expr, v);
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  return suggestCore(doc, values, cands, mv, target, Want::AtLeast, 16, true);
}

std::optional<double> solve1DPublic(const std::function<std::optional<double>(double)>& g, double x0, double lo,
                                    double hi) {
  return solve1D(g, x0, lo, hi, 80);
}

}  // namespace pc::mfg::detail

namespace pc::mfg {

std::optional<double> solveParamFor(const std::string& expr, const std::string& param, const ParamValues& values,
                                    double target) {
  ParamValues v = values;
  auto it = v.find(param);
  double x0 = it != v.end() && it->second > 1e-6 ? it->second : 1.0;
  if (x0 > 1e6) x0 = 1e6;
  auto g = [&](double x) -> std::optional<double> {
    v[param] = x;
    try {
      double r = evalWith(expr, v);
      if (!std::isfinite(r)) return std::nullopt;
      return r - target;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  };
  // reject constant expressions (param not influencing expr)
  auto ga = g(x0), gb = g(x0 * 1.01 + 1e-6);
  if (!ga || !gb) return std::nullopt;
  if (std::fabs(*ga) < 1e-9) return x0;
  if (std::fabs(*ga - *gb) < 1e-15) {
    auto gc = g(x0 * 0.5);
    if (!gc || std::fabs(*gc - *ga) < 1e-15) return std::nullopt;
  }
  auto root = detail::solve1DPublic(g, x0, 1e-6, 1e6);
  if (!root) return std::nullopt;
  auto gr = g(*root);
  if (!gr || std::fabs(*gr) > 1e-6 * std::max(1.0, std::fabs(target))) return std::nullopt;
  return root;
}

}  // namespace pc::mfg
