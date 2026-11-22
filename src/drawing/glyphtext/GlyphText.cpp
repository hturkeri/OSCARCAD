// R1: glyph-outline text recognition for vector PDFs (see GlyphText.h, docs/drawing_import/R1_GLYPHTEXT.md).
#include "glyphtext/GlyphText.h"
#include "ocr/Ocr.h"
#include "pc/drawing/DimGrammar.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numeric>
#include <set>

namespace pc::drawing::glyphtext {

namespace {

constexpr double kPi = 3.14159265358979323846;

Vec2 rot(Vec2 p, double c, double s) { return {p.x * c - p.y * s, p.x * s + p.y * c}; }   // frame -> sheet
Vec2 unrot(Vec2 p, double c, double s) { return {p.x * c + p.y * s, -p.x * s + p.y * c}; } // sheet -> frame

Box2 boxOf(const std::vector<Vec2>& v) { Box2 b = emptyBox(); for (auto& p : v) expand(b, p); return b; }
double shoelace(const std::vector<Vec2>& v) {
  double a = 0;
  for (std::size_t i = 0, n = v.size(); i < n; ++i) a += cross(v[i], v[(i + 1) % n]);
  return 0.5 * a;
}
bool boxInside(const Box2& in, const Box2& out, double tol) {
  return in.lo.x >= out.lo.x - tol && in.lo.y >= out.lo.y - tol && in.hi.x <= out.hi.x + tol && in.hi.y <= out.hi.y + tol;
}
Vec2 centerOf(const Box2& b) { return {(b.lo.x + b.hi.x) / 2, (b.lo.y + b.hi.y) / 2}; }
Vec2 centroid(const std::vector<Vec2>& v) {   // area centroid of a simple polygon (vertex mean if degenerate)
  double a = 0, cx = 0, cy = 0;
  for (std::size_t i = 0, n = v.size(); i < n; ++i) {
    const Vec2 p = v[i], q = v[(i + 1) % n];
    const double c = cross(p, q);
    a += c; cx += (p.x + q.x) * c; cy += (p.y + q.y) * c;
  }
  if (std::abs(a) < 1e-12) { Vec2 m{0, 0}; for (auto& p : v) m = m + p; return v.empty() ? m : m * (1.0 / v.size()); }
  return {cx / (3 * a), cy / (3 * a)};
}

std::size_t utf8Len(const std::string& s) {
  std::size_t n = 0;
  for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
  return n;
}
std::vector<std::string> utf8Split(const std::string& s) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const std::size_t l = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
    out.push_back(s.substr(i, l));
    i += l;
  }
  return out;
}
std::string canonChar(const std::string& c) {
  if (c == "\xE2\x8C\x80" || c == "\xC3\xB8") return "\xC3\x98";   // ⌀ ø -> Ø
  if (c == "\xE2\x80\x94" || c == "\xE2\x80\x93") return "-";       // dashes
  if (c == "\xE2\x80\x99" || c == "\xE2\x80\x98") return "'";
  return c;
}
std::string stripSpaces(const std::string& s) { std::string o; for (char c : s) if (c != ' ') o.push_back(c); return o; }

// one glyph-outline string (one fill operator)
struct Group {
  std::vector<std::size_t> prims;      // indices into sheet.prims
  std::vector<Contour> contours;       // sheet mm
  std::vector<std::vector<Vec2>> fpts, fctrl;  // text-frame copies (rotated by -rotDeg)
  double rotDeg = 0, c = 1, s = 0;
  bool rotKnown = false;
  std::vector<Glyph> glyphs;           // reading order
  double capH = 0, baseY = 0;          // text frame
  bool accepted = false;
  std::string text; float conf = 0; std::vector<TextHypothesis> alts; std::string method;
};

void flattenPrim(const Primitive& p, Contour& c) {
  if (p.kind == PrimKind::Polyline) { c.pts = p.pts; c.ctrl = p.pts; }
  else {
    c.ctrl = p.pts;
    if (p.pts.empty()) return;
    c.pts.push_back(p.pts[0]);
    for (std::size_t i = 1; i + 2 < p.pts.size(); i += 3) {
      const Vec2 a = p.pts[i - 1], b = p.pts[i], cc = p.pts[i + 1], d = p.pts[i + 2];
      const bool straight = distance(a + (d - a) * (1.0 / 3), b) < 1e-9 && distance(a + (d - a) * (2.0 / 3), cc) < 1e-9;
      const int n = straight ? 1 : 8;
      for (int k = 1; k <= n; ++k) {
        const double t = static_cast<double>(k) / n, u = 1 - t;
        c.pts.push_back(a * (u * u * u) + b * (3 * u * u * t) + cc * (3 * u * t * t) + d * (t * t * t));
      }
    }
    if (c.pts.size() > 2 && distance(c.pts.front(), c.pts.back()) < 1e-9) c.pts.pop_back();
  }
  c.box = boxOf(c.pts);
  c.area = shoelace(c.pts);
}

