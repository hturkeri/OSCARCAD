#pragma once
// PersistCAD drawing import — dimension text grammar (SPEC §2.10) [WP-2]. FROZEN (DrawingIR 1.0).
// Used by WP-2 OCR re-ranking and by WP-3 S4 (vector and raster). Pure, no optional dependency.
#include "pc/drawing/Annotation.h"

namespace pc::drawing {

struct DimTextContext {             // geometric context biases confusion-set corrections
  bool nearCircleOrCylinder = false;
  bool nearArc = false;
  bool betweenParallelLines = false;
  bool angular = false;
  bool fromOcr = false;             // allow confusion-set corrections (vector text: only symbol normalisation)
};

// Never throws. Unparseable text -> valid=false, kind Note or Unknown, raw kept.
DimText parseDimText(const std::string& text, const DimTextContext& ctx = {});
// Pick the best hypothesis among OCR alternatives with the grammar. Returns the index into hyps, -1 if hyps empty.
int rankDimTextHypotheses(const std::vector<TextHypothesis>& hyps, const DimTextContext& ctx, DimText* best = nullptr);
// Canonical text of a parsed dimension ("Ø16 +0/-0.5", "4x Ø6.6", "M8x1.25-6H") — wizard display and tests.
std::string formatDimText(const DimText& d);

}  // namespace pc::drawing
