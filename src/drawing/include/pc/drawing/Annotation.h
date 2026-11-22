#pragma once
// PersistCAD drawing import — S4 line roles and dimensions. FROZEN (DrawingIR 1.0).
#include "pc/drawing/Primitives.h"

namespace pc::drawing {

// Parsed dimension text (ISO 129-1 / ASME Y14.5 conventions, grammar SPEC §2.10). Produced by parseDimText().
struct DimText {
  std::string raw;
  bool valid = false;
  DimKind kind = DimKind::Unknown;  // implied by the text alone (Ø -> Diameter, R -> Radius, ° -> Angle, M -> Thread)
  int count = 1;                    // "4x", "4X", "4 adet", "4 DELİK"
  double nominal = 0;               // mm or deg
  std::optional<double> tolPlus, tolMinus;   // signed deviations: "+0.05/-0" -> +0.05, 0 ; "±0.1" -> +0.1,-0.1
  std::string fitClass;             // "H7", "g6"
  bool isMax = false, isMin = false;          // "max", "min", "maks."
  bool reference = false;           // "(12)"
  bool basic = false;               // boxed (vector: surrounding rectangle)
  bool spherical = false;           // SØ / SR
  bool squareSym = false;           // □
  double chamferAngle = 0;          // "2x45°" -> nominal 2, chamferAngle 45 ; "C2" -> 2,45
  std::string threadSpec;           // "M8x1.25-6H", "3/8\"-40UNS", "G1/4"
  double threadMajor = 0, threadPitch = 0;   // mm (inch threads converted)
  std::optional<double> depth;      // hole depth "↧10", "derinlik 10"
  bool through = false;             // "THRU", "boydan boya"
  std::string remainder;            // unparsed trailing text (notes), "" if fully consumed
  float conf = 0;                   // grammar confidence (penalised by confusion-set corrections)
  std::vector<std::string> corrections;  // "0->Ø (circle context)", "+->± (symmetric tol)"
  double tolMid() const { return nominal + ((tolPlus ? *tolPlus : 0.0) + (tolMinus ? *tolMinus : 0.0)) / 2; }
};

struct Dimension {
  int id = 0;
  int viewId = -1;
  DimKind kind = DimKind::Unknown;  // final kind (text + geometry); text.kind is the text-only guess
  DimText text;
  int textId = -1;                  // TextRun (norm)
  std::vector<int> tolTextIds;      // stacked tolerance runs ("+0" / "-0.5") bound to this dimension
  int nativeDimId = -1;             // NativeDimension (DXF)
  std::vector<int> dimLineIds, extLineIds, arrowIds, leaderIds;
  // Geometry the dimension constrains. Anchors are points ON the measured geometry.
  std::vector<int> geomPrimIds;     // e.g. two edges, a circle, an arc, two lines (angle)
  std::array<Vec2, 2> anchors{};    // measurement points, SHEET mm
  Vec2 direction{1, 0};             // measurement direction for Linear (unit, sheet)
  double measuredSheet = 0;         // from geometry, sheet mm (or deg)
  double measuredPart = 0;          // measuredSheet * scale factor (deg unchanged)
  double residual = 0;              // measuredPart - reference value (nominal or tolMid, whichever is closer)
  bool residualVsTolMid = false;    // true if `residual` was taken against the tolerance midpoint (SPEC §2.8.1)
  bool outOfBand = false;           // |residual| > acceptance band -> drawing error or wrong binding (red)
  bool driving = true;              // false for reference "(..)", redundant, max/min envelope dims
  bool userEdited = false;
  float conf = 0;
  Attrs attrs;
};

struct Annotation {
  std::map<int, LineRole> roles;    // primitive id -> role (norm primitives)
  std::map<int, float> roleConf;
  std::vector<Dimension> dims;
  std::vector<int> centerlineIds;   // axis candidates (symmetry axes of views)
  std::vector<int> unusedTextIds;   // texts not bound to a dimension / title block / note / view label
  Attrs attrs;
};

}  // namespace pc::drawing