void setRot(Group& g, double deg) {
  g.rotDeg = deg; g.c = std::cos(deg * kPi / 180); g.s = std::sin(deg * kPi / 180);
  if (std::abs(g.c) < 1e-12) g.c = 0;
  if (std::abs(g.s) < 1e-12) g.s = 0;
  g.fpts.clear(); g.fctrl.clear();
  for (auto& ct : g.contours) {
    std::vector<Vec2> a, b;
    for (auto& p : ct.pts) a.push_back(unrot(p, g.c, g.s));
    for (auto& p : ct.ctrl) b.push_back(unrot(p, g.c, g.s));
    g.fpts.push_back(std::move(a)); g.fctrl.push_back(std::move(b));
  }
}

// pieces = outer contours with the contours nested in them (holes, inner islands)
std::vector<std::vector<int>> pieces(const Group& g) {
  const int n = static_cast<int>(g.contours.size());
  std::vector<int> parent(n, -1);
  for (int i = 0; i < n; ++i) {
    double best = 1e300;
    for (int j = 0; j < n; ++j) {
      if (j == i) continue;
      const double ai = std::abs(g.contours[i].area), aj = std::abs(g.contours[j].area);
      if (aj <= ai) continue;
      if (!boxInside(g.contours[i].box, g.contours[j].box, 1e-6)) continue;
      if (aj < best) { best = aj; parent[i] = j; }
    }
  }
  auto root = [&](int i) { int k = 0; while (parent[i] >= 0 && k++ < n) i = parent[i]; return i; };
  std::map<int, std::vector<int>> m;
  for (int i = 0; i < n; ++i) m[root(i)].push_back(i);
  std::vector<std::vector<int>> out;
  for (auto& [r, v] : m) out.push_back(v);
  std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.front() < b.front(); });  // stream order
  return out;
}

void buildGlyphs(Group& g) {
  auto ps = pieces(g);
  struct P { std::vector<int> cs; Box2 b; int first; };
  std::vector<P> v;
  for (auto& pc : ps) {
    P p; p.cs = pc; p.b = emptyBox(); p.first = *std::min_element(pc.begin(), pc.end());
    for (int ci : pc) for (auto& q : g.fpts[ci]) expand(p.b, q);
    v.push_back(p);
  }
  // merge pieces overlapping in x (i/j dots, '=', ':', '%', 'Ø' slash ends) in the text frame
  std::sort(v.begin(), v.end(), [](const P& a, const P& b) { return a.first < b.first; });
  std::vector<P> merged;
  for (auto& p : v) {
    bool done = false;
    for (auto& m : merged) {
      const double ov = std::min(m.b.hi.x, p.b.hi.x) - std::max(m.b.lo.x, p.b.lo.x);
      const double mw = std::max(1e-9, std::min(width(m.b), width(p.b)));
      if (ov > 0.5 * mw) {
        m.cs.insert(m.cs.end(), p.cs.begin(), p.cs.end());
        expand(m.b, p.b.lo); expand(m.b, p.b.hi);
        done = true; break;
      }
    }
    if (!done) merged.push_back(p);
  }
  std::sort(merged.begin(), merged.end(), [](const P& a, const P& b) { return a.b.lo.x + a.b.hi.x < b.b.lo.x + b.b.hi.x; });
  g.glyphs.clear();
  for (auto& m : merged) {
    Glyph gl; gl.contours = m.cs; std::sort(gl.contours.begin(), gl.contours.end()); gl.box = m.b;
    // shape features (orientation-independent counts; layout in the text frame)
    std::vector<int> outer, hole;
    for (int ci : gl.contours) {
      bool nested = false;
      for (int cj : gl.contours)
        if (cj != ci && std::abs(g.contours[cj].area) > std::abs(g.contours[ci].area) && boxInside(g.contours[ci].box, g.contours[cj].box, 1e-6))
          nested = true;
      (nested ? hole : outer).push_back(ci);
    }
    gl.outers = static_cast<int>(outer.size()); gl.holes = static_cast<int>(hole.size());
    if (gl.outers == 1 && gl.holes == 2) {
      const Vec2 d = centroid(g.fpts[hole[1]]) - centroid(g.fpts[hole[0]]);
      // Ø: the slash splits the bowl into an upper-left and a lower-right half (8, B: stacked holes)
      gl.hole2diag = std::abs(d.x) > 0.12 * width(gl.box) && std::abs(d.y) > 0.12 * height(gl.box) && d.x * d.y < 0;
    }
    gl.ring = gl.outers == 1 && gl.holes == 1;
    g.glyphs.push_back(gl);
  }
}

