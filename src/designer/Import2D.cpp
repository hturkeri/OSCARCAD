// SC-2D-IMPORT — 2D drawing as curves (DXF / vector PDF) + curve operations. See Import2D.h.
#include "Import2D.h"
#include "pc/Expression.h"
#include "pc/SketchSolver.h"
#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Vec.hxx>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#ifdef PC_HAVE_DRAWING_IMPORT
#include "pc/drawing/DrawingIR.h"
#include "pc/drawing/Pipeline.h"
#endif

using namespace pc;

namespace pcd::import2d {

namespace {

std::string lowerExt(const std::string& p) {
  std::string e = std::filesystem::u8path(p).extension().u8string();
  if (!e.empty() && e[0] == '.') e = e.substr(1);
  for (auto& c : e) c = char(std::tolower((unsigned char)c));
  return e;
}

// ASCII feature-id fragment: Turkish letters transliterated, everything else not [A-Za-z0-9] -> '_'
std::string sanitize(const std::string& s) {
  static const std::pair<const char*, char> tr[] = {{"ç", 'c'}, {"Ç", 'C'}, {"ğ", 'g'}, {"Ğ", 'G'}, {"ı", 'i'}, {"İ", 'I'},
                                                     {"ö", 'o'}, {"Ö", 'O'}, {"ş", 's'}, {"Ş", 'S'}, {"ü", 'u'}, {"Ü", 'U'}};
  std::string out;
  for (size_t i = 0; i < s.size();) {
    bool done = false;
    for (auto& [k, c] : tr) {
      const size_t n = std::char_traits<char>::length(k);
      if (s.compare(i, n, k) == 0) { out += c; i += n; done = true; break; }
    }
    if (done) continue;
    const unsigned char c = (unsigned char)s[i];
    if (std::isalnum(c) && c < 128) out += char(c);
    else if (out.empty() || out.back() != '_') out += '_';
    ++i;
  }
  while (!out.empty() && out.back() == '_') out.pop_back();
  if (out.empty()) out = "L";
  return out;
}

std::string uniqueId(const Document& d, const std::string& base, const std::set<std::string>& taken) {
  if (!d.feature(base) && !taken.count(base)) return base;
  for (int k = 2;; ++k) {
    const std::string id = base + "_" + std::to_string(k);
    if (!d.feature(id) && !taken.count(id)) return id;
  }
}

// Display colour of a drawing colour on the light 3D background: white / near-white -> default ink, light colours
// (DXF yellow, cyan ...) darkened so that the curves stay readable.
std::string displayColor(uint32_t rgb) {
  int r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
  if (r > 200 && g > 200 && b > 200) return "#1C2432";
  const double lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
  if (lum > 0.55) { const double k = 0.55 / lum; r = int(r * k); g = int(g * k); b = int(b * k); }
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02X%02X%02X", r, g, b);
  return buf;
}
std::string hexColor(uint32_t rgb) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02X%02X%02X", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
  return buf;
}

// Sketch builder: points shared by coordinates (1e-4 mm grid) so that loops are connected by point ids.
struct Builder {
  Sketch sk = Sketch::create();
  std::map<std::pair<long long, long long>, int> pts;
  int count = 0;
  int pt(double u, double v) {
    const auto key = std::make_pair(std::llround(u * 1e4), std::llround(v * 1e4));
    auto it = pts.find(key);
    if (it != pts.end()) return it->second;
    const int id = sk.addPoint(u, v);
    pts[key] = id;
    return id;
  }
  void line(double u0, double v0, double u1, double v1, bool cons) {
    if (std::hypot(u1 - u0, v1 - v0) < 1e-6) return;
    const int a = pt(u0, v0), b = pt(u1, v1);
    if (a == b) return;
    sk.addLine(a, b, cons);
    ++count;
  }
  void circle(double cu, double cv, double r, bool cons) {
    if (!(r > 1e-6)) return;
    sk.addCircle(pt(cu, cv), r, cons);
    ++count;
  }
  void arc(double cu, double cv, double r, double a0deg, double a1deg, bool cons) {
    if (!(r > 1e-6)) return;
    double sweep = a1deg - a0deg;
    while (sweep <= 0) sweep += 360;
    if (sweep >= 359.999) { circle(cu, cv, r, cons); return; }
    const double a0 = a0deg * M_PI / 180, a1 = (a0deg + sweep) * M_PI / 180;
    const int c = pt(cu, cv), s = pt(cu + r * std::cos(a0), cv + r * std::sin(a0)), e = pt(cu + r * std::cos(a1), cv + r * std::sin(a1));
    if (s == e) return;
    const int id = sk.addArc(c, s, e, cons);
    if (SkEntity* en = sk.entity(id)) en->radius = r;
    ++count;
  }
  void polyline(const std::vector<std::pair<double, double>>& p, bool closed, bool cons) {
    for (size_t i = 0; i + 1 < p.size(); ++i) line(p[i].first, p[i].second, p[i + 1].first, p[i + 1].second, cons);
    if (closed && p.size() > 2) line(p.back().first, p.back().second, p.front().first, p.front().second, cons);
  }
};

const Sketch* sketchOf(const Document& doc, const model::ModelResult& model, const std::string& sid) {
  auto it = model.solvedSketches.find(sid);
  if (it != model.solvedSketches.end()) return &it->second;
  const Feature* f = doc.feature(sid);
  return f && f->sketch ? &*f->sketch : nullptr;
}
bool frameOf(const model::ModelResult& model, const std::string& sid, gp_Ax3& fr) {
  for (const auto& fi : model.info)
    if (fi.featureId == sid && fi.type == FeatureType::Sketch) { fr = fi.sketchFrame; return true; }
  return false;
}
gp_Pnt at(const gp_Ax3& fr, double u, double v) {
  return gp_Pnt(fr.Location().XYZ() + fr.XDirection().XYZ() * u + fr.YDirection().XYZ() * v);
}
struct Box3 {
  double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
  void add(const gp_Pnt& p) {
    const double c[3] = {p.X(), p.Y(), p.Z()};
    for (int i = 0; i < 3; ++i) { lo[i] = std::min(lo[i], c[i]); hi[i] = std::max(hi[i], c[i]); }
  }
  bool touches(const Box3& o, double tol) const {
    for (int i = 0; i < 3; ++i) if (lo[i] > o.hi[i] + tol || o.lo[i] > hi[i] + tol) return false;
    return true;
  }
};

} // namespace

