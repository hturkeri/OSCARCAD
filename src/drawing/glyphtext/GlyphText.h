#pragma once
// R1 private: text drawn as filled glyph OUTLINES in vector PDFs (Matplotlib "text as paths", Acrobat "convert text
// to outlines", SHX-like fills). pdftotext sees nothing; the interpreter emits every glyph contour as a filled
// Polyline/Bezier primitive. This module
//   1. detects glyph-outline groups (one fill operator = one text string; contours -> glyphs -> words/lines,
//      any rotation, reading direction from the order of the glyphs in the stream),
//   2. recognises them: glyph-signature cache (identical outline up to translation/rotation = same character),
//      labels learned by voting over Tesseract line readings + shape rules for Ø and °, fallback = line OCR
//      ranked with the dimension-text grammar,
//   3. replaces the glyph primitives by TextRun entries (asOutlines = true, mergedFrom = removed primitive ids).
// Offline and deterministic (Tesseract LSTM, fixed parameters; no randomness, stable ordering).
// Wired into pdf/PdfIngest.cpp (hook) and compiled through pdf/GlyphTextUnit.cpp (see docs/drawing_import/R1_GLYPHTEXT.md).
#include "pc/drawing/Primitives.h"
#include <string>
#include <vector>

namespace pc::drawing::glyphtext {

struct Options {
  bool enabled = true;
  std::string tessdataDir;          // "" = resolve like WP-2 OCR (shipped share/persistcad/tessdata, $TESSDATA_PREFIX, system)
  double maxGlyphMm = 12;           // a glyph (not a string) larger than this is geometry
  double minGlyphMm = 0.25;
  int minMultiGlyphGroups = 3;      // page activates only with >= this many multi-glyph outline strings
  double renderCapPx = 36;          // OCR raster: cap height in pixels
};

struct Stats {
  int groups = 0;                   // accepted text strings (fill operators)
  int glyphs = 0;
  int glyphClasses = 0;             // unique outline signatures
  int labeledClasses = 0;           // classes with a character from voting / shape rules / single-glyph OCR
  int runs = 0;                     // TextRuns emitted
  int removedPrims = 0;
  int ocrCalls = 0;
  bool ocrAvailable = false;
  std::string note;                 // why nothing was done ("", "ocr unavailable", "no glyph text")
};

// Detects and recognises glyph-outline text in `sheet` (vector PDF ingest output, sheet mm). Emits TextRuns
// (ids from nextId) and removes the consumed glyph primitives. A sheet without glyph text is left untouched.
Stats extractGlyphText(PrimitiveSheet& sheet, int& nextId, const Options& o = {});

// ---- building blocks (exposed for tests) -----------------------------------------------------------------------
struct Contour {
  std::vector<Vec2> pts;            // flattened, closed implicitly
  std::vector<Vec2> ctrl;           // original control polygon (signature)
  Box2 box;
  double area = 0;                  // signed (shoelace)
};
struct Glyph {
  std::vector<int> contours;        // indices into Group::contours
  Box2 box;                         // text frame
  int cls = -1;                     // signature class
  bool hole2diag = false, ring = false;  // shape features (Ø, °)
  int holes = 0, outers = 0;
};
// 8-bit gray raster, row 0 = top, 255 = background.
struct Gray { int w = 0, h = 0; std::vector<uint8_t> px; };
// Rasterise closed contours given in a frame where y is UP: `box` is mapped to the image (plus `pad` px border),
// `scale` px per unit, 4x4 supersampling. Even-odd by default: some producers emit glyph holes with the outer
// contour's winding (Matplotlib DejaVu 'Ø'), which a nonzero fill would show as a filled half.
Gray rasterize(const std::vector<const std::vector<Vec2>*>& polys, const Box2& box, double scale, int pad, bool evenOdd = true);

}  // namespace pc::drawing::glyphtext
