// PersistCAD sketch solver: public entry points solve() / degreesOfFreedom().
#include "pc/SketchSolver.h"

#include "SolverSystem.h"
#include <cmath>
#include <sstream>

namespace pc::sketch {

using namespace detail;

namespace {

std::string joinIds(const std::vector<int>& ids) {
  std::ostringstream os;
  for (size_t i = 0; i < ids.size(); ++i) os << (i ? ", " : "") << ids[i];
  return os.str();
}

// Did the solution jump to another branch (reversed line, flipped arc, negative radius)?
bool flipped(const Built& B, const std::vector<double>& x0, const std::vector<double>& x) {
  const double eps = 1e-9 * B.S.L;
  for (auto& l : B.lines) {
    double dx0 = x0[2 * l[1]] - x0[2 * l[0]], dy0 = x0[2 * l[1] + 1] - x0[2 * l[0] + 1];
    double dx1 = x[2 * l[1]] - x[2 * l[0]], dy1 = x[2 * l[1] + 1] - x[2 * l[0] + 1];
    if (std::hypot(dx0, dy0) > eps && std::hypot(dx1, dy1) > eps && dx0 * dx1 + dy0 * dy1 < 0) return true;
  }
  for (auto& [eid, slot] : B.circleSlot) {
    (void)eid;
    if (x[slot] <= 0 && x0[slot] > 0) return true;
  }
  auto sweep = [](const std::vector<double>& v, const std::array<int, 3>& a) {
    double cx = v[2 * a[0]], cy = v[2 * a[0] + 1];
    double a0 = std::atan2(v[2 * a[1] + 1] - cy, v[2 * a[1]] - cx);
    double a1 = std::atan2(v[2 * a[2] + 1] - cy, v[2 * a[2]] - cx);
    double s = std::fmod(a1 - a0, 2 * M_PI);
    if (s <= 0) s += 2 * M_PI;
    return s;
  };
  for (auto& a : B.arcs)
    if (std::abs(sweep(x, a) - sweep(x0, a)) > M_PI) return true;
  return false;
}

void writeBack(Sketch& sk, const Built& B, std::vector<double> x) {
  for (const auto& b : B.S.blocks)  // Fixed points keep their exact locked coordinates
    if (b.kind == Kind::Fixed && b.active) { x[b.slots[0]] = b.c0x; x[b.slots[1]] = b.c0y; }
  for (size_t i = 0; i < sk.points.size(); ++i) {
    if (sk.points[i].id == 0) continue;
    sk.points[i].x = x[2 * i];
    sk.points[i].y = x[2 * i + 1];
  }
  for (auto& e : sk.entities) {
    if (e.type == SkEntityType::Circle) e.radius = x[B.circleSlot.at(e.id)];
    else if (e.type == SkEntityType::Arc) {
      int c = B.ptIndex.at(e.points[0]), s = B.ptIndex.at(e.points[1]);
      e.radius = std::hypot(x[2 * s] - x[2 * c], x[2 * s + 1] - x[2 * c + 1]);
    }
  }
}

std::vector<int> cidsOf(const System& S, const std::vector<int>& blockIdx) {
  std::vector<int> ids;
  for (int i : blockIdx) ids.push_back(S.blocks[i].cid);
  return ids;
}

} // namespace

SolveResult solve(Sketch& sk, const ParamValues& params) {
  SolveResult res;
  Built B;
  if (!build(sk, params, false, B)) {
    res.status = SolveStatus::Failed;
    res.message = B.error;
    return res;
  }
  System& S = B.S;
  const std::vector<double> x0 = B.vals;

  std::vector<double> xa = x0;
  LMStats a = runLM(S, xa);
  res.iterations = a.iterations;

  std::vector<double> xs;
  bool accepted = false;
  if (a.converged && !flipped(B, x0, xa)) {
    xs = xa; accepted = true;
  } else {
    bool haveCand = false;
    std::vector<double> cand;
    for (int K : {8, 32}) {
      std::vector<double> xc = x0;
      int its = 0;
      bool ok = runContinuation(S, xc, K, its);
      res.iterations += its;
      if (!ok) continue;
      if (!flipped(B, x0, xc)) { xs = xc; accepted = true; break; }
      if (!haveCand) { cand = xc; haveCand = true; }
    }
    if (!accepted && haveCand) { xs = cand; accepted = true; }
    else if (!accepted && a.converged) { xs = xa; accepted = true; }
  }

  if (accepted) {
    Eigen::VectorXd r;
    S.residual(xs, r);
    res.converged = true;
    res.residual = S.m ? r.lpNorm<Eigen::Infinity>() : 0.0;
    int rank = 0;
    std::vector<int> red;
    rankAnalysis(S, xs, rank, red);
    res.dof = S.n - rank;
    writeBack(sk, B, xs);
    if (!red.empty()) {
      res.status = SolveStatus::OverConstrained;
      res.conflicting = cidsOf(S, red);
      res.message = "Gereksiz (fazla) kısıtlar: " + joinIds(res.conflicting);
      if (res.dof > 0) res.message += " — " + std::to_string(res.dof) + " serbestlik derecesi kaldı";
    } else if (res.dof == 0) {
      res.status = SolveStatus::Ok;
      res.message = "Tam tanımlı";
    } else {
      res.status = SolveStatus::UnderConstrained;
      res.message = std::to_string(res.dof) + " serbestlik derecesi kaldı";
    }
    return res;
  }

  // ---- not solvable: find the conflicting constraints (geometry left untouched) ----
  res.status = SolveStatus::Inconsistent;
  res.converged = false;
  Eigen::VectorXd r;
  S.residual(xa, r);
  res.residual = r.lpNorm<Eigen::Infinity>();
  {
    int rank = 0; std::vector<int> red;
    rankAnalysis(S, xa, rank, red);
    res.dof = S.n - rank;
  }
  const double thr = std::max(1e3 * S.tol, 1e-7 * S.L);
  std::vector<int> cand;
  for (size_t i = 0; i < S.blocks.size(); ++i) {
    const Block& b = S.blocks[i];
    if (b.cid >= 0 && b.active && S.blockResidualMax(b, xa) > thr) cand.push_back((int)i);
  }
  std::vector<int> confirmed;
  for (size_t k = 0; k < cand.size() && k < 24; ++k) {
    System S2 = S;
    S2.blocks[cand[k]].active = false;
    S2.finalize();
    std::vector<double> x = xa;
    if (runLM(S2, x).converged) { confirmed.push_back(cand[k]); continue; }
    x = x0;
    if (runLM(S2, x).converged) confirmed.push_back(cand[k]);
  }
  res.conflicting = cidsOf(S, confirmed.empty() ? cand : confirmed);
  if (res.conflicting.empty()) res.message = "Çözüm yakınsamadı (kalan hata " + std::to_string(res.residual) + ")";
  else res.message = "Çelişen kısıtlar: " + joinIds(res.conflicting);
  return res;
}

int degreesOfFreedom(const Sketch& sk, const ParamValues& params) {
  Sketch copy = sk;
  SolveResult r = solve(copy, params);
  if (r.status != SolveStatus::Failed) return r.dof;
  Built B;
  if (!build(sk, params, true, B)) return -1;
  int rank = 0;
  std::vector<int> red;
  rankAnalysis(B.S, B.vals, rank, red);
  return B.S.n - rank;
}

} // namespace pc::sketch
