// DXF -> PrimitiveSheet (S1b, SPEC §2.2). OWNER WP-1.
#include "dxf/DxfReader.h"
#include "normalize/GeomUtil.h"
#include "pc/drawing/Confidence.h"
#include "pc/drawing/Stages.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace pc::drawing {
namespace {
using namespace dxf;
using namespace geom;

uint32_t aciToRgb(int aci) {
  switch (aci) {
    case 1: return 0xFF0000; case 2: return 0xFFFF00; case 3: return 0x00FF00; case 4: return 0x00FFFF;
    case 5: return 0x0000FF; case 6: return 0xFF00FF; case 8: return 0x808080; case 9: return 0xC0C0C0;
    default: return 0x000000;   // 7 = black/white, others approximated
  }
}

double insunitsToMm(int u, bool* known) {
  *known = true;
  switch (u) {
    case 1: return 25.4; case 2: return 304.8; case 4: return 1.0; case 5: return 10.0; case 6: return 1000.0;
    case 8: return 0.0000254; case 9: return 0.0254; case 10: return 914.4; case 14: return 100.0;
    default: *known = false; return 1.0;
  }
}

// Inherited properties when expanding blocks (BYBLOCK resolution).
struct Ctx {
  Affine2 m = kIdentityAffine;
  std::string layer = "0", linetype = "BYLAYER";
  int color = 7, lineweight = -3;
  double ltscale = 1;
  int depth = 0;
  std::string blockPath;           // "BLK1/BLK2"
  bool dimBlock = false;
};

struct Builder {
  const DxfFile& f;
  const IngestOptions& opt;
  PrimitiveSheet& out;
  int& nextId;
  double u2mm = 1, ltscale = 1;
  std::map<std::string, int> unsupported;
  std::vector<int>* collectPrims = nullptr;   // DIMENSION block explosion
  std::vector<int>* collectTexts = nullptr;
  int inserts = 0;

  std::string dec(const std::string& s) const { return decodeString(s, f); }

  const Layer* layerOf(const std::string& name) const {
    auto it = f.layers.find(upper(name));
    return it == f.layers.end() ? nullptr : &it->second;
  }

  // Effective properties of entity `e` in context `c`.
  void props(const Ent& e, const Ctx& c, std::string& layer, LineStyle& st) const {
    layer = e.str(8, "0");
    if (layer == "0" && c.depth > 0) layer = c.layer;
    const Layer* L = layerOf(layer);
    std::string lt = e.str(6, "BYLAYER");
    const std::string ltU = upper(lt);
    if (ltU == "BYBLOCK") lt = c.linetype;
    if (upper(lt) == "BYLAYER") lt = L ? L->linetype : "CONTINUOUS";
    if (upper(lt) == "BYBLOCK") lt = "CONTINUOUS";
    int color = e.integer(62, 256);
    if (color == 0) color = c.color;
    if (color == 256) color = L ? std::abs(L->color) : 7;
    int lw = e.integer(370, -1);
    if (lw == -2) lw = c.lineweight;
    if (lw == -1) lw = L ? L->lineweight : -3;
    st = LineStyle{};
    st.layer = layer;
    st.linetype = upper(lt) == "CONTINUOUS" ? std::string() : upper(lt);
    st.rgb = e.first(420) ? static_cast<uint32_t>(e.integer(420)) & 0xFFFFFF : aciToRgb(color);
    st.width = lw >= 0 ? lw / 100.0 : 0.0;
    if (!st.linetype.empty()) {
      auto it = f.ltypes.find(st.linetype);
      if (it != f.ltypes.end()) {
        const double sc = ltscale * e.num(48, 1.0) * c.ltscale * u2mm;
        for (double v : it->second.pattern) st.dash.push_back(std::abs(v) * sc);
        if (st.dash.size() % 2 == 1) st.dash.push_back(0);
      }
    }
  }

  Vec2 P(const Affine2& m, double x, double y) const { return transformPoint(m, {x * u2mm, y * u2mm}); }
  double scaleOf(const Affine2& m) const { return std::sqrt(std::abs(m[0] * m[3] - m[1] * m[2])); }
  bool mirrored(const Affine2& m) const { return m[0] * m[3] - m[1] * m[2] < 0; }
  bool uniform(const Affine2& m) const {
    const double sx = std::hypot(m[0], m[1]), sy = std::hypot(m[2], m[3]);
    return std::abs(sx - sy) <= 1e-9 * std::max(sx, sy) && std::abs(m[0] * m[2] + m[1] * m[3]) <= 1e-9 * sx * sy;
  }
  // OCS -> WCS for the common 2D case: extrusion (0,0,-1) mirrors x.
  Affine2 ocs(const Ent& e, const Affine2& m) const {
    if (e.num(230, 1.0) < 0) return compose(Affine2{-1, 0, 0, 1, 0, 0}, m);
    return m;
  }

