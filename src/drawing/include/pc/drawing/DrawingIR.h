#pragma once
// =====================================================================================================================
// PersistCAD — "Teknik resimden içe aktar" (drawing import): the intermediate representation.  FROZEN: DrawingIR 1.0.
// Change requests: docs/CONTRACT_REQUESTS.md with prefix "DRW:"; both affected WPs approve, the owner (WP-3) applies.
//
//   S1 ingest -> raw | S2 normalize -> norm | S3 -> layout | S4 -> annot | S5 -> views | S6 -> sketches | S7 -> synthesis
//
// JSON (schema "pcdrw/1", nlohmann; implementation WP-1 in src/drawing/ir/IRJson.cpp) — conventions, binding for
// hand-written fixtures:
//   * object keys = C++ member names exactly (camelCase), every member written (defaults included);
//   * enums = names from Enums.h; Vec2 = [x,y]; Box2 = {"lo":[x,y],"hi":[x,y]}; std::array = JSON array;
//   * std::optional absent = null; std::map<int,T> = array of [key, value] pairs sorted by key;
//     std::map<std::string,T> (Attrs, fields) = object; DrawingIR::stageVersion = object keyed by Stage name;
//   * pc::Sketch / pc::Parameter / pc::Document via pc_core's toJson (Document.h); Parameter as in Document JSON;
//   * UserEdit::payload is embedded as a parsed JSON value (not as a string);
//   * doubles printed by nlohmann default (shortest round-trip) -> same IR gives byte-identical output (golden tests).
//   * Readers ignore unknown keys and default missing ones (forward compatibility inside major version 1).
// =====================================================================================================================
#include "pc/drawing/Annotation.h"
#include "pc/drawing/Layout.h"
#include "pc/drawing/Primitives.h"
#include "pc/drawing/Sketches.h"
#include "pc/drawing/Synthesis.h"
#include "pc/drawing/UserEdit.h"
#include "pc/drawing/Version.h"
#include "pc/drawing/Views.h"
#include <nlohmann/json_fwd.hpp>

namespace pc::drawing {

struct DrawingIR {
  std::string schema = kSchemaId;
  std::string irVersion = kIRVersion;
  int nextId = 1;                   // shared id counter (see Primitives.h)
  PrimitiveSheet raw;               // S1 output (never modified afterwards)
  PrimitiveSheet norm;              // S2 output
  SheetLayout layout;               // S3
  Annotation annot;                 // S4
  ViewGraph views;                  // S5
  std::vector<SketchCandidate> sketches;        // S6
  std::vector<SynthesisCandidate> synthesis;    // S7 (sorted by score desc)
  int chosenSynthesis = -1;         // SynthesisCandidate id chosen in the wizard (-1 = best)
  std::vector<UserEdit> edits;
  std::map<Stage, std::string> stageVersion;    // "pc_drawing 0.1 / normalize v3" (cache invalidation)
  int lastStage = 0;                // highest Stage completed (static_cast<int>), 0 = none
  std::vector<Diagnostic> diagnostics;          // accumulated by the pipeline (all stages)
  Attrs attrs;

  int newId() { return nextId++; }
};

// ---- JSON / side-car file [WP-1]
nlohmann::json toJson(const DrawingIR& ir);
DrawingIR drawingIRFromJson(const nlohmann::json& j);   // throws std::runtime_error (bad schema / major version)
nlohmann::json toJson(const PrimitiveSheet& s);         // same layout as the "raw"/"norm" members of the IR JSON
PrimitiveSheet primitiveSheetFromJson(const nlohmann::json& j);   // throws std::runtime_error
bool saveIR(const DrawingIR& ir, const std::string& path, std::string* err = nullptr);   // *.pcdrw.json, indent 1
bool loadIR(DrawingIR& ir, const std::string& path, std::string* err = nullptr);

// ---- lookup helpers [P0, src/drawing/core/IRUtil.cpp] — linear search, nullptr if absent
const Primitive* findPrim(const PrimitiveSheet& s, int id);
Primitive* findPrim(PrimitiveSheet& s, int id);
const TextRun* findText(const PrimitiveSheet& s, int id);
TextRun* findText(PrimitiveSheet& s, int id);
const ViewRegion* findView(const SheetLayout& l, int id);
ViewRegion* findView(SheetLayout& l, int id);
const Dimension* findDimension(const Annotation& a, int id);
Dimension* findDimension(Annotation& a, int id);
LineRole roleOf(const DrawingIR& ir, int primId);       // LineRole::Unknown if not classified
// Clear the sections produced by `from` and all later stages (raw is cleared only for Stage::Ingest).
// Does not touch edits, nextId or stageVersion of earlier stages.
void clearFromStage(DrawingIR& ir, Stage from);

}  // namespace pc::drawing
