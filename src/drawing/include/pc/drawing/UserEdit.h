#pragma once
// PersistCAD drawing import — replayable user edits (wizard corrections). FROZEN (DrawingIR 1.0).
//
// The wizard (WP-3) and the overlay view (WP-2) never mutate the IR directly: they append a UserEdit to
// DrawingIR::edits. The pipeline (WP-3, applyUserEdits) re-applies the edits of a stage right after that stage runs
// (also on "rerun from this stage"), in list order. Edits referring to ids that no longer exist are skipped with a
// warning ("edit.staleRef"). Elements created by an edit take ids from DrawingIR::nextId at replay time; replay
// is deterministic, so later edits may refer to them.
//
// Payload = JSON object text. Shared JSON conventions (same as IR JSON, see DrawingIR.h):
//   Vec2 = [x, y]   Box2 = {"lo":[x,y],"hi":[x,y]}   enums = names from Enums.h   ids = int
// Op catalogue (op string -> stage it belongs to -> payload):
#include "pc/drawing/Enums.h"
#include <string>

namespace pc::drawing {

struct UserEdit {
  Stage stage = Stage::Ingest;      // stage after which the edit is applied
  std::string op;                   // one of edit_op::*
  std::string payload;              // JSON text (kept as text so the header needs only json_fwd)
};

namespace edit_op {
// -- Ingest
inline constexpr const char* setSourceKind = "setSourceKind";  // {"kind":"ScannedPdf"}
inline constexpr const char* setPage = "setPage";              // {"page":0}
inline constexpr const char* setDpi = "setDpi";                // {"dpi":300}
// -- Normalize (geometry step of the wizard; emitted by OverlayView tools)
inline constexpr const char* deletePrims = "deletePrims";      // {"ids":[...]}
inline constexpr const char* addPrim = "addPrim";              // {"kind":"Line","p0":[x,y],"p1":[x,y]} |
                                                               // {"kind":"Circle","center":[x,y],"radius":r} |
                                                               // {"kind":"Arc","center":[x,y],"radius":r,"a0":d,"a1":d}
                                                               // optional "role":"Visible" (applied at Annotate)
inline constexpr const char* mergePrims = "mergePrims";        // {"ids":[...]}  (collinear lines / arc chain -> one)
inline constexpr const char* snapPoint = "snapPoint";          // {"id":n,"end":0|1,"to":[x,y]}  move a line/arc end
inline constexpr const char* setText = "setText";              // {"textId":n,"utf8":"Ø20 H7"}
inline constexpr const char* deleteTexts = "deleteTexts";      // {"ids":[...]}
// -- Layout
inline constexpr const char* setBorder = "setBorder";              // {"box":Box2}
inline constexpr const char* setTitleBlockBox = "setTitleBlockBox";// {"box":Box2}
inline constexpr const char* setTitleField = "setTitleField";      // {"key":"material","value":"..."}
inline constexpr const char* setScale = "setScale";                // {"num":1,"den":2} | {"nts":true}
inline constexpr const char* setProjection = "setProjection";      // {"method":"FirstAngle"}
inline constexpr const char* addView = "addView";                  // {"box":Box2,"role":"Front","label":""}
inline constexpr const char* setViewBox = "setViewBox";            // {"viewId":n,"box":Box2}
inline constexpr const char* setViewRole = "setViewRole";          // {"viewId":n,"role":"Section"}
inline constexpr const char* deleteView = "deleteView";            // {"viewId":n}
// -- Annotate
inline constexpr const char* setRole = "setRole";                  // {"ids":[...],"role":"Hidden"}
inline constexpr const char* setDimText = "setDimText";            // {"dimId":n,"utf8":"Ø16 +0/-0.5"}
inline constexpr const char* bindDimension = "bindDimension";      // {"dimId":n,"geomPrimIds":[a,b]} (+opt "anchors":[[x,y],[x,y]])
inline constexpr const char* setDriving = "setDriving";            // {"dimId":n,"driving":false}
inline constexpr const char* addDimension = "addDimension";        // {"textId":n,"geomPrimIds":[...]} (+opt "kind")
inline constexpr const char* deleteDimension = "deleteDimension";  // {"dimId":n}
// -- Views
inline constexpr const char* setPrimaryView = "setPrimaryView";    // {"viewId":n}
inline constexpr const char* setAxis = "setAxis";                  // {"viewId":n,"primId":m}  (-1 = none)
// -- Sketch
inline constexpr const char* setParamName = "setParamName";        // {"sketchId":n,"from":"D1","to":"D_govde"}
inline constexpr const char* setParamValueMode = "setParamValueMode"; // {"sketchId":n,"name":"D1","tolMid":true}
inline constexpr const char* closeDofMeasured = "closeDofMeasured";// {"sketchId":n}
inline constexpr const char* deleteConstraint = "deleteConstraint";// {"sketchId":n,"constraintId":c}
inline constexpr const char* replaceSketch = "replaceSketch";      // {"sketchId":n,"sketch":<pc::Sketch JSON>} (editor)
// -- Synthesize
inline constexpr const char* chooseStrategy = "chooseStrategy";    // {"candidateId":n}
}  // namespace edit_op

}  // namespace pc::drawing