bool isDrawingFile(const std::string& path) {
  const std::string e = lowerExt(path);
  return e == "dxf" || e == "pdf";
}

bool available() {
#ifdef PC_HAVE_DRAWING_IMPORT
  return true;
#else
  return false;
#endif
}

bool isImportedSketch(const Feature& f) { return f.type == FeatureType::Sketch && f.prop("importedCurves") == "1"; }

std::vector<Note> notesOf(const Feature& f) {
  std::vector<Note> out;
  const std::string s = f.prop("notes");
  if (s.empty()) return out;
  try {
    const auto j = nlohmann::json::parse(s);
    for (const auto& n : j) {
      Note t;
      t.u = n.value("u", 0.0); t.v = n.value("v", 0.0); t.h = n.value("h", 2.5); t.rotDeg = n.value("r", 0.0);
      t.text = n.value("t", std::string());
      if (!t.text.empty()) out.push_back(t);
    }
  } catch (...) {}
  return out;
}

// ======================================================================================= reading the drawing
Result readDrawing(const std::string& path, const Options& opt, const Document& into) {
  Result r;
#ifndef PC_HAVE_DRAWING_IMPORT
  (void)path; (void)opt; (void)into;
  r.message = "Bu sürüm çizim içe aktarma kütüphanesi olmadan derlendi.";
  return r;
#else
  namespace dr = pc::drawing;
  const std::string ext = lowerExt(path);
  if (ext != "dxf" && ext != "pdf") { r.message = "Desteklenmeyen dosya türü (yalnızca .dxf ve vektör .pdf)."; return r; }
  if (!std::filesystem::exists(std::filesystem::u8path(path))) { r.message = "Dosya bulunamadı: " + path; return r; }
  if (ext == "pdf") {
    const dr::SniffResult sn = dr::sniff(path);
    if (sn.result.ok() && sn.kind != dr::SourceKind::VectorPdf) {
      r.message = "Bu PDF taranmış (raster) görünüyor; eğri olarak içe aktarılamaz. 'Teknik resimden içe aktar' sihirbazını kullanın.";
      return r;
    }
  }
  dr::DrawingIR ir;
  dr::PipelineOptions po;
  po.interp.verifyByReprojection = false;
  dr::StageResult sr = dr::runPipeline(path, ir, dr::Stage::Ingest, dr::Stage::Annotate, po);
  const bool haveRoles = sr.ok();
  const dr::PrimitiveSheet& sheet = !ir.norm.prims.empty() || !ir.norm.texts.empty() ? ir.norm : ir.raw;
  if (sheet.prims.empty() && sheet.texts.empty()) {
    r.message = "Çizim okunamadı" + (sr.message.empty() ? std::string(".") : ": " + sr.message);
    return r;
  }
  const bool dxf = sheet.source == dr::SourceKind::Dxf;
  if (!dxf && sheet.source != dr::SourceKind::VectorPdf) {
    r.message = "Yalnızca DXF ve vektör PDF eğri olarak içe aktarılabilir.";
    return r;
  }
  r.sourceKind = dxf ? "DXF" : "PDF";

  // ---- scale (part mm per drawing mm) and origin
  double scale = 1;
  if (dxf) {
    std::ostringstream ev;
    ev << "DXF birimleri ($INSUNITS) -> mm x" << sheet.unitToMm;
    r.scaleEvidence = ev.str();
  } else {
    const dr::Scale& fd = ir.layout.scaleFromDims;
    const dr::Scale& tb = ir.layout.title.scale;
    if (haveRoles && fd.conf >= 0.5f && fd.factor() > 0) { scale = fd.factor(); r.scaleEvidence = "ölçü oranları: " + fd.evidence; }
    else if (haveRoles && tb.conf >= 0.5f && tb.factor() > 0) { scale = tb.factor(); r.scaleEvidence = "antet: " + tb.evidence; }
    else {
      r.scaleConfident = false;
      r.scaleSuggestion = (fd.conf > 0 && fd.factor() > 0) ? fd.factor() : 1.0;
      scale = r.scaleSuggestion;
      r.scaleEvidence = "ölçek belirlenemedi (öneri 1:" + formatNumber(1.0 / scale) + ")";
    }
  }
  if (opt.scale > 0) { scale = opt.scale; r.scaleEvidence = "kullanıcı"; r.scaleConfident = true; }
  r.scale = scale;
  double shx = 0, shy = 0;
  {
    auto it = sheet.attrs.find("dxf.shift");
    if (it != sheet.attrs.end()) std::sscanf(it->second.c_str(), "%lf,%lf", &shx, &shy);
  }
  auto U = [&](const dr::Vec2& p) { return std::make_pair((p.x - shx) * scale, (p.y - shy) * scale); };

  // ---- layer of every primitive
  auto roleOfPrim = [&](int id) {
    auto it = ir.annot.roles.find(id);
    return it == ir.annot.roles.end() ? dr::LineRole::Unknown : it->second;
  };
  auto styleFromLinetype = [](const std::string& lt0, const dr::LineStyle& st) -> std::string {
    std::string lt;
    for (char c : lt0) lt += char(std::toupper((unsigned char)c));
    if (lt.find("CENTER") != std::string::npos || lt.find("EKSEN") != std::string::npos || lt.find("CHAIN") != std::string::npos ||
        lt.find("DASHDOT") != std::string::npos)
      return "center";
    if (lt.find("HIDDEN") != std::string::npos || lt.find("DASH") != std::string::npos || lt.find("GIZLI") != std::string::npos)
      return "dashed";
    if (lt.find("DOT") != std::string::npos || lt.find("PHANTOM") != std::string::npos) return "dotted";
    if (!st.dash.empty()) return st.dash.size() >= 4 ? "center" : "dashed";
    return "solid";
  };
  struct L { LayerInfo info; Builder b; bool styled = false; };
  std::map<std::string, L> layers;
  std::vector<std::string> order;
  auto layer = [&](const std::string& name) -> L& {
    auto it = layers.find(name);
    if (it != layers.end()) return it->second;
    order.push_back(name);
    L& l = layers[name];
    l.info.name = name;
    return l;
  };
  const std::string notesName = kNotesLayerName;
  for (const auto& p : sheet.prims) {
    const dr::LineRole role = roleOfPrim(p.id);
    const bool dimRole = role == dr::LineRole::Dimension || role == dr::LineRole::Extension || role == dr::LineRole::Leader;
    const bool annot = p.fromDimBlock || p.kind == dr::PrimKind::Arrowhead || dimRole;
    if (p.kind == dr::PrimKind::Image || p.kind == dr::PrimKind::HatchRegion) { ++r.skipped; continue; }
    std::string lname;
    std::string style;
    std::string color;
    if (annot) {
      lname = notesName;
      style = "solid";
      color = "#2E6FD0";
    } else if (dxf) {
      lname = p.style.layer.empty() ? "0" : p.style.layer;
      style = styleFromLinetype(p.style.linetype, p.style);
      if (style == "solid" && role == dr::LineRole::Hidden) style = "dashed";
      if (style == "solid" && role == dr::LineRole::Center) style = "center";
      color = p.style.rgb ? displayColor(p.style.rgb) : "#1C2432";
    } else {
      switch (role) {
        case dr::LineRole::Visible: lname = "Görünür"; color = "#1C2432"; style = "solid"; break;
        case dr::LineRole::Hidden: lname = "Gizli çizgiler"; color = "#5A6170"; style = "dashed"; break;
        case dr::LineRole::Center: lname = "Eksen"; color = "#B03030"; style = "center"; break;
        case dr::LineRole::Hatch: lname = "Tarama"; color = "#7A7A7A"; style = "solid"; break;
        case dr::LineRole::Cutting: lname = "Kesit düzlemi"; color = "#B03030"; style = "center"; break;
        case dr::LineRole::Thread: lname = "Diş"; color = "#5A6170"; style = "solid"; break;
        case dr::LineRole::Border: case dr::LineRole::TitleBlock: lname = "Çerçeve/Antet"; color = "#808080"; style = "solid"; break;
        case dr::LineRole::Symbol: lname = "Semboller"; color = "#2E6FD0"; style = "solid"; break;
        case dr::LineRole::Phantom: lname = "Hayali"; color = "#5A6170"; style = "dotted"; break;
        default: {
          lname = "Diğer";
          if (p.style.rgb) lname += " " + hexColor(p.style.rgb);
          color = p.style.rgb ? displayColor(p.style.rgb) : "#1C2432";
          style = p.style.dash.empty() ? "solid" : "dashed";
        }
      }
    }
    L& l = layer(lname);
    l.info.notes = lname == notesName;
    if (!l.styled) { l.info.color = color; l.info.lineStyle = style; l.styled = true; }
    // construction: annotations, frame, title block, hatch, symbols (displayed, never part of a region)
    const bool cons = annot || role == dr::LineRole::Border || role == dr::LineRole::TitleBlock || role == dr::LineRole::Hatch ||
                      role == dr::LineRole::Symbol;
    Builder& b = l.b;
    switch (p.kind) {
      case dr::PrimKind::Line: { auto a = U(p.p0), c = U(p.p1); b.line(a.first, a.second, c.first, c.second, cons); break; }
      case dr::PrimKind::Circle: { auto c = U(p.center); b.circle(c.first, c.second, p.radius * scale, cons); break; }
      case dr::PrimKind::Arc: { auto c = U(p.center); b.arc(c.first, c.second, p.radius * scale, p.a0, p.a1, cons); break; }
      case dr::PrimKind::Ellipse: {
        std::vector<std::pair<double, double>> pts;
        double sweep = p.a1 - p.a0;
        while (sweep <= 0) sweep += 360;
        const int n = std::max(8, int(std::ceil(sweep / 7.5)));
        const double rot = p.rotDeg * M_PI / 180;
        for (int i = 0; i <= n; ++i) {
          const double t = (p.a0 + sweep * i / n) * M_PI / 180;
          const double x = p.radius * std::cos(t), y = p.radius * p.minorRatio * std::sin(t);
          pts.push_back(U({p.center.x + x * std::cos(rot) - y * std::sin(rot), p.center.y + x * std::sin(rot) + y * std::cos(rot)}));
        }
        const bool full = sweep >= 359.999;
        if (full) pts.pop_back();
        b.polyline(pts, full, cons);
        break;
      }
      case dr::PrimKind::Polyline: {
        std::vector<std::pair<double, double>> pts;
        for (const auto& q : p.pts) pts.push_back(U(q));
        b.polyline(pts, p.closed, cons);
        break;
      }
      case dr::PrimKind::Bezier: {
        std::vector<std::pair<double, double>> pts;
        if (!p.pts.empty()) pts.push_back(U(p.pts[0]));
        for (size_t k = 0; k + 3 < p.pts.size(); k += 3) {
          const dr::Vec2 P0 = p.pts[k], C1 = p.pts[k + 1], C2 = p.pts[k + 2], P1 = p.pts[k + 3];
          for (int i = 1; i <= 12; ++i) {
            const double t = i / 12.0, s = 1 - t;
            const dr::Vec2 q = P0 * (s * s * s) + C1 * (3 * s * s * t) + C2 * (3 * s * t * t) + P1 * (t * t * t);
            pts.push_back(U(q));
          }
        }
        b.polyline(pts, p.closed, cons);
        break;
      }
      case dr::PrimKind::Arrowhead: {
        const dr::Vec2 d = p.p1 - p.p0;
        const double len = dr::length(d);
        if (len < 1e-9) break;
        const dr::Vec2 n{-d.y / len * len * 0.3, d.x / len * len * 0.3};
        auto t = U(p.p0), a = U(p.p1 + n), c = U(p.p1 - n);
        b.line(t.first, t.second, a.first, a.second, true);
        b.line(a.first, a.second, c.first, c.second, true);
        b.line(c.first, c.second, t.first, t.second, true);
        break;
      }
      default: ++r.skipped; break;
    }
  }
  // ---- texts -> notes
  nlohmann::json notes = nlohmann::json::array();
  for (const auto& t : sheet.texts) {
    if (t.invisible || t.utf8.empty()) continue;
    auto o = U(t.origin);
    notes.push_back({{"u", o.first}, {"v", o.second}, {"h", t.height * scale}, {"r", t.rotDeg}, {"t", t.utf8}});
  }
  if (!notes.empty()) {
    L& l = layer(notesName);
    l.info.notes = true;
    if (!l.styled) { l.info.color = "#2E6FD0"; l.info.lineStyle = "solid"; l.styled = true; }
  }
  r.noteCount = int(notes.size());

  // ---- one sketch feature per layer
  int g = 1;
  std::string group = opt.groupName;
  if (group.empty()) {
    std::set<std::string> groups;
    for (const auto& f : into.features) if (isImportedSketch(f)) groups.insert(f.prop("importGroup"));
    while (groups.count("Cizim" + std::to_string(g))) ++g;
    group = "Cizim" + std::to_string(g);
  }
  std::set<std::string> taken;
  const std::string fileName = std::filesystem::u8path(path).filename().u8string();
  // notes layer last, every other layer in first-seen order
  std::stable_partition(order.begin(), order.end(), [&](const std::string& n) { return n != notesName; });
  for (const auto& name : order) {
    L& l = layers[name];
    const bool isNotes = name == notesName;
    if (l.b.count == 0 && !(isNotes && !notes.empty())) continue;
    Feature f;
    f.type = FeatureType::Sketch;
    f.id = uniqueId(into, sanitize(group) + "_" + sanitize(name), taken);
    taken.insert(f.id);
    l.b.sk.plane.kind = opt.plane;
    f.sketch = l.b.sk;
    f.props["importedCurves"] = "1";
    f.props["layer"] = name;
    f.props["source"] = fileName;
    f.props["importGroup"] = group;
    if (isNotes && !notes.empty()) f.props["notes"] = notes.dump();
    l.info.curves = l.b.count;
    r.curveCount += l.b.count;
    r.sketches.push_back(std::move(f));
    r.layers.push_back(l.info);
  }
  if (r.sketches.empty()) { r.message = "Çizimde içe aktarılacak eğri bulunamadı."; return r; }
  r.ok = true;
  std::ostringstream m;
  m << r.sourceKind << ": " << r.curveCount << " eğri, " << r.layers.size() << " katman";
  if (r.noteCount) m << ", " << r.noteCount << " not";
  m << " (ölçek " << formatNumber(scale) << ", " << r.scaleEvidence << ")";
  if (r.skipped) m << "; " << r.skipped << " öğe atlandı (tarama / görüntü)";
  r.message = m.str();
  return r;
#endif
}

