#pragma once
// SC-UX (WP-B) — PullMapping: pure logic of the Pull tool (SPEC §2.3, §2.5). No GUI.
//   analyze(): what does pulling the current selection edit? (new Extrude from a sketch region, Extrude end face ->
//              distance, fillet face -> radius, edges -> new Fillet/Chamfer, ...) + the current value and binding.
//   apply():   the value typed / dragged -> a candidate change: new feature, edited document or parameter change.
// SpaceClaim face rule (docs/spaceclaim_ux/PULL_FACE_FIX.md): a revolve face maps to the revolve ANGLE only for the
// start/end caps of a partial revolve. Any other planar face without a direct parametric mapping (revolve profile
// faces, faces of cuts / patterns / imported bodies, plain extrude sides) is pulled by EXTRUDING THE FACE: a new
// Sketch on the face (its outer + inner loops as lines / arcs / circles) + a new Extrude (pullFace=<face>) merged
// per the Options panel. A cylindrical revolve face offsets the radius dimensions of its profile line.
// Parameter rule: a field bound to a parameter with a plain-number expression edits the PARAMETER; a parameter
// with a derived expression ("Lbore = Hb + T - 4") is refused with a message; a number or free expression edits
// the feature's own expression.
#include "ToolApi.h"
#include <QString>
#include <QStringList>
#include <functional>
#include <QPair>
#include <gp_Ax3.hxx>
#include <TopoDS_Edge.hxx>
#include <vector>

