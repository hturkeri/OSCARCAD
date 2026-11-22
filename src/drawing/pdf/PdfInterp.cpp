// PDF content-stream interpreter (SPEC §2.1). OWNER WP-1. libqpdf (Apache-2.0) parses objects and decodes filters;
// graphics/text operators are interpreted here.
#include "pdf/PdfInterp.h"
#include "pdf/PdfText.h"
#include "normalize/GeomUtil.h"
#include "pc/drawing/Confidence.h"
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <sstream>

namespace pc::drawing::pdf {
namespace {
using geom::deg;

double numOf(QPDFObjectHandle h, double def = 0) {
  if (h.isInteger() || h.isReal()) return h.getNumericValue();
  return def;
}
std::string nameOf(QPDFObjectHandle h) { return h.isName() ? h.getName() : std::string(); }
QPDFObjectHandle key(QPDFObjectHandle d, const char* k) {
  if (!d.isDictionary() && !d.isStream()) return QPDFObjectHandle::newNull();
  QPDFObjectHandle dict = d.isStream() ? d.getDict() : d;
  return dict.hasKey(k) ? dict.getKey(k) : QPDFObjectHandle::newNull();
}
Affine2 matrixOf(QPDFObjectHandle a) {
  if (!a.isArray() || a.getArrayNItems() != 6) return kIdentityAffine;
  Affine2 m{};
  for (int i = 0; i < 6; ++i) m[i] = numOf(a.getArrayItem(i));
  return m;
}
std::string stripSubset(const std::string& n) {
  std::string s = n.size() && n[0] == '/' ? n.substr(1) : n;
  if (s.size() > 7 && s[6] == '+') { bool up = true; for (int i = 0; i < 6; ++i) up = up && std::isupper(static_cast<unsigned char>(s[i])); if (up) s = s.substr(7); }
  return s;
}

struct Font {
  std::string baseFont, subtype;
  bool type0 = false, type3 = false, symbolic = false, standard14 = false;
  ToUnicodeCMap tu;
  bool hasToUnicode = false;
  std::map<unsigned, std::string> diff;
  BaseEnc base = BaseEnc::Standard;
  std::vector<double> widths; unsigned firstChar = 0; double missingWidth = 0;
  std::map<uint32_t, double> cidW; double dw = 1000;
  double capHeight = 0;           // glyph space /1000; 0 = unknown
  double widthScale = 0.001;      // Type3: FontMatrix[0]
  double heightScale = 0.001;
  std::vector<CMapRange> codespace;
  int defaultBytes = 1;
  std::string diffNameSet;        // space-separated glyph names of /Differences (only if small)
};

struct Glyph { std::string utf8, name; double w = 0; bool fromToUnicode = false; bool known = true; };

std::unique_ptr<Font> loadFont(QPDFObjectHandle fd) {
  auto f = std::make_unique<Font>();
  f->subtype = nameOf(key(fd, "/Subtype"));
  f->baseFont = stripSubset(nameOf(key(fd, "/BaseFont")));
  f->type0 = f->subtype == "/Type0";
  f->type3 = f->subtype == "/Type3";
  static const std::set<std::string> std14 = {"Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique", "Times-Roman",
                                              "Times-Bold", "Times-Italic", "Times-BoldItalic", "Courier", "Courier-Bold",
                                              "Courier-Oblique", "Courier-BoldOblique", "Symbol", "ZapfDingbats", "Arial", "ArialMT"};
  f->standard14 = std14.count(f->baseFont) > 0;
  QPDFObjectHandle desc = key(fd, "/FontDescriptor");
  QPDFObjectHandle cidFont;
  if (f->type0) {
    QPDFObjectHandle df = key(fd, "/DescendantFonts");
    if (df.isArray() && df.getArrayNItems() > 0) { cidFont = df.getArrayItem(0); desc = key(cidFont, "/FontDescriptor"); }
    f->defaultBytes = 2;
    f->dw = numOf(key(cidFont, "/DW"), 1000);
    QPDFObjectHandle w = key(cidFont, "/W");
    if (w.isArray()) {
      const int n = w.getArrayNItems();
      for (int i = 0; i < n;) {
        const uint32_t c0 = static_cast<uint32_t>(numOf(w.getArrayItem(i)));
        if (i + 1 < n && w.getArrayItem(i + 1).isArray()) {
          auto arr = w.getArrayItem(i + 1);
          for (int k = 0; k < arr.getArrayNItems(); ++k) f->cidW[c0 + k] = numOf(arr.getArrayItem(k));
          i += 2;
        } else if (i + 2 < n) {
          const uint32_t c1 = static_cast<uint32_t>(numOf(w.getArrayItem(i + 1)));
          const double ww = numOf(w.getArrayItem(i + 2));
          for (uint32_t c = c0; c <= c1 && c - c0 < 65536; ++c) f->cidW[c] = ww;
          i += 3;
        } else break;
      }
    }
    // Identity-H/V => 2 bytes; embedded CMap stream: parse codespace
    QPDFObjectHandle enc = key(fd, "/Encoding");
    if (enc.isStream()) {
      try { auto data = enc.getStreamData(qpdf_dl_generalized); f->codespace = parseToUnicode(std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize())).codespace; } catch (...) {}
    }
  } else {
    f->firstChar = static_cast<unsigned>(numOf(key(fd, "/FirstChar"), 0));
    QPDFObjectHandle w = key(fd, "/Widths");
    if (w.isArray()) for (int i = 0; i < w.getArrayNItems(); ++i) f->widths.push_back(numOf(w.getArrayItem(i)));
    QPDFObjectHandle enc = key(fd, "/Encoding");
    const std::string encName = nameOf(enc);
    auto baseOf = [](const std::string& n, BaseEnc def) {
      if (n == "/WinAnsiEncoding") return BaseEnc::WinAnsi;
      if (n == "/MacRomanEncoding") return BaseEnc::MacRoman;
      if (n == "/StandardEncoding") return BaseEnc::Standard;
      return def;
    };
    const bool isSymbolFont = f->baseFont == "Symbol";
    BaseEnc def = isSymbolFont ? BaseEnc::Symbol : (f->subtype == "/TrueType" ? BaseEnc::WinAnsi : BaseEnc::Standard);
    if (f->baseFont == "ZapfDingbats") def = BaseEnc::None;
    f->base = baseOf(encName, def);
    if (enc.isDictionary()) {
      f->base = baseOf(nameOf(key(enc, "/BaseEncoding")), def);
      QPDFObjectHandle d = key(enc, "/Differences");
      if (d.isArray()) {
        unsigned code = 0;
        std::vector<std::string> names;
        for (int i = 0; i < d.getArrayNItems(); ++i) {
          auto it = d.getArrayItem(i);
          if (it.isInteger()) code = static_cast<unsigned>(it.getIntValue());
          else if (it.isName()) { f->diff[code] = it.getName().substr(1); names.push_back(it.getName().substr(1)); ++code; }
        }
        if (names.size() <= 32) { std::sort(names.begin(), names.end()); for (auto& n : names) f->diffNameSet += (f->diffNameSet.empty() ? "" : " ") + n; }
      }
    }
    if (f->type3) {
      auto fm = matrixOf(key(fd, "/FontMatrix"));
      f->widthScale = fm[0]; f->heightScale = std::abs(fm[3]) > 0 ? fm[3] : fm[0];
    }
  }
  if (desc.isDictionary()) {
    f->missingWidth = numOf(key(desc, "/MissingWidth"), 0);
    f->capHeight = numOf(key(desc, "/CapHeight"), 0);
    const int flags = static_cast<int>(numOf(key(desc, "/Flags"), 0));
    f->symbolic = (flags & 4) != 0 && (flags & 32) == 0;
  }
  if (f->baseFont == "Symbol" || f->baseFont == "ZapfDingbats") f->symbolic = true;
  QPDFObjectHandle tu = key(fd, "/ToUnicode");
  if (tu.isStream()) {
    try {
      auto data = tu.getStreamData(qpdf_dl_generalized);
      f->tu = parseToUnicode(std::string(reinterpret_cast<const char*>(data->getBuffer()), data->getSize()));
      f->hasToUnicode = !f->tu.empty();
      if (f->codespace.empty()) f->codespace = f->tu.codespace;
    } catch (...) {}
  }
  if (f->capHeight <= 0) f->capHeight = 700;   // typical; QPdfWriter / CAD fonts carry /CapHeight
  return f;
}

Glyph decodeGlyph(const Font& f, uint32_t code, int nbytes) {
  Glyph g;
  // width
  if (f.type0) { auto it = f.cidW.find(code); g.w = it != f.cidW.end() ? it->second : f.dw; }
  else if (code >= f.firstChar && code - f.firstChar < f.widths.size()) g.w = f.widths[code - f.firstChar];
  else g.w = f.missingWidth;
  // unicode
  if (f.hasToUnicode) {
    auto it = f.tu.map.find(code);
    if (it != f.tu.map.end()) { g.utf8 = it->second; g.fromToUnicode = true; }
  }
  if (!f.type0) {
    auto it = f.diff.find(code);
    g.name = it != f.diff.end() ? it->second : encodingGlyphName(f.base, code);
    if (g.utf8.empty()) {
      uint32_t u = glyphNameToUnicode(g.name);
      if (!u && f.symbolic && code >= 0xF020 && code <= 0xF0FF) u = code - 0xF000;
      if (!u && !g.name.empty() && g.name.rfind("g", 0) == 0) u = 0;
      if (!u && g.name.empty() && code >= 0x20 && code < 0x7F) u = code;   // symbolic TrueType w/o encoding
      if (u) appendUtf8(g.utf8, u); else { g.utf8 = "\xEF\xBF\xBD"; g.known = false; }
    }
  } else if (g.utf8.empty()) {
    g.utf8 = "\xEF\xBF\xBD"; g.known = false;
  }
  if (!f.type0 && f.widths.empty() && (f.standard14 || f.baseFont.empty())) {
    uint32_t u = 0; if (!g.utf8.empty()) u = static_cast<unsigned char>(g.utf8[0]);
    g.w = standardWidth(f.baseFont, u < 0x80 ? u : 0x41);
  }
  (void)nbytes;
  return g;
}

struct GState {
  Affine2 ctm = kIdentityAffine;
  double lw = 1; std::vector<double> dash; double dashPhase = 0;
  uint32_t strokeRgb = 0, fillRgb = 0;
  int strokeComps = 1, fillComps = 1;
  // text
  Font* font = nullptr; std::string fontRes; double fs = 0, tc = 0, tw = 0, th = 1, tl = 0, rise = 0; int tr = 0;
  bool clipped = false;
};

uint32_t toRgb(const std::vector<double>& c) {
  auto cl = [](double v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255)); };
  if (c.size() == 1) { const uint32_t g = cl(c[0]); return (g << 16) | (g << 8) | g; }
  if (c.size() == 3) return (cl(c[0]) << 16) | (cl(c[1]) << 8) | cl(c[2]);
  if (c.size() == 4) {
    const double k = c[3];
    return (cl((1 - c[0]) * (1 - k)) << 16) | (cl((1 - c[1]) * (1 - k)) << 8) | cl((1 - c[2]) * (1 - k));
  }
  return 0;
}

