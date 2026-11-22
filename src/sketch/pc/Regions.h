#pragma once
// PersistCAD sketch — tolerant planar REGIONS (SpaceClaim "Doldur (Fill)" semantics). SC-PULLUPTO,
// docs/spaceclaim_ux/PULL_UPTO.md.
//
// arrangeRegions(): every bounded minimal face of the planar arrangement of the NON-construction curves of a solved
// sketch becomes a region; a connected group of curves lying inside a region is a hole of that region (nested loops
// -> regions with holes, the island inside a hole is a region of its own). Curves from imported drawings rarely meet
// exactly, so before the arrangement is built:
//   * end points closer than gapTol are merged (gap closing; also "overlapping" end points),
//   * an end point closer than gapTol to the inside of another curve splits that curve there (T-junction),
//   * proper crossings split both curves, duplicate (overlapping) pieces are dropped,
//   * dangling chains are pruned; their free ends are reported as openPoints (the gaps to show the user).
// Region order: outer-boundary area descending, then centroid (u, then v). Persistent naming: every piece keeps the
// entity id of the curve it came from (Profile::edges), so Extrude side faces stay "<fid>.side.e<id>" (":k" if split).
#include "pc/SketchSolver.h"
#include <gp_Pnt2d.hxx>
#include <string>
#include <vector>

namespace pc::sketch {

struct RegionResult {
  Profile profile;                    // faces = regions in region order; edges -> entity ids; ok / message
  bool legacy = false;                // true: the regions ARE buildProfile()'s faces (classic profile, unchanged)
  double gapTol = 0;                  // tolerance used (mm)
  std::vector<int> holes;             // number of inner loops per region
  std::vector<double> areas;          // net area per region (mm^2)
  std::vector<gp_Pnt2d> openPoints;   // dangling curve ends (sketch u,v): the open gaps
  std::vector<int> openEntities;      // entity ids that end at an open point (sorted, unique)
  int healedGaps = 0;                 // end point groups merged although not coincident (0 < d <= gapTol)
  int tJunctions = 0;                 // curves split at another curve's end point
  int crossings = 0;                  // proper intersections split
  int overlaps = 0;                   // duplicate (overlapping) pieces dropped
  double nearestGap = -1;             // smallest distance between two open points, -1 if fewer than two
};

// Default gap tolerance of a sketch: min(0.1 mm, 0.5 % of the size of its non-construction geometry).
double autoGapTol(const Sketch& sk);

// Pure tolerant arrangement (see above). gapTol <= 0 -> autoGapTol(sk).
RegionResult arrangeRegions(const Sketch& sk, const gp_Ax3& frame, double gapTol = -1);

// The REGION LIST of a sketch, shared by the designer ("sketch:<sid>:r<k>") and Extrude/Revolve `regions=`:
// the classic buildProfile() faces when that profile is clean and has as many faces as the arrangement (so existing
// documents keep their regions), otherwise arrangeRegions(). freePoints are always buildProfile()'s.
RegionResult sketchRegions(const Sketch& sk, const gp_Ax3& frame, double gapTol = -1);

// Human readable summary ("2 bölge (1 delikli), 3 boşluk kapatıldı (≤ 0.05 mm), 1 T-birleşim") / open-gap message.
std::string describeRegions(const RegionResult& r);

} // namespace pc::sketch