  Primitive& add(PrimKind k, const Ent& e, const Ctx& c) {
    Primitive p; p.id = nextId++; p.kind = k; p.conf = kConfNative;
    std::string layer; props(e, c, layer, p.style);
    p.prov.source = SourceKind::Dxf; p.prov.page = 0;
    const std::string h = e.str(5);
    p.prov.ref = "DXF " + e.type + (h.empty() ? "" : " handle " + h);
    p.fromDimBlock = c.dimBlock;
    if (!c.blockPath.empty()) p.attrs["dxf.block"] = c.blockPath;
    out.prims.push_back(std::move(p));
    if (collectPrims) collectPrims->push_back(out.prims.back().id);
    return out.prims.back();
  }

  void line(const Ent& e, const Ctx& c, Vec2 a, Vec2 b) {
    if (distance(a, b) < 1e-12) return;
    auto& p = add(PrimKind::Line, e, c); p.p0 = a; p.p1 = b;
  }
  // Arc given in local (pre-transform) coordinates; m maps to sheet.
  void arcLocal(const Ent& e, const Ctx& c, const Affine2& m, Vec2 ctr, double r, double a0, double a1, bool full) {
    if (r <= 0) return;
    if (uniform(m)) {
      const double s = scaleOf(m);
      if (full) { auto& p = add(PrimKind::Circle, e, c); p.center = transformPoint(m, ctr); p.radius = r * s; p.a0 = 0; p.a1 = 360; return; }
      const Vec2 C = transformPoint(m, ctr), S = transformPoint(m, polar(ctr, r, a0)), E = transformPoint(m, polar(ctr, r, a1));
      double b0 = deg(std::atan2(S.y - C.y, S.x - C.x)), b1 = deg(std::atan2(E.y - C.y, E.x - C.x));
      if (mirrored(m)) std::swap(b0, b1);
      b0 = norm360(b0); b1 = norm360(b1);
      const double sweep = norm360(a1 - a0);
      if (b1 <= b0 + 1e-12) b1 += 360;
      if (sweep < 1e-9 && b1 - b0 > 359) b1 = b0 + 360;   // degenerate full sweep
      auto& p = add(PrimKind::Arc, e, c); p.center = C; p.radius = r * s; p.a0 = b0; p.a1 = b1;
      return;
    }
    // non-uniform scale: ellipse approximated by a polyline
    auto& p = add(PrimKind::Polyline, e, c);
    double s0 = full ? 0 : a0, s1 = full ? 360 : a1;
    if (s1 <= s0) s1 += 360;
    const int n = std::max(16, static_cast<int>((s1 - s0) / 3));
    for (int i = 0; i <= n; ++i) p.pts.push_back(transformPoint(m, polar(ctr, r, s0 + (s1 - s0) * i / n)));
    p.closed = full;
    p.attrs["dxf.approx"] = "nonUniformArc";
  }

  void bulgeSeg(const Ent& e, const Ctx& c, const Affine2& m, Vec2 p, Vec2 q, double b) {
    if (std::abs(b) < 1e-12) { line(e, c, transformPoint(m, p), transformPoint(m, q)); return; }
    const double chord = distance(p, q);
    if (chord < 1e-12) return;
    const double th = 4 * std::atan(std::abs(b));
    const double r = chord / (2 * std::sin(th / 2));
    const Vec2 mid = (p + q) * 0.5, d = unit(q - p);
    const Vec2 n = b > 0 ? Vec2{-d.y, d.x} : Vec2{d.y, -d.x};
    const double h = r * std::cos(th / 2);   // signed: th>pi -> centre on other side
    const Vec2 ctr = mid + n * h;
    double a0 = deg(std::atan2(p.y - ctr.y, p.x - ctr.x)), a1 = deg(std::atan2(q.y - ctr.y, q.x - ctr.x));
    if (b < 0) std::swap(a0, a1);
    a0 = norm360(a0); a1 = norm360(a1); if (a1 <= a0) a1 += 360;
    arcLocal(e, c, m, ctr, r, a0, a1, false);
  }

  void vertexChain(const Ent& e, const Ctx& c, const Affine2& m, const std::vector<Vec2>& v, const std::vector<double>& bulge,
                   bool closed) {
    const std::size_t n = v.size();
    for (std::size_t i = 0; i + 1 < n + (closed ? 1 : 0); ++i) {
      const Vec2 a = v[i], b = v[(i + 1) % n];
      bulgeSeg(e, c, m, a, b, i < bulge.size() ? bulge[i] : 0.0);
    }
  }

