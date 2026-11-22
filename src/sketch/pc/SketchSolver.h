#pragma once
// PersistCAD sketch module — geometric constraint solver + profile (face) construction.
// OWNER: Agent A (sketch). Interface is frozen; implementation in src/sketch/*.cpp.
#include "pc/Parameter.h"
#include "pc/SketchData.h"
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Ax3.hxx>
#include <string>
#include <utility>
#include <vector>

namespace pc::sketch {

enum class SolveStatus { Ok, UnderConstrained /*solved but DOF>0*/, OverConstrained, Inconsistent, Failed };

struct SolveResult {
  SolveStatus status = SolveStatus::Failed;
  bool converged = false;          // residual below tolerance
  int dof = -1;                    // remaining degrees of freedom (>=0)
  double residual = 0;             // max |constraint error| after solve
  int iterations = 0;
  std::vector<int> conflicting;    // constraint ids suspected redundant/conflicting
  std::string message;             // human readable (Turkish OK)
  // Redundant-but-consistent (OverConstrained) sketches are usable geometry, so they count as ok.
  bool ok() const {
    return converged && (status == SolveStatus::Ok || status == SolveStatus::UnderConstrained ||
                         status == SolveStatus::OverConstrained);
  }
};

// Solve in place: updates point coordinates (and circle radii) of `sk` so that all constraints
// hold. Dimensional valueExpr are evaluated with `params` (pc::evalWith). Deterministic: same
// input -> same output. Must keep the solution close to the current coordinates (minimal motion).
SolveResult solve(Sketch& sk, const ParamValues& params);

// Degrees of freedom analysis without moving geometry.
int degreesOfFreedom(const Sketch& sk, const ParamValues& params);

// Profile edges keep a link to the sketch entity that produced them (for persistent naming).
struct ProfileEdge {
  TopoDS_Edge edge;
  int entityId;
};

struct Profile {
  // One planar face per closed region (outer loop + inner hole loops; nested islands become
  // separate faces). Faces are oriented so their normal == plane normal.
  std::vector<TopoDS_Face> faces;
  std::vector<ProfileEdge> edges;   // every 3D edge used in `faces`
  std::vector<gp_Pnt> freePoints;   // non-origin points not used by any entity (hole locations), 3D
  std::vector<std::pair<int, gp_Pnt2d>> freePoints2d; // (pointId, uv) same order as freePoints
  bool ok = false;
  std::string message;              // e.g. "Açık kontur: nokta 7"
};

// Build OCCT geometry for the NON-construction entities of an (already solved) sketch, placed on
// `frame` (frame.Location = sketch origin, XDirection = u, YDirection = v, Direction = normal).
Profile buildProfile(const Sketch& sk, const gp_Ax3& frame);

// 3D frame for the standard planes (XY/XZ/YZ) with offset applied. For OnFace the caller (model)
// computes the frame itself.
gp_Ax3 standardFrame(SketchPlaneKind kind, double offset);

} // namespace pc::sketch
