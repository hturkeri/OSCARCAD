#pragma once
// SC-2D-IMPORT — "2B çizim (eğri olarak)": a 2D drawing (DXF / vector PDF) inserted as CURVES in 3D space, grouped
// by layer, plus the curve operations of the SpaceClaim "2D drawing -> 3D" workflow (view cluster, view placement,
// move to layer). Pure document logic (no widgets); the UI is Import2DUi.h. Design: docs/import2d/IMPORT2D.md.
//
// Document representation ("ImportedCurves"): every imported layer is a pc::FeatureType::Sketch feature WITHOUT
// constraints whose props mark it as imported curves:
//   importedCurves=1, layer=<source layer name>, source=<file name>, importGroup=<Cizim<k>>,
//   notes=<JSON array [{"u","v","h","r","t"}]>  (only on the "Ölçüler/Notlar" sketch: text, displayed only)
// Dimension / text / border / title-block curves are construction entities (displayed, never part of a region).
// Layer membership + colour / line style / visibility live in Document::layers (pc/Layers.h). Because the curves
// are ordinary sketch entities, sketch regions (Fill) and Pull / Extrude work on them unchanged, on any plane.
#include "pc/Document.h"
#include "pc/Model.h"
#include <gp_Trsf.hxx>
#include <gp_Pnt.hxx>
#include <string>
#include <vector>

namespace pcd::import2d {

struct Options {
  pc::SketchPlaneKind plane = pc::SketchPlaneKind::XY;   // XY | XZ | YZ
  double scale = 0;          // part mm per drawing mm; 0 = automatic (DXF: $INSUNITS; PDF: dimensions / title block)
  std::string groupName;     // "" = "Cizim<k>"
};

struct LayerInfo {
  std::string name;          // layer name in the document (DXF layer, PDF line role, "Ölçüler/Notlar")
  std::string color = "#1C2432";
  std::string lineStyle = "solid";
  int curves = 0;
  bool notes = false;        // annotations only (construction)
};

struct Note { double u = 0, v = 0, h = 2.5, rotDeg = 0; std::string text; };

struct Result {
  bool ok = false;
  std::string message;       // Turkish summary or error
  std::string sourceKind;    // "DXF" | "PDF"
  double scale = 1;          // applied (part mm per drawing mm)
  std::string scaleEvidence;
  bool scaleConfident = true;   // false: PDF scale guessed (the UI asks the user)
  double scaleSuggestion = 1;
  std::vector<pc::Feature> sketches;   // new sketch features (props as above), not yet in a document
  std::vector<LayerInfo> layers;
  int curveCount = 0, noteCount = 0, skipped = 0;
};

bool isDrawingFile(const std::string& path);          // .dxf / .pdf
bool available();                                     // built with the drawing import library
// Reads page 0 / model space. Feature ids are made unique against `into`.
Result readDrawing(const std::string& path, const Options& opt, const pc::Document& into);
// Appends the sketches and creates / updates the layers (colour / style of new layers). Returns new feature ids.
std::vector<std::string> mergeInto(pc::Document& doc, const Result& r);
// Notes stored on an imported sketch.
std::vector<Note> notesOf(const pc::Feature& f);
bool isImportedSketch(const pc::Feature& f);

// ---------------------------------------------------------------- curves in 3D
struct CurveRef {
  std::string sid;
  int eid = 0;
  bool operator==(const CurveRef& o) const { return sid == o.sid && eid == o.eid; }
  bool operator<(const CurveRef& o) const { return sid < o.sid || (sid == o.sid && eid < o.eid); }
};
bool parseCurveName(const std::string& name, CurveRef& out);   // "sketch:<sid>:e<id>"
std::string curveName(const CurveRef& c);
// 3D sample points of a curve (end points first: lines 2, arcs start/end/mid, circles 4 quadrants + centre last).
std::vector<gp_Pnt> curvePoints(const pc::Document& doc, const pc::model::ModelResult& model, const CurveRef& c);
// Characteristic points for snapping: line / arc end points, arc / circle centres.
std::vector<gp_Pnt> snapPoints(const pc::Document& doc, const pc::model::ModelResult& model, const CurveRef& c);

// "Görünüş seç": the connected cluster of non-construction curves around `seed` (bounding boxes touching within
// `tol` mm), over the visible sketches given (sketch ids).
std::vector<CurveRef> connectedCluster(const pc::Document& doc, const pc::model::ModelResult& model, const CurveRef& seed,
                                       const std::vector<std::string>& sketchIds, double tol = 0.5);

// ---------------------------------------------------------------- view placement ("Görünüşü yerleştir")
enum class FoldEdge { Auto, Top, Bottom, Left, Right };
struct Placement {
  bool ok = false;
  std::string message;
  FoldEdge edge = FoldEdge::Top;     // resolved edge of the cluster box (frame of the cluster's plane)
  double angleDeg = 90;
  gp_Pnt axisPoint;                  // fold axis
  gp_Dir axisDir;
  gp_Pnt anchorSrc, anchorDst;       // snapping: anchorSrc (after rotation) lands on anchorDst
  gp_Trsf trsf() const;              // rotation about the axis, then translation R(anchorSrc) -> anchorDst
};
// Default fold of `cluster` towards the nearest aligned other view among `sketchIds` (SpaceClaim: fold a side view
// up about the edge that faces the main view and snap that edge onto the main view's matching edge).
Placement proposePlacement(const pc::Document& doc, const pc::model::ModelResult& model, const std::vector<CurveRef>& cluster,
                           const std::vector<std::string>& sketchIds, FoldEdge edge = FoldEdge::Auto, double angleDeg = 90);
// Recomputes axis / rotation for a new edge or angle while keeping user anchors.
void rebuildPlacement(Placement& p, const pc::Document& doc, const pc::model::ModelResult& model,
                      const std::vector<CurveRef>& cluster);

// Moves `curves` by the rigid transform `T` into sketches on the transformed plane (one per source sketch; layer
// and import props kept). targetLayer != "" puts the moved curves into that layer instead (move-to-layer, T may be
// identity). Planes: XY (+offset) or a hidden axisAngle datum plane (show=0) for planes containing a world axis
// direction. Emptied imported source sketches are removed. Returns false + err (doc untouched) on failure.
bool moveCurves(pc::Document& doc, const pc::model::ModelResult& model, const std::vector<CurveRef>& curves, const gp_Trsf& T,
                const std::string& targetLayer, std::string* err, std::vector<std::string>* newSketchIds = nullptr);

} // namespace pcd::import2d
