// PersistCAD core — layers (SC-2D-IMPORT). See pc/Layers.h.
#include "pc/Layers.h"
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace pc {

namespace {
bool validColor(const std::string& c) {
  if (c.size() != 7 || c[0] != '#') return false;
  for (size_t i = 1; i < 7; ++i)
    if (!std::isxdigit((unsigned char)c[i])) return false;
  return true;
}
void eraseAll(std::vector<std::string>& v, const std::string& s) { v.erase(std::remove(v.begin(), v.end(), s), v.end()); }
} // namespace

Layer* LayerTable::find(const std::string& name) {
  for (auto& l : layers) if (l.name == name) return &l;
  return nullptr;
}
const Layer* LayerTable::find(const std::string& name) const {
  for (auto& l : layers) if (l.name == name) return &l;
  return nullptr;
}
std::string LayerTable::layerOfSketch(const std::string& id) const {
  for (auto& l : layers)
    if (std::find(l.sketches.begin(), l.sketches.end(), id) != l.sketches.end()) return l.name;
  return kDefaultLayerName;
}
std::string LayerTable::layerOfBody(const std::string& b) const {
  for (auto& l : layers)
    if (std::find(l.bodies.begin(), l.bodies.end(), b) != l.bodies.end()) return l.name;
  // split pieces "<body>:k" follow their stem
  const auto c = b.find(':');
  if (c != std::string::npos) return layerOfBody(b.substr(0, c));
  return kDefaultLayerName;
}
bool LayerTable::sketchVisible(const std::string& id) const {
  const Layer* l = find(layerOfSketch(id));
  return !l || l->visible;
}
bool LayerTable::bodyVisible(const std::string& b) const {
  const Layer* l = find(layerOfBody(b));
  return !l || l->visible;
}
Layer& LayerTable::ensureDefault() {
  if (Layer* l = find(kDefaultLayerName)) return *l;
  Layer d;
  d.name = kDefaultLayerName;
  layers.insert(layers.begin(), d);
  return layers.front();
}
Layer& LayerTable::ensure(const std::string& name) {
  ensureDefault();
  if (Layer* l = find(name)) return *l;
  Layer n;
  n.name = name;
  layers.push_back(n);
  return layers.back();
}
void LayerTable::assignSketch(const std::string& id, const std::string& layer) {
  for (auto& l : layers) eraseAll(l.sketches, id);
  if (layer != kDefaultLayerName) ensure(layer).sketches.push_back(id);
  else ensureDefault();
}
void LayerTable::assignBody(const std::string& b, const std::string& layer) {
  for (auto& l : layers) eraseAll(l.bodies, b);
  if (layer != kDefaultLayerName) ensure(layer).bodies.push_back(b);
  else ensureDefault();
}
void LayerTable::forgetSketch(const std::string& id) {
  for (auto& l : layers) eraseAll(l.sketches, id);
}
std::string LayerTable::uniqueName(const std::string& base) const {
  for (int k = 1;; ++k) {
    const std::string n = base + std::to_string(k);
    if (!find(n)) return n;
  }
}

bool isValidLineStyle(const std::string& s) { return s == "solid" || s == "dashed" || s == "center" || s == "dotted"; }

json toJson(const LayerTable& t) {
  json a = json::array();
  for (const auto& l : t.layers) {
    json j = {{"name", l.name}, {"color", l.color}, {"lineStyle", l.lineStyle}, {"visible", l.visible}};
    if (!l.sketches.empty()) j["sketches"] = l.sketches;
    if (!l.bodies.empty()) j["bodies"] = l.bodies;
    a.push_back(j);
  }
  return a;
}

LayerTable layerTableFromJson(const json& j) {
  LayerTable t;
  if (!j.is_array()) return t;
  for (const auto& jl : j) {
    try {
      if (!jl.is_object() || !jl.contains("name") || !jl["name"].is_string()) continue;
      Layer l;
      l.name = jl["name"].get<std::string>();
      if (l.name.empty() || t.find(l.name)) continue;
      l.color = jl.value("color", l.color);
      if (!validColor(l.color)) l.color = Layer{}.color;
      l.lineStyle = jl.value("lineStyle", l.lineStyle);
      if (!isValidLineStyle(l.lineStyle)) l.lineStyle = "solid";
      l.visible = jl.value("visible", true);
      l.sketches = jl.value("sketches", std::vector<std::string>{});
      l.bodies = jl.value("bodies", std::vector<std::string>{});
      t.layers.push_back(std::move(l));
    } catch (...) {}
  }
  if (!t.layers.empty()) t.ensureDefault();
  return t;
}

} // namespace pc
