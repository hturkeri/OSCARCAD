#include "pc/Document.h"
#include "pc/Expression.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using nlohmann::json;

namespace pc {

// ---------------------------------------------------------------- enum strings
namespace {
struct FName { FeatureType t; const char* n; };
const FName kFeat[] = {{FeatureType::Sketch, "Sketch"},   {FeatureType::Extrude, "Extrude"},
                       {FeatureType::Revolve, "Revolve"}, {FeatureType::Hole, "Hole"},
                       {FeatureType::Fillet, "Fillet"},   {FeatureType::Chamfer, "Chamfer"},
                       {FeatureType::Shell, "Shell"},     {FeatureType::Pattern, "Pattern"},
                       {FeatureType::Plane, "Plane"},     {FeatureType::Combine, "Combine"},
                       {FeatureType::Transform, "Transform"}};
struct AName { AmProcess t; const char* n; };
const AName kAm[] = {{AmProcess::None, "None"},           {AmProcess::LPBF_Metal, "LPBF_Metal"},
                     {AmProcess::EBM_Metal, "EBM_Metal"}, {AmProcess::DED_Metal, "DED_Metal"},
                     {AmProcess::FDM_Polymer, "FDM_Polymer"}, {AmProcess::SLA_Resin, "SLA_Resin"},
                     {AmProcess::SLS_Polymer, "SLS_Polymer"}};
struct CName { ConvProcess t; const char* n; };
const CName kConv[] = {{ConvProcess::None, "None"},
                       {ConvProcess::CNC_Milling3Axis, "CNC_Milling3Axis"},
                       {ConvProcess::CNC_Milling5Axis, "CNC_Milling5Axis"},
                       {ConvProcess::CNC_Turning, "CNC_Turning"}};
} // namespace

const char* toString(FeatureType t) { for (auto& f : kFeat) if (f.t == t) return f.n; return "?"; }
bool fromString(const std::string& s, FeatureType& o) { for (auto& f : kFeat) if (s == f.n) { o = f.t; return true; } return false; }
const char* toString(AmProcess t) { for (auto& f : kAm) if (f.t == t) return f.n; return "?"; }
bool fromString(const std::string& s, AmProcess& o) { for (auto& f : kAm) if (s == f.n) { o = f.t; return true; } return false; }
const char* toString(ConvProcess t) { for (auto& f : kConv) if (f.t == t) return f.n; return "?"; }
bool fromString(const std::string& s, ConvProcess& o) { for (auto& f : kConv) if (s == f.n) { o = f.t; return true; } return false; }

// ---------------------------------------------------------------- Document
Feature* Document::feature(const std::string& id) {
  for (auto& f : features) if (f.id == id) return &f;
  return nullptr;
}
const Feature* Document::feature(const std::string& id) const {
  for (auto& f : features) if (f.id == id) return &f;
  return nullptr;
}
int Document::indexOf(const std::string& id) const {
  for (size_t i = 0; i < features.size(); ++i) if (features[i].id == id) return static_cast<int>(i);
  return -1;
}
std::string Document::uniqueFeatureId(FeatureType t) const {
  std::string base = toString(t);
  for (int i = 1;; ++i) {
    std::string id = base + std::to_string(i);
    if (!feature(id)) return id;
  }
}

// ---------------------------------------------------------------- named selections (pure data)
const char* toString(NsMemberKind k) {
  return k == NsMemberKind::Edge ? "edge" : k == NsMemberKind::Body ? "body" : "face";
}
bool fromString(const std::string& s, NsMemberKind& o) {
  if (s == "face") o = NsMemberKind::Face;
  else if (s == "edge") o = NsMemberKind::Edge;
  else if (s == "body") o = NsMemberKind::Body;
  else return false;
  return true;
}

bool isValidSelectionName(const std::string& n, std::string* why) {
  auto fail = [&](const char* m) { if (why) *why = m; return false; };
  if (n.empty()) return fail("ad boş olamaz");
  if (n.size() > 128) return fail("ad en fazla 128 bayt olabilir");
  if (n.front() == ' ' || n.back() == ' ' || n.front() == '\t' || n.back() == '\t')
    return fail("ad boşlukla başlayamaz veya bitemez");
  for (unsigned char ch : n)
    if (ch < 0x20 || ch == 0x7f) return fail("ad kontrol karakteri içeremez");
  return true;
}

bool sameSelectionName(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}

bool isValidColor(const std::string& c) {
  if (c.size() != 7 || c[0] != '#') return false;
  for (size_t i = 1; i < 7; ++i)
    if (!std::isxdigit((unsigned char)c[i])) return false;
  return true;
}

NamedSelection* Document::namedSelection(const std::string& name) {
  for (auto& n : namedSelections) if (sameSelectionName(n.name, name)) return &n;
  return nullptr;
}
const NamedSelection* Document::namedSelection(const std::string& name) const {
  for (auto& n : namedSelections) if (sameSelectionName(n.name, name)) return &n;
  return nullptr;
}
const NamedSelection* Document::namedSelectionById(const std::string& id) const {
  for (auto& n : namedSelections) if (n.id == id) return &n;
  return nullptr;
}
std::string Document::uniqueNamedSelectionId() const {
  int mx = 0;
  for (auto& n : namedSelections)
    if (n.id.size() > 2 && n.id.compare(0, 2, "ns") == 0) {
      try { mx = std::max(mx, std::stoi(n.id.substr(2))); } catch (...) {}
    }
  for (int k = mx + 1;; ++k) {
    std::string id = "ns" + std::to_string(k);
    if (!namedSelectionById(id)) return id;
  }
}

ParamValues Document::evaluateParams(const std::map<std::string, std::string>& overrides) const {
  std::vector<ParamError> errs;
  auto v = params.evaluate(&errs, overrides);
  if (!v) {
    std::ostringstream os;
    os << "Parametre hatası:";
    for (auto& e : errs) os << " [" << e.param << ": " << e.message << "]";
    throw std::runtime_error(os.str());
  }
  return *v;
}

// ---------------------------------------------------------------- JSON helpers
namespace {
const char* kindStr(ParamKind k) {
  switch (k) {
    case ParamKind::Length: return "length";
    case ParamKind::Angle: return "angle";
    case ParamKind::Count: return "count";
    default: return "scalar";
  }
}
ParamKind kindFrom(const std::string& s) {
  if (s == "angle") return ParamKind::Angle;
  if (s == "count") return ParamKind::Count;
  if (s == "scalar") return ParamKind::Scalar;
  return ParamKind::Length;
}
json numOrNull(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }
double numFrom(const json& j, const char* k, double def) {
  if (!j.contains(k) || j[k].is_null()) return def;
  return j[k].get<double>();
}
const char* planeStr(SketchPlaneKind k) {
  switch (k) {
    case SketchPlaneKind::XY: return "XY";
    case SketchPlaneKind::XZ: return "XZ";
    case SketchPlaneKind::YZ: return "YZ";
    case SketchPlaneKind::OnPlane: return "OnPlane";
    default: return "OnFace";
  }
}
SketchPlaneKind planeFrom(const std::string& s) {
  if (s == "XZ") return SketchPlaneKind::XZ;
  if (s == "YZ") return SketchPlaneKind::YZ;
  if (s == "OnFace") return SketchPlaneKind::OnFace;
  if (s == "OnPlane") return SketchPlaneKind::OnPlane;
  return SketchPlaneKind::XY;
}
const char* entStr(SkEntityType t) {
  return t == SkEntityType::Line ? "line" : t == SkEntityType::Circle ? "circle" : "arc";
}
SkEntityType entFrom(const std::string& s) {
  if (s == "circle") return SkEntityType::Circle;
  if (s == "arc") return SkEntityType::Arc;
  return SkEntityType::Line;
}
} // namespace

json toJson(const Sketch& s) {
  json j;
  j["plane"] = {{"kind", planeStr(s.plane.kind)}, {"faceRef", s.plane.faceRef}, {"offset", s.plane.offsetExpr}};
  if (!s.plane.planeRef.empty()) j["plane"]["planeRef"] = s.plane.planeRef;
  j["points"] = json::array();
  for (auto& p : s.points) j["points"].push_back({{"id", p.id}, {"x", p.x}, {"y", p.y}});
  j["entities"] = json::array();
  for (auto& e : s.entities) {
    json je = {{"id", e.id}, {"type", entStr(e.type)}, {"points", e.points}};
    if (e.type != SkEntityType::Line) je["radius"] = e.radius;
    if (e.construction) je["construction"] = true;
    j["entities"].push_back(je);
  }
  j["constraints"] = json::array();
  for (auto& c : s.constraints) {
    json jc = {{"id", c.id}, {"type", toString(c.type)}, {"refs", c.refs}};
    if (isDimensional(c.type)) jc["value"] = c.valueExpr;
    j["constraints"].push_back(jc);
  }
  j["nextId"] = s.nextId;
  return j;
}

Sketch sketchFromJson(const json& j) {
  Sketch s;
  if (j.contains("plane")) {
    auto& p = j["plane"];
    s.plane.kind = planeFrom(p.value("kind", "XY"));
    s.plane.faceRef = p.value("faceRef", "");
    s.plane.offsetExpr = p.value("offset", "0");
    s.plane.planeRef = p.value("planeRef", "");
  }
  int maxId = 0;
  for (auto& p : j.value("points", json::array())) {
    s.points.push_back({p.at("id").get<int>(), p.at("x").get<double>(), p.at("y").get<double>()});
    maxId = std::max(maxId, s.points.back().id);
  }
  if (!s.point(0)) s.points.insert(s.points.begin(), SkPoint{0, 0, 0});
  for (auto& e : j.value("entities", json::array())) {
    SkEntity en;
    en.id = e.at("id").get<int>();
    en.type = entFrom(e.at("type").get<std::string>());
    en.points = e.at("points").get<std::vector<int>>();
    en.radius = e.value("radius", 0.0);
    en.construction = e.value("construction", false);
    maxId = std::max(maxId, en.id);
    s.entities.push_back(en);
  }
  for (auto& c : j.value("constraints", json::array())) {
    SkConstraint sc;
    sc.id = c.at("id").get<int>();
    if (!fromString(c.at("type").get<std::string>(), sc.type))
      throw std::runtime_error("Bilinmeyen kısıt tipi: " + c.at("type").get<std::string>());
    sc.refs = c.at("refs").get<std::vector<int>>();
    if (c.contains("value")) {
      if (c["value"].is_number()) sc.valueExpr = formatNumber(c["value"].get<double>());
      else sc.valueExpr = c["value"].get<std::string>();
    }
    maxId = std::max(maxId, sc.id);
    s.constraints.push_back(sc);
  }
  s.nextId = std::max(j.value("nextId", 1), maxId + 1);
  return s;
}

json toJson(const NamedSelection& n) {
  json j = {{"id", n.id}, {"name", n.name}, {"color", n.color}};
  if (!n.description.empty()) j["description"] = n.description;
  j["createdAfter"] = n.createdAfter;
  j["createdAt"] = n.createdAt;
  if (!n.exportStep) j["exportStep"] = false;
  j["members"] = json::array();
  for (auto& m : n.members) {
    json jm = {{"kind", toString(m.kind)}, {"ref", m.ref}};
    if (!m.anchors.empty()) jm["anchors"] = m.anchors;
    if (m.sig.valid) {
      const auto& g = m.sig;
      json js = {{"surface", g.surface}, {"n", {g.nx, g.ny, g.nz}}, {"c", {g.cx, g.cy, g.cz}}, {"size", g.size}};
      if (g.radius != 0) js["radius"] = g.radius;
      if (!g.neighbours.empty()) js["neighbours"] = g.neighbours;
      if (!g.body.empty()) js["body"] = g.body;
      jm["sig"] = js;
    }
    j["members"].push_back(jm);
  }
  return j;
}

NamedSelection namedSelectionFromJson(const json& j) {
  NamedSelection n;
  n.id = j.value("id", "");
  n.name = j.at("name").get<std::string>();
  n.color = j.value("color", n.color);
  n.description = j.value("description", "");
  n.createdAfter = j.value("createdAfter", "");
  n.createdAt = j.value("createdAt", -1);
  n.exportStep = j.value("exportStep", true);
  for (auto& jm : j.value("members", json::array())) {
    NamedSelectionMember m;
    if (!fromString(jm.value("kind", "face"), m.kind))
      throw std::runtime_error("Bilinmeyen adlandırılmış seçim üye türü: " + jm.value("kind", ""));
    m.ref = jm.at("ref").get<std::string>();
    m.anchors = jm.value("anchors", std::vector<std::string>{});
    if (jm.contains("sig") && jm["sig"].is_object()) {
      const json& js = jm["sig"];
      auto& g = m.sig;
      g.valid = true;
      g.surface = js.value("surface", "");
      if (js.contains("n")) { g.nx = js["n"].at(0); g.ny = js["n"].at(1); g.nz = js["n"].at(2); }
      if (js.contains("c")) { g.cx = js["c"].at(0); g.cy = js["c"].at(1); g.cz = js["c"].at(2); }
      g.size = js.value("size", 0.0);
      g.radius = js.value("radius", 0.0);
      g.neighbours = js.value("neighbours", std::vector<std::string>{});
      g.body = js.value("body", "");
    }
    n.members.push_back(std::move(m));
  }
  return n;
}

json toJson(const MfgSettings& m) {
  const auto& a = m.am;
  const auto& c = m.conv;
  json j;
  j["additive"] = {{"enabled", a.enabled}, {"process", toString(a.process)}, {"material", a.material},
                   {"minWallThickness", a.minWallThickness}, {"minFeatureSize", a.minFeatureSize},
                   {"minHoleDiameter", a.minHoleDiameter}, {"maxSelfSupportingHoleDia", a.maxSelfSupportingHoleDia},
                   {"minOverhangAngle", a.minOverhangAngle}, {"minGap", a.minGap},
                   {"maxAspectRatio", a.maxAspectRatio}, {"minEscapeHoleDia", a.minEscapeHoleDia},
                   {"build", {a.buildX, a.buildY, a.buildZ}}, {"buildDir", {a.buildDirX, a.buildDirY, a.buildDirZ}},
                   {"layerThickness", a.layerThickness}};
  j["conventional"] = {{"enabled", c.enabled}, {"process", toString(c.process)}, {"material", c.material},
                       {"minToolDiameter", c.minToolDiameter}, {"maxToolLengthToDia", c.maxToolLengthToDia},
                       {"maxHoleDepthToDia", c.maxHoleDepthToDia}, {"minWallThickness", c.minWallThickness},
                       {"maxWallHeightToThickness", c.maxWallHeightToThickness},
                       {"minHoleDiameter", c.minHoleDiameter}, {"preferStandardDrills", c.preferStandardDrills},
                       {"stock", {c.stockX, c.stockY, c.stockZ}}, {"toolAxis", {c.toolAxisX, c.toolAxisY, c.toolAxisZ}}};
  return j;
}

MfgSettings mfgSettingsFromJson(const json& j) {
  MfgSettings m;
  if (j.contains("additive")) {
    auto& a = j["additive"];
    auto& o = m.am;
    o.enabled = a.value("enabled", o.enabled);
    fromString(a.value("process", std::string(toString(o.process))), o.process);
    o.material = a.value("material", o.material);
    o.minWallThickness = a.value("minWallThickness", o.minWallThickness);
    o.minFeatureSize = a.value("minFeatureSize", o.minFeatureSize);
    o.minHoleDiameter = a.value("minHoleDiameter", o.minHoleDiameter);
    o.maxSelfSupportingHoleDia = a.value("maxSelfSupportingHoleDia", o.maxSelfSupportingHoleDia);
    o.minOverhangAngle = a.value("minOverhangAngle", o.minOverhangAngle);
    o.minGap = a.value("minGap", o.minGap);
    o.maxAspectRatio = a.value("maxAspectRatio", o.maxAspectRatio);
    o.minEscapeHoleDia = a.value("minEscapeHoleDia", o.minEscapeHoleDia);
    // .at(): const operator[] with an out-of-range index is undefined behaviour (short arrays in a file).
    if (a.contains("build")) { o.buildX = a.at("build").at(0); o.buildY = a.at("build").at(1); o.buildZ = a.at("build").at(2); }
    if (a.contains("buildDir")) { o.buildDirX = a.at("buildDir").at(0); o.buildDirY = a.at("buildDir").at(1); o.buildDirZ = a.at("buildDir").at(2); }
    o.layerThickness = a.value("layerThickness", o.layerThickness);
  }
  if (j.contains("conventional")) {
    auto& c = j["conventional"];
    auto& o = m.conv;
    o.enabled = c.value("enabled", o.enabled);
    fromString(c.value("process", std::string(toString(o.process))), o.process);
    o.material = c.value("material", o.material);
    o.minToolDiameter = c.value("minToolDiameter", o.minToolDiameter);
    o.maxToolLengthToDia = c.value("maxToolLengthToDia", o.maxToolLengthToDia);
    o.maxHoleDepthToDia = c.value("maxHoleDepthToDia", o.maxHoleDepthToDia);
    o.minWallThickness = c.value("minWallThickness", o.minWallThickness);
    o.maxWallHeightToThickness = c.value("maxWallHeightToThickness", o.maxWallHeightToThickness);
    o.minHoleDiameter = c.value("minHoleDiameter", o.minHoleDiameter);
    o.preferStandardDrills = c.value("preferStandardDrills", o.preferStandardDrills);
    if (c.contains("stock")) { o.stockX = c.at("stock").at(0); o.stockY = c.at("stock").at(1); o.stockZ = c.at("stock").at(2); }
    if (c.contains("toolAxis")) { o.toolAxisX = c.at("toolAxis").at(0); o.toolAxisY = c.at("toolAxis").at(1); o.toolAxisZ = c.at("toolAxis").at(2); }
  }
  return m;
}

json toJson(const Document& d) {
  json j;
  j["schema"] = "persistcad/1";
  j["name"] = d.name;
  j["parameters"] = json::array();
  for (auto& p : d.params.all()) {
    json jp = {{"name", p.name}, {"expr", p.expression}, {"kind", kindStr(p.kind)},
               {"min", numOrNull(p.minValue)}, {"max", numOrNull(p.maxValue)}, {"description", p.description},
               {"exposed", p.exposed}, {"label", p.label}, {"step", p.step}, {"group", p.group}};
    j["parameters"].push_back(jp);
  }
  j["features"] = json::array();
  for (auto& f : d.features) {
    json jf = {{"id", f.id}, {"type", toString(f.type)}, {"suppressed", f.suppressed},
               {"props", f.props}, {"refs", f.refs}};
    if (f.sketch) jf["sketch"] = toJson(*f.sketch);
    j["features"].push_back(jf);
  }
  j["mfg"] = toJson(d.mfg);
  if (!d.namedSelections.empty()) {   // omitted when empty: files without selections are byte-identical to before
    j["namedSelections"] = json::array();
    for (auto& n : d.namedSelections) j["namedSelections"].push_back(toJson(n));
  }
  if (!d.view.empty()) {   // view state (Gizle / Göster); omitted when nothing is hidden or transparent
    json jv = json::object();
    if (!d.view.hidden.members.empty()) jv["hidden"] = toJson(d.view.hidden)["members"];
    if (!d.view.transparent.members.empty()) jv["transparent"] = toJson(d.view.transparent)["members"];
    if (!d.view.colors.empty()) {   // SC-COLOR
      jv["colors"] = json::array();
      for (const auto& g : d.view.colors)
        if (!g.members.empty()) jv["colors"].push_back({{"color", g.color}, {"members", toJson(g)["members"]}});
    }
    j["view"] = jv;
  }
  if (!d.layers.empty()) j["layers"] = toJson(d.layers);   // SC-2D-IMPORT
  return j;
}

Document documentFromJson(const json& j) {
  try {
    Document d;
    std::string schema = j.value("schema", "persistcad/1");
    if (schema.rfind("persistcad/", 0) != 0) throw std::runtime_error("Tanınmayan şema: " + schema);
    d.name = j.value("name", "Part1");
    for (auto& jp : j.value("parameters", json::array())) {
      Parameter p;
      p.name = jp.at("name").get<std::string>();
      if (jp.at("expr").is_number()) p.expression = formatNumber(jp["expr"].get<double>());
      else p.expression = jp.at("expr").get<std::string>();
      p.kind = kindFrom(jp.value("kind", "length"));
      p.minValue = numFrom(jp, "min", -std::numeric_limits<double>::infinity());
      p.maxValue = numFrom(jp, "max", std::numeric_limits<double>::infinity());
      p.description = jp.value("description", "");
      p.exposed = jp.value("exposed", false);
      p.label = jp.value("label", "");
      p.step = jp.value("step", 0.0);
      p.group = jp.value("group", "");
      if (!d.params.add(p)) throw std::runtime_error("Geçersiz/tekrarlı parametre adı: " + p.name);
    }
    for (auto& jf : j.value("features", json::array())) {
      Feature f;
      f.id = jf.at("id").get<std::string>();
      if (!fromString(jf.at("type").get<std::string>(), f.type))
        throw std::runtime_error("Bilinmeyen feature tipi: " + jf.at("type").get<std::string>());
      f.suppressed = jf.value("suppressed", false);
      if (jf.contains("props"))
        for (auto& [k, v] : jf["props"].items())
          f.props[k] = v.is_string() ? v.get<std::string>() : (v.is_number() ? formatNumber(v.get<double>()) : v.dump());
      f.refs = jf.value("refs", std::vector<std::string>{});
      if (jf.contains("sketch")) f.sketch = sketchFromJson(jf["sketch"]);
      if (f.type == FeatureType::Sketch && !f.sketch) f.sketch = Sketch::create();
      d.features.push_back(std::move(f));
    }
    if (j.contains("mfg")) d.mfg = mfgSettingsFromJson(j["mfg"]);
    if (j.contains("namedSelections") && j["namedSelections"].is_array()) {
      for (auto& jn : j["namedSelections"]) {
        NamedSelection n = namedSelectionFromJson(jn);
        std::string why;
        if (!isValidSelectionName(n.name, &why)) throw std::runtime_error("Geçersiz seçim adı '" + n.name + "': " + why);
        if (d.namedSelection(n.name)) throw std::runtime_error("Tekrarlı seçim adı: " + n.name);
        if (!isValidColor(n.color)) n.color = NamedSelection{}.color;
        if (n.id.empty() || d.namedSelectionById(n.id)) n.id = d.uniqueNamedSelectionId();
        d.namedSelections.push_back(std::move(n));
      }
    }
    if (j.contains("view") && j["view"].is_object()) {
      const json& jv = j["view"];
      try {   // view state is never worth refusing a file for
        if (jv.contains("hidden")) d.view.hidden = namedSelectionFromJson(json{{"name", "Gizli"}, {"members", jv["hidden"]}});
        if (jv.contains("transparent")) d.view.transparent = namedSelectionFromJson(json{{"name", "Saydam"}, {"members", jv["transparent"]}});
        if (jv.contains("colors") && jv["colors"].is_array())   // SC-COLOR
          for (const auto& jg : jv["colors"]) {
            const std::string hex = jg.value("color", "");
            if (!isValidColor(hex)) continue;
            NamedSelection g = namedSelectionFromJson(json{{"name", "Renk " + hex}, {"members", jg.at("members")}});
            g.color = hex;
            g.id = "view.color." + hex;
            d.view.colors.push_back(std::move(g));
          }
      } catch (...) { d.view = ViewState(); }
    }
    if (j.contains("layers")) d.layers = layerTableFromJson(j["layers"]);   // SC-2D-IMPORT (tolerant)
    d.view.hidden.name = "Gizli";
    d.view.hidden.id = "view.hidden";
    d.view.transparent.name = "Saydam";
    d.view.transparent.id = "view.transparent";
    return d;
  } catch (const json::exception& ex) {
    throw std::runtime_error(std::string("JSON hatası: ") + ex.what());
  }
}

bool saveDocument(const Document& d, const std::string& path, std::string* err) {
  // Serialise first: dump() throws on invalid UTF-8 and must not leave a truncated file behind.
  std::string txt;
  try {
    txt = toJson(d).dump(2);
  } catch (const std::exception& ex) {
    if (err) *err = std::string("Belge serileştirilemedi: ") + ex.what();
    return false;
  }
  std::ofstream f(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
  if (!f) { if (err) *err = "Dosya yazılamadı: " + path; return false; }
  f << txt;
  f.flush();
  if (!f) { if (err) *err = "Yazma hatası: " + path; return false; }
  return true;
}

bool loadDocument(Document& d, const std::string& path, std::string* err) {
  try {
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "Dosya açılamadı: " + path; return false; }
    json j = json::parse(f);
    d = documentFromJson(j);
    return true;
  } catch (const std::exception& ex) {
    if (err) *err = ex.what();
    return false;
  }
}

} // namespace pc