  // MTEXT/TEXT/ATTRIB
  void addText(const Ent& e, const Ctx& c, const std::string& utf8, Vec2 originLocal, double h, double rotLocalDeg, double widthFactor,
               const char* tag) {
    if (utf8.empty()) return;
    const Affine2& m = c.m;
    TextRun t; t.id = nextId++; t.utf8 = utf8; t.conf = kConfNative; t.fromDimBlock = c.dimBlock;
    const Vec2 o = P(m, originLocal.x, originLocal.y);
    const Vec2 dir = transformPoint(m, polar({0, 0}, 1, rotLocalDeg)) - transformPoint(m, {0, 0});
    const double s = scaleOf(m);
    t.origin = o; t.height = h * u2mm * s; t.rotDeg = norm360(deg(std::atan2(dir.y, dir.x)));
    if (t.rotDeg > 359.9999) t.rotDeg = 0;
    std::size_t cps = 0; for (unsigned char ch : utf8) cps += (ch & 0xC0) != 0x80;
    const double w = 0.6 * t.height * widthFactor * static_cast<double>(cps);   // estimate (no font metrics)
    const Vec2 u = unit(dir), nrm{-u.y, u.x};
    t.quad = {o, o + u * w, o + u * w + nrm * t.height, o + nrm * t.height};
    t.font = e.str(7, "STANDARD");
    std::string layer; LineStyle st; props(e, c, layer, st); t.rgb = st.rgb;
    t.prov.source = SourceKind::Dxf; const std::string hd = e.str(5);
    t.prov.ref = std::string("DXF ") + tag + (hd.empty() ? "" : " handle " + hd);
    t.attrs["dxf.layer"] = layer;
    t.attrs["dxf.widthEstimated"] = "1";
    if (!c.blockPath.empty()) t.attrs["dxf.block"] = c.blockPath;
    out.texts.push_back(std::move(t));
    if (collectTexts) collectTexts->push_back(out.texts.back().id);
  }

  void textEntity(const Ent& e, const Ctx& c0) {
    Ctx c = c0; c.m = ocs(e, c0.m);
    const std::string s = convertPercentCodes(dec(e.str(1)));
    const double h = e.num(40, 2.5), rot = e.num(50), wf = e.num(41, 1.0);
    const int hj = e.integer(72), vj = e.integer(e.type == "TEXT" ? 73 : 74);
    Vec2 o{e.num(10), e.num(20)};
    if ((hj != 0 || vj != 0) && e.first(11)) {
      // derive baseline-left from the alignment point (width estimated)
      const Vec2 a{e.num(11), e.num(21)};
      std::size_t cps = 0; for (unsigned char ch : s) cps += (ch & 0xC0) != 0x80;
      const double w = 0.6 * h * wf * static_cast<double>(cps);
      double dx = 0, dy = 0;
      if (hj == 1 || hj == 4) dx = -w / 2; else if (hj == 2) dx = -w; else if (hj == 3 || hj == 5) dx = 0;
      if (hj == 4 || vj == 2) dy = -h / 2; else if (vj == 3) dy = -h; else if (vj == 1) dy = h * 0.2;
      if (hj == 3 || hj == 5) o = {e.num(10), e.num(20)};
      else {
        const double cr = std::cos(rad(rot)), sr = std::sin(rad(rot));
        o = {a.x + dx * cr - dy * sr, a.y + dx * sr + dy * cr};
      }
    }
    addText(e, c, s, o, h, rot, wf, e.type.c_str());
  }

  void mtextEntity(const Ent& e, const Ctx& c0) {
    std::string raw;
    for (auto& g : e.g) if (g.code == 3) raw += g.value;
    raw += e.str(1);
    const auto paras = stripMText(convertPercentCodes(dec(raw)));
    const double h = e.num(40, 2.5);
    double rot = e.num(50);
    if (e.first(11)) rot = deg(std::atan2(e.num(21), e.num(11)));
    const int att = e.integer(71, 1);
    const Vec2 ins{e.num(10), e.num(20)};
    const double lineSp = 1.667 * h * e.num(44, 1.0);
    const std::size_t nl = paras.size();
    const double totalH = h + lineSp * static_cast<double>(nl - 1);
    // y of the first baseline relative to the insertion point (unrotated frame)
    double y0 = 0;
    const int row = (att - 1) / 3, col = (att - 1) % 3;
    if (row == 0) y0 = -h; else if (row == 1) y0 = totalH / 2 - h; else y0 = totalH - h;
    const double cr = std::cos(rad(rot)), sr = std::sin(rad(rot));
    for (std::size_t i = 0; i < nl; ++i) {
      std::string s = paras[i];
      while (!s.empty() && s.back() == ' ') s.pop_back();
      while (!s.empty() && s.front() == ' ') s.erase(s.begin());
      if (s.empty()) continue;
      std::size_t cps = 0; for (unsigned char ch : s) cps += (ch & 0xC0) != 0x80;
      const double w = 0.6 * h * static_cast<double>(cps);
      const double dx = col == 0 ? 0 : col == 1 ? -w / 2 : -w;
      const double dy = y0 - lineSp * static_cast<double>(i);
      addText(e, c0, s, {ins.x + dx * cr - dy * sr, ins.y + dx * sr + dy * cr}, h, rot, 1.0, "MTEXT");
    }
  }