struct Seg { bool curve; Vec2 p[4]; };
struct SubPath { std::vector<Seg> segs; bool closed = false; Vec2 start; };

class Interp : public QPDFObjectHandle::ParserCallbacks {
 public:
  Interp(PrimitiveSheet* out, int* nextId, PageStats* st, const Affine2& pageToSheet, double pageArea, const InterpOptions& o,
         std::map<QPDFObjGen, std::unique_ptr<Font>>* fonts, std::map<std::string, int>* warn)
      : out_(out), nextId_(nextId), st_(st), p2s_(pageToSheet), pageArea_(pageArea), opt_(o), fonts_(fonts), warn_(warn) {
    gs_.push_back(GState{});
  }
  QPDFObjectHandle res;
  int depth = 0;
  std::string xobjRef;
  std::vector<std::string> ocStack;   // "" for non-OC marked content

  void handleObject(QPDFObjectHandle h) override {
    if (h.isOperator()) {
      const std::string op = h.getOperatorValue();
      try { run(op); } catch (const std::exception&) { (*warn_)["pdf.op.malformed"]++; }
      ops_.clear();
      ++opIndex_;
      return;
    }
    if (h.isInlineImage()) { inlineImage_ = true; }
    ops_.push_back(h);
  }
  void handleEOF() override {}

