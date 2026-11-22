#pragma once
// WP-1 private: PDF content-stream interpreter (libqpdf for objects/filters; operators interpreted here).
#include "pc/drawing/Primitives.h"
#include <string>

namespace pc::drawing::pdf {

struct PageStats {
  int index = 0;
  Box2 box;                         // displayed page box, sheet mm
  int pathCount = 0, textCount = 0, imageCount = 0;
  double largestImageArea = 0;      // sheet mm^2
  double imageCoverage = 0;         // largest image / page area
};

struct InterpOptions { bool collect = true; int maxFormDepth = 8; };

// Opens `path` (throws std::exception on qpdf errors). pageCount < 0 on failure.
int pdfPageCount(const std::string& path, std::string* err);
// Interprets page `page` (0-based) into `out` (prims/texts in sheet mm, ids from nextId). Returns false + err.
bool interpretPage(const std::string& path, int page, const InterpOptions& o, PrimitiveSheet& out, int& nextId, PageStats& stats,
                   std::string* err);

}  // namespace pc::drawing::pdf

namespace pc::drawing::pdf {
// Fast statistics pass over all pages (no primitives kept). Returns false + err if unreadable.
bool pdfPageStats(const std::string& path, std::vector<PageStats>& pages, std::string* err);
}  // namespace pc::drawing::pdf
