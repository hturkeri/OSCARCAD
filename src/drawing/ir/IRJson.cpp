// DrawingIR <-> JSON (schema pcdrw/1). OWNER WP-1. Conventions: DrawingIR.h header comment.
// Every member is written (defaults included); readers ignore unknown keys and default missing ones.
#include "ir/JsonUtil.h"
#include "pc/Document.h"
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace pc::drawing {
namespace jsonio {

json toJsonScale(const Scale& s) {
  return json{{"num", num(s.num)}, {"den", num(s.den)}, {"conf", fl(s.conf)}, {"evidence", s.evidence},
              {"notToScale", s.notToScale}};
}
Scale scaleFromJson(const json& j) {
  Scale s;
  if (!j.is_object()) return s;
  s.num = getD(j, "num", 1); s.den = getD(j, "den", 1); s.conf = getF(j, "conf"); s.evidence = getS(j, "evidence");
  s.notToScale = getB(j, "notToScale");
  return s;
}
json toJsonDiag(const Diagnostic& d) {
  return json{{"severity", en(d.severity)}, {"code", d.code}, {"message", d.message}, {"stage", d.stage}, {"ids", d.ids}};
}
Diagnostic diagFromJson(const json& j) {
  Diagnostic d;
  d.severity = getE(j, "severity", Severity::Info); d.code = getS(j, "code"); d.message = getS(j, "message");
  d.stage = getI(j, "stage"); d.ids = getIds(j, "ids");
  return d;
}
}  // namespace jsonio

namespace {
using namespace jsonio;

json diags(const std::vector<Diagnostic>& v) { json a = json::array(); for (auto& d : v) a.push_back(toJsonDiag(d)); return a; }
std::vector<Diagnostic> getDiags(const json& j, const char* k) {
  std::vector<Diagnostic> v;
  auto it = j.find(k);
  if (it != j.end() && it->is_array()) for (auto& x : *it) v.push_back(diagFromJson(x));
  return v;
}
template <class T, class F> std::vector<T> getArr(const json& j, const char* k, F f) {
  std::vector<T> v;
  auto it = j.find(k);
  if (it != j.end() && it->is_array()) for (auto& x : *it) v.push_back(f(x));
  return v;
}
json pts(const std::vector<Vec2>& v) { json a = json::array(); for (auto& p : v) a.push_back(vec(p)); return a; }
std::vector<Vec2> getPts(const json& j, const char* k) { return getArr<Vec2>(j, k, [](const json& x) { return toVec(x); }); }

json prov(const Provenance& p) { return json{{"source", en(p.source)}, {"page", p.page}, {"ref", p.ref}}; }
Provenance getProv(const json& j) {
  Provenance p;
  if (!j.is_object()) return p;
  p.source = getE(j, "source", SourceKind::VectorPdf); p.page = getI(j, "page"); p.ref = getS(j, "ref");
  return p;
}

json style(const LineStyle& s) {
  json d = json::array(); for (double v : s.dash) d.push_back(num(v));
  return json{{"width", num(s.width)}, {"dash", d}, {"dashPhase", num(s.dashPhase)}, {"rgb", s.rgb}, {"stroked", s.stroked},
              {"filled", s.filled}, {"layer", s.layer}, {"linetype", s.linetype}};
}
LineStyle getStyle(const json& j) {
  LineStyle s;
  if (!j.is_object()) return s;
  s.width = getD(j, "width"); s.dash = getDv(j, "dash"); s.dashPhase = getD(j, "dashPhase");
  s.rgb = static_cast<uint32_t>(getD(j, "rgb")); s.stroked = getB(j, "stroked", true); s.filled = getB(j, "filled");
  s.layer = getS(j, "layer"); s.linetype = getS(j, "linetype");
  return s;
}

json prim(const Primitive& p) {
  return json{{"id", p.id}, {"kind", en(p.kind)}, {"p0", vec(p.p0)}, {"p1", vec(p.p1)}, {"center", vec(p.center)},
              {"radius", num(p.radius)}, {"a0", num(p.a0)}, {"a1", num(p.a1)}, {"minorRatio", num(p.minorRatio)},
              {"rotDeg", num(p.rotDeg)}, {"pts", pts(p.pts)}, {"closed", p.closed}, {"hatchAngleDeg", num(p.hatchAngleDeg)},
              {"hatchPitch", num(p.hatchPitch)}, {"style", style(p.style)}, {"conf", fl(p.conf)}, {"prov", prov(p.prov)},
              {"mergedFrom", p.mergedFrom}, {"fromDimBlock", p.fromDimBlock}, {"clipped", p.clipped},
              {"attrs", attrs(p.attrs)}};
}
Primitive getPrim(const json& j) {
  Primitive p;
  p.id = getI(j, "id"); p.kind = getE(j, "kind", PrimKind::Line); p.p0 = getV(j, "p0"); p.p1 = getV(j, "p1");
  p.center = getV(j, "center"); p.radius = getD(j, "radius"); p.a0 = getD(j, "a0"); p.a1 = getD(j, "a1", 360);
  p.minorRatio = getD(j, "minorRatio", 1); p.rotDeg = getD(j, "rotDeg"); p.pts = getPts(j, "pts"); p.closed = getB(j, "closed");
  p.hatchAngleDeg = getD(j, "hatchAngleDeg"); p.hatchPitch = getD(j, "hatchPitch");
  if (j.contains("style")) p.style = getStyle(j["style"]);
  p.conf = getF(j, "conf", 1); if (j.contains("prov")) p.prov = getProv(j["prov"]);
  p.mergedFrom = getIds(j, "mergedFrom"); p.fromDimBlock = getB(j, "fromDimBlock"); p.clipped = getB(j, "clipped");
  p.attrs = getAttrs(j, "attrs");
  return p;
}

json text(const TextRun& t) {
  json q = json::array(); for (auto& v : t.quad) q.push_back(vec(v));
  json alt = json::array(); for (auto& h : t.alternatives) alt.push_back(json{{"utf8", h.utf8}, {"conf", fl(h.conf)}});
  return json{{"id", t.id}, {"utf8", t.utf8}, {"origin", vec(t.origin)}, {"height", num(t.height)}, {"rotDeg", num(t.rotDeg)},
              {"quad", q}, {"font", t.font}, {"rgb", t.rgb}, {"alternatives", alt}, {"asOutlines", t.asOutlines},
              {"invisible", t.invisible}, {"fromDimBlock", t.fromDimBlock}, {"conf", fl(t.conf)}, {"prov", prov(t.prov)},
              {"mergedFrom", t.mergedFrom}, {"attrs", attrs(t.attrs)}};
}
TextRun getText(const json& j) {
  TextRun t;
  t.id = getI(j, "id"); t.utf8 = getS(j, "utf8"); t.origin = getV(j, "origin"); t.height = getD(j, "height");
  t.rotDeg = getD(j, "rotDeg");
  if (j.contains("quad") && j["quad"].is_array())
    for (std::size_t i = 0; i < 4 && i < j["quad"].size(); ++i) t.quad[i] = toVec(j["quad"][i]);
  t.font = getS(j, "font"); t.rgb = static_cast<uint32_t>(getD(j, "rgb"));
  t.alternatives = getArr<TextHypothesis>(j, "alternatives", [](const json& x) { return TextHypothesis{getS(x, "utf8"), getF(x, "conf")}; });
  t.asOutlines = getB(j, "asOutlines"); t.invisible = getB(j, "invisible"); t.fromDimBlock = getB(j, "fromDimBlock");
  t.conf = getF(j, "conf", 1); if (j.contains("prov")) t.prov = getProv(j["prov"]);
  t.mergedFrom = getIds(j, "mergedFrom"); t.attrs = getAttrs(j, "attrs");
  return t;
}

json ndim(const NativeDimension& d) {
  json dp = json::array(); for (auto& v : d.defpts) dp.push_back(vec(v));
  return json{{"id", d.id}, {"kind", en(d.kind)}, {"rotatedLinear", d.rotatedLinear}, {"rotDeg", num(d.rotDeg)}, {"defpts", dp},
              {"textMid", vec(d.textMid)}, {"measured", num(d.measured)}, {"textOverride", d.textOverride}, {"block", d.block},
              {"blockPrimitiveIds", d.blockPrimitiveIds}, {"blockTextIds", d.blockTextIds}, {"dimStyle", d.dimStyle},
              {"conf", fl(d.conf)}, {"prov", prov(d.prov)}, {"attrs", attrs(d.attrs)}};
}
NativeDimension getNdim(const json& j) {
  NativeDimension d;
  d.id = getI(j, "id"); d.kind = getE(j, "kind", NativeDimKind::Unknown); d.rotatedLinear = getB(j, "rotatedLinear");
  d.rotDeg = getD(j, "rotDeg");
  if (j.contains("defpts") && j["defpts"].is_array())
    for (std::size_t i = 0; i < 5 && i < j["defpts"].size(); ++i) d.defpts[i] = toVec(j["defpts"][i]);
  d.textMid = getV(j, "textMid"); d.measured = getD(j, "measured"); d.textOverride = getS(j, "textOverride");
  d.block = getS(j, "block"); d.blockPrimitiveIds = getIds(j, "blockPrimitiveIds"); d.blockTextIds = getIds(j, "blockTextIds");
  d.dimStyle = getS(j, "dimStyle"); d.conf = getF(j, "conf", 1); if (j.contains("prov")) d.prov = getProv(j["prov"]);
  d.attrs = getAttrs(j, "attrs");
  return d;
}

json raster(const RasterInfo& r) {
  json h = json::array(); for (double v : r.homography) h.push_back(num(v));
  return json{{"widthPx", r.widthPx}, {"heightPx", r.heightPx}, {"dpi", num(r.dpi)}, {"dpiEstimated", r.dpiEstimated},
              {"deskewDeg", num(r.deskewDeg)}, {"homography", h}, {"pxToSheet", affine(r.pxToSheet)},
              {"preprocessedImagePath", r.preprocessedImagePath}};
}
RasterInfo getRaster(const json& j) {
  RasterInfo r;
  r.widthPx = getI(j, "widthPx"); r.heightPx = getI(j, "heightPx"); r.dpi = getD(j, "dpi"); r.dpiEstimated = getB(j, "dpiEstimated");
  r.deskewDeg = getD(j, "deskewDeg");
  auto h = getDv(j, "homography");
  if (h.size() == 9) for (int i = 0; i < 9; ++i) r.homography[i] = h[i];
  r.pxToSheet = getAffine(j, "pxToSheet", Affine2{}); r.preprocessedImagePath = getS(j, "preprocessedImagePath");
  return r;
}

// ---------------------------------------------------------------- layout
json title(const TitleBlock& t) {
  json f = json::object(); for (auto& [k, v] : t.fields) f[k] = v;
  return json{{"box", box(t.box)}, {"conf", fl(t.conf)}, {"fields", f}, {"textIds", t.textIds}, {"scale", toJsonScale(t.scale)},
              {"projection", en(t.projection)}, {"projectionSymbolPrim", t.projectionSymbolPrim}};
}
TitleBlock getTitle(const json& j) {
  TitleBlock t;
  if (!j.is_object()) return t;
  t.box = getBox(j, "box"); t.conf = getF(j, "conf"); t.fields = getAttrs(j, "fields"); t.textIds = getIds(j, "textIds");
  if (j.contains("scale")) t.scale = scaleFromJson(j["scale"]);
  t.projection = getE(j, "projection", ProjectionMethod::Unknown); t.projectionSymbolPrim = getI(j, "projectionSymbolPrim", -1);
  return t;
}
json view(const ViewRegion& v) {
  return json{{"id", v.id}, {"box", box(v.box)}, {"role", en(v.role)}, {"roleConf", fl(v.roleConf)}, {"label", v.label},
              {"labelTextId", v.labelTextId}, {"scale", toJsonScale(v.scale)}, {"sectionOfView", v.sectionOfView},
              {"cuttingLinePrim", v.cuttingLinePrim}, {"primIds", v.primIds}, {"textIds", v.textIds},
              {"sheetToView", affine(v.sheetToView)}, {"userEdited", v.userEdited}, {"attrs", attrs(v.attrs)}};
}
ViewRegion getView(const json& j) {
  ViewRegion v;
  v.id = getI(j, "id"); v.box = getBox(j, "box"); v.role = getE(j, "role", ViewRole::Unknown); v.roleConf = getF(j, "roleConf");
  v.label = getS(j, "label"); v.labelTextId = getI(j, "labelTextId", -1);
  if (j.contains("scale")) v.scale = scaleFromJson(j["scale"]);
  v.sectionOfView = getI(j, "sectionOfView", -1); v.cuttingLinePrim = getI(j, "cuttingLinePrim", -1);
  v.primIds = getIds(j, "primIds"); v.textIds = getIds(j, "textIds"); v.sheetToView = getAffine(j, "sheetToView", kIdentityAffine);
  v.userEdited = getB(j, "userEdited"); v.attrs = getAttrs(j, "attrs");
  return v;
}
json layout(const SheetLayout& l) {
  json nb = json::array(); for (auto& b : l.noteBlocks) nb.push_back(box(b));
  json vs = json::array(); for (auto& v : l.views) vs.push_back(view(v));
  json res = json::array(); for (double v : l.scaleResiduals) res.push_back(num(v));
  return json{{"border", box(l.border)}, {"borderConf", fl(l.borderConf)}, {"title", title(l.title)}, {"noteBlocks", nb},
              {"noteTextIds", l.noteTextIds}, {"views", vs}, {"scaleFromDims", toJsonScale(l.scaleFromDims)},
              {"scaleResiduals", res}, {"attrs", attrs(l.attrs)}};
}
SheetLayout getLayout(const json& j) {
  SheetLayout l;
  if (!j.is_object()) return l;
  l.border = getBox(j, "border"); l.borderConf = getF(j, "borderConf");
  if (j.contains("title")) l.title = getTitle(j["title"]);
  l.noteBlocks = getArr<Box2>(j, "noteBlocks", [](const json& x) { return toBox(x); });
  l.noteTextIds = getIds(j, "noteTextIds"); l.views = getArr<ViewRegion>(j, "views", getView);
  if (j.contains("scaleFromDims")) l.scaleFromDims = scaleFromJson(j["scaleFromDims"]);
  l.scaleResiduals = getDv(j, "scaleResiduals"); l.attrs = getAttrs(j, "attrs");
  return l;
}

// ---------------------------------------------------------------- annotation
json dimText(const DimText& d) {
  return json{{"raw", d.raw}, {"valid", d.valid}, {"kind", en(d.kind)}, {"count", d.count}, {"nominal", num(d.nominal)},
              {"tolPlus", optD(d.tolPlus)}, {"tolMinus", optD(d.tolMinus)}, {"fitClass", d.fitClass}, {"isMax", d.isMax},
              {"isMin", d.isMin}, {"reference", d.reference}, {"basic", d.basic}, {"spherical", d.spherical},
              {"squareSym", d.squareSym}, {"chamferAngle", num(d.chamferAngle)}, {"threadSpec", d.threadSpec},
              {"threadMajor", num(d.threadMajor)}, {"threadPitch", num(d.threadPitch)}, {"depth", optD(d.depth)},
              {"through", d.through}, {"remainder", d.remainder}, {"conf", fl(d.conf)}, {"corrections", d.corrections}};
}
DimText getDimText(const json& j) {
  DimText d;
  if (!j.is_object()) return d;
  d.raw = getS(j, "raw"); d.valid = getB(j, "valid"); d.kind = getE(j, "kind", DimKind::Unknown); d.count = getI(j, "count", 1);
  d.nominal = getD(j, "nominal"); d.tolPlus = getOpt(j, "tolPlus"); d.tolMinus = getOpt(j, "tolMinus");
  d.fitClass = getS(j, "fitClass"); d.isMax = getB(j, "isMax"); d.isMin = getB(j, "isMin"); d.reference = getB(j, "reference");
  d.basic = getB(j, "basic"); d.spherical = getB(j, "spherical"); d.squareSym = getB(j, "squareSym");
  d.chamferAngle = getD(j, "chamferAngle"); d.threadSpec = getS(j, "threadSpec"); d.threadMajor = getD(j, "threadMajor");
  d.threadPitch = getD(j, "threadPitch"); d.depth = getOpt(j, "depth"); d.through = getB(j, "through");
  d.remainder = getS(j, "remainder"); d.conf = getF(j, "conf");
  d.corrections = getArr<std::string>(j, "corrections", [](const json& x) { return x.is_string() ? x.get<std::string>() : x.dump(); });
  return d;
}
json dimension(const Dimension& d) {
  return json{{"id", d.id}, {"viewId", d.viewId}, {"kind", en(d.kind)}, {"text", dimText(d.text)}, {"textId", d.textId},
              {"tolTextIds", d.tolTextIds}, {"nativeDimId", d.nativeDimId}, {"dimLineIds", d.dimLineIds},
              {"extLineIds", d.extLineIds}, {"arrowIds", d.arrowIds}, {"leaderIds", d.leaderIds}, {"geomPrimIds", d.geomPrimIds},
              {"anchors", json::array({vec(d.anchors[0]), vec(d.anchors[1])})}, {"direction", vec(d.direction)},
              {"measuredSheet", num(d.measuredSheet)}, {"measuredPart", num(d.measuredPart)}, {"residual", num(d.residual)},
              {"residualVsTolMid", d.residualVsTolMid}, {"outOfBand", d.outOfBand}, {"driving", d.driving},
              {"userEdited", d.userEdited}, {"conf", fl(d.conf)}, {"attrs", attrs(d.attrs)}};
}
Dimension getDimension(const json& j) {
  Dimension d;
  d.id = getI(j, "id"); d.viewId = getI(j, "viewId", -1); d.kind = getE(j, "kind", DimKind::Unknown);
  if (j.contains("text")) d.text = getDimText(j["text"]);
  d.textId = getI(j, "textId", -1); d.tolTextIds = getIds(j, "tolTextIds"); d.nativeDimId = getI(j, "nativeDimId", -1);
  d.dimLineIds = getIds(j, "dimLineIds"); d.extLineIds = getIds(j, "extLineIds"); d.arrowIds = getIds(j, "arrowIds");
  d.leaderIds = getIds(j, "leaderIds"); d.geomPrimIds = getIds(j, "geomPrimIds");
  if (j.contains("anchors") && j["anchors"].is_array() && j["anchors"].size() == 2) {
    d.anchors[0] = toVec(j["anchors"][0]); d.anchors[1] = toVec(j["anchors"][1]);
  }
  d.direction = j.contains("direction") ? getV(j, "direction") : Vec2{1, 0};
  d.measuredSheet = getD(j, "measuredSheet"); d.measuredPart = getD(j, "measuredPart"); d.residual = getD(j, "residual");
  d.residualVsTolMid = getB(j, "residualVsTolMid"); d.outOfBand = getB(j, "outOfBand"); d.driving = getB(j, "driving", true);
  d.userEdited = getB(j, "userEdited"); d.conf = getF(j, "conf"); d.attrs = getAttrs(j, "attrs");
  return d;
}
template <class V, class F> json intMap(const std::map<int, V>& m, F f) {
  json a = json::array(); for (auto& [k, v] : m) a.push_back(json::array({k, f(v)})); return a;
}
template <class V, class F> std::map<int, V> getIntMap(const json& j, const char* k, F f) {
  std::map<int, V> m;
  auto it = j.find(k);
  if (it != j.end() && it->is_array())
    for (auto& e : *it)
      if (e.is_array() && e.size() == 2 && e[0].is_number()) m[static_cast<int>(e[0].get<double>())] = f(e[1]);
  return m;
}
json annotation(const Annotation& a) {
  json ds = json::array(); for (auto& d : a.dims) ds.push_back(dimension(d));
  return json{{"roles", intMap(a.roles, [](LineRole r) { return json(toString(r)); })},
              {"roleConf", intMap(a.roleConf, [](float f) { return json(fl(f)); })}, {"dims", ds},
              {"centerlineIds", a.centerlineIds}, {"unusedTextIds", a.unusedTextIds}, {"attrs", attrs(a.attrs)}};
}
LineRole roleFrom(const json& x) { LineRole r = LineRole::Unknown; if (x.is_string()) fromString(x.get<std::string>(), r); return r; }
Annotation getAnnotation(const json& j) {
  Annotation a;
  if (!j.is_object()) return a;
  a.roles = getIntMap<LineRole>(j, "roles", roleFrom);
  a.roleConf = getIntMap<float>(j, "roleConf", [](const json& x) { return x.is_number() ? x.get<float>() : 0.f; });
  a.dims = getArr<Dimension>(j, "dims", getDimension); a.centerlineIds = getIds(j, "centerlineIds");
  a.unusedTextIds = getIds(j, "unusedTextIds"); a.attrs = getAttrs(j, "attrs");
  return a;
}

// ---------------------------------------------------------------- views
json views(const ViewGraph& g) {
  json rel = json::array();
  for (auto& r : g.relations)
    rel.push_back(json{{"a", r.a}, {"b", r.b}, {"adj", en(r.adj)}, {"alignErr", num(r.alignErr)}, {"conf", fl(r.conf)}});
  return json{{"projection", en(g.projection)}, {"projectionConf", fl(g.projectionConf)}, {"relations", rel},
              {"primaryView", g.primaryView}, {"axisOfView", intMap(g.axisOfView, [](int v) { return json(v); })},
              {"symmetryScore", intMap(g.symmetryScore, [](double v) { return num(v); })},
              {"rotational", intMap(g.rotational, [](bool v) { return json(v); })}, {"attrs", attrs(g.attrs)}};
}
ViewGraph getViews(const json& j) {
  ViewGraph g;
  if (!j.is_object()) return g;
  g.projection = getE(j, "projection", ProjectionMethod::Unknown); g.projectionConf = getF(j, "projectionConf");
  g.relations = getArr<ViewRelation>(j, "relations", [](const json& x) {
    ViewRelation r; r.a = getI(x, "a", -1); r.b = getI(x, "b", -1); r.adj = getE(x, "adj", Adjacency::None);
    r.alignErr = getD(x, "alignErr"); r.conf = getF(x, "conf"); return r; });
  g.primaryView = getI(j, "primaryView", -1);
  g.axisOfView = getIntMap<int>(j, "axisOfView", [](const json& x) { return x.is_number() ? x.get<int>() : -1; });
  g.symmetryScore = getIntMap<double>(j, "symmetryScore", [](const json& x) { return x.is_number() ? x.get<double>() : 0.0; });
  g.rotational = getIntMap<bool>(j, "rotational", [](const json& x) { return x.is_boolean() && x.get<bool>(); });
  g.attrs = getAttrs(j, "attrs");
  return g;
}

// ---------------------------------------------------------------- sketches / synthesis
const char* paramKindStr(ParamKind k) {
  switch (k) { case ParamKind::Length: return "length"; case ParamKind::Angle: return "angle";
               case ParamKind::Count: return "count"; default: return "scalar"; }
}
ParamKind paramKindFrom(const std::string& s) {
  if (s == "angle") return ParamKind::Angle;
  if (s == "count") return ParamKind::Count;
  if (s == "scalar") return ParamKind::Scalar;
  return ParamKind::Length;
}
// Same layout as one entry of Document JSON "parameters" (src/core/Document.cpp).
json param(const Parameter& p) {
  return json{{"name", p.name}, {"expr", p.expression}, {"kind", paramKindStr(p.kind)}, {"min", num(p.minValue)},
              {"max", num(p.maxValue)}, {"description", p.description}, {"exposed", p.exposed}, {"label", p.label},
              {"step", num(p.step)}, {"group", p.group}};
}
Parameter getParam(const json& j) {
  Parameter p;
  if (!j.is_object()) return p;
  p.name = getS(j, "name");
  if (j.contains("expr") && j["expr"].is_number()) { std::ostringstream o; o << j["expr"].get<double>(); p.expression = o.str(); }
  else p.expression = getS(j, "expr");
  p.kind = paramKindFrom(getS(j, "kind", "length"));
  p.minValue = getD(j, "min", -std::numeric_limits<double>::infinity());
  p.maxValue = getD(j, "max", std::numeric_limits<double>::infinity());
  p.description = getS(j, "description"); p.exposed = getB(j, "exposed"); p.label = getS(j, "label");
  p.step = getD(j, "step"); p.group = getS(j, "group");
  return p;
}
const char* planeStr(SketchPlaneKind k) {
  switch (k) { case SketchPlaneKind::XY: return "XY"; case SketchPlaneKind::XZ: return "XZ"; case SketchPlaneKind::YZ: return "YZ";
               case SketchPlaneKind::OnFace: return "OnFace"; default: return "OnPlane"; }
}
SketchPlaneKind planeFrom(const std::string& s) {
  if (s == "XZ") return SketchPlaneKind::XZ;
  if (s == "YZ") return SketchPlaneKind::YZ;
  if (s == "OnFace") return SketchPlaneKind::OnFace;
  if (s == "OnPlane") return SketchPlaneKind::OnPlane;
  return SketchPlaneKind::XY;
}
json proposal(const ParamProposal& p) {
  return json{{"param", param(p.param)}, {"tag", en(p.tag)}, {"dimensionId", p.dimensionId}, {"tolPlus", optD(p.tolPlus)},
              {"tolMinus", optD(p.tolMinus)}, {"nominalValue", num(p.nominalValue)}, {"tolMidValue", num(p.tolMidValue)},
              {"rangeMin", optD(p.rangeMin)}, {"rangeMax", optD(p.rangeMax)}, {"userEdited", p.userEdited}};
}
ParamProposal getProposal(const json& j) {
  ParamProposal p;
  if (j.contains("param")) p.param = getParam(j["param"]);
  p.tag = getE(j, "tag", ParamTag::FromDimension); p.dimensionId = getI(j, "dimensionId", -1);
  p.tolPlus = getOpt(j, "tolPlus"); p.tolMinus = getOpt(j, "tolMinus"); p.nominalValue = getD(j, "nominalValue");
  p.tolMidValue = getD(j, "tolMidValue"); p.rangeMin = getOpt(j, "rangeMin"); p.rangeMax = getOpt(j, "rangeMax");
  p.userEdited = getB(j, "userEdited");
  return p;
}
json sketchCand(const SketchCandidate& s) {
  json ps = json::array(); for (auto& p : s.params) ps.push_back(proposal(p));
  return json{{"id", s.id}, {"viewId", s.viewId}, {"purpose", s.purpose}, {"plane", planeStr(s.plane)}, {"sketch", pc::toJson(s.sketch)},
              {"params", ps}, {"constraintToDimension", intMap(s.constraintToDimension, [](int v) { return json(v); })},
              {"entityToPrimitive", intMap(s.entityToPrimitive, [](int v) { return json(v); })},
              {"measuredConstraints", s.measuredConstraints}, {"axisEntity", s.axisEntity}, {"dof", s.dof},
              {"conflicting", s.conflicting}, {"deviatingEntities", s.deviatingEntities},
              {"maxGeomDeviation", num(s.maxGeomDeviation)}, {"profileClosed", s.profileClosed}, {"conf", fl(s.conf)},
              {"attrs", attrs(s.attrs)}};
}
SketchCandidate getSketchCand(const json& j) {
  SketchCandidate s;
  s.id = getI(j, "id"); s.viewId = getI(j, "viewId", -1); s.purpose = getS(j, "purpose"); s.plane = planeFrom(getS(j, "plane", "XY"));
  if (j.contains("sketch") && j["sketch"].is_object()) s.sketch = pc::sketchFromJson(j["sketch"]);
  s.params = getArr<ParamProposal>(j, "params", getProposal);
  auto iv = [](const json& x) { return x.is_number() ? x.get<int>() : -1; };
  s.constraintToDimension = getIntMap<int>(j, "constraintToDimension", iv);
  s.entityToPrimitive = getIntMap<int>(j, "entityToPrimitive", iv);
  s.measuredConstraints = getIds(j, "measuredConstraints"); s.axisEntity = getI(j, "axisEntity", -1); s.dof = getI(j, "dof", -1);
  s.conflicting = getIds(j, "conflicting"); s.deviatingEntities = getIds(j, "deviatingEntities");
  s.maxGeomDeviation = getD(j, "maxGeomDeviation"); s.profileClosed = getB(j, "profileClosed"); s.conf = getF(j, "conf");
  s.attrs = getAttrs(j, "attrs");
  return s;
}
json synth(const SynthesisCandidate& c) {
  return json{{"id", c.id}, {"strategy", en(c.strategy)}, {"score", fl(c.score)}, {"explanation", c.explanation},
              {"fragment", pc::toJson(c.fragment)}, {"sketchIds", c.sketchIds}, {"warnings", c.warnings},
              {"conditionScore", num(c.conditionScore)}, {"dofCoverage", num(c.dofCoverage)}, {"unresolved", c.unresolved},
              {"reprojectionError", num(c.reprojectionError)}, {"volume", num(c.volume)}, {"regenerated", c.regenerated},
              {"attrs", attrs(c.attrs)}};
}
SynthesisCandidate getSynth(const json& j) {
  SynthesisCandidate c;
  c.id = getI(j, "id"); c.strategy = getE(j, "strategy", Strategy::SketchOnly); c.score = getF(j, "score");
  c.explanation = getS(j, "explanation");
  if (j.contains("fragment") && j["fragment"].is_object()) c.fragment = pc::documentFromJson(j["fragment"]);
  c.sketchIds = getIds(j, "sketchIds");
  c.warnings = getArr<std::string>(j, "warnings", [](const json& x) { return x.is_string() ? x.get<std::string>() : x.dump(); });
  c.conditionScore = getD(j, "conditionScore"); c.dofCoverage = getD(j, "dofCoverage"); c.unresolved = getI(j, "unresolved");
  c.reprojectionError = getD(j, "reprojectionError", -1); c.volume = getD(j, "volume"); c.regenerated = getB(j, "regenerated");
  c.attrs = getAttrs(j, "attrs");
  return c;
}
json edit(const UserEdit& e) {
  json payload;
  try { payload = e.payload.empty() ? json::object() : json::parse(e.payload); } catch (...) { payload = e.payload; }
  return json{{"stage", en(e.stage)}, {"op", e.op}, {"payload", payload}};
}
UserEdit getEdit(const json& j) {
  UserEdit e;
  e.stage = getE(j, "stage", Stage::Ingest); e.op = getS(j, "op");
  if (j.contains("payload")) e.payload = j["payload"].is_string() ? j["payload"].get<std::string>() : j["payload"].dump();
  return e;
}
}  // namespace

nlohmann::json toJson(const PrimitiveSheet& s) {
  json pr = json::array(); for (auto& p : s.prims) pr.push_back(prim(p));
  json tx = json::array(); for (auto& t : s.texts) tx.push_back(text(t));
  json nd = json::array(); for (auto& d : s.nativeDims) nd.push_back(ndim(d));
  return json{{"source", en(s.source)}, {"file", s.file}, {"fileSha256", s.fileSha256}, {"page", s.page}, {"layout", s.layout},
              {"sheet", box(s.sheet)}, {"unitToMm", num(s.unitToMm)}, {"prims", pr}, {"texts", tx}, {"nativeDims", nd},
              {"raster", s.raster ? raster(*s.raster) : json(nullptr)}, {"diagnostics", diags(s.diagnostics)},
              {"attrs", attrs(s.attrs)}};
}

PrimitiveSheet primitiveSheetFromJson(const nlohmann::json& j) {
  if (!j.is_object()) throw std::runtime_error("PrimitiveSheet: JSON nesnesi bekleniyordu");
  PrimitiveSheet s;
  s.source = getE(j, "source", SourceKind::VectorPdf); s.file = getS(j, "file"); s.fileSha256 = getS(j, "fileSha256");
  s.page = getI(j, "page"); s.layout = getS(j, "layout"); s.sheet = getBox(j, "sheet"); s.unitToMm = getD(j, "unitToMm", 1);
  s.prims = getArr<Primitive>(j, "prims", getPrim); s.texts = getArr<TextRun>(j, "texts", getText);
  s.nativeDims = getArr<NativeDimension>(j, "nativeDims", getNdim);
  if (j.contains("raster") && j["raster"].is_object()) s.raster = getRaster(j["raster"]);
  s.diagnostics = getDiags(j, "diagnostics"); s.attrs = getAttrs(j, "attrs");
  return s;
}

nlohmann::json toJson(const DrawingIR& ir) {
  json sk = json::array(); for (auto& s : ir.sketches) sk.push_back(sketchCand(s));
  json sy = json::array(); for (auto& c : ir.synthesis) sy.push_back(synth(c));
  json ed = json::array(); for (auto& e : ir.edits) ed.push_back(edit(e));
  json sv = json::object(); for (auto& [st, v] : ir.stageVersion) sv[toString(st)] = v;
  return json{{"schema", ir.schema}, {"irVersion", ir.irVersion}, {"nextId", ir.nextId}, {"raw", toJson(ir.raw)},
              {"norm", toJson(ir.norm)}, {"layout", layout(ir.layout)}, {"annot", annotation(ir.annot)}, {"views", views(ir.views)},
              {"sketches", sk}, {"synthesis", sy}, {"chosenSynthesis", ir.chosenSynthesis}, {"edits", ed}, {"stageVersion", sv},
              {"lastStage", ir.lastStage}, {"diagnostics", diags(ir.diagnostics)}, {"attrs", attrs(ir.attrs)}};
}

DrawingIR drawingIRFromJson(const nlohmann::json& j) {
  if (!j.is_object()) throw std::runtime_error("DrawingIR: JSON nesnesi bekleniyordu");
  const std::string schema = getS(j, "schema");
  if (schema.rfind("pcdrw/", 0) != 0) throw std::runtime_error("DrawingIR: tanınmayan şema '" + schema + "'");
  const std::string ver = getS(j, "irVersion", kIRVersion);
  if (std::atoi(ver.c_str()) != PC_DRAWING_IR_VERSION_MAJOR || schema != kSchemaId)
    throw std::runtime_error("DrawingIR: desteklenmeyen sürüm " + schema + " / " + ver);
  DrawingIR ir;
  ir.schema = schema; ir.irVersion = ver; ir.nextId = getI(j, "nextId", 1);
  if (j.contains("raw") && j["raw"].is_object()) ir.raw = primitiveSheetFromJson(j["raw"]);
  if (j.contains("norm") && j["norm"].is_object()) ir.norm = primitiveSheetFromJson(j["norm"]);
  if (j.contains("layout")) ir.layout = getLayout(j["layout"]);
  if (j.contains("annot")) ir.annot = getAnnotation(j["annot"]);
  if (j.contains("views")) ir.views = getViews(j["views"]);
  ir.sketches = getArr<SketchCandidate>(j, "sketches", getSketchCand);
  ir.synthesis = getArr<SynthesisCandidate>(j, "synthesis", getSynth);
  ir.chosenSynthesis = getI(j, "chosenSynthesis", -1);
  ir.edits = getArr<UserEdit>(j, "edits", getEdit);
  if (j.contains("stageVersion") && j["stageVersion"].is_object())
    for (auto& [k, v] : j["stageVersion"].items()) {
      Stage st; if (fromString(k, st) && v.is_string()) ir.stageVersion[st] = v.get<std::string>();
    }
  ir.lastStage = getI(j, "lastStage"); ir.diagnostics = getDiags(j, "diagnostics"); ir.attrs = getAttrs(j, "attrs");
  return ir;
}

bool saveIR(const DrawingIR& ir, const std::string& path, std::string* err) {
  try {
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "Dosya yazılamadı: " + path; return false; }
    f << toJson(ir).dump(1, ' ', false, nlohmann::json::error_handler_t::replace) << "\n";
    return static_cast<bool>(f);
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

bool loadIR(DrawingIR& ir, const std::string& path, std::string* err) {
  try {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "Dosya açılamadı: " + path; return false; }
    json j = json::parse(f);
    ir = drawingIRFromJson(j);
    return true;
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return false;
  }
}

}  // namespace pc::drawing
