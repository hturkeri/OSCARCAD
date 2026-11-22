#pragma once
// PersistCAD drawing import — corpus ground truth (gt.json). OWNER WP-1. NOT frozen, but ADDITIVE ONLY:
// WP-1 may add members/functions; existing members and signatures below must not change (WP-3 codes against them).
#include "pc/drawing/DrawingIR.h"
#include <initializer_list>

namespace pc::drawing {

struct GtDimension {
  std::string text;                 // as printed ("Ø16 +0/-0.5")
  DimKind kind = DimKind::Unknown;
  double nominal = 0;
  std::optional<double> tolPlus, tolMinus;
  int viewIndex = -1;               // index into GroundTruth::views
  std::vector<int> geomPrimIds;     // ids into GroundTruth::sheet.prims (bound geometry)
  std::string paramName;            // expected parameter name ("D1")
  bool driving = true;
  // --- additive (WP-1, after P0)
  int textId = -1;                  // id into GroundTruth::sheet.texts of the main dimension text (-1 = none)
  std::vector<int> tolTextIds;      // stacked tolerance runs in sheet.texts ("+0" / "-0.5"), if printed separately
  int nativeDimId = -1;             // id into sheet.nativeDims when the case has a DXF DIMENSION for it
};
struct GtView { ViewRole role = ViewRole::Unknown; Box2 box; std::string label; Affine2 sheetToView = kIdentityAffine; };
struct GtParam { std::string name; double value = 0; ParamTag tag = ParamTag::FromDimension; };

struct GroundTruth {
  std::string family, caseName;     // "flange", "flange_03"
  std::string dir;                  // directory of gt.json
  std::map<std::string, std::string> files;   // "sheet.pdf" -> absolute path (only those that exist)
  PrimitiveSheet sheet;             // perfect S2 output (Visible/Hidden/Center... geometry + texts) in sheet mm
  std::map<int, LineRole> roles;    // sheet prim id -> role
  std::vector<GtView> views;
  std::vector<GtDimension> dims;
  Scale scale;
  ProjectionMethod projection = ProjectionMethod::FirstAngle;
  std::vector<GtParam> params;
  Strategy expectedStrategy = Strategy::SketchOnly;
  double volume = 0;                // mm^3
  std::string modelPath;            // model.pcad
  Attrs attrs;
};

// --- additive (WP-1): writer used by the corpus generator; same layout loadGroundTruth reads.
//   gt.json = {"schema":"pcdrw-gt/1","family","caseName","sheet":<PrimitiveSheet JSON>,"roles":[[id,"Visible"],..],
//              "views":[{role,box,label,sheetToView}],"dims":[{text,kind,nominal,tolPlus,tolMinus,viewIndex,geomPrimIds,
//              paramName,driving,textId,tolTextIds,nativeDimId}],"scale":{..},"projection","params":[{name,value,tag}],
//              "expectedStrategy","volume","modelPath","attrs":{}}
//   `dir` and `files` are not stored: loadGroundTruth fills them from the gt.json location.
nlohmann::json toJson(const GroundTruth& gt);
bool saveGroundTruth(const GroundTruth& gt, const std::string& gtJsonPath, std::string* err = nullptr);
// Primitive ids of `gt.sheet` whose role is one of `roles` (e.g. {Visible}).
std::vector<int> gtPrimIdsWithRole(const GroundTruth& gt, std::initializer_list<LineRole> roles);

bool loadGroundTruth(const std::string& gtJsonPath, GroundTruth& gt, std::string* err = nullptr);
// "Perfect S2 output" for WP-3 before WP-1's ingest is ready: IR with raw = norm = gt.sheet, nextId set.
DrawingIR irFromGroundTruth(const GroundTruth& gt);

}  // namespace pc::drawing
