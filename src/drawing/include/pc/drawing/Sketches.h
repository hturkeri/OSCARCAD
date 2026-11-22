#pragma once
// PersistCAD drawing import — S6 sketch candidates and parameter proposals. FROZEN (DrawingIR 1.0).
// Sketches and parameters are carried as pc::Sketch / pc::Parameter directly (no conversion layer).
#include "pc/drawing/Primitives.h"
#include "pc/Parameter.h"
#include "pc/SketchData.h"

namespace pc::drawing {

struct ParamProposal {
  pc::Parameter param;              // name, expression (nominal), kind, description ("[D] Ø16 +0/-0.5, görünüş A")
  ParamTag tag = ParamTag::FromDimension;
  int dimensionId = -1;
  std::optional<double> tolPlus, tolMinus;
  double nominalValue = 0;
  double tolMidValue = 0;           // nominal + (tolPlus+tolMinus)/2 — offered as alternative value
  std::optional<double> rangeMin, rangeMax;   // from "min"/"max" envelope dims (proposed as param range rule)
  bool userEdited = false;
};

struct SketchCandidate {
  int id = 0;
  int viewId = -1;
  std::string purpose;              // "revolve-half-profile", "extrude-profile", "cut-profile", "hole-pattern"
  pc::SketchPlaneKind plane = pc::SketchPlaneKind::XY;
  pc::Sketch sketch;                // ids in sketch are local; mappings below
  std::vector<ParamProposal> params;
  std::map<int, int> constraintToDimension;   // sketch constraint id -> Dimension id
  std::map<int, int> entityToPrimitive;       // sketch entity id -> Primitive id (norm)
  std::vector<int> measuredConstraints;       // constraints added by DOF closing ([Ö] parameters)
  int axisEntity = -1;              // construction line used as revolve axis (or -1 -> sketch axis V)
  int dof = -1;                     // after pc::sketch::solve; -1 = not solved
  std::vector<int> conflicting;     // constraint ids
  std::vector<int> deviatingEntities;   // entities with deviation > tolerance
  double maxGeomDeviation = 0;      // solved sketch vs drawing geometry, part mm
  bool profileClosed = false;
  float conf = 0;
  Attrs attrs;
};

}  // namespace pc::drawing