std::vector<std::string> mergeInto(Document& doc, const Result& r) {
  std::vector<std::string> ids;
  for (const auto& li : r.layers) {
    const bool existed = doc.layers.find(li.name) != nullptr;
    Layer& l = doc.layers.ensure(li.name);
    if (!existed) { l.color = li.color; l.lineStyle = li.lineStyle; }
  }
  for (size_t i = 0; i < r.sketches.size(); ++i) {
    Feature f = r.sketches[i];
    std::set<std::string> none;
    f.id = uniqueId(doc, f.id, none);
    doc.features.push_back(f);
    doc.layers.assignSketch(f.id, f.prop("layer"));
    ids.push_back(f.id);
  }
  return ids;
}

// ======================================================================================= curves in 3D
bool parseCurveName(const std::string& name, CurveRef& out) {
  if (name.rfind("sketch:", 0) != 0) return false;
  const auto c2 = name.rfind(':');
  if (c2 == std::string::npos || c2 <= 7 || c2 + 2 > name.size() || name[c2 + 1] != 'e') return false;
  try {
    size_t used = 0;
    const int id = std::stoi(name.substr(c2 + 2), &used);
    if (used != name.size() - c2 - 2) return false;
    out.sid = name.substr(7, c2 - 7);
    out.eid = id;
  } catch (...) { return false; }
  return !out.sid.empty();
}
std::string curveName(const CurveRef& c) { return "sketch:" + c.sid + ":e" + std::to_string(c.eid); }

