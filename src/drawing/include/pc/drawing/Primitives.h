#pragma once
// PersistCAD drawing import — S1/S2 data: primitives, text runs, native DXF dimensions, raster info, sheet.
// FROZEN (DrawingIR 1.0). See Geometry.h for the coordinate contract and Confidence.h for `conf`.
//
// Id contract: every element id is unique within one DrawingIR (single counter DrawingIR::nextId, shared by
// primitives, texts, native dims, views, dimensions, sketch candidates and synthesis candidates). Ids are never
// reused; a stage that replaces elements records the replaced ids in `mergedFrom`. S2 keeps the id of an element it
// passes through unchanged (so raw id == norm id for untouched elements).
#include "pc/drawing/Diagnostics.h"
#include "pc/drawing/Enums.h"
#include "pc/drawing/Geometry.h"
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pc::drawing {

// Free-form extension attributes. Keys are namespaced by the writing WP/module ("pdf.ocg", "raster.lsdWidthPx",
// "interp.mirrorOf"). Serialized as a JSON object. Lets a WP carry extra data WITHOUT a contract change; a key that
// another WP starts to depend on must be promoted to a real field via DRW: request.
using Attrs = std::map<std::string, std::string>;

struct Provenance {
  SourceKind source = SourceKind::VectorPdf;
  int page = 0;                     // 0-based page / DXF layout index
  std::string ref;                  // free text: "obj 659 0", "DXF handle 2F", "LSD#1234", "tess word 17"
};

struct LineStyle {
  double width = 0;                 // stroke width, sheet mm (raster: measured stroke thickness); 0 = hairline/unknown
  std::vector<double> dash;         // on/off pattern, sheet mm; empty = continuous. Filled by S2 dash-chaining too.
  double dashPhase = 0;             // sheet mm
  uint32_t rgb = 0;                 // 0xRRGGBB (stroke colour; fill colour in attrs "pdf.fillRgb" if different)
  bool stroked = true, filled = false;
  std::string layer;                // DXF layer / PDF optional-content group name ("" if none)
  std::string linetype;             // DXF linetype name ("CENTER", "HIDDEN"; BYLAYER/BYBLOCK resolved) or ""
};

struct Primitive {
  int id = 0;
  PrimKind kind = PrimKind::Line;
  Vec2 p0, p1;                                               // Line / Arrowhead
  Vec2 center; double radius = 0; double a0 = 0, a1 = 360;  // Arc / Circle / Ellipse
  double minorRatio = 1, rotDeg = 0;                         // Ellipse
  std::vector<Vec2> pts; bool closed = false;                // Polyline / Bezier / HatchRegion / Image
  double hatchAngleDeg = 0, hatchPitch = 0;                  // HatchRegion
  LineStyle style;
  float conf = 1.f;
  Provenance prov;
  std::vector<int> mergedFrom;      // S2: ids of raw primitives this one replaces (dash pieces, polyline -> arc ...)
  bool fromDimBlock = false;        // DXF: part of a DIMENSION's anonymous block (*D..) — not part geometry
  bool clipped = false;             // PDF: drawn under a clip path (clip recorded, not applied; phase 2)
  Attrs attrs;
};

// Text as found. Vector PDF: raw = one run per glyph/show-text op; norm = merged words/lines (S2).
struct TextHypothesis { std::string utf8; float conf = 0; };

struct TextRun {
  int id = 0;
  std::string utf8;                 // best reading, NFC, symbols normalised: Ø ± ° × ; "⌀"->"Ø", "%%c"->"Ø",
                                    // "%%p"->"±", "%%d"->"°"
  Vec2 origin;                      // baseline start
  double height = 0;                // cap height, sheet mm
  double rotDeg = 0;                // baseline direction
  std::array<Vec2, 4> quad{};       // tight box, sheet mm, CCW starting at the baseline-frame lower-left corner
  std::string font;                 // PDF BaseFont / DXF style / "ocr"
  uint32_t rgb = 0;
  std::vector<TextHypothesis> alternatives;  // OCR n-best or symbol-repair alternatives (best first, excl. utf8)
  bool asOutlines = false;          // recognised from vector glyph outlines (SHX / "text as geometry")
  bool invisible = false;           // PDF Tr 3 (OCR text layer of a scanned PDF): candidate only
  bool fromDimBlock = false;        // DXF: text of a DIMENSION block
  float conf = 1.f;
  Provenance prov;
  std::vector<int> mergedFrom;
  Attrs attrs;
};

// DXF-native dimension (DIMENSION entity). Measured value is COMPUTED from defpoints (group 42 is optional and
// often absent — spike finding); textOverride "<>" or "" means "measured value".
struct NativeDimension {
  int id = 0;
  NativeDimKind kind = NativeDimKind::Unknown;
  bool rotatedLinear = false; double rotDeg = 0;        // Linear: dimension line direction (DXF 50)
  std::array<Vec2, 5> defpts{};                         // DXF 10/13/14/15/16 in sheet mm (unused = {0,0})
  Vec2 textMid;                                         // DXF 11
  double measured = 0;                                  // computed (mm or deg), drawing units -> mm applied
  std::string textOverride;                             // DXF 1, MTEXT codes stripped, %% codes converted
  std::string block;                                    // "*D12"
  std::vector<int> blockPrimitiveIds;                   // primitives exploded from the block (fromDimBlock=true)
  std::vector<int> blockTextIds;                        // texts exploded from the block
  std::string dimStyle;                                 // DXF 3
  float conf = 1.f;
  Provenance prov;
  Attrs attrs;
};

struct RasterInfo {                 // only for raster sources
  int widthPx = 0, heightPx = 0;    // original image
  double dpi = 0;                   // from file metadata or estimated (sheet size fit); 0 = unknown
  bool dpiEstimated = false;
  double deskewDeg = 0;             // applied rotation (CCW positive)
  std::array<double, 9> homography{1, 0, 0, 0, 1, 0, 0, 0, 1};  // photo: original px -> rectified px (row-major)
  Affine2 pxToSheet{};              // rectified px (x right, y DOWN, origin top-left) -> sheet mm (y up)
  std::string preprocessedImagePath;  // optional debug artefact (binarized, rectified)
};

struct PrimitiveSheet {
  SourceKind source = SourceKind::VectorPdf;
  std::string file;                 // absolute path at import time
  std::string fileSha256;           // lowercase hex; "" if not computed
  int page = 0;
  std::string layout;               // DXF layout name ("" = model space)
  Box2 sheet;                       // page box in sheet mm ({0,0}-{w,h}); DXF: drawing extents shifted to >= 0
  double unitToMm = 1;              // DXF: $INSUNITS factor applied; PDF: kPtToMm
  std::vector<Primitive> prims;
  std::vector<TextRun> texts;
  std::vector<NativeDimension> nativeDims;
  std::optional<RasterInfo> raster;
  std::vector<Diagnostic> diagnostics;   // "pdf.font.symbolRepaired": "R269 sembol fontu glif adlarıyla onarıldı"
  Attrs attrs;
};

}  // namespace pc::drawing
