#pragma once
// PersistCAD core — the parametric part document + JSON (de)serialization.
// File extension: .pcad (JSON).  Schema version: "persistcad/1".
#include "pc/Feature.h"
#include "pc/Layers.h"   // SC-2D-IMPORT
#include "pc/MfgSettings.h"
#include "pc/NamedSelection.h"
#include "pc/Parameter.h"
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace pc {

// View state saved with the document (SpaceClaim-like "Hide / Show"): never affects geometry, volume, mfg
// checks, STEP export, Save-as-App or the runner. Members are persistent references resolved like named
// selections (pc::model::resolve): a hidden face that is split stays hidden in every piece, a face that no longer
// exists is ignored. Body members hide whole bodies (split pieces follow). Key "view" omitted when empty.
struct ViewState {
  NamedSelection hidden;               // name "Gizli"; Face + Body members
  NamedSelection transparent;          // name "Saydam"; Face + Body members ("Saydam yap")
  // SC-COLOR: face / body colours. One group per colour ("#rrggbb" lower case in NamedSelection::color); members are
  // Face and Body persistent references (same robustness as `hidden`). A Body member colours every face of the body
  // unless a Face member (own group) overrides it. Key "colors" in the .pcad "view" object; omitted when empty.
  std::vector<NamedSelection> colors;
  bool empty() const { return hidden.members.empty() && transparent.members.empty() && colors.empty(); }
  ViewState() {
    hidden.name = "Gizli"; hidden.id = "view.hidden";
    transparent.name = "Saydam"; transparent.id = "view.transparent";
  }
};

struct Document {
  std::string name = "Part1";
  ParamTable params;
  std::vector<Feature> features;      // history order
  MfgSettings mfg;
  std::vector<NamedSelection> namedSelections;   // user-named face/edge/body sets (pc/NamedSelection.h)
  ViewState view;                     // hidden / transparent faces and bodies (view state, see above)
  LayerTable layers;                  // SC-2D-IMPORT: layers (pc/Layers.h), view state; key "layers" omitted when empty

  Feature* feature(const std::string& id);
  const Feature* feature(const std::string& id) const;
  int indexOf(const std::string& id) const;               // -1 if missing
  std::string uniqueFeatureId(FeatureType t) const;       // "Extrude3" style

  NamedSelection* namedSelection(const std::string& name);              // case-insensitive, nullptr if none
  const NamedSelection* namedSelection(const std::string& name) const;
  const NamedSelection* namedSelectionById(const std::string& id) const;
  std::string uniqueNamedSelectionId() const;                            // "ns<k>", k > every used k

  // Evaluate params; throws std::runtime_error with combined message on failure.
  ParamValues evaluateParams(const std::map<std::string, std::string>& overrides = {}) const;
};

nlohmann::json toJson(const Document& d);
Document documentFromJson(const nlohmann::json& j);       // throws std::runtime_error
bool saveDocument(const Document& d, const std::string& path, std::string* err = nullptr);
bool loadDocument(Document& d, const std::string& path, std::string* err = nullptr);

nlohmann::json toJson(const MfgSettings& m);
MfgSettings mfgSettingsFromJson(const nlohmann::json& j);
nlohmann::json toJson(const Sketch& s);
nlohmann::json toJson(const NamedSelection& n);
NamedSelection namedSelectionFromJson(const nlohmann::json& j);          // throws on malformed members
Sketch sketchFromJson(const nlohmann::json& j);

} // namespace pc