 private:
  PrimitiveSheet* out_; int* nextId_; PageStats* st_; Affine2 p2s_; double pageArea_; InterpOptions opt_;
  std::map<QPDFObjGen, std::unique_ptr<Font>>* fonts_;
  std::map<std::string, int>* warn_;
  std::vector<GState> gs_;
  std::vector<QPDFObjectHandle> ops_;
  std::vector<SubPath> path_;
  Vec2 cur_{0, 0};
  Affine2 tm_ = kIdentityAffine, tlm_ = kIdentityAffine;
  long opIndex_ = 0;
  bool inlineImage_ = false;
  bool pendingClip_ = false;

  GState& g() { return gs_.back(); }
  double n(std::size_t i) { if (i >= ops_.size()) throw std::runtime_error("operand"); return numOf(ops_[i]); }
  Affine2 toSheet() const { return compose(gs_.back().ctm, p2s_); }
  std::string layer() const { for (auto it = ocStack.rbegin(); it != ocStack.rend(); ++it) if (!it->empty()) return *it; return {}; }

  void newSub(Vec2 p) { path_.push_back(SubPath{}); path_.back().start = p; cur_ = p; }
  void lineTo(Vec2 p) {
    if (path_.empty()) newSub(cur_);
    path_.back().segs.push_back({false, {cur_, p, {}, {}}}); cur_ = p;
  }
  void curveTo(Vec2 c1, Vec2 c2, Vec2 p) {
    if (path_.empty()) newSub(cur_);
    path_.back().segs.push_back({true, {cur_, c1, c2, p}}); cur_ = p;
  }
  void closePath() {
    if (path_.empty()) return;
    auto& sp = path_.back();
    if (distance(cur_, sp.start) > 1e-9) sp.segs.push_back({false, {cur_, sp.start, {}, {}}});
    sp.closed = true; cur_ = sp.start;
  }