namespace pcd::pull {

enum class Kind {
  None, NewExtrude, NewRevolve, ExtrudeEnd, ExtrudeStart, FilletRadius, ChamferDistance, HoleDiameter, HoleDepth,
  RevolveAngle, SketchDim, NewFillet, NewChamfer, MoveDatum,
  FaceExtrude,                // planar face -> new Sketch(on face) + Extrude of that face region (pull = offset)
  RevolveRadius,              // cylindrical revolve face -> radial dimensions of its profile line (offset)
  UpToOffset,                 // SC-PULLUPTO: end face of an Up To extrude -> its upToOffset (delta)
  Unsupported
};

struct Options {
  QString merge = "auto";     // auto | add | cut | nomerge | intersect
  QString edge = "round";     // round | chamfer
  bool bothSides = false;     // Extrude direction=symmetric
  bool keepTool = false;
  QString bodyName;           // nomerge only
  QString targets;            // comma separated body names ("" = automatic)
  QString draft;              // Extrude draft (deg) expression, "" = none
  QString revolveAxis;        // NewRevolve: "U" | "V" | entity id
};

struct Target {
  Kind kind = Kind::None;
  QString featureId;          // edited feature (the sketch for NewExtrude / NewRevolve)
  QString prop;               // edited prop ("distance", "radius", ...); SketchDim: constraint id
  QString sketchId;
  int regionIndex = -1, regionCount = 0;
  QStringList refs;           // NewFillet / NewChamfer edges
  QString face;               // the selected face / region name (handle anchor)
  QString fieldLabel = "Mesafe";
  QString unit = "mm";
  bool delta = false;         // primary value is a change (Extrude end / start face)
  double current = 0;         // current numeric value of the field
  QString currentExpr;
  QString boundParam;         // plain-number parameter the field is bound to
  QString refusal;            // non-empty: pulling is refused (message)
  QString message;            // information (e.g. multi-region warning)
  QString describe;           // "bölge Sketch5:r0", "Extrude1.end", "3 kenar"
  QString body;               // FaceExtrude: body owning the face (Add / Cut target)
  QStringList dims;           // RevolveRadius: sketch constraint ids that place the line radially
  int sign = 1;               // RevolveRadius: +1 = pulling along the face normal grows the radius, -1 shrinks it
  // SC-PULLUPTO: NewExtrude / NewRevolve — the selected regions per sketch (selection order; first = sketchId).
  QList<QPair<QString, QList<int>>> regionSets;
  bool valid() const { return kind != Kind::None && kind != Kind::Unsupported && kind != Kind::MoveDatum && refusal.isEmpty(); }
};

Target analyze(const pc::Document& doc, const pc::model::ModelResult& model, const QList<SelItem>& sel, const Options& opt);

struct Result {
  bool ok = false;
  QString refusal;            // why nothing was produced
  bool isNewFeature = false;
  pc::Feature feature;        // isNewFeature: insert through IDesignHost::insertFeature
  bool hasPreFeature = false; // FaceExtrude: `preFeature` (the face sketch) is inserted right before `feature`
  pc::Feature preFeature;
  QList<pc::Feature> moreFeatures;   // SC-PULLUPTO: regions of further sketches pulled together (inserted after `feature`)
  pc::Document doc;           // otherwise (and !paramName): commit through IDesignHost::commitDocument
  QString paramName, paramExpr;   // a parameter change through IDesignHost::changeParameter
  QString undoText;           // "Çek: Extrude3 = 8"
  QString featureId, prop;    // the field that now holds the value (for Ctrl+G "Grup oluştur")
  QString valueExpr;          // the expression written into feature/prop
  QString resultFace;         // face to keep selected after the commit ("Extrude3.end")
  QString status;             // status-bar text after success
};

using Evaluator = std::function<bool(const QString& expr, double* value)>;
// absolute=false: `value` is the primary field (a change for delta fields); absolute=true: the new total.
Result apply(const pc::Document& doc, const pc::model::ModelResult& model, const Target& t, const QString& value,
             bool absolute, const Options& opt, const Evaluator& eval);

// The document after inserting a new-feature result (preFeature + feature) at the rollback position (-1 = end).
pc::Document withNewFeatures(const pc::Document& doc, const Result& r, int rollbackIndex = -1);

// FaceExtrude helper: a Sketch feature on planar face `face` (OnFace, offset 0) whose geometry is the face's outer and
// inner loops (lines, arcs, circles). false + *err for non-planar faces or other edge types (splines, ellipses).
// *normalInward: the sketch frame normal points into the material (indirect plane, e.g. Revolve faces) -> the Extrude
// direction "reverse" is the outward pull.
bool faceSketch(const pc::Document& doc, const pc::model::ModelResult& model, const QString& face, pc::Feature* out,
                QString* err, bool* normalInward = nullptr, gp_Ax3* frameOut = nullptr,
                const std::vector<TopoDS_Edge>* onlyEdges = nullptr);   // SC-PULLUPTO: only these edges (Fill a hole)

// ---- SC-PULLUPTO: Kadar (Up To) / Cetvel (Ruler), docs/spaceclaim_ux/PULL_UPTO.md
// `regions=` value for pulling `indices` of sketch `sid`: "" when the sketch uses its classic profile and every region is
// pulled (unchanged documents), else "0,2".
QString regionsProp(const pc::Document& doc, const pc::model::ModelResult& model, const QString& sid, const QList<int>& indices);
// Can the target be pulled "up to" something? (NewExtrude, FaceExtrude, ExtrudeEnd, UpToOffset)
bool supportsUpTo(const Target& t);
// Signed distance of `ref` (pc/UpTo.h grammar) from the plane the target is extruded from (first sketch / pulled face /
// the extrude's sketch), along that plane's frame normal. *outward: s measured along the pulled face's outward normal.
bool upToPreview(const pc::Document& doc, const pc::model::ModelResult& model, const Target& t, const QString& ref, double* s,
                 QString* err, double* outward = nullptr);
// The associative change: new Extrude(s) with upTo=<ref> (+ upToOffset), or the existing Extrude switched to Up To.
// offsetExpr: "" / "0" = exactly up to the target; Cetvel: "-<value>" stops <value> before it.
Result applyUpTo(const pc::Document& doc, const pc::model::ModelResult& model, const Target& t, const QString& ref,
                 const QString& offsetExpr, const Options& opt, const Evaluator& eval);

// Binding analysis of an expression (exposed for Move and the tests).
// returns: 0 = free (number / expression), 1 = bound to a plain-number parameter (*param), 2 = derived (*message).
int binding(const pc::Document& doc, const std::string& expr, const QString& fieldLabel, QString* param, QString* message);
bool isPlainNumber(const std::string& expr, double* v = nullptr);
QString unsupportedFaceMessage();

} // namespace pcd::pull