  // B-spline evaluation (de Boor), rational if weights given.
  std::vector<Vec2> splinePoints(const Ent& e) const {
    const int deg_ = e.integer(71, 3);
    const auto knots = e.all(40);
    const auto xs = e.all(10), ys = e.all(20), ws = e.all(41);
    const std::size_t n = std::min(xs.size(), ys.size());
    std::vector<Vec2> out;
    if (n >= 2 && knots.size() == n + deg_ + 1 && deg_ >= 1) {
      const double t0 = knots[deg_], t1 = knots[n];
      const int samples = std::max(32, static_cast<int>(n) * 16);
      for (int s = 0; s <= samples; ++s) {
        double t = t0 + (t1 - t0) * s / samples;
        std::size_t k = deg_;
        while (k + 1 < n && knots[k + 1] <= t) ++k;
        if (t >= t1) k = n - 1;
        std::vector<double> X(deg_ + 1), Y(deg_ + 1), W(deg_ + 1);
        for (int j = 0; j <= deg_; ++j) {
          const std::size_t idx = k - deg_ + j;
          const double w = idx < ws.size() ? ws[idx] : 1.0;
          X[j] = xs[idx] * w; Y[j] = ys[idx] * w; W[j] = w;
        }
        for (int r = 1; r <= deg_; ++r)
          for (int j = deg_; j >= r; --j) {
            const std::size_t i = k - deg_ + j;
            const double den = knots[i + deg_ - r + 1] - knots[i];
            const double a = den > 0 ? (t - knots[i]) / den : 0;
            X[j] = (1 - a) * X[j - 1] + a * X[j]; Y[j] = (1 - a) * Y[j - 1] + a * Y[j]; W[j] = (1 - a) * W[j - 1] + a * W[j];
          }
        out.push_back({X[deg_] / W[deg_], Y[deg_] / W[deg_]});
      }
      return out;
    }
    const auto fx = e.all(11), fy = e.all(21);
    for (std::size_t i = 0; i < std::min(fx.size(), fy.size()); ++i) out.push_back({fx[i], fy[i]});
    if (out.empty()) for (std::size_t i = 0; i < n; ++i) out.push_back({xs[i], ys[i]});
    return out;
  }

  void entity(const Ent& e, const Ctx& c);

  void insert(const Ent& e, const Ctx& c) {
    const std::string name = e.str(2);
    auto it = f.blocks.find(name);
    if (it == f.blocks.end()) { unsupported["INSERT(eksik blok " + name + ")"]++; return; }
    if (c.depth >= 16) { unsupported["INSERT(derinlik>16)"]++; return; }
    ++inserts;
    const Block& b = it->second;
    const double sx = e.num(41, 1), sy = e.num(42, 1), rot = e.num(50);
    const int cols = std::max(1, e.integer(70, 1)), rows = std::max(1, e.integer(71, 1));
    const double dc = e.num(44), dr = e.num(45);
    std::string layer; LineStyle st; props(e, c, layer, st);
    for (int r = 0; r < rows; ++r)
      for (int k = 0; k < cols; ++k) {
        // local: translate(-base) -> scale -> rotate -> translate(ins + array offset rotated)
        const double cr = std::cos(rad(rot)), sr = std::sin(rad(rot));
        const double ox = k * dc, oy = r * dr;
        const double ix = e.num(10) + ox * cr - oy * sr, iy = e.num(20) + ox * sr + oy * cr;
        Affine2 T{sx * cr, sx * sr, -sy * sr, sy * cr, 0, 0};
        T[4] = ix * u2mm - (T[0] * b.bx + T[2] * b.by) * u2mm;
        T[5] = iy * u2mm - (T[1] * b.bx + T[3] * b.by) * u2mm;
        Ctx cc = c;
        // block entities are given in block units: P() multiplies by unit first, so T acts on mm: T' = S(unit)^-1 * T * S(unit)
        // implemented by keeping linear part and translating in mm (unit applied in P()).
        cc.m = compose(ocs(e, T), c.m);
        cc.depth = c.depth + 1;
        cc.layer = layer; cc.linetype = e.str(6, "BYLAYER");
        if (upper(cc.linetype) == "BYLAYER") { const Layer* L = layerOf(layer); cc.linetype = L ? L->linetype : "CONTINUOUS"; }
        cc.color = e.integer(62, 256); if (cc.color == 256) { const Layer* L = layerOf(layer); cc.color = L ? std::abs(L->color) : 7; }
        cc.lineweight = e.integer(370, -1);
        cc.ltscale = c.ltscale * e.num(48, 1.0);
        cc.blockPath = c.blockPath.empty() ? name : c.blockPath + "/" + name;
        for (auto& be : b.ents) entity(be, cc);
      }
    for (auto& a : e.children) if (a.type == "ATTRIB" && !(a.integer(70) & 1)) textEntity(a, c);
  }