  LineStyle style(bool stroke, bool fill) {
    LineStyle s;
    const Affine2 m = toSheet();
    const double sc = std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2]));
    s.width = g().lw * sc;
    for (double d : g().dash) s.dash.push_back(d * sc);
    s.dashPhase = g().dashPhase * sc;
    s.rgb = stroke ? g().strokeRgb : g().fillRgb;
    s.stroked = stroke; s.filled = fill;
    s.layer = layer();
    return s;
  }
  Primitive& emit(PrimKind k, const LineStyle& s) {
    Primitive p; p.id = (*nextId_)++; p.kind = k; p.style = s; p.conf = 0.95f;
    p.prov.source = SourceKind::VectorPdf; p.prov.page = st_->index;
    p.prov.ref = (xobjRef.empty() ? std::string("op ") : "XObject " + xobjRef + " op ") + std::to_string(opIndex_);
    p.clipped = g().clipped;
    if (s.filled && g().fillRgb != s.rgb) { char b[16]; std::snprintf(b, sizeof b, "%06X", g().fillRgb); p.attrs["pdf.fillRgb"] = b; }
    out_->prims.push_back(std::move(p));
    return out_->prims.back();
  }

  void paint(bool stroke, bool fill) {
    if (!stroke && !fill) { path_.clear(); return; }
    ++st_->pathCount;
    if (!opt_.collect) { path_.clear(); return; }
    const Affine2 m = toSheet();
    const LineStyle s = style(stroke, fill);
    for (auto& sp : path_) {
      if (sp.segs.empty()) continue;
      if (!stroke) {
        // fill-only: one primitive per subpath (arrowheads, solid fills, glyph outlines)
        bool anyCurve = false; for (auto& sg : sp.segs) anyCurve |= sg.curve;
        if (!anyCurve) {
          auto& p = emit(PrimKind::Polyline, s);
          p.pts.push_back(transformPoint(m, sp.segs.front().p[0]));
          for (auto& sg : sp.segs) p.pts.push_back(transformPoint(m, sg.p[1]));
          if (p.pts.size() > 2 && distance(p.pts.front(), p.pts.back()) < 1e-9) p.pts.pop_back();
          p.closed = true;
        } else {
          auto& p = emit(PrimKind::Bezier, s);
          p.pts.push_back(transformPoint(m, sp.segs.front().p[0]));
          for (auto& sg : sp.segs) {
            if (sg.curve) { for (int k = 1; k < 4; ++k) p.pts.push_back(transformPoint(m, sg.p[k])); }
            else {
              const Vec2 a = transformPoint(m, sg.p[0]), b = transformPoint(m, sg.p[1]);
              p.pts.push_back(a + (b - a) * (1.0 / 3)); p.pts.push_back(a + (b - a) * (2.0 / 3)); p.pts.push_back(b);
            }
          }
          p.closed = true;
        }
        continue;
      }
      std::size_t i = 0;
      while (i < sp.segs.size()) {
        if (!sp.segs[i].curve) {
          const Vec2 a = transformPoint(m, sp.segs[i].p[0]), b = transformPoint(m, sp.segs[i].p[1]);
          if (distance(a, b) > 1e-9) { auto& p = emit(PrimKind::Line, s); p.p0 = a; p.p1 = b; }
          ++i; continue;
        }
        auto& p = emit(PrimKind::Bezier, s);
        p.pts.push_back(transformPoint(m, sp.segs[i].p[0]));
        while (i < sp.segs.size() && sp.segs[i].curve) { for (int k = 1; k < 4; ++k) p.pts.push_back(transformPoint(m, sp.segs[i].p[k])); ++i; }
      }
    }
    path_.clear();
  }

  void applyClip() {
    // record (not applied): elements drawn under a clip noticeably smaller than the page are flagged
    Box2 b = emptyBox();
    const Affine2 m = toSheet();
    for (auto& sp : path_) for (auto& sg : sp.segs) for (int k = 0; k < (sg.curve ? 4 : 2); ++k) expand(b, transformPoint(m, sg.p[k]));
    if (!isEmpty(b) && width(b) * height(b) < 0.95 * pageArea_) g().clipped = true;
  }

  Font* font(const std::string& resName) {
    QPDFObjectHandle fd = key(key(res, "/Font"), resName.c_str());
    if (!fd.isDictionary()) return nullptr;
    auto og = fd.getObjGen();
    auto it = fonts_->find(og);
    if (it == fonts_->end() || og.getObj() == 0) {
      auto f = loadFont(fd);
      if (!f->hasToUnicode) (*warn_)["pdf.font.noToUnicode:" + f->baseFont]++;
      if (og.getObj() == 0) { static thread_local std::vector<std::unique_ptr<Font>> direct; direct.push_back(std::move(f)); return direct.back().get(); }
      it = fonts_->emplace(og, std::move(f)).first;
    }
    return it->second.get();
  }

  void showText(const std::string& bytes, const std::vector<double>* adjust = nullptr, const std::vector<std::string>* parts = nullptr) {
    ++st_->textCount;
    GState& s = g();
    Font* f = s.font;
    if (!f) { (*warn_)["pdf.text.noFont"]++; return; }
    const Affine2 startTm = tm_;
    std::string utf8, names;
    std::vector<Affine2> glyphTm;   // text matrix at the start of each emitted character (1 char per glyph)
    bool oneCharPerGlyph = true;
    bool anyNoTU = false;
    double maxSpace = 0;
    auto doString = [&](const std::string& b) {
      for (auto& [code, nb] : splitCodes(b, f->codespace, f->defaultBytes)) {
        const Glyph gl = decodeGlyph(*f, code, nb);
        { int cps = 0; for (unsigned char ch : gl.utf8) cps += (ch & 0xC0) != 0x80; if (cps != 1) oneCharPerGlyph = false; }
        glyphTm.push_back(tm_);
        utf8 += gl.utf8;
        if (!gl.fromToUnicode) anyNoTU = true;
        names += (names.empty() ? "" : " ") + (gl.name.empty() ? std::string("#") + std::to_string(code) : gl.name);
        double tx = (gl.w * f->widthScale * s.fs + s.tc + (nb == 1 && code == 32 ? s.tw : 0)) * s.th;
        tm_ = compose(Affine2{1, 0, 0, 1, tx, 0}, tm_);
      }
    };
    if (parts) {
      for (std::size_t i = 0; i < parts->size(); ++i) {
        if (!(*parts)[i].empty()) doString((*parts)[i]);
        const double adj = (*adjust)[i];
        if (adj != 0) {
          const double tx = -adj / 1000.0 * s.fs * s.th;
          tm_ = compose(Affine2{1, 0, 0, 1, tx, 0}, tm_);
          if (-adj > 250 && !utf8.empty() && utf8.back() != ' ') {
            glyphTm.push_back(compose(Affine2{1, 0, 0, 1, -tx, 0}, tm_));
            utf8 += ' '; names += " space"; maxSpace = std::max(maxSpace, -adj);
          }
        }
      }
    } else {
      doString(bytes);
    }
    if (!opt_.collect || utf8.empty()) return;
    const Affine2 m = toSheet();
    const Affine2 trm0 = compose(compose(Affine2{s.fs * s.th, 0, 0, s.fs, 0, s.rise}, startTm), m);
    const Affine2 trm1 = compose(compose(Affine2{s.fs * s.th, 0, 0, s.fs, 0, s.rise}, tm_), m);
    const Vec2 o = transformPoint(trm0, {0, 0}), e = transformPoint(trm1, {0, 0});
    Vec2 xdir{trm0[0], trm0[1]};
    const Vec2 ydir{trm0[2], trm0[3]};
    if (length(xdir) < 1e-12) xdir = {1, 0};
    const Vec2 u = geom::unit(distance(o, e) > 1e-9 ? e - o : xdir);
    const double emY = length(ydir);
    const double capH = f->capHeight * (f->type3 ? f->heightScale : 0.001) * emY;
    Vec2 nrm{-u.y, u.x};
    if (cross(xdir, ydir) < 0) nrm = nrm * -1;   // mirrored text
    TextRun t; t.id = (*nextId_)++; t.utf8 = utf8; t.origin = o; t.height = capH; t.rotDeg = geom::norm360(deg(std::atan2(u.y, u.x)));
    if (t.rotDeg >= 359.9999) t.rotDeg = 0;
    const double w = distance(o, e);
    t.quad = {o, o + u * w, o + u * w + nrm * capH, o + nrm * capH};
    t.font = f->baseFont; t.rgb = s.fillRgb;
    t.invisible = s.tr == 3 || s.tr == 7;
    t.conf = f->hasToUnicode && !anyNoTU ? 0.97f : (anyNoTU && f->symbolic ? 0.7f : 0.92f);
    t.prov.source = SourceKind::VectorPdf; t.prov.page = st_->index;
    t.prov.ref = (xobjRef.empty() ? std::string("op ") : "XObject " + xobjRef + " op ") + std::to_string(opIndex_);
    t.attrs["pdf.fontRes"] = s.fontRes;
    { std::ostringstream os; os << emY / kPtToMm; t.attrs["pdf.fontSizePt"] = os.str(); }
    if (anyNoTU) {
      t.attrs["pdf.glyphNames"] = names;
      if (f->symbolic) t.attrs["pdf.symbolic"] = "1";
      if (!f->diffNameSet.empty()) t.attrs["pdf.fontDiffNames"] = f->diffNameSet;
    }
    if (!layer().empty()) t.attrs["pdf.ocg"] = layer();
    if (oneCharPerGlyph && glyphTm.size() > 1) {
      std::ostringstream gp; gp.precision(5);
      for (std::size_t k = 0; k < glyphTm.size(); ++k) {
        const Affine2 trm = compose(compose(Affine2{s.fs * s.th, 0, 0, s.fs, 0, s.rise}, glyphTm[k]), m);
        gp << (k ? "," : "") << dot(transformPoint(trm, {0, 0}) - o, u);
      }
      t.attrs["pdf.glyphPos"] = gp.str();   // start of each character along the baseline, mm from origin
    }
    out_->texts.push_back(std::move(t));
  }

  void doXObject(const std::string& name) {
    QPDFObjectHandle xo = key(key(res, "/XObject"), name.c_str());
    if (!xo.isStream()) return;
    const std::string sub = nameOf(key(xo, "/Subtype"));
    if (sub == "/Image") { image(); return; }
    if (sub != "/Form") return;
    if (depth >= opt_.maxFormDepth) { (*warn_)["pdf.form.depth"]++; return; }
    Interp in(out_, nextId_, st_, p2s_, pageArea_, opt_, fonts_, warn_);
    in.depth = depth + 1;
    in.gs_.back() = g();
    in.gs_.back().ctm = compose(matrixOf(key(xo, "/Matrix")), g().ctm);
    in.res = key(xo, "/Resources").isDictionary() ? key(xo, "/Resources") : res;
    in.xobjRef = name.substr(1);
    in.ocStack = ocStack;
    // optional content on the form itself
    QPDFObjectHandle oc = key(xo, "/OC");
    if (oc.isDictionary()) { QPDFObjectHandle nm = key(oc, "/Name"); if (nm.isString()) in.ocStack.push_back(nm.getUTF8Value()); }
    xo.parseAsContents(&in);
  }

  void image() {
    ++st_->imageCount;
    const Affine2 m = toSheet();
    const Vec2 a = transformPoint(m, {0, 0}), b = transformPoint(m, {1, 0}), c = transformPoint(m, {1, 1}), d = transformPoint(m, {0, 1});
    const double area = std::abs(cross(b - a, d - a));
    st_->largestImageArea = std::max(st_->largestImageArea, area);
    if (!opt_.collect) return;
    LineStyle s; s.stroked = false; s.filled = true; s.layer = layer();
    auto& p = emit(PrimKind::Image, s);
    p.pts = {a, b, c, d}; p.closed = true;
  }

  void setColor(bool stroke, std::size_t ncomp) {
    std::vector<double> c;
    for (std::size_t i = 0; i < ops_.size() && i < ncomp; ++i) if (ops_[i].isInteger() || ops_[i].isReal()) c.push_back(numOf(ops_[i]));
    if (c.empty()) return;
    (stroke ? g().strokeRgb : g().fillRgb) = toRgb(c);
  }
  int compsOfCS(const std::string& cs) {
    if (cs == "/DeviceGray" || cs == "/CalGray" || cs == "/G") return 1;
    if (cs == "/DeviceRGB" || cs == "/CalRGB" || cs == "/RGB") return 3;
    if (cs == "/DeviceCMYK" || cs == "/CMYK") return 4;
    if (cs == "/Pattern") return 0;
    QPDFObjectHandle o = key(key(res, "/ColorSpace"), cs.c_str());
    if (o.isArray() && o.getArrayNItems() > 0) {
      const std::string kind = nameOf(o.getArrayItem(0));
      if (kind == "/ICCBased" && o.getArrayNItems() > 1) return static_cast<int>(numOf(key(o.getArrayItem(1), "/N"), 3));
      if (kind == "/CalRGB" || kind == "/Lab") return 3;
      if (kind == "/CalGray") return 1;
    }
    return 1;
  }

  void run(const std::string& op) {
    GState& s = g();
    if (op == "q") { gs_.push_back(g()); return; }
    if (op == "Q") { if (gs_.size() > 1) gs_.pop_back(); return; }
    if (op == "cm") { g().ctm = compose(Affine2{n(0), n(1), n(2), n(3), n(4), n(5)}, g().ctm); return; }
    if (op == "w") { s.lw = n(0); return; }
    if (op == "d") {
      s.dash.clear();
      if (!ops_.empty() && ops_[0].isArray()) for (auto& v : ops_[0].getArrayAsVector()) s.dash.push_back(numOf(v));
      s.dashPhase = ops_.size() > 1 ? n(1) : 0;
      return;
    }
    if (op == "J" || op == "j" || op == "M" || op == "i" || op == "ri") return;
    if (op == "gs") {
      QPDFObjectHandle e = key(key(res, "/ExtGState"), nameOf(ops_.at(0)).c_str());
      if (e.isDictionary()) {
        QPDFObjectHandle lw = key(e, "/LW"); if (!lw.isNull()) s.lw = numOf(lw, s.lw);
        QPDFObjectHandle d = key(e, "/D");
        if (d.isArray() && d.getArrayNItems() == 2 && d.getArrayItem(0).isArray()) {
          s.dash.clear(); for (auto& v : d.getArrayItem(0).getArrayAsVector()) s.dash.push_back(numOf(v));
          s.dashPhase = numOf(d.getArrayItem(1));
        }
        QPDFObjectHandle fnt = key(e, "/Font");
        if (fnt.isArray() && fnt.getArrayNItems() == 2) { (void)fnt; (*warn_)["pdf.gs.font"]++; }
      }
      return;
    }
    if (op == "RG") { s.strokeComps = 3; setColor(true, 3); return; }
    if (op == "rg") { s.fillComps = 3; setColor(false, 3); return; }
    if (op == "G") { s.strokeComps = 1; setColor(true, 1); return; }
    if (op == "g") { s.fillComps = 1; setColor(false, 1); return; }
    if (op == "K") { s.strokeComps = 4; setColor(true, 4); return; }
    if (op == "k") { s.fillComps = 4; setColor(false, 4); return; }
    if (op == "CS") { s.strokeComps = compsOfCS(nameOf(ops_.at(0))); s.strokeRgb = 0; return; }
    if (op == "cs") { s.fillComps = compsOfCS(nameOf(ops_.at(0))); s.fillRgb = 0; return; }
    if (op == "SC" || op == "SCN") { if (s.strokeComps > 0) setColor(true, static_cast<std::size_t>(s.strokeComps)); return; }
    if (op == "sc" || op == "scn") { if (s.fillComps > 0) setColor(false, static_cast<std::size_t>(s.fillComps)); return; }
    // path construction
    if (op == "m") { newSub({n(0), n(1)}); return; }
    if (op == "l") { lineTo({n(0), n(1)}); return; }
    if (op == "c") { curveTo({n(0), n(1)}, {n(2), n(3)}, {n(4), n(5)}); return; }
    if (op == "v") { curveTo(cur_, {n(0), n(1)}, {n(2), n(3)}); return; }
    if (op == "y") { const Vec2 p{n(2), n(3)}; curveTo({n(0), n(1)}, p, p); return; }
    if (op == "h") { closePath(); return; }
    if (op == "re") {
      const double x = n(0), y = n(1), w = n(2), h = n(3);
      newSub({x, y}); lineTo({x + w, y}); lineTo({x + w, y + h}); lineTo({x, y + h}); closePath(); return;
    }
    if (op == "W" || op == "W*") { pendingClip_ = true; return; }
    auto finishClip = [&] { if (pendingClip_) { applyClip(); pendingClip_ = false; } };
    if (op == "S") { finishClip(); paint(true, false); return; }
    if (op == "s") { closePath(); finishClip(); paint(true, false); return; }
    if (op == "f" || op == "F" || op == "f*") { finishClip(); paint(false, true); return; }
    if (op == "B" || op == "B*") { finishClip(); paint(true, true); return; }
    if (op == "b" || op == "b*") { closePath(); finishClip(); paint(true, true); return; }
    if (op == "n") { finishClip(); path_.clear(); return; }
    // text
    if (op == "BT") { tm_ = tlm_ = kIdentityAffine; return; }
    if (op == "ET") return;
    if (op == "Tf") { s.fontRes = nameOf(ops_.at(0)); s.font = font(s.fontRes); s.fs = n(1); return; }
    if (op == "Tc") { s.tc = n(0); return; }
    if (op == "Tw") { s.tw = n(0); return; }
    if (op == "Tz") { s.th = n(0) / 100.0; return; }
    if (op == "TL") { s.tl = n(0); return; }
    if (op == "Ts") { s.rise = n(0); return; }
    if (op == "Tr") { s.tr = static_cast<int>(n(0)); return; }
    if (op == "Td") { tlm_ = compose(Affine2{1, 0, 0, 1, n(0), n(1)}, tlm_); tm_ = tlm_; return; }
    if (op == "TD") { s.tl = -n(1); tlm_ = compose(Affine2{1, 0, 0, 1, n(0), n(1)}, tlm_); tm_ = tlm_; return; }
    if (op == "Tm") { tlm_ = tm_ = Affine2{n(0), n(1), n(2), n(3), n(4), n(5)}; return; }
    if (op == "T*") { tlm_ = compose(Affine2{1, 0, 0, 1, 0, -s.tl}, tlm_); tm_ = tlm_; return; }
    if (op == "Tj") { showText(ops_.at(0).getStringValue()); return; }
    if (op == "'") { run("T*"); showText(ops_.at(0).getStringValue()); return; }
    if (op == "\"") { s.tw = n(0); s.tc = n(1); run("T*"); showText(ops_.at(2).getStringValue()); return; }
    if (op == "TJ") {
      std::vector<std::string> parts; std::vector<double> adj;
      if (!ops_.empty() && ops_[0].isArray())
        for (auto& e : ops_[0].getArrayAsVector()) {
          if (e.isString()) { parts.push_back(e.getStringValue()); adj.push_back(0); }
          else if (e.isInteger() || e.isReal()) {
            if (parts.empty()) { parts.emplace_back(); adj.push_back(0); }
            adj.back() += numOf(e);
          }
        }
      showText({}, &adj, &parts);
      return;
    }
    // XObjects, images, marked content
    if (op == "Do") { doXObject(nameOf(ops_.at(0))); return; }
    if (op == "EI" || (op == "ID")) { if (inlineImage_ || op == "EI") image(); inlineImage_ = false; return; }
    if (op == "BI") return;
    if (op == "BDC" || op == "BMC") {
      std::string oc;
      if (op == "BDC" && ops_.size() >= 2 && nameOf(ops_[0]) == "/OC") {
        QPDFObjectHandle props = ops_[1].isName() ? key(key(res, "/Properties"), ops_[1].getName().c_str()) : ops_[1];
        QPDFObjectHandle nm = key(props, "/Name");
        if (!nm.isString()) {
          QPDFObjectHandle ocgs = key(props, "/OCGs");
          if (ocgs.isArray() && ocgs.getArrayNItems() > 0) nm = key(ocgs.getArrayItem(0), "/Name");
          else if (ocgs.isDictionary()) nm = key(ocgs, "/Name");
        }
        if (nm.isString()) oc = nm.getUTF8Value();
      }
      ocStack.push_back(oc);
      return;
    }
    if (op == "EMC") { if (!ocStack.empty()) ocStack.pop_back(); return; }
    if (op == "sh") { (*warn_)["pdf.op.shading"]++; return; }
    if (op == "BX" || op == "EX" || op == "MP" || op == "DP" || op == "d0" || op == "d1") return;
    (*warn_)["pdf.op.unknown:" + op]++;
  }
};

}  // namespace