namespace {
// 2D local samples of an entity (sketch coordinates)
bool localSamples(const Sketch& sk, const SkEntity& e, std::vector<std::pair<double, double>>& out, bool snapOnly) {
  out.clear();
  auto P = [&](int id) { const SkPoint* p = sk.point(id); return p ? std::make_pair(p->x, p->y) : std::make_pair(0.0, 0.0); };
  if (e.type == SkEntityType::Line && e.points.size() == 2) { out = {P(e.points[0]), P(e.points[1])}; return true; }
  if (e.type == SkEntityType::Circle && !e.points.empty()) {
    const auto c = P(e.points[0]);
    if (!snapOnly)
      for (int k = 0; k < 4; ++k) out.push_back({c.first + e.radius * std::cos(k * M_PI / 2), c.second + e.radius * std::sin(k * M_PI / 2)});
    out.push_back(c);
    return true;
  }
  if (e.type == SkEntityType::Arc && e.points.size() == 3) {
    const auto c = P(e.points[0]), s = P(e.points[1]), t = P(e.points[2]);
    out.push_back(s);
    out.push_back(t);
    if (!snapOnly) {
      const double r = std::hypot(s.first - c.first, s.second - c.second);
      double a0 = std::atan2(s.second - c.second, s.first - c.first), a1 = std::atan2(t.second - c.second, t.first - c.first);
      while (a1 <= a0) a1 += 2 * M_PI;
      // samples at the quadrant angles inside the sweep + the middle (exact bounding box)
      for (int k = -4; k <= 8; ++k) {
        const double a = k * M_PI / 2;
        if (a > a0 && a < a1) out.push_back({c.first + r * std::cos(a), c.second + r * std::sin(a)});
      }
      const double am = 0.5 * (a0 + a1);
      out.push_back({c.first + r * std::cos(am), c.second + r * std::sin(am)});
    }
    out.push_back(c);
    if (!snapOnly) out.pop_back();   // the centre is not on the curve
    return true;
  }
  return false;
}
} // namespace

