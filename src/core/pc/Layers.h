#pragma once
// PersistCAD core — LAYERS (SpaceClaim "Layers" tab), pure data. SC-2D-IMPORT (docs/import2d/IMPORT2D.md).
//
// A layer is a named display group with a colour, a line style and a visibility flag. Members are sketch feature
// ids (imported 2D drawing curves are sketch features, one per source layer / placed view) and body names. Every
// sketch / body that is not listed in any layer belongs to the default layer "Katman0".
// View state only: layers never change geometry, volume, mfg checks, STEP export or Save-as-App.
// Stored in the .pcad document under "layers" (key omitted when the table is empty -> old files unchanged).
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace pc {

inline constexpr const char* kDefaultLayerName = "Katman0";
inline constexpr const char* kNotesLayerName = "Ölçüler/Notlar";

struct Layer {
  std::string name;
  std::string color = "#1C2432";        // "#RRGGBB"
  std::string lineStyle = "solid";      // solid | dashed | center | dotted
  bool visible = true;
  std::vector<std::string> sketches;    // sketch feature ids
  std::vector<std::string> bodies;      // body names
};

struct LayerTable {
  std::vector<Layer> layers;            // display order; "Katman0" first once the table is non-empty
  bool empty() const { return layers.empty(); }
  Layer* find(const std::string& name);
  const Layer* find(const std::string& name) const;
  // Layer name of a sketch / body ("Katman0" when not listed).
  std::string layerOfSketch(const std::string& sketchId) const;
  std::string layerOfBody(const std::string& body) const;
  bool sketchVisible(const std::string& sketchId) const;
  bool bodyVisible(const std::string& body) const;
  // Makes sure "Katman0" exists (first). Returns the layer.
  Layer& ensureDefault();
  // Adds (or returns) a layer with that name.
  Layer& ensure(const std::string& name);
  // Moves a sketch / body into `layer` (removed from every other layer; "Katman0" membership stays implicit).
  void assignSketch(const std::string& sketchId, const std::string& layer);
  void assignBody(const std::string& body, const std::string& layer);
  void forgetSketch(const std::string& sketchId);   // removed from every layer
  std::string uniqueName(const std::string& base) const;   // "Katman1", "Katman2"...
};

bool isValidLineStyle(const std::string& s);
nlohmann::json toJson(const LayerTable& t);
LayerTable layerTableFromJson(const nlohmann::json& j);   // tolerant: malformed entries are skipped

} // namespace pc
