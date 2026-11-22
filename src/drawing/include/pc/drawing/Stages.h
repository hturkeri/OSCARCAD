#pragma once
// PersistCAD drawing import — stage interfaces S0..S7. FROZEN (DrawingIR 1.0). Owners in brackets.
// Stages are PURE: (const input, options) -> output; no globals, no Qt, deterministic. Long stages poll
// ProgressSink::cancelled() and return Status::Cancelled ("İptal edildi").
#include "pc/drawing/DrawingIR.h"
#include "pc/drawing/Raster.h"
#include <functional>

namespace pc::drawing {

struct ProgressSink {
  std::function<void(double /*0..1*/, const std::string& /*Turkish status*/)> progress;
  std::function<bool()> cancelled;
};
inline void report(ProgressSink* p, double f, const std::string& msg) { if (p && p->progress) p->progress(f, msg); }
inline bool isCancelled(ProgressSink* p) { return p && p->cancelled && p->cancelled(); }

// ---- S0: routing [WP-1]
struct PageInfo {
  int index = 0;
  Box2 box;                         // page box, sheet mm
  SourceKind kind = SourceKind::VectorPdf;   // per-page verdict (VectorPdf / ScannedPdf)
  double vectorScore = 0;           // stroked-path + text density vs page-covering images (scanned => ~0)
  double imageCoverage = 0;         // largest image area / page area
  int pathCount = 0, textCount = 0, imageCount = 0;
};
struct SniffResult {
  StageResult result;               // status Ok if the file type was recognised
  SourceKind kind = SourceKind::VectorPdf;   // verdict for page 0 (or the file)
  int pageCount = 1;
  double vectorScore = 0;
  std::vector<PageInfo> pages;      // PDF: one per page; raster: one; DXF: one per layout
  std::vector<std::string> dxfLayouts;       // "" (model space) first
  double dpi = 0;                   // raster metadata (0 = unknown)
  bool looksLikePhoto = false;      // raster: page quadrilateral not rectangular
  std::string message;              // Turkish summary for wizard step 1
};
SniffResult sniff(const std::string& path);

// ---- S1: ingest [WP-1: PDF vector, DXF] [WP-2: raster]
struct IngestOptions {
  int page = 0;
  bool explodeDxfDimBlocks = true;
  std::string dxfLayout;            // "" = model space
  double rasterDpiOverride = 0;     // 0 = from file / estimate
  double pdfRenderDpi = 300;        // scanned PDF pages that need rendering
  bool rasterIsPhoto = false;       // enables perspective rectification
  std::string tessdataDir;          // "" = <app>/share/persistcad/tessdata (offline, shipped), then $TESSDATA_PREFIX
  std::vector<std::string> ocrLanguages{"eng", "tur"};   // + "pcdim" (fine-tuned) when present
  int threads = 0;                  // OCR worker threads, 0 = hardware concurrency
  std::string debugDir;             // "" = no debug artefacts
  PdfRasterizer pdfRasterizer;      // optional (Qt layer); used for scanned PDFs extractPdfPageImage cannot decode
};
StageResult ingestPdfVector(const std::string& path, const IngestOptions&, PrimitiveSheet& out, int& nextId,
                            ProgressSink* = nullptr);                                                     // WP-1
StageResult ingestDxf(const std::string& path, const IngestOptions&, PrimitiveSheet& out, int& nextId,
                      ProgressSink* = nullptr);                                                           // WP-1
StageResult ingestRaster(const RasterImage& img, const IngestOptions&, PrimitiveSheet& out, int& nextId,
                         ProgressSink* = nullptr);                                                        // WP-2
// Ingest fills out.source / file / page / sheet; `nextId` is DrawingIR::nextId (ids assigned from it).

// ---- S2: normalize [WP-1] — shared by all sources: unit/scale-free cleanup in SHEET mm
struct NormalizeOptions {
  double snapTol = 0.05;            // mm; raster: max(0.05, 1.5 px)
  double angleTolDeg = 0.3;         // raster: 1.0
  double arcFitTol = 0.02;          // polyline -> arc max deviation (mm); raster 0.25
  bool chainDashes = true;          // PScript-style split dashes -> one styled line (spike: 0 'd' operators)
  bool mergeGlyphRuns = true;       // per-glyph runs -> words on a common baseline (spike: rotated text per glyph)
  double glyphGapFactor = 1.2;      // merge if gap < factor * height
  bool repairSymbolFonts = true;    // glyph-name tables (grave->±, n->Ø, asciitilde->° ...) SPEC §2.4
  bool detectVectorArrowheads = true;
  double arrowMinMm = 2, arrowMaxMm = 6;
  bool detectHatch = true;
  static NormalizeOptions forRaster(double dpi) {
    NormalizeOptions o;
    const double px = dpi > 0 ? 25.4 / dpi : 0.1;
    o.snapTol = 1.5 * px > 0.05 ? 1.5 * px : 0.05;
    o.angleTolDeg = 1.0;
    o.arcFitTol = 0.25;
    return o;
  }
};
StageResult normalize(const PrimitiveSheet& raw, const NormalizeOptions&, PrimitiveSheet& out, int& nextId,
                      ProgressSink* = nullptr);                                                           // WP-1

// ---- S3..S7 [WP-3]
struct InterpretOptions {
  ProjectionMethod defaultProjection = ProjectionMethod::FirstAngle;   // TS/ISO 128 practice
  bool strict = false;              // strict: elements with conf < 0.6 are not used without user confirmation
  double tolMatchFactor = 1.0;      // dim vs geometry acceptance = max(abs tol band, 0.5 % nominal, snap) * factor
  bool preferTolMid = false;        // use tolerance midpoint as parameter value (enjektör lesson, SPEC §2.8)
  bool closeDofWithMeasured = true; // SPEC §2.9 step 5
  double measuredRounding = 0.01;   // mm; raster 0.1
  bool verifyByReprojection = true; // S7 HLR check (slow on big parts)
};
StageResult analyzeLayout(DrawingIR& ir, const InterpretOptions&, ProgressSink* = nullptr);       // S3: norm -> layout
StageResult annotate(DrawingIR& ir, const InterpretOptions&, ProgressSink* = nullptr);            // S4: roles + dims
StageResult buildViews(DrawingIR& ir, const InterpretOptions&, ProgressSink* = nullptr);          // S5
StageResult reconstructSketches(DrawingIR& ir, const InterpretOptions&, ProgressSink* = nullptr); // S6 (pc::sketch)
StageResult synthesize(DrawingIR& ir, const InterpretOptions&, ProgressSink* = nullptr);          // S7 (pc::model)

}  // namespace pc::drawing