std::vector<gp_Pnt> curvePoints(const Document& doc, const model::ModelResult& model, const CurveRef& c) {
  std::vector<gp_Pnt> out;
  const Sketch* sk = sketchOf(doc, model, c.sid);
  gp_Ax3 fr;
  if (!sk || !frameOf(model, c.sid, fr)) return out;
  const SkEntity* e = sk->entity(c.eid);
  std::vector<std::pair<double, double>> s;
  if (!e || !localSamples(*sk, *e, s, false)) return out;
  for (auto& [u, v] : s) out.push_back(at(fr, u, v));
  return out;
}

std::vector<gp_Pnt> snapPoints(const Document& doc, const model::ModelResult& model, const CurveRef& c) {
  std::vector<gp_Pnt> out;
  const Sketch* sk = sketchOf(doc, model, c.sid);
  gp_Ax3 fr;
  if (!sk || !frameOf(model, c.sid, fr)) return out;
  const SkEntity* e = sk->entity(c.eid);
  std::vector<std::pair<double, double>> s;
  if (!e || !localSamples(*sk, *e, s, true)) return out;
  for (auto& [u, v] : s) out.push_back(at(fr, u, v));
  return out;
}

namespace {
struct Item { CurveRef ref; Box3 box; };
std::vector<Item> items(const Document& doc, const model::ModelResult& model, const std::vector<std::string>& sketchIds) {
  std::vector<Item> out;
  for (const auto& sid : sketchIds) {
    const Sketch* sk = sketchOf(doc, model, sid);
    if (!sk) continue;
    for (const auto& e : sk->entities) {
      if (e.construction) continue;
      Item it;
      it.ref = {sid, e.id};
      for (const auto& p : curvePoints(doc, model, it.ref)) it.box.add(p);
      if (it.box.lo[0] > it.box.hi[0]) continue;
      out.push_back(it);
    }
  }
  return out;
}
std::vector<int> components(const std::vector<Item>& its, double tol) {
  std::vector<int> par(its.size());
  std::iota(par.begin(), par.end(), 0);
  std::function<int(int)> find = [&](int x) { return par[x] == x ? x : par[x] = find(par[x]); };
  for (size_t i = 0; i < its.size(); ++i)
    for (size_t j = i + 1; j < its.size(); ++j)
      if (its[i].box.touches(its[j].box, tol)) par[find(int(i))] = find(int(j));
  std::vector<int> comp(its.size());
  for (size_t i = 0; i < its.size(); ++i) comp[i] = find(int(i));
  return comp;
}
} // namespace

std::vector<CurveRef> connectedCluster(const Document& doc, const model::ModelResult& model, const CurveRef& seed,
                                       const std::vector<std::string>& sketchIds, double tol) {
  std::vector<std::string> ids = sketchIds;
  if (std::find(ids.begin(), ids.end(), seed.sid) == ids.end()) ids.push_back(seed.sid);
  const auto its = items(doc, model, ids);
  int seedIdx = -1;
  for (size_t i = 0; i < its.size(); ++i) if (its[i].ref == seed) seedIdx = int(i);
  if (seedIdx < 0) {
    // construction seed (e.g. a dimension line): just itself
    return {seed};
  }
  const auto comp = components(its, tol);
  std::vector<CurveRef> out;
  for (size_t i = 0; i < its.size(); ++i) if (comp[i] == comp[size_t(seedIdx)]) out.push_back(its[i].ref);
  std::sort(out.begin(), out.end());
  return out;
}

// ======================================================================================= placement
gp_Trsf Placement::trsf() const {
  gp_Trsf R;
  R.SetRotation(gp_Ax1(axisPoint, axisDir), angleDeg * M_PI / 180.0);
  const gp_Pnt moved = anchorSrc.Transformed(R);
  gp_Trsf T;
  T.SetTranslation(gp_Vec(moved, anchorDst));
  T.Multiply(R);   // R first, then the translation
  return T;
}