  void dimension(const Ent& e, const Ctx& c) {
    NativeDimension d; d.id = nextId++; d.conf = kConfNative;
    const int type = e.integer(70) & 7;
    static const NativeDimKind kinds[] = {NativeDimKind::Linear, NativeDimKind::Aligned, NativeDimKind::Angular, NativeDimKind::Diameter,
                                          NativeDimKind::Radius, NativeDimKind::Angular3P, NativeDimKind::Ordinate, NativeDimKind::Unknown};
    d.kind = kinds[type];
    d.rotatedLinear = type == 0; d.rotDeg = e.num(50);
    const int codes[5] = {10, 13, 14, 15, 16};
    std::array<Vec2, 5> local{};
    std::string mask;
    for (int i = 0; i < 5; ++i) {
      mask.push_back(e.first(codes[i]) ? '1' : '0');
      local[i] = {e.num(codes[i]), e.num(codes[i] + 10)};
      d.defpts[i] = e.first(codes[i]) ? P(c.m, local[i].x, local[i].y) : Vec2{0, 0};
    }
    d.textMid = P(c.m, e.num(11), e.num(21));
    d.attrs["dxf.defptMask"] = mask;   // which of 10/13/14/15/16 were present
    const double s = scaleOf(c.m);
    const Vec2 p10 = d.defpts[0], p13 = d.defpts[1], p14 = d.defpts[2], p15 = d.defpts[3], p16 = d.defpts[4];
    switch (d.kind) {
      case NativeDimKind::Linear: {
        const Vec2 dl = transformPoint(c.m, polar({0, 0}, 1, d.rotDeg)) - transformPoint(c.m, {0, 0});
        d.measured = std::abs(dot(p14 - p13, unit(dl)));
        break;
      }
      case NativeDimKind::Aligned: d.measured = distance(p13, p14); break;
      case NativeDimKind::Diameter: d.measured = distance(p10, p15); break;
      case NativeDimKind::Radius: d.measured = distance(p10, p15); break;
      case NativeDimKind::Angular3P: {
        const Vec2 a = p13 - p15, b = p14 - p15;
        d.measured = deg(std::acos(std::clamp(dot(unit(a), unit(b)), -1.0, 1.0)));
        break;
      }
      case NativeDimKind::Angular: {
        // lines 13-14 and 10-15, arc location 16
        const Vec2 d1 = unit(p14 - p13), d2 = unit(p10 - p15);
        const double den = cross(d1, d2);
        Vec2 X = p13;
        if (std::abs(den) > 1e-12) { const double t = cross(p15 - p13, d2) / den; X = p13 + d1 * t; }
        Vec2 u1 = d1, u2 = d2;
        if (dot(u1, p16 - X) < 0) u1 = u1 * -1;
        if (dot(u2, p16 - X) < 0) u2 = u2 * -1;
        d.measured = deg(std::acos(std::clamp(dot(u1, u2), -1.0, 1.0)));
        break;
      }
      case NativeDimKind::Ordinate: {
        const bool xType = (e.integer(70) & 64) != 0;
        d.measured = xType ? std::abs(p13.x - p10.x) : std::abs(p13.y - p10.y);
        break;
      }
      default: break;
    }
    (void)s;
    d.textOverride = convertPercentCodes(dec(e.str(1)));
    { const auto paras = stripMText(d.textOverride); std::string j; for (auto& p : paras) { if (!j.empty()) j += " "; j += p; } d.textOverride = j; }
    d.block = e.str(2);
    d.dimStyle = e.str(3, "STANDARD");
    d.prov.source = SourceKind::Dxf; const std::string h = e.str(5); d.prov.ref = "DXF DIMENSION" + (h.empty() ? "" : " handle " + h);
    {
      std::string layer; LineStyle st; props(e, c, layer, st); d.attrs["dxf.layer"] = layer;
    }
    double lfac = 1;
    if (auto it = f.dimlfac.find(upper(d.dimStyle)); it != f.dimlfac.end()) lfac = it->second;
    if (auto it = f.header.find("$DIMLFAC"); it != f.header.end() && !it->second.empty() && lfac == 1)
      lfac = std::strtod(it->second[0].value.c_str(), nullptr);
    if (lfac <= 0) lfac = 1;
    if (lfac != 1) {
      std::ostringstream o; o << lfac; d.attrs["dxf.dimlfac"] = o.str();
    }
    if (const std::string* g42 = e.first(42)) d.attrs["dxf.measured42"] = *g42;
    if (opt.explodeDxfDimBlocks && !d.block.empty()) {
      auto it = f.blocks.find(d.block);
      if (it != f.blocks.end()) {
        std::vector<int> bp, bt;
        auto* sp = collectPrims; auto* stx = collectTexts;
        collectPrims = &bp; collectTexts = &bt;
        Ctx cc = c; cc.dimBlock = true; cc.depth = c.depth + 1;
        std::string layer; LineStyle st; props(e, c, layer, st); cc.layer = layer;
        cc.blockPath = c.blockPath.empty() ? d.block : c.blockPath + "/" + d.block;
        // *D blocks are in WCS with base point (0,0) and implicit insertion at the origin
        const Block& b = it->second;
        cc.m = compose(Affine2{1, 0, 0, 1, -b.bx * u2mm, -b.by * u2mm}, c.m);
        for (auto& be : b.ents) entity(be, cc);
        collectPrims = sp; collectTexts = stx;
        if (sp) sp->insert(sp->end(), bp.begin(), bp.end());
        if (stx) stx->insert(stx->end(), bt.begin(), bt.end());
        d.blockPrimitiveIds = bp; d.blockTextIds = bt;
      } else {
        unsupported["DIMENSION(eksik blok)"]++;
      }
    }
    out.nativeDims.push_back(std::move(d));
  }

