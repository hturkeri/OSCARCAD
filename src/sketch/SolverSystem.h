#pragma once
// Internal (not a public contract): equation system of the sketch solver.
#include "pc/Parameter.h"
#include "pc/SketchData.h"
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace pc::sketch::detail {

// Kinds of equation blocks. Local variable layouts are documented in SolverSystem.cpp.
enum class Kind {
  Coinc, Diff, Fixed, Parallel, Perp, Angle, Dist, LineDist, TanLR, TanRR,
  EqLen, EqRad, OnRound, Mid, Sym, Rad, Arc, TanLP, TanRP
};

struct Block {
  Kind kind = Kind::Coinc;
  int cid = -1;                 // constraint id, -1 = implicit (arc radius equation)
  int rows = 1;
  std::vector<int> slots;       // global slot indices (local variable order)
  double t = 0;                 // current target (dimensional; angles in radians)
  double tFull = 0;             // requested target
  double t0 = 0;                // value measured at the initial configuration
  bool dimensional = false;
  double s = 1;                 // sign choice (branch)
  int mode = 0;                 // TanRR: 0 external, 1 internal
  bool arc1 = false, arc2 = false; // round entity layout: arc (center,start) vs circle (center,r)
  double c0x = 0, c0y = 0;      // Fixed: locked coordinates
  double kfac = 1;              // Rad: r = kfac * t
  bool active = true;
  int rowStart = 0;
};

struct System {
  std::vector<Block> blocks;
  std::vector<int> slotCol;     // slot -> unknown column (-1 = fixed slot)
  std::vector<int> colSlot;     // column -> slot
  int n = 0, m = 0;
  double L = 1, tol = 1e-9;

  void finalize();
  void residual(const std::vector<double>& vals, Eigen::VectorXd& r) const;
  void jacobian(const std::vector<double>& vals, Eigen::SparseMatrix<double>& J, Eigen::VectorXd& r) const;
  double blockResidualMax(const Block& b, const std::vector<double>& vals) const;
};

struct Built {
  System S;
  std::vector<double> vals;           // slot values
  std::vector<int> pointIds;          // point index -> id (slots 2i, 2i+1)
  std::map<int, int> ptIndex;         // id -> point index
  std::map<int, int> circleSlot;      // circle entity id -> radius slot
  std::vector<std::array<int, 2>> lines;  // point indices
  std::vector<std::array<int, 3>> arcs;   // center, start, end point indices
  std::string error;
};

// Build the equation system. If measuredFallback, unevaluable expressions take the currently
// measured value instead of failing.
bool build(const Sketch& sk, const ParamValues& params, bool measuredFallback, Built& out);

struct LMStats { bool converged = false; double maxRes = 0; int iterations = 0; };
LMStats runLM(const System& S, std::vector<double>& vals, int maxIter = 120);
bool runContinuation(System& S, std::vector<double>& vals, int steps, int& iterations);
void rankAnalysis(const System& S, const std::vector<double>& vals, int& rank, std::vector<int>& redundantBlocks);

} // namespace pc::sketch::detail