Placement proposePlacement(const Document& doc, const model::ModelResult& model, const std::vector<CurveRef>& cluster,
                           const std::vector<std::string>& sketchIds, FoldEdge edge, double angleDeg) {
  Placement p;
  p.angleDeg = angleDeg;
  if (cluster.empty()) { p.message = "Önce bir görünüşün eğrilerini seçin."; return p; }
  gp_Ax3 fr;
  if (!frameOf(model, cluster.front().sid, fr)) { p.message = "Eğrinin düzlemi bulunamadı."; return p; }
  const gp_Pnt O = fr.Location();
  const gp_Vec X(fr.XDirection()), Y(fr.YDirection()), N(fr.Direction());
  struct B2 { double lu = 1e300, lv = 1e300, hu = -1e300, hv = -1e300; void add(double u, double v) { lu = std::min(lu, u); lv = std::min(lv, v); hu = std::max(hu, u); hv = std::max(hv, v); } };
  auto uv = [&](const gp_Pnt& q) { const gp_Vec d(O, q); return std::make_tuple(d.Dot(X), d.Dot(Y), d.Dot(N)); };
  // boxes of the OUTLINES: curves of solid-line layers (center / hidden lines overhang the outline); all if none
  auto solid = [&](const std::string& sid) {
    const Layer* l = doc.layers.find(doc.layers.layerOfSketch(sid));
    return !l || l->lineStyle == "solid";
  };
  B2 b, ball;
  std::set<CurveRef> inCluster(cluster.begin(), cluster.end());
  for (const auto& c : cluster)
    for (const auto& q : curvePoints(doc, model, c)) {
      auto [u, v, w] = uv(q);
      if (std::fabs(w) > 1e-4) { p.message = "Seçili eğriler aynı düzlemde değil."; return p; }
      ball.add(u, v);
      if (solid(c.sid)) b.add(u, v);
    }
  if (ball.lu > ball.hu) { p.message = "Seçili eğrilerin geometrisi yok."; return p; }
  if (b.lu > b.hu) b = ball;
  // other views on the same plane: components of the remaining curves
  std::vector<Item> others;
  for (auto& it : items(doc, model, sketchIds)) {
    if (inCluster.count(it.ref)) continue;
    bool coplanar = true;
    Item lit = it;
    lit.box = Box3();
    for (const auto& q : curvePoints(doc, model, it.ref)) {
      auto [u, v, w] = uv(q);
      if (std::fabs(w) > 1e-4) { coplanar = false; break; }
      lit.box.add(gp_Pnt(u, v, 0));
    }
    if (coplanar) others.push_back(lit);
  }
  const auto comp = components(others, 0.5);
  std::map<int, B2> boxes, boxesAll;
  for (size_t i = 0; i < others.size(); ++i) {
    for (auto* ob : {&boxesAll[comp[i]], solid(others[i].ref.sid) ? &boxes[comp[i]] : nullptr}) {
      if (!ob) continue;
      ob->add(others[i].box.lo[0], others[i].box.lo[1]);
      ob->add(others[i].box.hi[0], others[i].box.hi[1]);
    }
  }
  for (auto& [k, ob] : boxesAll) if (!boxes.count(k)) boxes[k] = ob;
  // nearest aligned view in each direction
  const double tol = 1e-3;
  double best = 1e300;
  FoldEdge bestEdge = FoldEdge::Top;
  B2 bestBox;
  bool found = false;
  for (const auto& [k, ob] : boxes) {
    (void)k;
    const bool xo = std::min(b.hu, ob.hu) - std::max(b.lu, ob.lu) > tol;
    const bool yo = std::min(b.hv, ob.hv) - std::max(b.lv, ob.lv) > tol;
    struct C { FoldEdge e; bool ok; double gap; };
    const C cs[] = {{FoldEdge::Top, xo && ob.lv >= b.hv - tol, ob.lv - b.hv}, {FoldEdge::Bottom, xo && ob.hv <= b.lv + tol, b.lv - ob.hv},
                    {FoldEdge::Right, yo && ob.lu >= b.hu - tol, ob.lu - b.hu}, {FoldEdge::Left, yo && ob.hu <= b.lu + tol, b.lu - ob.hu}};
    for (const C& c : cs) {
      if (!c.ok || (edge != FoldEdge::Auto && c.e != edge)) continue;
      if (c.gap < best) { best = c.gap; bestEdge = c.e; bestBox = ob; found = true; }
    }
  }
  p.edge = edge == FoldEdge::Auto ? (found ? bestEdge : FoldEdge::Top) : edge;
  auto P = [&](double u, double v) { return gp_Pnt(O.XYZ() + X.XYZ() * u + Y.XYZ() * v); };
  switch (p.edge) {
    case FoldEdge::Bottom:
      p.axisPoint = P(b.lu, b.lv); p.axisDir = gp_Dir(X);
      p.anchorDst = found ? P(b.lu, bestBox.hv) : p.axisPoint;
      break;
    case FoldEdge::Right:
      p.axisPoint = P(b.hu, b.lv); p.axisDir = gp_Dir(Y);
      p.anchorDst = found ? P(bestBox.lu, b.lv) : p.axisPoint;
      break;
    case FoldEdge::Left:
      p.axisPoint = P(b.lu, b.lv); p.axisDir = gp_Dir(-Y);
      p.anchorDst = found ? P(bestBox.hu, b.lv) : p.axisPoint;
      break;
    default:
      p.axisPoint = P(b.lu, b.hv); p.axisDir = gp_Dir(-X);
      p.anchorDst = found ? P(b.lu, bestBox.lv) : p.axisPoint;
      break;
  }
  p.anchorSrc = p.axisPoint;
  p.ok = true;
  static const char* names[] = {"otomatik", "üst", "alt", "sol", "sağ"};
  p.message = std::string("Katlama kenarı: ") + names[int(p.edge)] + (found ? ", komşu görünüşe hizalandı" : ", komşu görünüş bulunamadı");
  return p;
}