int pdfPageCount(const std::string& path, std::string* err) {
  try {
    QPDF q; q.setSuppressWarnings(true); q.processFile(path.c_str());
    return static_cast<int>(QPDFPageDocumentHelper(q).getAllPages().size());
  } catch (const std::exception& e) {
    if (err) *err = e.what();
    return -1;
  }
}

bool interpretPage(const std::string& path, int page, const InterpOptions& o, PrimitiveSheet& out, int& nextId, PageStats& stats,
                   std::string* err) {
  try {
    QPDF q; q.setSuppressWarnings(true); q.processFile(path.c_str());
    auto pages = QPDFPageDocumentHelper(q).getAllPages();
    if (page < 0 || page >= static_cast<int>(pages.size())) { if (err) *err = "Sayfa yok: " + std::to_string(page + 1); return false; }
    QPDFPageObjectHelper ph = pages[static_cast<std::size_t>(page)];
    QPDFObjectHandle box = ph.getAttribute("/CropBox", false);
    if (!box.isArray() || box.getArrayNItems() != 4) box = ph.getAttribute("/MediaBox", false);
    double x0 = 0, y0 = 0, x1 = 612, y1 = 792;
    if (box.isArray() && box.getArrayNItems() == 4) {
      x0 = numOf(box.getArrayItem(0)); y0 = numOf(box.getArrayItem(1)); x1 = numOf(box.getArrayItem(2)); y1 = numOf(box.getArrayItem(3));
      if (x1 < x0) std::swap(x0, x1);
      if (y1 < y0) std::swap(y0, y1);
    }
    const double w = x1 - x0, h = y1 - y0;
    int rot = static_cast<int>(numOf(ph.getAttribute("/Rotate", false), 0)) % 360;
    if (rot < 0) rot += 360;
    Affine2 r = kIdentityAffine;   // page user space (relative to box) -> displayed
    double W = w, H = h;
    switch (rot) {
      case 90: r = {0, -1, 1, 0, 0, w}; W = h; H = w; break;
      case 180: r = {-1, 0, 0, -1, w, h}; break;
      case 270: r = {0, 1, -1, 0, h, 0}; W = h; H = w; break;
      default: break;
    }
    const Affine2 p2s = compose(compose(Affine2{1, 0, 0, 1, -x0, -y0}, r), Affine2{kPtToMm, 0, 0, kPtToMm, 0, 0});
    stats.index = page;
    stats.box = {{0, 0}, {W * kPtToMm, H * kPtToMm}};
    out.sheet = stats.box;
    std::map<QPDFObjGen, std::unique_ptr<Font>> fonts;
    std::map<std::string, int> warn;
    Interp in(&out, &nextId, &stats, p2s, width(stats.box) * height(stats.box), o, &fonts, &warn);
    in.res = ph.getAttribute("/Resources", false);
    ph.parseContents(&in);
    const double pageArea = width(stats.box) * height(stats.box);
    stats.imageCoverage = pageArea > 0 ? stats.largestImageArea / pageArea : 0;
    for (auto& [k, v] : warn) {
      Diagnostic d; d.severity = Severity::Warning; d.stage = static_cast<int>(Stage::Ingest);
      const std::size_t c = k.find(':');
      d.code = c == std::string::npos ? k : k.substr(0, c);
      const std::string arg = c == std::string::npos ? "" : k.substr(c + 1);
      if (d.code == "pdf.font.noToUnicode") { d.severity = Severity::Info; d.message = "Yazı tipi ToUnicode içermiyor: " + arg + " (glif adları / kodlama kullanıldı)"; }
      else if (d.code == "pdf.op.unknown") d.message = "Bilinmeyen PDF işleci: " + arg + " x" + std::to_string(v);
      else if (d.code == "pdf.op.malformed") d.message = "Bozuk işlenen içeren işleç x" + std::to_string(v);
      else if (d.code == "pdf.form.depth") d.message = "Form XObject derinliği 8'i aştı";
      else if (d.code == "pdf.op.shading") { d.severity = Severity::Info; d.message = "Gölgelendirme (sh) yok sayıldı x" + std::to_string(v); }
      else d.message = k + " x" + std::to_string(v);
      out.diagnostics.push_back(d);
    }
    return true;
  } catch (const std::exception& e) {
    if (err) *err = std::string("PDF okunamadı: ") + e.what();
    return false;
  }
}

}  // namespace pc::drawing::pdf