bool trivialGlyph(const Group& g, const Glyph& gl) {
  if (gl.contours.size() != 1) return false;
  const auto& c = g.contours[gl.contours[0]];
  return c.ctrl.size() <= 4 && c.ctrl.size() == c.pts.size();   // triangle / quad without curves (arrowhead, tick)
}

void measureLine(Group& g) {
  std::vector<double> hs, bottoms;
  double maxH = 0;
  for (auto& gl : g.glyphs) maxH = std::max(maxH, height(gl.box));
  for (auto& gl : g.glyphs) if (height(gl.box) >= 0.55 * maxH) { hs.push_back(height(gl.box)); bottoms.push_back(gl.box.lo.y); }
  if (hs.empty()) { g.capH = maxH; g.baseY = 0; return; }
  std::sort(hs.begin(), hs.end()); std::sort(bottoms.begin(), bottoms.end());
  g.capH = hs[hs.size() / 2];
  g.baseY = bottoms[bottoms.size() / 2];
}

// glyph outline in the text frame relative to its own box corner, scaled by the box's larger side: the same font
// glyph at another size has the same signature (control points: exact up to print rounding)
struct Sig { std::vector<int> counts; std::vector<Vec2> pts; double w = 0, h = 0; };
Sig sigOf(const Group& g, const Glyph& gl) {
  Sig s;
  Box2 b = emptyBox();
  for (int ci : gl.contours) for (auto& p : g.fctrl[ci]) expand(b, p);
  const double k = 1.0 / std::max(1e-9, std::max(width(b), height(b)));
  for (int ci : gl.contours) {   // stream order of a glyph's contours is fixed by its font program
    // start point and direction of a contour are not (the same glyph is written CW in one string and CCW in another):
    // drop the closing duplicate, orient CCW, start the cycle at the lowest-then-leftmost control point
    std::vector<Vec2> c;
    for (auto& p : g.fctrl[ci]) c.push_back((p - b.lo) * k);
    if (c.size() > 1 && distance(c.front(), c.back()) < 1e-9) c.pop_back();
    if (shoelace(c) < 0) std::reverse(c.begin(), c.end());
    std::size_t st = 0;
    for (std::size_t i = 1; i < c.size(); ++i) {
      const double dy = c[i].y - c[st].y;
      if (dy < -1e-4 || (dy <= 1e-4 && c[i].x < c[st].x - 1e-4)) st = i;
    }
    std::rotate(c.begin(), c.begin() + static_cast<std::ptrdiff_t>(st), c.end());
    s.counts.push_back(static_cast<int>(c.size()));
    s.pts.insert(s.pts.end(), c.begin(), c.end());
  }
  s.w = width(b) * k; s.h = height(b) * k;
  return s;
}
bool sameSig(const Sig& a, const Sig& b, double tol) {
  if (a.counts != b.counts || std::abs(a.w - b.w) > tol || std::abs(a.h - b.h) > tol) return false;
  for (std::size_t i = 0; i < a.pts.size(); ++i)
    if (std::abs(a.pts[i].x - b.pts[i].x) > tol || std::abs(a.pts[i].y - b.pts[i].y) > tol) return false;
  return true;
}

struct ClassInfo {
  Sig sig;
  std::map<std::string, double> votes;
  std::string label;
  float labelConf = 0;
  int count = 0, diag = 0, ring = 0, raisedRing = 0;
  int exGroup = -1, exGlyph = -1;     // example occurrence (for single-glyph OCR)
};

int classify(std::vector<ClassInfo>& cls, const Sig& s, double tol, bool create) {
  for (std::size_t i = 0; i < cls.size(); ++i) if (sameSig(cls[i].sig, s, tol)) return static_cast<int>(i);
  if (!create) return -1;
  cls.push_back(ClassInfo{}); cls.back().sig = s;
  return static_cast<int>(cls.size()) - 1;
}

}  // namespace