// ======================================================================================= moving curves
namespace {
bool consumedBy(const Document& doc, const std::string& sid, std::string* by) {
  for (const auto& f : doc.features) {
    if (f.suppressed || f.type == FeatureType::Sketch) continue;
    if (f.prop("sketch") == sid) { if (by) *by = f.id; return true; }
  }
  return false;
}
bool isIdentity(const gp_Trsf& T) {
  const gp_Pnt a(0, 0, 0), b(1, 0, 0), c(0, 1, 0), d(0, 0, 1);
  return a.Transformed(T).Distance(a) < 1e-12 && b.Transformed(T).Distance(b) < 1e-12 && c.Transformed(T).Distance(c) < 1e-12 &&
         d.Transformed(T).Distance(d) < 1e-12;
}
bool samePlane(const SketchPlane& a, const SketchPlane& b) {
  return a.kind == b.kind && a.faceRef == b.faceRef && a.planeRef == b.planeRef && a.offsetExpr == b.offsetExpr;
}
// Target plane for frame (O', N'): XY + offset or an axisAngle datum plane (reused if an equal one exists).
bool targetPlane(Document& doc, const model::ModelResult& model, const gp_Pnt& O, const gp_Dir& Nd, SketchPlane& plane, gp_Ax3& frame,
                 std::string* err, std::map<std::string, gp_Ax3>& made) {
  const gp_Vec N(Nd);
  if (std::fabs(std::fabs(N.Z()) - 1) < 1e-9) {
    plane = SketchPlane();
    plane.kind = SketchPlaneKind::XY;
    plane.offsetExpr = formatNumber(std::fabs(O.Z()) < 1e-9 ? 0.0 : O.Z());
    frame = sketch::standardFrame(SketchPlaneKind::XY, O.Z());
    return true;
  }
  // reuse a plane made earlier by this command (this call or an earlier placement)
  std::map<std::string, gp_Ax3> known = made;
  for (const auto& [id, dp] : model.datumPlanes) known.emplace(id, dp.frame);
  for (const auto& [id, fr] : known) {
    const Feature* pf = doc.feature(id);
    if (!pf || pf->prop("import2d") != "1" || pf->suppressed) continue;
    const gp_Vec n(fr.Direction());
    if (std::fabs(std::fabs(n.Dot(N)) - 1) > 1e-9) continue;
    if (std::fabs(gp_Vec(fr.Location(), O).Dot(n)) > 1e-6) continue;
    plane = SketchPlane();
    plane.kind = SketchPlaneKind::OnPlane;
    plane.planeRef = id;
    frame = fr;
    return true;
  }
  std::string axis;
  double a0 = 0;
  if (std::fabs(N.Z()) < 1e-9) { axis = "Z"; a0 = std::atan2(N.X(), -N.Y()); }
  else if (std::fabs(N.X()) < 1e-9) { axis = "X"; a0 = std::atan2(N.Y(), -N.Z()); }
  else if (std::fabs(N.Y()) < 1e-9) { axis = "Y"; a0 = std::atan2(N.Z(), -N.X()); }
  else {
    if (err) *err = "Görünüş yalnızca bir dünya eksenine paralel düzleme yerleştirilebilir (90° katlama).";
    return false;
  }
  a0 *= 180.0 / M_PI;
  Feature pf;
  pf.type = FeatureType::Plane;
  pf.id = doc.uniqueFeatureId(FeatureType::Plane);
  pf.props["kind"] = "axisAngle";
  pf.props["axis"] = axis;
  pf.props["show"] = "0";
  pf.props["import2d"] = "1";
  for (double a : {a0, -a0, a0 + 180, 180 - a0}) {
    while (a > 180) a -= 360;
    while (a <= -180) a += 360;
    const double ar = std::round(a * 1e9) / 1e9;
    pf.props["angle"] = formatNumber(ar);
    pf.props["offset"] = "0";
    Document mini;
    mini.features.push_back(pf);
    const model::ModelResult mr = model::regenerate(mini, std::map<std::string, std::string>());
    auto it = mr.datumPlanes.find(pf.id);
    if (it == mr.datumPlanes.end()) continue;
    const gp_Vec n(it->second.frame.Direction());
    if (std::fabs(n.Dot(N) - 1) > 1e-9 && std::fabs(n.Dot(N) + 1) > 1e-9) continue;
    const double off = gp_Vec(it->second.frame.Location(), O).Dot(n);
    pf.props["offset"] = formatNumber(std::fabs(off) < 1e-9 ? 0.0 : std::round(off * 1e9) / 1e9);
    frame = it->second.frame;
    frame.Translate(n * off);
    doc.features.push_back(pf);
    made[pf.id] = frame;
    plane = SketchPlane();
    plane.kind = SketchPlaneKind::OnPlane;
    plane.planeRef = pf.id;
    return true;
  }
  if (err) *err = "Hedef düzlem oluşturulamadı.";
  return false;
}
} // namespace

