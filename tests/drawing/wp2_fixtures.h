#pragma once
// WP-2 test helpers: synthetic fixture truth (tests/drawing/fixtures/wp2/*.truth.json), noisy / skewed variants
// generated at test time (OpenCV, fixed seed) and recognition metrics (segment / circle / arc / arrow recall and
// precision, dimension-text exact match before / after the grammar).
#include "pc/drawing/Drawing.h"
#include <map>
#include <string>
#include <vector>

namespace wp2 {

struct TLine { double x0, y0, x1, y1, w; std::string type; };
struct TCircle { double cx, cy, r, w; };
struct TArc { double cx, cy, r, a0, a1, w; };
struct TArrow { double tx, ty, bx, by; };
struct TText { double x0, y0, x1, y1; std::string text; int rot; bool dim, circle, angular; };
struct Truth {
  std::string name;
  int W = 0, H = 0;
  double dpi = 300;
  std::vector<TLine> lines;
  std::vector<TCircle> circles;
  std::vector<TArc> arcs;
  std::vector<TArrow> arrows;
  std::vector<TText> texts;
};

std::string fixtureDir();                          // PC_DRAWING_TEST_DIR/fixtures/wp2
std::vector<std::string> fixtureNames();           // wp2_flange, wp2_shaft, wp2_plate
bool loadTruth(const std::string& name, Truth& t, std::string* err = nullptr);

// variant: "clean" (file as is), "noisy" (blur + gaussian noise + JPEG q60), "skewed" (2° rotation + light noise +
// JPEG q85). Deterministic. Returns false if OpenCV is unavailable.
bool makeVariant(const std::string& name, const std::string& variant, pc::drawing::RasterImage& img, std::string* err = nullptr);

struct PR { int tp = 0, nTruth = 0, nDet = 0; double recall() const { return nTruth ? double(tp) / nTruth : 1; }
            double precision() const { return nDet ? double(tp) / nDet : 1; } };
struct Metrics {
  PR lines, circles, arcs, arrows, textBoxes;
  int dimTotal = 0, dimRawExact = 0, dimGrammarExact = 0;
  int noteTotal = 0, noteExact = 0;
  double deskewDeg = 0;
  std::vector<std::string> notes;   // mismatches
};
// Compare an ingested sheet (sheet mm) with the pixel truth (pxToSheet inverted). tolMm: endpoint tolerance.
Metrics evaluate(const pc::drawing::PrimitiveSheet& sheet, const Truth& t, double tolMm = 0.5);
void accumulate(Metrics& acc, const Metrics& m);
std::string normalizeWs(const std::string& s);

}  // namespace wp2
