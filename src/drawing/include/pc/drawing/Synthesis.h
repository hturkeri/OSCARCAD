#pragma once
// PersistCAD drawing import — S7 3D synthesis candidates. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Primitives.h"
#include "pc/Document.h"

namespace pc::drawing {

struct SynthesisCandidate {
  int id = 0;
  Strategy strategy = Strategy::SketchOnly;
  float score = 0;                  // ranking 0..1
  std::string explanation;          // Turkish, shown in wizard: "Ön görünüşte eksen + ayna simetrisi %97 -> döndürme"
  pc::Document fragment;            // params + features (Sketch*, Revolve/Extrude/Hole/Chamfer/Fillet/Pattern)
  std::vector<int> sketchIds;       // SketchCandidate ids used
  std::vector<std::string> warnings;
  // Score components (SPEC §2.11)
  double conditionScore = 0;        // geometric precondition (axis+mirror score, view alignment ...)
  double dofCoverage = 0;           // fraction of DOF closed by driving dimensions
  int unresolved = 0;               // elements that could not be used
  // Verification against the drawing (pc::model::regenerate + HLR re-projection):
  double reprojectionError = -1;    // mean distance (part mm) of re-projected model edges to Visible prims; -1 = n/a
  double volume = 0;                // mm^3; 0 = not computed
  bool regenerated = false;         // fragment regenerated without error
  Attrs attrs;
};

}  // namespace pc::drawing