  void hatch(const Ent& e, const Ctx& c) {
    // boundary paths: polyline paths (flag 2) and edge paths with line/arc edges
    const auto& g = e.g;
    std::size_t i = 0;
    auto nextCode = [&](int code) { while (i < g.size() && g[i].code != code) ++i; return i < g.size(); };
    std::vector<std::vector<Vec2>> loops;
    if (!nextCode(91)) return;
    const int nPaths = std::atoi(g[i].value.c_str()); ++i;
    for (int pth = 0; pth < nPaths; ++pth) {
      if (!nextCode(92)) break;
      const int flags = std::atoi(g[i].value.c_str()); ++i;
      std::vector<Vec2> loop;
      if (flags & 2) {
        bool hasBulge = false; int nv = 0;
        while (i < g.size() && g[i].code != 93) { if (g[i].code == 72) hasBulge = std::atoi(g[i].value.c_str()) != 0; ++i; }
        if (i < g.size()) { nv = std::atoi(g[i].value.c_str()); ++i; }
        for (int v = 0; v < nv && i < g.size(); ++v) {
          double x = 0, y = 0;
          if (nextCode(10)) x = std::strtod(g[i].value.c_str(), nullptr);
          if (nextCode(20)) y = std::strtod(g[i].value.c_str(), nullptr);
          ++i;
          if (hasBulge && i < g.size() && g[i].code == 42) ++i;
          loop.push_back(P(c.m, x, y));
        }
      } else {
        if (!nextCode(93)) break;
        const int ne = std::atoi(g[i].value.c_str()); ++i;
        for (int k = 0; k < ne && i < g.size(); ++k) {
          if (!nextCode(72)) break;
          const int et = std::atoi(g[i].value.c_str()); ++i;
          auto rd = [&](int code) { return nextCode(code) ? std::strtod(g[i++].value.c_str(), nullptr) : 0.0; };
          if (et == 1) { const double x0 = rd(10), y0 = rd(20), x1 = rd(11), y1 = rd(21); loop.push_back(P(c.m, x0, y0)); loop.push_back(P(c.m, x1, y1)); }
          else if (et == 2) {
            const double cx = rd(10), cy = rd(20), r = rd(40), a0 = rd(50), a1 = rd(51); const bool ccw = rd(73) != 0;
            double s0 = ccw ? a0 : -a0, s1 = ccw ? a1 : -a1; if (!ccw) std::swap(s0, s1); if (s1 <= s0) s1 += 360;
            for (int q = 0; q <= 16; ++q) loop.push_back(P(c.m, cx + r * std::cos(rad(s0 + (s1 - s0) * q / 16)), cy + r * std::sin(rad(s0 + (s1 - s0) * q / 16))));
          } else { unsupported["HATCH(kenar türü " + std::to_string(et) + ")"]++; }
        }
      }
      if (loop.size() >= 3) loops.push_back(loop);
    }
    const double angle = e.num(52), pitch = e.num(41, 1.0) * u2mm;
    for (auto& l : loops) {
      auto& p = add(PrimKind::HatchRegion, e, c);
      p.pts = l; p.closed = true; p.hatchAngleDeg = angle; p.hatchPitch = pitch;
      p.style.filled = e.integer(70) == 1; p.attrs["dxf.pattern"] = e.str(2);
    }
  }

