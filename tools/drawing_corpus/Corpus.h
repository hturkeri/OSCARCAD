#pragma once
// pc_drawing_corpus — synthetic drawing corpus generator (SPEC §6.1). OWNER WP-1.
//   Part (OCCT solid + dimension plan) -> HLR views -> GroundTruth sheet (single source of truth)
//   -> writers: vector PDF (3 variants), DXF (with / without DIMENSION), PNG, scan / photo JPG, gt.json.
#include "pc/drawing/GroundTruth.h"
#include <TopoDS_Shape.hxx>
#include <random>
#include <string>
#include <vector>

class QImage;

namespace corpus {
using namespace pc::drawing;

// Axis-aligned cylinder (axis 0=X 1=Y 2=Z) for centre lines: axis line from c+t0*axis to c+t1*axis.
struct Cyl { double c[3]{0, 0, 0}; int axis = 0; double r = 0, t0 = 0, t1 = 0; };

enum class DimType { Linear, Radial, Leader };
struct DimSpec {
  int view = 0;                     // index into Part::views
  DimType type = DimType::Linear;
  Vec2 a, b;                        // Linear: measured points in VIEW part mm
  bool horiz = true;                // Linear: measures |dx| (else |dy|)
  double offset = 10;               // Linear: sheet mm from the view box edge (+ above/right, - below/left)
  bool inside = false;              // Linear: dimension line through a/b (diameters across a turned profile)
  Vec2 c; double r = 0;             // Radial: circle centre / radius (view mm); Leader: point c
  double angDeg = 45;               // Radial / Leader direction
  DimKind kind = DimKind::Linear;
  std::string text; double nominal = 0; std::optional<double> tolPlus, tolMinus;
  std::string param; bool driving = true;
};

struct Part {
  std::string family, name, material;
  TopoDS_Shape shape;
  std::vector<Cyl> cyls;
  std::vector<ViewRole> views;      // Front (X,Z) / Top (X,Y) / Left (-Y,Z); first-angle placement
  std::vector<DimSpec> dims;
  std::vector<GtParam> params;
  Strategy strategy = Strategy::SketchOnly;
};

const std::vector<std::string>& families();
// index 0 = nominal parameters; >0 = random variation from rng
Part makePart(const std::string& family, int index, std::mt19937& rng);

// HLR + layout + dimensions + title block. Throws std::runtime_error on failure.
GroundTruth buildSheet(const Part& part, int index);

// Writers. pdfVariant: 0 = real text + Bezier arcs (sheet.pdf), 1 = text as outlines (sheet_outline.pdf),
// 2 = PScript-like: flattened arcs, dashes as separate segments (sheet_pscript.pdf).
bool writePdf(const GroundTruth& gt, const std::string& path, int pdfVariant);
QImage renderImage(const GroundTruth& gt, double dpi);
bool writeDxf(const GroundTruth& gt, const std::string& path, bool withDimensions);
// Degradations; parameters recorded into gt attrs by the caller.
QImage makeScan(const QImage& clean, double skewDeg, double blurSigma, int noise, std::mt19937& rng);
QImage makePhoto(const QImage& clean, double tiltDeg, std::mt19937& rng);

// shared drawing constants (sheet mm)
inline constexpr double kVisW = 0.5, kThinW = 0.25, kTextH = 3.5, kArrowLen = 3.0, kArrowW = 1.0;
inline const std::vector<double> kHiddenDash{3, 1.5}, kCenterDash{8, 2, 1, 2};
std::string fontFamily();
double textAdvanceMm(const std::string& utf8, double capHeightMm);   // needs QGuiApplication
}  // namespace corpus