Gray rasterize(const std::vector<const std::vector<Vec2>*>& polys, const Box2& box, double scale, int pad, bool evenOdd) {
  Gray img;
  img.w = static_cast<int>(std::ceil(width(box) * scale)) + 2 * pad;
  img.h = static_cast<int>(std::ceil(height(box) * scale)) + 2 * pad;
  img.w = std::clamp(img.w, 1, 8000); img.h = std::clamp(img.h, 1, 4000);
  constexpr int SS = 4;
  std::vector<uint16_t> cov(static_cast<std::size_t>(img.w) * img.h, 0);
  struct E { double x0, y0, x1, y1; int dir; };
  std::vector<E> edges;
  for (auto* pv : polys) {
    const auto& v = *pv;
    for (std::size_t i = 0, n = v.size(); i < n; ++i) {
      const Vec2 a = v[i], b = v[(i + 1) % n];
      // to pixel space: x right, y down
      const double ax = (a.x - box.lo.x) * scale + pad, ay = (box.hi.y - a.y) * scale + pad;
      const double bx = (b.x - box.lo.x) * scale + pad, by = (box.hi.y - b.y) * scale + pad;
      if (ay == by) continue;
      edges.push_back(ay < by ? E{ax, ay, bx, by, 1} : E{bx, by, ax, ay, -1});
    }
  }
  std::vector<std::pair<double, int>> xs;
  for (int sy = 0; sy < img.h * SS; ++sy) {
    const double y = (sy + 0.5) / SS;
    xs.clear();
    for (auto& e : edges)
      if (y >= e.y0 && y < e.y1) xs.push_back({e.x0 + (y - e.y0) / (e.y1 - e.y0) * (e.x1 - e.x0), e.dir});
    if (xs.empty()) continue;
    std::sort(xs.begin(), xs.end());
    int wind = 0;
    uint16_t* row = cov.data() + static_cast<std::size_t>(sy / SS) * img.w;
    for (std::size_t k = 0; k + 1 < xs.size(); ++k) {
      wind += evenOdd ? 1 : xs[k].second;
      if (evenOdd ? (wind & 1) == 0 : wind == 0) continue;
      const int s0 = std::max(0, static_cast<int>(std::ceil(xs[k].first * SS - 0.5)));
      const int s1 = std::min(img.w * SS - 1, static_cast<int>(std::floor(xs[k + 1].first * SS - 0.5)));
      for (int sx = s0; sx <= s1; ++sx) ++row[sx / SS];
    }
  }
  img.px.resize(cov.size());
  for (std::size_t i = 0; i < cov.size(); ++i) img.px[i] = static_cast<uint8_t>(255 - std::min(255, cov[i] * 255 / (SS * SS)));
  return img;
}

namespace {

struct Recognizer {
  std::unique_ptr<ocr::Engine> eng;
  int calls = 0;
  ocr::Result run(const Gray& img, int psm, const std::string& wl) {
    ++calls;
    ocr::GrayView v; v.width = img.w; v.height = img.h; v.stride = static_cast<std::size_t>(img.w); v.data = img.px.data();
    return eng->recognize(v, psm, wl, 300, 4);
  }
};

Gray renderGlyphs(const Group& g, const std::vector<int>& glyphIdx, double capPx, int pad, Box2* used = nullptr) {
  std::vector<const std::vector<Vec2>*> polys;
  Box2 b = emptyBox();
  for (int gi : glyphIdx)
    for (int ci : g.glyphs[gi].contours) { polys.push_back(&g.fpts[ci]); for (auto& p : g.fpts[ci]) expand(b, p); }
  // keep the line metrics in the image so case / position are visible to the recogniser
  expand(b, {b.lo.x, g.baseY}); expand(b, {b.lo.x, g.baseY + g.capH});
  if (used) *used = b;
  const double scale = capPx / std::max(1e-6, g.capH);
  return rasterize(polys, b, scale, pad);
}

std::vector<int> allGlyphs(const Group& g) { std::vector<int> v(g.glyphs.size()); std::iota(v.begin(), v.end(), 0); return v; }

// visual gap between glyph i-1 and i: closest horizontal approach of the ink over cap-height scanlines
// (box gaps under-estimate spaces between slanted glyphs: "REV A", "SHEET A3")
double inkGap(const Group& g, std::size_t i) {
  const Glyph& A = g.glyphs[i - 1];
  const Glyph& B = g.glyphs[i];
  auto extent = [&](const Glyph& gl, double y, bool right) {
    double v = right ? -1e300 : 1e300;
    for (int ci : gl.contours) {
      const auto& p = g.fpts[ci];
      for (std::size_t k = 0, n = p.size(); k < n; ++k) {
        const Vec2 a = p[k], b = p[(k + 1) % n];
        if ((a.y <= y) == (b.y <= y)) continue;
        const double x = a.x + (y - a.y) / (b.y - a.y) * (b.x - a.x);
        v = right ? std::max(v, x) : std::min(v, x);
      }
    }
    return v;
  };
  std::vector<double> gaps;
  for (int k = 0; k < 12; ++k) {
    const double y = g.baseY + (k + 0.5) / 12 * g.capH;
    const double ra = extent(A, y, true), lb = extent(B, y, false);
    if (ra > -1e299 && lb < 1e299) gaps.push_back(lb - ra);
  }
  if (gaps.size() < 4) return B.box.lo.x - A.box.hi.x;   // '.', '-', '°' share too few scanlines: box gap
  return std::max(B.box.lo.x - A.box.hi.x, *std::min_element(gaps.begin(), gaps.end()));
}

// characters from the class labels; spaces from the glyph gaps, with the OCR word breaks deciding ambiguous gaps
std::string compose(const Group& g, const std::vector<ClassInfo>& cls, const ocr::Result& line, float* conf) {
  std::set<std::size_t> ocrSpace;
  bool ocrAligned = false;
  if (line.ok) {
    std::size_t k = 0;
    bool pend = false;
    for (const auto& ch : utf8Split(line.text)) {
      if (ch == " ") { pend = k > 0; continue; }
      if (pend) ocrSpace.insert(k);
      pend = false;
      ++k;
    }
    ocrAligned = k == g.glyphs.size();
  }
  std::string out;
  double cs = 0;
  for (std::size_t i = 0; i < g.glyphs.size(); ++i) {
    const auto& gl = g.glyphs[i];
    if (gl.cls < 0 || cls[gl.cls].label.empty()) return {};
    if (i > 0) {
      const double gap = inkGap(g, i) / std::max(1e-9, g.capH);
      const bool sp = ocrAligned ? (gap > 0.7 || (gap > 0.25 && ocrSpace.count(i))) : gap > 0.45;
      if (sp) out.push_back(' ');
    }
    out += cls[gl.cls].label;
    cs += cls[gl.cls].labelConf;
  }
  if (conf) *conf = g.glyphs.empty() ? 0.f : static_cast<float>(cs / g.glyphs.size());
  return out;
}

bool dimLike(const std::string& t) {
  if (t.empty() || utf8Len(t) > 14) return false;
  int digits = 0;
  for (char ch : t) digits += (ch >= '0' && ch <= '9');
  return digits > 0;
}

}  // namespace

