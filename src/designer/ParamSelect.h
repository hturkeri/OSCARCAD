#pragma once
// "Seçimden parametre" — pure logic (no widgets). What can the current selection be measured as (line -> length,
// circle -> diameter / radius, arc -> radius + sweep, two points -> distance / dx / dy, two lines -> angle or
// distance, point -> X / Y from the origin, an existing dimension label, 3D faces / edges -> the feature field or
// sketch dimension that produced them), and how a parameter is bound to it:
//   * sketch targets: the driving dimension is created if missing (or the existing one is re-bound: "W", "W/2"
//     for an existing radius when the parameter is a diameter, ...); over-constraining is reported with the
//     conflicting constraint ids (never silently dropped);
//   * feature targets: the feature expression becomes the parameter name.
// Used by ParamSelectUi (badge, quick popup, Properties rows, context menus, Ctrl+G) in sketch and 3D mode.
#include "ToolApi.h"
#include "pc/Document.h"
#include "pc/Model.h"
#include <QList>
#include <QString>
#include <map>
#include <string>
#include <vector>

namespace pcd::psel {

// One place a parameter is written to.
struct Target {
  enum class Kind { SketchDim, FeatureProp };
  Kind kind = Kind::SketchDim;
  // SketchDim: sketchFeature "" = the sketch open in the editor; otherwise a Sketch feature id of the document.
  std::string sketchFeature;
  pc::SkConstraintType type = pc::SkConstraintType::Length;   // dimension to ADD when constraintId < 0
  std::vector<int> refs;
  int constraintId = -1;      // existing dimension that will be re-bound (-1: a new one is added)
  std::string scale;          // how the parameter enters the existing dimension: "" (N), "half" (N/2), "double" (2*N), "neg" (-N)
  // FeatureProp
  std::string featureId, prop;
  std::string currentExpr;    // current expression of the existing dimension / field ("" = none)
  bool operator==(const Target& o) const {
    return kind == o.kind && sketchFeature == o.sketchFeature && type == o.type && refs == o.refs && featureId == o.featureId &&
           prop == o.prop && constraintId == o.constraintId;
  }
};

struct Measure {
  QString key;            // "L", "D", "R", "sweep", "dist", "dx", "dy", "pld", "angle", "x", "y", "dim", "T", ...
  QString label;          // Turkish: "Uzunluk", "Çap", "Yarıçap", ...
  QString prefix;         // suggested-name prefix ("L", "W", "H", "D", "R", "A", ...)
  bool barePrefix = false;   // W / H: the bare prefix is tried first ("W", then "W2")
  pc::ParamKind kind = pc::ParamKind::Length;
  double value = 0;       // current value (mm / degree)
  QList<Target> targets;
  QString boundParam;     // every target is exactly this parameter already
  QString currentExpr;    // common current expression of the targets ("" if none / different)
  bool canParam = true;
  QString why;            // reason when !canParam
  QString alt;            // toggle partner key (D <-> R)
  QString where;          // short context ("Sketch1", "Extrude1", "2 çember")
  QString unit() const { return kind == pc::ParamKind::Angle ? QString::fromUtf8("°") : QString("mm"); }
  QString valueText() const;   // "60 mm", "45°"
  QString text() const;        // badge text: "L = 60 mm", "Ø 20 mm", "W = 60 mm (W)"
};

// ---- measuring
// Sketch selection (ids of points / entities / dimensional constraints of `sk`, the solved sketch).
QList<Measure> sketchMeasures(const pc::Sketch& sk, const pc::ParamValues& vals, const std::vector<int>& sel);
// 3D selection (faces, edges, sketch curves drawn in 3D). Several items of the same measure kind are merged into one
// measure with several targets ("4 delik çapı" -> one parameter for all).
QList<Measure> modelMeasures(const pc::Document& doc, const pc::model::ModelResult& model, const QList<SelItem>& sel);

// ---- naming
QString suggestName(const pc::Document& doc, const Measure& m, const QStringList& alsoTaken = {});
QString checkName(const pc::Document& doc, const QString& name);   // "" = ok, else the Turkish reason

// ---- binding
struct BindResult {
  enum class Status { Ok, Conflict, Failed };
  Status status = Status::Failed;
  std::vector<int> conflicting;    // sketch constraint ids; removing ANY ONE of them makes the binding solvable (best first)
  QString conflictText;            // "Uzunluk W = 80 (#12) (başka seçenek: Dikey (#10))"
  QString message;
  std::string conflictSketch;      // bindInDocument: sketch feature the conflicting ids belong to
  int added = 0, rebound = 0;
};
// `vals` must already contain `param`. removeIds: constraints deleted before binding (conflict resolution).
BindResult bindInSketch(pc::Sketch& sk, const pc::ParamValues& vals, const Measure& m, const std::string& param,
                        const std::vector<int>& removeIds = {});
// 3D mode: `cand` must already contain the parameter. Sketch targets are bound in their sketch features, feature
// targets get `param` as expression. removeIds per sketch feature id.
BindResult bindInDocument(pc::Document& cand, const Measure& m, const std::string& param,
                          const std::map<std::string, std::vector<int>>& removeIds = {});

QString constraintText(const pc::Sketch& sk, const pc::ParamValues& vals, int constraintId);
QString dimensionTypeLabel(pc::SkConstraintType t);

// Parameters the expressions of `doc` (features, sketches, other parameters) use.
bool documentUsesParameter(const pc::Document& doc, const std::string& name);

} // namespace pcd::psel
