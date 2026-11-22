// Minimal DXF R12 (AC1009) writer for the corpus: layers + linetypes, LINE/ARC/CIRCLE/LWPOLY-free POLYLINE/TEXT/SOLID,
// optional DIMENSION entities with *D blocks. Sheet mm, $INSUNITS 4, $DIMLFAC = drawing scale factor. OWNER WP-1.
#include "Corpus.h"
#include <QString>
#include <QStringEncoder>
#include <fstream>
#include <set>
#include <sstream>

namespace corpus {
namespace {
std::string layerOf(LineRole r) {
  switch (r) {
    case LineRole::Visible: return "GORUNUR";
    case LineRole::Hidden: return "GIZLI";
    case LineRole::Center: return "EKSEN";
    case LineRole::Dimension: case LineRole::Extension: case LineRole::Leader: return "OLCU";
    case LineRole::Border: return "CERCEVE";
    case LineRole::TitleBlock: return "ANTET";
    default: return "0";
  }
}
// UTF-8 -> CP1254 with AutoCAD %% codes for Ø ± °
std::string encode(const std::string& u) {
  QString s = QString::fromStdString(u);
  s.replace(QChar(0x00D8), "%%c").replace(QChar(0x00B1), "%%p").replace(QChar(0x00B0), "%%d");
  std::string out;
  static const std::pair<char16_t, unsigned char> tr[] = {{0x011E, 0xD0}, {0x011F, 0xF0}, {0x0130, 0xDD}, {0x0131, 0xFD},
                                                          {0x015E, 0xDE}, {0x015F, 0xFE}};
  for (QChar c : s) {
    const char16_t v = c.unicode();
    if (v < 0x80) { out += static_cast<char>(v); continue; }
    bool done = false;
    for (auto [a, b] : tr) if (a == v) { out += static_cast<char>(b); done = true; }
    if (!done) out += v < 0x100 ? static_cast<char>(v) : '?';
  }
  return out;
}
struct W {
  std::ostringstream o;
  void g(int code, const std::string& v) { o << code << "\n" << v << "\n"; }
  void g(int code, double v) { std::ostringstream s; s.precision(10); s << v; g(code, s.str()); }
  void gi(int code, int v) { g(code, std::to_string(v)); }
  void pt(int base, Vec2 p) { g(base, p.x); g(base + 10, p.y); g(base + 20, 0.0); }
};
std::vector<Vec2> parse(const std::string& s) {
  std::vector<Vec2> v; std::istringstream is(s); std::string tok;
  while (std::getline(is, tok, ';')) { Vec2 p; if (std::sscanf(tok.c_str(), "%lf,%lf", &p.x, &p.y) == 2) v.push_back(p); else v.push_back({std::atof(tok.c_str()), 0}); }
  return v;
}
void entity(W& w, const Primitive& p, LineRole role) {
  const std::string L = layerOf(role);
  switch (p.kind) {
    case PrimKind::Line: w.g(0, "LINE"); w.g(8, L); w.pt(10, p.p0); w.pt(11, p.p1); break;
    case PrimKind::Circle: w.g(0, "CIRCLE"); w.g(8, L); w.pt(10, p.center); w.g(40, p.radius); break;
    case PrimKind::Arc: w.g(0, "ARC"); w.g(8, L); w.pt(10, p.center); w.g(40, p.radius); w.g(50, p.a0); w.g(51, p.a1); break;
    case PrimKind::Arrowhead: {
      const Vec2 d = (p.p1 - p.p0) * (1 / distance(p.p0, p.p1)), n{-d.y, d.x};
      w.g(0, "SOLID"); w.g(8, L); w.pt(10, p.p0); w.pt(11, p.p1 + n * (kArrowW / 2)); w.pt(12, p.p1 - n * (kArrowW / 2)); w.pt(13, p.p1 - n * (kArrowW / 2));
      break;
    }
    case PrimKind::Polyline:
      w.g(0, "POLYLINE"); w.g(8, L); w.gi(66, 1); w.pt(10, {0, 0}); w.gi(70, p.closed ? 1 : 0);
      for (auto& q : p.pts) { w.g(0, "VERTEX"); w.g(8, L); w.pt(10, q); }
      w.g(0, "SEQEND"); w.g(8, L); break;
    default: break;
  }
}
void text(W& w, const TextRun& t, const std::string& layer) {
  w.g(0, "TEXT"); w.g(8, layer); w.pt(10, t.origin); w.g(40, t.height); w.g(1, encode(t.utf8)); w.g(50, t.rotDeg); w.g(7, "STANDARD");
}
}  // namespace

bool writeDxf(const GroundTruth& gt, const std::string& path, bool withDims) {
  // which prims / texts belong to native dimensions
  struct ND { std::vector<int> prims; int text; std::vector<Vec2> def; };
  std::vector<ND> nds; std::set<int> inBlock;
  if (withDims)
    for (auto& t : gt.sheet.texts) {
      auto a = t.attrs.find("corpus.dxfDim"), b = t.attrs.find("corpus.dimPrims");
      if (a == t.attrs.end() || b == t.attrs.end()) continue;
      ND n; n.text = t.id; n.def = parse(a->second);
      std::istringstream is(b->second); std::string tok;
      while (std::getline(is, tok, ',')) { n.prims.push_back(std::stoi(tok)); inBlock.insert(n.prims.back()); }
      inBlock.insert(t.id);
      nds.push_back(n);
    }
  auto role = [&](int id) { auto it = gt.roles.find(id); return it == gt.roles.end() ? LineRole::Unknown : it->second; };
  W w;
  w.g(0, "SECTION"); w.g(2, "HEADER");
  w.g(9, "$ACADVER"); w.g(1, "AC1009");
  w.g(9, "$DWGCODEPAGE"); w.g(3, "ANSI_1254");
  w.g(9, "$INSUNITS"); w.gi(70, 4);
  w.g(9, "$LTSCALE"); w.g(40, 1.0);
  w.g(9, "$DIMLFAC"); w.g(40, gt.scale.den / gt.scale.num);
  w.g(9, "$EXTMIN"); w.pt(10, {0, 0}); w.g(9, "$EXTMAX"); w.pt(10, {420, 297});
  w.g(9, "$LIMMIN"); w.g(10, 0.0); w.g(20, 0.0); w.g(9, "$LIMMAX"); w.g(10, 420.0); w.g(20, 297.0);
  w.g(0, "ENDSEC");
  w.g(0, "SECTION"); w.g(2, "TABLES");
  w.g(0, "TABLE"); w.g(2, "LTYPE"); w.gi(70, 3);
  auto lt = [&](const std::string& n, const std::vector<double>& pat) {
    w.g(0, "LTYPE"); w.g(2, n); w.gi(70, 0); w.g(3, n); w.gi(72, 65); w.gi(73, static_cast<int>(pat.size()));
    double tot = 0; for (double x : pat) tot += x;
    w.g(40, tot);
    for (std::size_t i = 0; i < pat.size(); ++i) w.g(49, i % 2 ? -pat[i] : pat[i]);
  };
  lt("CONTINUOUS", {}); lt("HIDDEN", kHiddenDash); lt("CENTER", kCenterDash);
  w.g(0, "ENDTAB");
  w.g(0, "TABLE"); w.g(2, "LAYER"); w.gi(70, 7);
  for (auto [n, l, c] : {std::tuple<const char*, const char*, int>{"0", "CONTINUOUS", 7}, {"GORUNUR", "CONTINUOUS", 7}, {"GIZLI", "HIDDEN", 2},
                         {"EKSEN", "CENTER", 1}, {"OLCU", "CONTINUOUS", 3}, {"CERCEVE", "CONTINUOUS", 7}, {"ANTET", "CONTINUOUS", 7}}) {
    w.g(0, "LAYER"); w.g(2, n); w.gi(70, 0); w.gi(62, c); w.g(6, l);
  }
  w.g(0, "ENDTAB");
  w.g(0, "TABLE"); w.g(2, "STYLE"); w.gi(70, 1);
  w.g(0, "STYLE"); w.g(2, "STANDARD"); w.gi(70, 0); w.g(40, 0.0); w.g(41, 1.0); w.g(50, 0.0); w.gi(71, 0); w.g(42, 3.5); w.g(3, "txt"); w.g(4, "");
  w.g(0, "ENDTAB");
  w.g(0, "ENDSEC");
  w.g(0, "SECTION"); w.g(2, "BLOCKS");
  for (std::size_t i = 0; i < nds.size(); ++i) {
    w.g(0, "BLOCK"); w.g(8, "0"); w.g(2, "*D" + std::to_string(i + 1)); w.gi(70, 1); w.pt(10, {0, 0}); w.g(3, "*D" + std::to_string(i + 1));
    for (int id : nds[i].prims) for (auto& p : gt.sheet.prims) if (p.id == id) entity(w, p, role(id));
    for (auto& t : gt.sheet.texts) if (t.id == nds[i].text) text(w, t, "OLCU");
    w.g(0, "ENDBLK"); w.g(8, "0");
  }
  w.g(0, "ENDSEC");
  w.g(0, "SECTION"); w.g(2, "ENTITIES");
  for (auto& p : gt.sheet.prims) if (!inBlock.count(p.id)) entity(w, p, role(p.id));
  for (auto& t : gt.sheet.texts) if (!inBlock.count(t.id)) text(w, t, t.attrs.count("corpus.dimPrims") ? "OLCU" : "ANTET");
  for (std::size_t i = 0; i < nds.size(); ++i) {
    const auto& d = nds[i].def;   // type, angle, p10, p13, p14, p15
    const TextRun* t = nullptr; for (auto& x : gt.sheet.texts) if (x.id == nds[i].text) t = &x;
    w.g(0, "DIMENSION"); w.g(8, "OLCU"); w.g(2, "*D" + std::to_string(i + 1));
    w.pt(10, d[2]);
    if (t) w.pt(11, (t->quad[0] + t->quad[2]) * 0.5);
    w.gi(70, static_cast<int>(d[0].x)); 
    if (t) w.g(1, encode(t->utf8));
    w.g(3, "STANDARD");
    if (d[0].x == 0) { w.pt(13, d[3]); w.pt(14, d[4]); w.g(50, d[1].x); }
    else w.pt(15, d[5]);
  }
  w.g(0, "ENDSEC");
  w.g(0, "EOF");
  std::ofstream f(path, std::ios::binary);
  f << w.o.str();
  return static_cast<bool>(f);
}
}  // namespace corpus
