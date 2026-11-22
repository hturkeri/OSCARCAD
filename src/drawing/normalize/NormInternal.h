#pragma once
// WP-1 private: S2 normalization passes. Each pass edits `w.prims` / `w.texts` in place, takes new ids from w.nextId,
// records replaced ids in mergedFrom and counts its work in w.stats.
#include "normalize/GeomUtil.h"
#include "pc/drawing/Stages.h"
#include <map>
#include <string>

namespace pc::drawing::norm {

struct Work {
  std::vector<Primitive> prims;
  std::vector<TextRun> texts;
  const NormalizeOptions& opt;
  int& nextId;
  std::map<std::string, int> stats;
  std::vector<Diagnostic> diags;
  Work(const NormalizeOptions& o, int& id) : opt(o), nextId(id) {}
};

void cleanup(Work& w);            // zero-length, exact duplicates, fill+stroke copies
void detectArrowheads(Work& w);   // filled small triangles -> Arrowhead
void bezierToArcs(Work& w);       // cubic chains -> Arc/Circle (exact quarter-circle test + fit)
void fitArcs(Work& w);            // flattened line chains / polylines -> Arc/Circle
void mergeCocircularArcs(Work& w);   // touching co-circular arcs -> arc / circle (called by fitArcs)
void chainDashes(Work& w);        // split dashes -> one styled line; collinear overlaps merged
void detectHatch(Work& w);        // parallel thin line families + image tiles -> HatchRegion
void snapEndpoints(Work& w);      // endpoint clustering (snapTol)
void repairSymbolFonts(Work& w);  // glyph-name tables
void mergeTextRuns(Work& w);      // per-glyph runs -> words

bool sameStroke(const LineStyle& a, const LineStyle& b);

}  // namespace pc::drawing::norm