  void solid(const Ent& e, const Ctx& c) {
    std::vector<Vec2> v;
    for (int k = 0; k < 4; ++k) if (e.first(10 + k)) v.push_back(P(c.m, e.num(10 + k), e.num(20 + k)));
    // SOLID vertex order 1,2,4,3; triangle when 3 == 4
    if (v.size() == 4 && distance(v[2], v[3]) < 1e-9) v.pop_back();
    else if (v.size() == 4) std::swap(v[2], v[3]);
    if (v.size() == 3) {
      // arrowhead: tip = vertex opposite the shortest side
      double best = 1e300; int tip = 0;
      for (int k = 0; k < 3; ++k) { const double len = distance(v[(k + 1) % 3], v[(k + 2) % 3]); if (len < best) { best = len; tip = k; } }
      auto& p = add(PrimKind::Arrowhead, e, c);
      p.p0 = v[tip]; p.p1 = (v[(tip + 1) % 3] + v[(tip + 2) % 3]) * 0.5; p.style.filled = true;
      return;
    }
    auto& p = add(PrimKind::HatchRegion, e, c); p.pts = v; p.closed = true; p.style.filled = true;
  }
};

void Builder::entity(const Ent& e, const Ctx& c0) {
  if (e.integer(60) == 1) return;   // invisible
  const bool paper = e.integer(67) == 1;
  if (c0.depth == 0 && (paper != !opt.dxfLayout.empty())) return;
  {
    const Layer* L = layerOf(e.str(8, "0") == "0" && c0.depth > 0 ? c0.layer : e.str(8, "0"));
    if (L && (L->off || L->frozen)) return;
    if (upper(e.str(8)) == "DEFPOINTS") return;
  }
  const std::string& t = e.type;
  const Affine2 m = ocs(e, c0.m);
  if (t == "LINE") { line(e, c0, P(c0.m, e.num(10), e.num(20)), P(c0.m, e.num(11), e.num(21))); }
  else if (t == "CIRCLE") arcLocal(e, c0, m, {e.num(10) * u2mm, e.num(20) * u2mm}, e.num(40) * u2mm, 0, 360, true);
  else if (t == "ARC") {
    double a0 = norm360(e.num(50)), a1 = norm360(e.num(51));
    if (a1 <= a0) a1 += 360;
    arcLocal(e, c0, m, {e.num(10) * u2mm, e.num(20) * u2mm}, e.num(40) * u2mm, a0, a1, false);
  } else if (t == "LWPOLYLINE") {
    std::vector<Vec2> v; std::vector<double> b;
    for (auto& g : e.g) {
      if (g.code == 10) { v.push_back({std::strtod(g.value.c_str(), nullptr) * u2mm, 0}); b.push_back(0); }
      else if (g.code == 20 && !v.empty()) v.back().y = std::strtod(g.value.c_str(), nullptr) * u2mm;
      else if (g.code == 42 && !b.empty()) b.back() = std::strtod(g.value.c_str(), nullptr);
    }
    vertexChain(e, c0, m, v, b, (e.integer(70) & 1) != 0);
  } else if (t == "POLYLINE") {
    const int fl = e.integer(70);
    if (fl & (16 | 64)) { unsupported["POLYLINE(mesh/polyface)"]++; return; }
    std::vector<Vec2> v; std::vector<double> b;
    for (auto& vx : e.children) {
      if (vx.integer(70) & 16) continue;   // spline frame control point
      v.push_back({vx.num(10) * u2mm, vx.num(20) * u2mm}); b.push_back(vx.num(42));
    }
    vertexChain(e, c0, (fl & 8) ? c0.m : m, v, b, (fl & 1) != 0);
  } else if (t == "SPLINE") {
    auto pts = splinePoints(e);
    if (pts.size() >= 2) {
      auto& p = add(PrimKind::Polyline, e, c0);
      for (auto& q : pts) p.pts.push_back(P(m, q.x, q.y));
      p.closed = (e.integer(70) & 1) != 0;
      p.attrs["dxf.approx"] = "spline";
    }
  } else if (t == "ELLIPSE") {
    const Vec2 ctr{e.num(10), e.num(20)}, maj{e.num(11), e.num(21)};
    const double ratio = e.num(40, 1.0);
    double t0 = deg(e.num(41, 0.0)), t1 = deg(e.num(42, 2 * kPi));
    if (t1 <= t0) t1 += 360;
    const bool full = t1 - t0 >= 359.999;
    const double rmaj = length(maj) * u2mm, rot = deg(std::atan2(maj.y, maj.x));
    if (std::abs(ratio - 1) < 1e-9) {
      arcLocal(e, c0, m, ctr * u2mm, rmaj, norm360(t0 + rot), norm360(t0 + rot) + (t1 - t0), full);
    } else {
      // Ellipse primitive (+ polyline approximation in pts, since pc::Sketch has no ellipse)
      Primitive tmp; tmp.kind = PrimKind::Ellipse; tmp.center = ctr * u2mm; tmp.radius = rmaj; tmp.minorRatio = ratio; tmp.rotDeg = rot;
      tmp.a0 = t0; tmp.a1 = t1;
      const auto loc = samplePrim(tmp, 0.25);
      auto& p = add(PrimKind::Ellipse, e, c0);
      const Vec2 C = transformPoint(m, tmp.center), A = transformPoint(m, tmp.center + Vec2{rmaj * std::cos(rad(rot)), rmaj * std::sin(rad(rot))});
      p.center = C; p.radius = distance(C, A); p.minorRatio = ratio; p.rotDeg = deg(std::atan2(A.y - C.y, A.x - C.x));
      p.a0 = t0; p.a1 = t1;
      if (mirrored(m)) { p.a0 = 360 - t1; p.a1 = 360 - t0; }
      for (auto& q : loc) p.pts.push_back(transformPoint(m, q));
      p.closed = full;
      p.attrs["dxf.approx"] = "ellipsePts";
    }
  } else if (t == "TEXT" || t == "ATTRIB") textEntity(e, c0);
  else if (t == "MTEXT") mtextEntity(e, c0);
  else if (t == "INSERT") insert(e, c0);
  else if (t == "DIMENSION") dimension(e, c0);
  else if (t == "HATCH") hatch(e, c0);
  else if (t == "SOLID" || t == "TRACE") solid(e, c0);
  else if (t == "LEADER") {
    std::vector<Vec2> v; for (auto& g : e.g) {
      if (g.code == 10) v.push_back({std::strtod(g.value.c_str(), nullptr), 0});
      else if (g.code == 20 && !v.empty()) v.back().y = std::strtod(g.value.c_str(), nullptr);
    }
    for (std::size_t k = 0; k + 1 < v.size(); ++k) {
      line(e, c0, P(c0.m, v[k].x, v[k].y), P(c0.m, v[k + 1].x, v[k + 1].y));
      if (!out.prims.empty()) out.prims.back().attrs["dxf.leader"] = "1";
    }
  } else if (t == "POINT" || t == "VIEWPORT" || t == "ATTDEF" || t == "SEQEND") {
    // ignored by design
  } else {
    unsupported[t]++;
  }
}

}  // namespace

