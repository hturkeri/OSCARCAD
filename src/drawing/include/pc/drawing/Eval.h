#pragma once
// PersistCAD drawing import — evaluation metrics (SPEC §6.2) [WP-1]. FROZEN (DrawingIR 1.0).
#include "pc/drawing/DrawingIR.h"

namespace pc::drawing {

struct GroundTruth;                 // pc/drawing/GroundTruth.h (WP-1, additive-only)
struct Metrics {
  std::map<std::string, double> values;   // "geom.recall", "dim.valueAcc", "solid.volErr", ... (SPEC §6.2 names)
  std::vector<std::string> notes;         // per-element mismatches for the report
};
Metrics evaluate(const DrawingIR& ir, const GroundTruth& gt);

}  // namespace pc::drawing
