// P0 (frozen): lookup helpers and stage-section clearing declared in DrawingIR.h.
#include "pc/drawing/DrawingIR.h"
#include <algorithm>

namespace pc::drawing {
namespace {
template <class V> auto* findById(V& v, int id) {
  auto it = std::find_if(v.begin(), v.end(), [id](const auto& e) { return e.id == id; });
  return it == v.end() ? nullptr : &*it;
}
}  // namespace

const Primitive* findPrim(const PrimitiveSheet& s, int id) { return findById(s.prims, id); }
Primitive* findPrim(PrimitiveSheet& s, int id) { return findById(s.prims, id); }
const TextRun* findText(const PrimitiveSheet& s, int id) { return findById(s.texts, id); }
TextRun* findText(PrimitiveSheet& s, int id) { return findById(s.texts, id); }
const ViewRegion* findView(const SheetLayout& l, int id) { return findById(l.views, id); }
ViewRegion* findView(SheetLayout& l, int id) { return findById(l.views, id); }
const Dimension* findDimension(const Annotation& a, int id) { return findById(a.dims, id); }
Dimension* findDimension(Annotation& a, int id) { return findById(a.dims, id); }

LineRole roleOf(const DrawingIR& ir, int primId) {
  auto it = ir.annot.roles.find(primId);
  return it == ir.annot.roles.end() ? LineRole::Unknown : it->second;
}

void clearFromStage(DrawingIR& ir, Stage from) {
  const int f = static_cast<int>(from);
  if (f <= static_cast<int>(Stage::Ingest)) ir.raw = {};
  if (f <= static_cast<int>(Stage::Normalize)) ir.norm = {};
  if (f <= static_cast<int>(Stage::Layout)) ir.layout = {};
  if (f <= static_cast<int>(Stage::Annotate)) ir.annot = {};
  if (f <= static_cast<int>(Stage::Views)) ir.views = {};
  if (f <= static_cast<int>(Stage::Sketch)) ir.sketches.clear();
  if (f <= static_cast<int>(Stage::Synthesize)) { ir.synthesis.clear(); ir.chosenSynthesis = -1; }
  for (auto it = ir.stageVersion.begin(); it != ir.stageVersion.end();)
    it = static_cast<int>(it->first) >= f ? ir.stageVersion.erase(it) : std::next(it);
  ir.lastStage = std::min(ir.lastStage, f - 1);
}

}  // namespace pc::drawing