StageResult ingestDxf(const std::string& path, const IngestOptions& opt, PrimitiveSheet& out, int& nextId, ProgressSink* progress) {
  report(progress, 0.0, "DXF okunuyor");
  DxfFile f;
  std::string err;
  if (!readDxf(path, f, &err)) return StageResult::failure(Status::InvalidInput, err);
  if (isCancelled(progress)) return StageResult::failure(Status::Cancelled, "İptal edildi");
  PrimitiveSheet s;
  s.source = SourceKind::Dxf; s.file = path; s.page = 0; s.layout = opt.dxfLayout;
  StageResult res = StageResult::success();
  Builder b{f, opt, s, nextId};
  {
    bool known = false;
    int ins = 0;
    if (auto it = f.header.find("$INSUNITS"); it != f.header.end() && !it->second.empty()) ins = std::atoi(it->second[0].value.c_str());
    b.u2mm = insunitsToMm(ins, &known);
    if (!known) {
      int meas = 1;
      if (auto it = f.header.find("$MEASUREMENT"); it != f.header.end() && !it->second.empty()) meas = std::atoi(it->second[0].value.c_str());
      if (ins != 0) res.warn("dxf.units.unknown", "Bilinmeyen $INSUNITS=" + std::to_string(ins) + "; mm kabul edildi");
      else if (meas == 0) res.warn("dxf.units.imperialUnitless", "Birimsiz çizim, $MEASUREMENT=0 (inç) ama mm kabul edildi");
    }
    if (auto it = f.header.find("$LTSCALE"); it != f.header.end() && !it->second.empty()) b.ltscale = std::strtod(it->second[0].value.c_str(), nullptr);
    if (b.ltscale <= 0) b.ltscale = 1;
  }
  s.unitToMm = b.u2mm;
  s.attrs["dxf.acadver"] = f.acadver;
  if (!f.codepage.empty()) s.attrs["dxf.codepage"] = f.codepage;
  if (!opt.dxfLayout.empty())
    res.warn("dxf.layout.paperSpace", "Paper space düzeni '" + opt.dxfLayout + "' (VIEWPORT yok sayıldı, faz 2); etkin paper space alındı");
  Ctx root;
  const std::size_t n = f.entities.size();
  for (std::size_t i = 0; i < n; ++i) {
    b.entity(f.entities[i], root);
    if ((i & 255) == 0) {
      report(progress, 0.1 + 0.8 * static_cast<double>(i) / std::max<std::size_t>(1, n), "DXF varlıkları");
      if (isCancelled(progress)) return StageResult::failure(Status::Cancelled, "İptal edildi");
    }
  }
  for (auto& [k, v] : b.unsupported)
    res.warn("dxf.entity.unsupported", "Desteklenmeyen DXF varlığı: " + k + " x" + std::to_string(v));
  // extents; shift to >= 0
  Box2 ext = emptyBox();
  for (auto& p : s.prims) { const Box2 bb = primBox(p); if (!isEmpty(bb)) { expand(ext, bb.lo); expand(ext, bb.hi); } }
  for (auto& t : s.texts) for (auto& q : t.quad) expand(ext, q);
  if (isEmpty(ext)) ext = {{0, 0}, {0, 0}};
  const Vec2 sh{ext.lo.x < 0 ? -ext.lo.x : 0.0, ext.lo.y < 0 ? -ext.lo.y : 0.0};
  if (sh.x != 0 || sh.y != 0) {
    for (auto& p : s.prims) { p.p0 = p.p0 + sh; p.p1 = p.p1 + sh; p.center = p.center + sh; for (auto& q : p.pts) q = q + sh; }
    for (auto& t : s.texts) { t.origin = t.origin + sh; for (auto& q : t.quad) q = q + sh; }
    for (auto& d : s.nativeDims) {
      const std::string& mask = d.attrs["dxf.defptMask"];
      for (std::size_t k = 0; k < 5; ++k) if (k < mask.size() && mask[k] == '1') d.defpts[k] = d.defpts[k] + sh;
      d.textMid = d.textMid + sh;
    }
    std::ostringstream o; o.precision(17); o << sh.x << "," << sh.y;
    s.attrs["dxf.shift"] = o.str();
    res.warn("dxf.extents.shifted", "Çizim sınırları negatif; " + o.str() + " mm kaydırıldı");
  }
  s.sheet = {{0, 0}, {ext.hi.x + sh.x, ext.hi.y + sh.y}};
  s.diagnostics = res.diagnostics;
  for (auto& d : s.diagnostics) d.stage = static_cast<int>(Stage::Ingest);
  out = std::move(s);
  std::ostringstream msg;
  msg << "DXF: " << out.prims.size() << " ilkel, " << out.texts.size() << " metin, " << out.nativeDims.size() << " ölçü";
  res.message = msg.str();
  report(progress, 1.0, res.message);
  return res;
}

}  // namespace pc::drawing
