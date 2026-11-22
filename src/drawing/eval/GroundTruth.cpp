// gt.json reader/writer and the "perfect S2" converter WP-3 builds on. OWNER WP-1.
#include "pc/drawing/GroundTruth.h"
#include "ir/JsonUtil.h"
#include <filesystem>
#include <fstream>

namespace pc::drawing {
using namespace jsonio;
namespace fs = std::filesystem;

nlohmann::json toJson(const GroundTruth& gt) {
  json roles = json::array();
  for (auto& [id, r] : gt.roles) roles.push_back(json::array({id, toString(r)}));
  json views = json::array();
  for (auto& v : gt.views)
    views.push_back(json{{"role", en(v.role)}, {"box", box(v.box)}, {"label", v.label}, {"sheetToView", affine(v.sheetToView)}});
  json dims = json::array();
  for (auto& d : gt.dims)
    dims.push_back(json{{"text", d.text}, {"kind", en(d.kind)}, {"nominal", num(d.nominal)}, {"tolPlus", optD(d.tolPlus)},
                        {"tolMinus", optD(d.tolMinus)}, {"viewIndex", d.viewIndex}, {"geomPrimIds", d.geomPrimIds},
                        {"paramName", d.paramName}, {"driving", d.driving}, {"textId", d.textId},
                        {"tolTextIds", d.tolTextIds}, {"nativeDimId", d.nativeDimId}});
  json params = json::array();
  for (auto& p : gt.params) params.push_back(json{{"name", p.name}, {"value", num(p.value)}, {"tag", en(p.tag)}});
  return json{{"schema", "pcdrw-gt/1"}, {"family", gt.family}, {"caseName", gt.caseName}, {"sheet", toJson(gt.sheet)},
              {"roles", roles}, {"views", views}, {"dims", dims}, {"scale", toJsonScale(gt.scale)},
              {"projection", en(gt.projection)}, {"params", params}, {"expectedStrategy", en(gt.expectedStrategy)},
              {"volume", num(gt.volume)}, {"modelPath", gt.modelPath}, {"attrs", attrs(gt.attrs)}};
}

bool saveGroundTruth(const GroundTruth& gt, const std::string& path, std::string* err) {
  std::ofstream f(path, std::ios::binary);
  if (!f) { if (err) *err = "Dosya yazılamadı: " + path; return false; }
  f << toJson(gt).dump(1, ' ', false, json::error_handler_t::replace) << "\n";
  return static_cast<bool>(f);
}

bool loadGroundTruth(const std::string& gtJsonPath, GroundTruth& gt, std::string* err) {
  try {
    std::ifstream f(gtJsonPath, std::ios::binary);
    if (!f) { if (err) *err = "gt.json açılamadı: " + gtJsonPath; return false; }
    const json j = json::parse(f);
    if (!j.is_object() || !j.contains("sheet")) { if (err) *err = "gt.json: 'sheet' yok"; return false; }
    GroundTruth g;
    g.family = getS(j, "family"); g.caseName = getS(j, "caseName");
    g.sheet = primitiveSheetFromJson(j["sheet"]);
    if (j.contains("roles") && j["roles"].is_array())
      for (auto& e : j["roles"])
        if (e.is_array() && e.size() == 2 && e[0].is_number() && e[1].is_string()) {
          LineRole r = LineRole::Unknown; fromString(e[1].get<std::string>(), r); g.roles[e[0].get<int>()] = r;
        }
    if (j.contains("views") && j["views"].is_array())
      for (auto& v : j["views"]) {
        GtView gv; gv.role = getE(v, "role", ViewRole::Unknown); gv.box = getBox(v, "box"); gv.label = getS(v, "label");
        gv.sheetToView = getAffine(v, "sheetToView", kIdentityAffine); g.views.push_back(gv);
      }
    if (j.contains("dims") && j["dims"].is_array())
      for (auto& d : j["dims"]) {
        GtDimension gd; gd.text = getS(d, "text"); gd.kind = getE(d, "kind", DimKind::Unknown); gd.nominal = getD(d, "nominal");
        gd.tolPlus = getOpt(d, "tolPlus"); gd.tolMinus = getOpt(d, "tolMinus"); gd.viewIndex = getI(d, "viewIndex", -1);
        gd.geomPrimIds = getIds(d, "geomPrimIds"); gd.paramName = getS(d, "paramName"); gd.driving = getB(d, "driving", true);
        gd.textId = getI(d, "textId", -1); gd.tolTextIds = getIds(d, "tolTextIds"); gd.nativeDimId = getI(d, "nativeDimId", -1);
        g.dims.push_back(gd);
      }
    if (j.contains("scale")) g.scale = scaleFromJson(j["scale"]);
    g.projection = getE(j, "projection", ProjectionMethod::FirstAngle);
    if (j.contains("params") && j["params"].is_array())
      for (auto& p : j["params"]) g.params.push_back(GtParam{getS(p, "name"), getD(p, "value"), getE(p, "tag", ParamTag::FromDimension)});
    g.expectedStrategy = getE(j, "expectedStrategy", Strategy::SketchOnly);
    g.volume = getD(j, "volume"); g.attrs = getAttrs(j, "attrs");
    const fs::path dir = fs::absolute(fs::path(gtJsonPath)).parent_path();
    g.dir = dir.string();
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec))
      if (e.is_regular_file() && e.path().filename() != "gt.json") g.files[e.path().filename().string()] = e.path().string();
    const std::string mp = getS(j, "modelPath");
    if (!mp.empty()) g.modelPath = fs::path(mp).is_absolute() ? mp : (dir / mp).string();
    else if (g.files.count("model.pcad")) g.modelPath = g.files["model.pcad"];
    gt = std::move(g);
    return true;
  } catch (const std::exception& e) {
    if (err) *err = std::string("gt.json okunamadı: ") + e.what();
    return false;
  }
}

std::vector<int> gtPrimIdsWithRole(const GroundTruth& gt, std::initializer_list<LineRole> roles) {
  std::vector<int> out;
  for (auto& p : gt.sheet.prims) {
    auto it = gt.roles.find(p.id);
    const LineRole r = it == gt.roles.end() ? LineRole::Unknown : it->second;
    for (LineRole q : roles) if (q == r) { out.push_back(p.id); break; }
  }
  return out;
}

DrawingIR irFromGroundTruth(const GroundTruth& gt) {
  DrawingIR ir;
  ir.raw = gt.sheet;
  ir.norm = gt.sheet;
  int mx = 0;
  for (auto& p : gt.sheet.prims) mx = std::max(mx, p.id);
  for (auto& t : gt.sheet.texts) mx = std::max(mx, t.id);
  for (auto& d : gt.sheet.nativeDims) mx = std::max(mx, d.id);
  ir.nextId = mx + 1;
  ir.stageVersion[Stage::Ingest] = "groundTruth";
  ir.stageVersion[Stage::Normalize] = "groundTruth";
  ir.lastStage = static_cast<int>(Stage::Normalize);
  ir.attrs["eval.fromGroundTruth"] = gt.family + "/" + gt.caseName;
  return ir;
}

}  // namespace pc::drawing