bool moveCurves(Document& doc0, const model::ModelResult& model, const std::vector<CurveRef>& curves, const gp_Trsf& T,
                const std::string& targetLayer, std::string* err, std::vector<std::string>* newIds) {
  if (curves.empty()) { if (err) *err = "Taşınacak eğri yok."; return false; }
  Document doc = doc0;
  std::map<std::string, std::vector<int>> bySketch;
  for (const auto& c : curves) bySketch[c.sid].push_back(c.eid);
  const bool ident = isIdentity(T);
  std::vector<std::string> created;
  std::map<std::string, gp_Ax3> madePlanes;
  for (auto& [sid, eids] : bySketch) {
    Feature* sf = doc.feature(sid);
    if (!sf || !sf->sketch) { if (err) *err = "Sketch bulunamadı: " + sid; return false; }
    std::string by;
    if (consumedBy(doc, sid, &by)) {
      if (err) *err = "'" + sid + "' eğrileri " + by + " tarafından kullanılıyor; önce o özelliği silin.";
      return false;
    }
    const Sketch* solved = sketchOf(doc0, model, sid);
    gp_Ax3 fr;
    if (!solved || !frameOf(model, sid, fr)) { if (err) *err = "Sketch düzlemi bilinmiyor: " + sid; return false; }
    const std::string layerName = targetLayer.empty() ? doc.layers.layerOfSketch(sid) : targetLayer;
    // ---- target plane / sketch
    SketchPlane plane;
    gp_Ax3 tfr;
    Feature* target = nullptr;
    if (ident) {
      plane = sf->sketch->plane;
      tfr = fr;
      if (layerName == doc.layers.layerOfSketch(sid)) continue;   // nothing to do
      for (auto& f : doc.features)
        if (f.id != sid && f.sketch && isImportedSketch(f) && samePlane(f.sketch->plane, plane) && doc.layers.layerOfSketch(f.id) == layerName &&
            !consumedBy(doc, f.id, nullptr) && f.prop("notes").empty())
          target = &f;
    } else {
      const gp_Pnt O2 = fr.Location().Transformed(T);
      const gp_Dir N2 = fr.Direction().Transformed(T);
      if (!targetPlane(doc, model, O2, N2, plane, tfr, err, madePlanes)) return false;
      // SC-VIDEO2D: targetPlane may push_back a datum Plane feature -> doc.features reallocates -> sf dangled and the
      // new sketch copied garbage / empty import props (folded view no longer "importedCurves", no layer prop)
      sf = doc.feature(sid);
    }
    std::string targetId;
    if (!target) {
      Feature nf;
      nf.type = FeatureType::Sketch;
      std::set<std::string> none;
      nf.id = uniqueId(doc, sid + "_y1", none);
      if (nf.id != sid + "_y1") {
        for (int k = 2;; ++k) { const std::string id = sid + "_y" + std::to_string(k); if (!doc.feature(id)) { nf.id = id; break; } }
      }
      nf.sketch = Sketch::create();
      nf.sketch->plane = plane;
      for (const char* k : {"importedCurves", "layer", "source", "importGroup"}) if (sf->props.count(k)) nf.props[k] = sf->props.at(k);
      if (!targetLayer.empty() && nf.props.count("layer")) nf.props["layer"] = targetLayer;
      doc.features.push_back(nf);
      targetId = nf.id;
      created.push_back(nf.id);
    } else {
      targetId = target->id;
    }
    sf = doc.feature(sid);   // push_back may have moved it
    Feature* tf = doc.feature(targetId);
    const gp_Vec tN(tfr.Direction()), tX(tfr.XDirection()), tY(tfr.YDirection());
    const bool flip = gp_Vec(fr.Direction().Transformed(T)).Dot(tN) < 0;
    // ---- copy entities (geometry from the solved sketch)
    Builder b;
    b.sk = *tf->sketch;
    for (const auto& p : b.sk.points) b.pts[{std::llround(p.x * 1e4), std::llround(p.y * 1e4)}] = p.id;
    auto local = [&](double u, double v, double* ou, double* ov) {
      const gp_Pnt q = at(fr, u, v).Transformed(T);
      const gp_Vec d(tfr.Location(), q);
      *ou = d.Dot(tX);
      *ov = d.Dot(tY);
      return std::fabs(d.Dot(tN)) < 1e-5;
    };
    std::set<int> moved(eids.begin(), eids.end());
    for (int eid : eids) {
      const SkEntity* e = solved->entity(eid);
      if (!e) continue;
      auto P = [&](int id) { const SkPoint* p = solved->point(id); return std::make_pair(p ? p->x : 0.0, p ? p->y : 0.0); };
      double u0, v0, u1, v1, uc, vc;
      bool ok = true;
      if (e->type == SkEntityType::Line && e->points.size() == 2) {
        auto a = P(e->points[0]), c = P(e->points[1]);
        ok = local(a.first, a.second, &u0, &v0) && local(c.first, c.second, &u1, &v1);
        if (ok) { const int pa = b.pt(u0, v0), pb = b.pt(u1, v1); if (pa != pb) b.sk.addLine(pa, pb, e->construction); }
      } else if (e->type == SkEntityType::Circle && !e->points.empty()) {
        auto c = P(e->points[0]);
        ok = local(c.first, c.second, &uc, &vc);
        if (ok) b.sk.addCircle(b.pt(uc, vc), e->radius, e->construction);
      } else if (e->type == SkEntityType::Arc && e->points.size() == 3) {
        auto c = P(e->points[0]), s = P(e->points[1]), t = P(e->points[2]);
        ok = local(c.first, c.second, &uc, &vc) && local(s.first, s.second, &u0, &v0) && local(t.first, t.second, &u1, &v1);
        if (ok) {
          int pc = b.pt(uc, vc), ps = b.pt(u0, v0), pe = b.pt(u1, v1);
          if (flip) std::swap(ps, pe);
          const int id = b.sk.addArc(pc, ps, pe, e->construction);
          if (SkEntity* ne = b.sk.entity(id)) ne->radius = std::hypot(s.first - c.first, s.second - c.second);
        }
      }
      if (!ok) { if (err) *err = "Eğri hedef düzlemde değil (iç hata)."; return false; }
    }
    tf->sketch = b.sk;
    // ---- remove from the source sketch
    Sketch& src = *sf->sketch;
    std::set<int> removedPts;
    for (const auto& e : src.entities) if (moved.count(e.id)) for (int p : e.points) removedPts.insert(p);
    src.entities.erase(std::remove_if(src.entities.begin(), src.entities.end(), [&](const SkEntity& e) { return moved.count(e.id) > 0; }),
                       src.entities.end());
    for (const auto& e : src.entities) for (int p : e.points) removedPts.erase(p);
    removedPts.erase(0);
    src.constraints.erase(std::remove_if(src.constraints.begin(), src.constraints.end(), [&](const SkConstraint& c) {
                            for (int r : c.refs) if (moved.count(r) || removedPts.count(r)) return true;
                            return false;
                          }), src.constraints.end());
    src.points.erase(std::remove_if(src.points.begin(), src.points.end(), [&](const SkPoint& p) { return removedPts.count(p.id) > 0; }),
                     src.points.end());
    if (!targetId.empty()) doc.layers.assignSketch(targetId, layerName);
    if (src.entities.empty() && isImportedSketch(*sf) && sf->prop("notes").empty()) {
      doc.layers.forgetSketch(sid);
      doc.features.erase(doc.features.begin() + doc.indexOf(sid));
    }
  }
  if (doc.layers.layers.size() == 1 && doc.layers.layers.front().name == kDefaultLayerName && doc.layers.layers.front().sketches.empty() &&
      doc.layers.layers.front().bodies.empty() && doc0.layers.empty())
    doc.layers = LayerTable();
  doc0 = doc;
  if (newIds) *newIds = created;
  return true;
}

} // namespace pcd::import2d