Stats extractGlyphText(PrimitiveSheet& sheet, int& nextId, const Options& o) {
  Stats st;
  if (!o.enabled) { st.note = "disabled"; return st; }
  // ---- 1. candidate fill operators: consecutive fill-only closed primitives with the same provenance ref
  std::vector<Group> groups;
  for (std::size_t i = 0; i < sheet.prims.size();) {
    const Primitive& p = sheet.prims[i];
    const bool fillOnly = p.style.filled && !p.style.stroked && p.closed && (p.kind == PrimKind::Polyline || p.kind == PrimKind::Bezier);
    if (!fillOnly) { ++i; continue; }
    Group g;
    std::size_t j = i;
    while (j < sheet.prims.size()) {
      const Primitive& q = sheet.prims[j];
      if (!(q.style.filled && !q.style.stroked && q.closed && (q.kind == PrimKind::Polyline || q.kind == PrimKind::Bezier)) ||
          q.prov.ref != p.prov.ref)
        break;
      g.prims.push_back(j);
      ++j;
    }
    i = j;
    bool ok = true;
    for (std::size_t k : g.prims) {
      Contour c; flattenPrim(sheet.prims[k], c);
      if (c.pts.size() < 3) { ok = false; break; }
      if (std::max(width(c.box), height(c.box)) > o.maxGlyphMm) { ok = false; break; }
      g.contours.push_back(std::move(c));
    }
    if (ok && !g.contours.empty()) groups.push_back(std::move(g));
  }
  // ---- 2. orientation + glyph segmentation
  int multi = 0;
  for (auto& g : groups) {
    setRot(g, 0);
    auto ps = pieces(g);
    if (ps.size() >= 2) {
      // baseline axis from the piece centres (PCA); the reading sense (0 vs 180) is decided by OCR in step 3
      std::vector<Vec2> cs;
      for (auto& pc : ps) { Box2 b = emptyBox(); for (int ci : pc) { expand(b, g.contours[ci].box.lo); expand(b, g.contours[ci].box.hi); } cs.push_back(centerOf(b)); }
      Vec2 m{0, 0}; for (auto& c : cs) m = m + c; m = m * (1.0 / cs.size());
      double sxx = 0, syy = 0, sxy = 0;
      for (auto& c : cs) { const Vec2 d = c - m; sxx += d.x * d.x; syy += d.y * d.y; sxy += d.x * d.y; }
      const double th = 0.5 * std::atan2(2 * sxy, sxx - syy);
      Vec2 u{std::cos(th), std::sin(th)};
      double deg = std::atan2(u.y, u.x) * 180 / kPi;
      const double snap = std::round(deg / 90) * 90;
      // short strings ("90°", "Ø2") give a noisy axis: snap harder to the drawing axes
      if (std::abs(deg - snap) < (ps.size() <= 4 ? 16.0 : 4.0)) deg = snap;
      if (deg < -90 + 1e-9) deg += 180;   // axis in (-90, 90]
      if (deg > 90 + 1e-9) deg -= 180;
      if (deg <= -180) deg += 360;
      if (deg > 180) deg -= 360;
      setRot(g, deg);
      buildGlyphs(g);
      if (g.glyphs.size() >= 2) g.rotKnown = true;
      else { setRot(g, 0); buildGlyphs(g); }
    } else buildGlyphs(g);
    measureLine(g);
    // plausibility
    bool ok = !g.glyphs.empty();
    int nontrivial = 0;
    for (std::size_t k = 0; k < g.glyphs.size() && ok; ++k) {
      const auto& gl = g.glyphs[k];
      const double ext = std::max(width(gl.box), height(gl.box));
      if (ext > o.maxGlyphMm) ok = false;
      if (!trivialGlyph(g, gl)) ++nontrivial;
      if (k > 0 && gl.box.lo.x - g.glyphs[k - 1].box.hi.x > 4 * g.capH) ok = false;   // not one string
    }
    if (g.capH < o.minGlyphMm || nontrivial == 0) ok = false;
    g.accepted = ok;
    if (ok && g.rotKnown && g.glyphs.size() >= 3 && nontrivial >= 2) ++multi;
  }
  if (multi < o.minMultiGlyphGroups) { st.note = "no glyph text"; return st; }
  st.ocrAvailable = ocr::available();
  Recognizer R;
  if (st.ocrAvailable) {
    R.eng = std::make_unique<ocr::Engine>(o.tessdataDir, "eng");
    if (!R.eng->ok()) st.ocrAvailable = false;
  }
  if (!st.ocrAvailable) { st.note = "ocr unavailable"; return st; }

  // ---- 3. reading sense of multi-glyph strings: OCR the line both ways
  std::vector<ocr::Result> lineRes(groups.size());
  std::vector<std::array<ocr::Result, 2>> both(groups.size());
  std::vector<std::array<double, 2>> senseDeg(groups.size());
  std::vector<char> confident(groups.size(), 0);
  std::vector<int> chosenSense(groups.size(), 0);
  const int pad = static_cast<int>(o.renderCapPx * 0.6);
  auto orient = [&](Group& g, double d) { setRot(g, d); buildGlyphs(g); measureLine(g); };
  for (std::size_t gi = 0; gi < groups.size(); ++gi) {
    auto& g = groups[gi];
    if (!g.accepted || !g.rotKnown) continue;
    const double d0 = g.rotDeg, d1 = d0 > 0 ? d0 - 180 : d0 + 180;
    senseDeg[gi] = {d0, d1};
    both[gi][0] = R.run(renderGlyphs(g, allGlyphs(g), o.renderCapPx, pad), 7, "");
    orient(g, d1);
    both[gi][1] = R.run(renderGlyphs(g, allGlyphs(g), o.renderCapPx, pad), 7, "");
    const float s0 = both[gi][0].ok ? both[gi][0].conf + 0.02f : -1.f, s1 = both[gi][1].ok ? both[gi][1].conf : -1.f;
    const int k = s0 >= s1 ? 0 : 1;   // tie -> axis in (-90, 90]
    orient(g, senseDeg[gi][k]);
    lineRes[gi] = both[gi][k];
    chosenSense[gi] = k;
    // long lines: Tesseract's sense decision is reliable; short ones ("Ø50", "Ø3.2") are decided by outline matches
    confident[gi] = g.glyphs.size() >= 5;
    if (g.glyphs.size() < 2) g.rotKnown = false;
  }

  // ---- 4. signature classes + votes (a line reading with one character per glyph labels the glyph classes)
  std::vector<ClassInfo> cls;
  auto tolOf = [](const Group&) { return 0.006; };   // relative to the glyph size
  auto addClasses = [&](std::size_t gi) {
    auto& g = groups[gi];
    for (std::size_t k = 0; k < g.glyphs.size(); ++k) {
      auto& gl = g.glyphs[k];
      gl.cls = classify(cls, sigOf(g, gl), tolOf(g), true);
      auto& c = cls[gl.cls];
      if (c.exGroup < 0) { c.exGroup = static_cast<int>(gi); c.exGlyph = static_cast<int>(k); }
    }
    if (!lineRes[gi].ok) return;
    const auto chars = utf8Split(stripSpaces(lineRes[gi].text));
    if (chars.size() != g.glyphs.size()) return;
    for (std::size_t k = 0; k < chars.size(); ++k) cls[g.glyphs[k].cls].votes[canonChar(chars[k])] += 0.2 + lineRes[gi].conf;
  };
  auto labelClasses = [&]() {
    for (auto& c : cls) { c.count = c.diag = c.ring = c.raisedRing = 0; }
    for (auto& g : groups) {
      if (!g.accepted) continue;
      for (auto& gl : g.glyphs) {
        if (gl.cls < 0) continue;
        auto& c = cls[gl.cls];
        ++c.count;
        c.diag += gl.hole2diag;
        if (gl.ring) { ++c.ring; if (g.glyphs.size() >= 2 && gl.box.lo.y > g.baseY + 0.4 * g.capH && height(gl.box) < 0.6 * g.capH) ++c.raisedRing; }
      }
    }
    for (auto& c : cls) {
      if (c.votes.empty() && c.count == 0) continue;
      double tot = 0, best = 0;
      c.label.clear();
      for (auto& [ch, v] : c.votes) { tot += v; if (v > best) { best = v; c.label = ch; } }
      c.labelConf = tot > 0 ? static_cast<float>(std::min(1.0, best / tot) * std::min(1.0, 0.5 + 0.25 * tot)) : 0.f;
      if (c.count > 0 && 2 * c.diag > c.count) { c.label = "\xC3\x98"; c.labelConf = std::max(c.labelConf, 0.9f); }   // Ø
      else if (c.count > 0 && 2 * c.raisedRing > c.count) { c.label = "\xC2\xB0"; c.labelConf = std::max(c.labelConf, 0.9f); }  // °
    }
  };
  for (std::size_t gi = 0; gi < groups.size(); ++gi)
    if (groups[gi].accepted && groups[gi].rotKnown && confident[gi]) addClasses(gi);
  labelClasses();
  // short strings with an unclear OCR sense ("Ø8", "Ø3.2": Tesseract is weak on Ø): the sense in which more glyphs
  // match already labelled outlines wins (outlines are compared point by point, so a flipped glyph does not match)
  for (std::size_t gi = 0; gi < groups.size(); ++gi) {
    auto& g = groups[gi];
    if (!g.accepted || !g.rotKnown || confident[gi]) continue;
    int score[2] = {0, 0};
    for (int k = 0; k < 2; ++k) {
      orient(g, senseDeg[gi][k]);
      for (auto& gl : g.glyphs) {
        const int c = classify(cls, sigOf(g, gl), tolOf(g), false);
        score[k] += c >= 0 && !cls[c].label.empty();
      }
    }
    const int k = score[0] == score[1] ? chosenSense[gi] : score[0] > score[1] ? 0 : 1;
    orient(g, senseDeg[gi][k]);
    lineRes[gi] = both[gi][k];
    if (g.glyphs.size() < 2) { g.rotKnown = false; continue; }
    addClasses(gi);
  }
  labelClasses();
  // single-piece strings: try the known classes in 0, 90, 270, 180 orientation (vertical dims are rot 90)
  for (auto& g : groups) {
    if (!g.accepted || g.rotKnown) continue;
    for (double d : {0.0, 90.0, -90.0, 180.0}) {
      orient(g, d);
      if (g.glyphs.size() != 1) continue;
      const int c = classify(cls, sigOf(g, g.glyphs[0]), tolOf(g), false);
      if (c >= 0) { g.glyphs[0].cls = c; g.rotKnown = true; break; }
    }
    if (!g.rotKnown) orient(g, 0);
  }
  labelClasses();
  // unlabeled classes seen in multi-glyph strings: OCR the glyph alone (single character mode)
  for (auto& c : cls) {
    if (!c.label.empty() || c.exGroup < 0) continue;
    const auto& g = groups[c.exGroup];
    const Gray img = renderGlyphs(g, {c.exGlyph}, o.renderCapPx, static_cast<int>(o.renderCapPx));
    const auto r = R.run(img, 10, "");
    const auto ch = utf8Split(stripSpaces(r.text));
    if (ch.size() == 1) { c.label = canonChar(ch[0]); c.labelConf = 0.5f * r.conf; }
  }
  for (auto& c : cls) if (!c.label.empty()) ++st.labeledClasses;
  st.glyphClasses = static_cast<int>(cls.size());

  // ---- 6. text per string
  for (std::size_t gi = 0; gi < groups.size(); ++gi) {
    auto& g = groups[gi];
    if (!g.accepted) continue;
    if (!g.rotKnown) {
      // isolated glyph with an unknown outline: OCR in the four orientations, keep a confident alphanumeric
      ocr::Result best; double bestRot = 0; float bestScore = -1;
      for (double d : {0.0, 90.0, -90.0, 180.0}) {
        setRot(g, d); buildGlyphs(g); measureLine(g);
        if (g.glyphs.size() != 1) continue;
        g.baseY = g.glyphs[0].box.lo.y; g.capH = height(g.glyphs[0].box);
        const auto r = R.run(renderGlyphs(g, {0}, o.renderCapPx, static_cast<int>(o.renderCapPx)), 10,
                             "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        const float score = r.conf - (d == 0 ? 0.f : d == 90 ? 0.02f : 0.04f);
        if (r.ok && utf8Len(stripSpaces(r.text)) == 1 && score > bestScore) { bestScore = score; best = r; bestRot = d; }
      }
      setRot(g, bestRot); buildGlyphs(g); measureLine(g);
      if (bestScore < 0.6f) { g.accepted = false; continue; }
      g.text = stripSpaces(best.text); g.conf = best.conf * 0.8f; g.method = "glyph-ocr";
      continue;
    }
    float cconf = 0;
    const ocr::Result& lr = lineRes[gi];
    const std::string composed = compose(g, cls, lr, &cconf);
    if (!composed.empty()) {
      g.text = composed; g.conf = cconf; g.method = "signature";
      if (lr.ok && lr.text != composed) g.alts.push_back({lr.text, lr.conf});
    } else if (lr.ok) {
      g.text = lr.text; g.conf = lr.conf * 0.8f; g.method = "line-ocr";
      g.alts = lr.alternatives;
      if (dimLike(lr.text)) {
        std::vector<TextHypothesis> hyps{{lr.text, lr.conf}};
        hyps.insert(hyps.end(), lr.alternatives.begin(), lr.alternatives.end());
        DimTextContext ctx; ctx.fromOcr = true;
        const int k = rankDimTextHypotheses(hyps, ctx);
        if (k > 0) { g.alts.erase(g.alts.begin() + (k - 1)); g.alts.insert(g.alts.begin(), {g.text, g.conf}); g.text = hyps[k].utf8; g.method = "line-ocr+grammar"; }
      }
    } else { g.accepted = false; }
  }

  // ---- 7. emit TextRuns, remove glyph primitives
  std::vector<char> drop(sheet.prims.size(), 0);
  for (auto& g : groups) {
    if (!g.accepted || g.text.empty()) continue;
    TextRun t;
    t.id = nextId++;
    t.utf8 = g.text;
    Box2 fb = emptyBox();
    for (auto& gl : g.glyphs) { expand(fb, gl.box.lo); expand(fb, gl.box.hi); }
    t.rotDeg = g.rotDeg;
    t.height = g.capH;
    t.origin = rot({fb.lo.x, g.baseY}, g.c, g.s);
    t.quad = {rot({fb.lo.x, fb.lo.y}, g.c, g.s), rot({fb.hi.x, fb.lo.y}, g.c, g.s), rot({fb.hi.x, fb.hi.y}, g.c, g.s),
              rot({fb.lo.x, fb.hi.y}, g.c, g.s)};
    t.font = "glyph-outline";
    t.asOutlines = true;
    t.alternatives = g.alts;
    t.conf = std::clamp(g.conf, 0.05f, 0.99f);
    const Primitive& p0 = sheet.prims[g.prims.front()];
    t.rgb = p0.attrs.count("pdf.fillRgb") ? static_cast<uint32_t>(std::stoul(p0.attrs.at("pdf.fillRgb"), nullptr, 16)) : p0.style.rgb;
    t.prov = p0.prov;
    t.attrs["glyphtext.method"] = g.method;
    t.attrs["glyphtext.glyphs"] = std::to_string(g.glyphs.size());
    for (std::size_t k : g.prims) { t.mergedFrom.push_back(sheet.prims[k].id); drop[k] = 1; }
    sheet.texts.push_back(std::move(t));
    ++st.runs; ++st.groups;
    st.glyphs += static_cast<int>(g.glyphs.size());
    st.removedPrims += static_cast<int>(g.prims.size());
  }
  std::vector<Primitive> keep;
  keep.reserve(sheet.prims.size());
  for (std::size_t i = 0; i < sheet.prims.size(); ++i) if (!drop[i]) keep.push_back(std::move(sheet.prims[i]));
  sheet.prims = std::move(keep);
  st.ocrCalls = R.calls;
  sheet.attrs["glyphtext.runs"] = std::to_string(st.runs);
  return st;
}

}  // namespace pc::drawing::glyphtext
