#pragma once
// PersistCAD drawing import — S5 view graph. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Primitives.h"

namespace pc::drawing {

struct ViewRelation {
  int a = -1, b = -1;               // ViewRegion ids
  Adjacency adj = Adjacency::None;  // placement of b relative to a
  double alignErr = 0;              // sheet mm (projection-line misalignment)
  float conf = 0;
};

struct ViewGraph {
  ProjectionMethod projection = ProjectionMethod::Unknown; float projectionConf = 0;
  std::vector<ViewRelation> relations;
  int primaryView = -1;                  // most informative view (profile candidate)
  std::map<int, int> axisOfView;         // view id -> centerline primitive id (rotational symmetry axis)
  std::map<int, double> symmetryScore;   // view id -> mirror-match score about its axis (0..1)
  std::map<int, bool> rotational;        // view id -> part judged rotationally symmetric from this view
  Attrs attrs;
};

}  // namespace pc::drawing
