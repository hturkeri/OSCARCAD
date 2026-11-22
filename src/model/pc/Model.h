#pragma once
// PersistCAD model module — history-based regeneration with persistent topological naming.
// OWNER: Agent B (model). Interface frozen; implementation in src/model/*.cpp.
#include "pc/Document.h"
#include "pc/SketchSolver.h"
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
#include <map>
#include <string>
#include <vector>

namespace pc::model {

struct FeatureStatus {
  std::string featureId;
  bool ok = true;
  std::string message;           // error/warning text (Turkish)
};

// Per-feature data captured during regeneration (consumed by mfg checks and GUI).
struct FeatureInfo {
  std::string featureId;
  FeatureType type = FeatureType::Sketch;
  std::map<std::string, double> values;   // evaluated numeric props (e.g. distance, diameter, radius, thickness)
  std::vector<std::string> createdFaces;  // persistent names created by this feature
  gp_Ax3 sketchFrame;                     // Sketch: its 3D frame. Extrude/Revolve/Hole: frame of their sketch
  TopoDS_Shape toolShape;                 // Extrude/Revolve/Hole/Pattern: the tool body before the boolean
};

// v2: one entry per body (multi-body parts). Names per the BODIES contract in pc/Feature.h.
struct BodyInfo {
  std::string name;                        // "Extrude1", user bodyName, "Body:1" after splits ...
  std::string createdBy;                   // feature id that created the body
  TopoDS_Shape shape;                      // TopoDS_Solid (a sub-shape of ModelResult::shape)
  std::vector<std::string> faces;          // persistent face names belonging to this body
};

// v2: evaluated datum plane (FeatureType::Plane). frame: Location = origin, XDirection = u,
// YDirection = v, Direction = normal.
struct DatumPlane {
  std::string featureId;
  gp_Ax3 frame;
  double displaySize = 50;                 // suggested half-size of the drawn square (mm), from part bbox
};

struct ModelResult {
  bool ok = false;                        // all non-suppressed features regenerated
  TopoDS_Shape shape;                     // final body (TopoDS_Solid or compound of solids)
  std::map<std::string, TopoDS_Face> faces;   // persistent name -> face of `shape`
  std::map<std::string, TopoDS_Edge> edges;   // persistent name -> edge of `shape`
  std::vector<FeatureStatus> status;          // one per feature, in history order
  std::vector<FeatureInfo> info;              // one per regenerated feature
  std::map<std::string, sketch::SolveResult> sketchResults; // sketch feature id -> solve result
  std::map<std::string, Sketch> solvedSketches;             // sketch feature id -> solved copy
  ParamValues params;                         // values used
  std::vector<BodyInfo> bodies;               // v2: every body, in creation order (shape = compound of them)
  std::map<std::string, DatumPlane> datumPlanes; // v2: plane feature id -> evaluated plane

  // Named selections support (docs/named_selections/MODEL.md). Face LINEAGE: final face name -> sorted set of
  // every persistent name the face carried or inherited through the history (itself included): split pieces
  // inherit their parent name ("Extrude1.end:2" has "Extrude1.end"), a face formed from several faces (merge,
  // or a face shared by body and tool) inherits all of them, a trimmed/extended face (OCCT Modified) keeps its.
  std::map<std::string, std::vector<std::string>> faceLineage;
  // A persistent face name that existed during this regeneration but no longer exists on any face ->
  // the feature id that removed it (deleted by a cut/fillet/shell/intersect, consumed body, ...).
  std::map<std::string, std::string> removedBy;

  const BodyInfo* body(const std::string& name) const;   // v2
  std::string bodyOfFace(const std::string& faceName) const; // v2: "" if unknown

  std::string nameOf(const TopoDS_Face& f) const;   // "" if unknown
  std::string nameOf(const TopoDS_Edge& e) const;
};

// Regenerate the document with the given parameter values (from doc.evaluateParams(...)).
// stopAfter: feature index to stop after (-1 = all) — used for "rollback" in the GUI.
// Never throws: errors are reported in status; regeneration continues with remaining features
// where possible (failed feature is skipped).
ModelResult regenerate(const Document& doc, const ParamValues& values, int stopAfter = -1);

// Convenience: evaluate params with overrides then regenerate.
ModelResult regenerate(const Document& doc, const std::map<std::string, std::string>& overrides = {});

// 3D frame of a sketch feature (resolves OnFace using `partial`, the result up to before it).
bool sketchFrame(const Document& doc, const std::string& sketchFeatureId, const ParamValues& values,
                 const ModelResult& partial, gp_Ax3& out, std::string* err = nullptr);

// Mass properties of the result (mm^3, mm^2, mm; bounding box min/max)
struct MassProps {
  double volume = 0, area = 0;
  double cx = 0, cy = 0, cz = 0;
  double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
};
MassProps massProperties(const TopoDS_Shape& s);

// Export helpers
bool exportSTEP(const TopoDS_Shape& s, const std::string& path, std::string* err = nullptr);
bool exportSTL(const TopoDS_Shape& s, const std::string& path, double linDefl = 0.05, std::string* err = nullptr);

} // namespace pc::model
